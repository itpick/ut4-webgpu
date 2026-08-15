// Copyright: DawnRHI project (itpick/ut4-webgpu).
// See dawn_tint_bridge.h for why this file exists (ABI-boundary isolation
// for tint::Result<T>). Compiled standalone by
// tools/build_dawn_tint_thirdparty.sh with the exact same flags used to
// self-build Tint/SPIRV-Tools — never compiled by UBT.
//
// This is the SPIR-V(legalized-eligible)->WGSL half of
// tools/hlsl_to_wgsl.cpp's chain (LegalizeAndStrip + SpirvToWgsl +
// binding reflection), moved here verbatim in behaviour so
// DawnShaderCompiler.cpp (the UE-module side) never touches a Tint type
// directly.
#include "dawn_tint_bridge.h"

#include "spirv-tools/optimizer.hpp"
#include "spirv-tools/libspirv.hpp"
#include "src/tint/lang/spirv/reader/reader.h"
#include "src/tint/lang/wgsl/writer/writer.h"
#include "src/tint/lang/core/ir/module.h"

#include <algorithm>
#include <csetjmp>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>
#include <sstream>

namespace
{
	char* DupToMalloc(const std::string& S, unsigned int& OutLen)
	{
		OutLen = static_cast<unsigned int>(S.size());
		char* Buf = static_cast<char*>(std::malloc(S.size() + 1));
		std::memcpy(Buf, S.data(), S.size());
		Buf[S.size()] = '\0';
		return Buf;
	}

	// Split into two separate optimizer passes (was one combined pass) so
	// real reflection (ReflectBindings, below) can run on the module
	// AFTER dead-resource elimination (part of RegisterLegalizationPasses())
	// but BEFORE CreateStripReflectInfoPass() removes the OpName/OpDecorate
	// info reflection needs. Reflecting on the ORIGINAL pre-legalization
	// SPIR-V (the previous, single-pass version of this function) reported
	// every resource a real UE uniform buffer struct *declares* (e.g. all
	// ~100 individually-bound Texture/Sampler resource members of the real
	// `View` uniform buffer) rather than the handful an actual shader
	// *uses* — legalization's dead-variable elimination is exactly what
	// prunes that down to what Tint's WGSL output will actually declare,
	// so reflection must run after it to stay truthful to the real cooked
	// WGSL's real @group/@binding set.
	bool Legalize(std::vector<uint32_t>& Spirv, std::string& OutLog)
	{
		spvtools::Optimizer Opt(SPV_ENV_VULKAN_1_1);
		Opt.SetMessageConsumer([&OutLog](spv_message_level_t, const char*, const spv_position_t&, const char* Msg)
		{
			OutLog += Msg;
			OutLog += "\n";
		});
		Opt.RegisterLegalizationPasses();

		std::vector<uint32_t> Result;
		if (!Opt.Run(Spirv.data(), Spirv.size(), &Result))
		{
			return false;
		}
		Spirv = std::move(Result);
		return true;
	}

	bool StripReflectInfo(std::vector<uint32_t>& Spirv, std::string& OutLog)
	{
		spvtools::Optimizer Opt(SPV_ENV_VULKAN_1_1);
		Opt.SetMessageConsumer([&OutLog](spv_message_level_t, const char*, const spv_position_t&, const char* Msg)
		{
			OutLog += Msg;
			OutLog += "\n";
		});
		Opt.RegisterPass(spvtools::CreateStripReflectInfoPass());

		std::vector<uint32_t> Result;
		if (!Opt.Run(Spirv.data(), Spirv.size(), &Result))
		{
			return false;
		}
		Spirv = std::move(Result);
		return true;
	}

	bool SpirvToWgsl(const std::vector<uint32_t>& Spirv, std::string& OutWgsl, std::string& OutError)
	{
		tint::spirv::reader::Options ReadOpts = {};
		// Allow every WGSL extension/language feature (notably
		// readonly_and_readwrite_storage_textures) and derivative builtins in
		// non-uniform control flow: with the default (empty) feature set, the
		// first full UT cook (2026-08-11) failed ~750 real global shaders on
		// "read-write storage textures require the readonly_and_readwrite_-
		// storage_textures language feature" / "textureStore: no matching call
		// ... read_write" and 33 more on "'textureSample' must only be called
		// from uniform control flow". Whether the runtime (Dawn/browser)
		// supports each feature is a separate, runtime-side check; the cook
		// should emit the WGSL and let per-feature gating happen there.
		ReadOpts.allow_non_uniform_derivatives = true;
		ReadOpts.allowed_features = tint::wgsl::AllowedFeatures::Everything();
		auto IrResult = tint::spirv::reader::ReadIR(Spirv, ReadOpts);
		if (IrResult != tint::Success)
		{
			OutError = "ReadIR: " + IrResult.Failure().reason;
			return false;
		}

		tint::wgsl::writer::Options WriteOpts = {};
		WriteOpts.allowed_features = tint::wgsl::AllowedFeatures::Everything();
		auto WgslResult = tint::wgsl::writer::WgslFromIR(IrResult.Get(), WriteOpts);
		if (WgslResult != tint::Success)
		{
			OutError = "WgslFromIR: " + WgslResult.Failure().reason;
			return false;
		}

		OutWgsl = WgslResult.Get().wgsl;
		return true;
	}

	// Real SPIR-V reflection (milestone step 1): disassemble the
	// pre-legalization SPIR-V (still has OpName/OpDecorate DescriptorSet/
	// Binding — legalize+strip-reflect removes these, which is why this
	// runs on the PRE-legalized copy, see Dawn_LegalizeAndCookSpirvToWgsl)
	// and walk it as a real (if text-form rather than binary-API-form)
	// SPIR-V module: every resource's {DescriptorSet, Binding, Name} comes
	// straight off real OpDecorate/OpName instructions, and its resource
	// KIND (uniform buffer vs texture vs sampler) is derived by walking
	// the real OpVariable -> OpTypePointer -> pointee-type chain
	// (OpTypeStruct+StorageClass=Uniform => uniform buffer;
	// OpTypeImage => texture; OpTypeSampler => sampler) — not guessed,
	// not hardcoded, read straight off the module the same way SPIRV-Reflect's
	// own binary walker would. (We disassemble via spvtools — already a
	// build dependency here for LegalizeAndStrip — rather than adding a
	// second SPIRV-Reflect third-party dependency to this standalone
	// bridge; see HANDOFF.md for this trade-off.)
	struct FBinding
	{
		std::string Name;
		int Set = -1;
		int Binding = -1;
		EDawnReflectedBindingKind Kind = DawnBindingKind_Unknown;
		bool bDeclaredOnly = false;
	};

	struct FLooseMember
	{
		std::string Name;
		unsigned Offset = 0;
		unsigned Size = 0;
	};
	struct FGlobalsInfo
	{
		bool bFound = false;
		int Set = -1;
		int Binding = -1;
		std::vector<FLooseMember> Members;
	};

	std::vector<FBinding> ReflectBindings(const std::vector<uint32_t>& Spirv, std::string& OutDisassemblyError, FGlobalsInfo* OutGlobals = nullptr)
	{
		spvtools::SpirvTools Tools(SPV_ENV_VULKAN_1_1);
		std::string Text;
		if (!Tools.Disassemble(Spirv, &Text, SPV_BINARY_TO_TEXT_OPTION_NO_HEADER | SPV_BINARY_TO_TEXT_OPTION_FRIENDLY_NAMES))
		{
			OutDisassemblyError = "(disassembly failed, no reflection available)";
			return {};
		}

		std::vector<std::pair<std::string, FBinding>> ByIdOrdered;
		auto FindOrAdd = [&](const std::string& Id) -> FBinding&
		{
			for (auto& Pair : ByIdOrdered)
			{
				if (Pair.first == Id) return Pair.second;
			}
			ByIdOrdered.emplace_back(Id, FBinding{});
			return ByIdOrdered.back().second;
		};

		// Second index: result-id -> full token list, for every
		// "%id = OpXxx ..." instruction (OpVariable/OpTypePointer/
		// OpTypeImage/OpTypeSampler/OpTypeStruct) — used below to walk the
		// type chain for each bound variable.
		std::unordered_map<std::string, std::vector<std::string>> DefById;
		// Which struct type ids are Block-decorated (real uniform-buffer marker).
		std::unordered_map<std::string, bool> BlockDecoratedType;
		// BufferBlock-decorated struct types (legacy SPIR-V storage buffers)
		// and NonWritable-decorated variables (read-only storage buffers, i.e.
		// HLSL StructuredBuffer/ByteAddressBuffer SRVs).
		std::unordered_map<std::string, bool> BufferBlockDecoratedType;
		std::unordered_map<std::string, bool> NonWritableVar;
		// Member-level info per struct type id (for $Globals loose-parameter
		// reflection): real OpMemberName text + OpMemberDecorate Offset,
		// keyed/ordered by member index; plus OpDecorate ArrayStride for
		// array-size derivation of the final member.
		std::unordered_map<std::string, std::map<int, std::string>> MemberNamesByType;
		std::unordered_map<std::string, std::map<int, unsigned>> MemberOffsetsByType;
		std::unordered_map<std::string, unsigned> ArrayStrideByType;

		std::istringstream Stream(Text);
		std::string Line;
		while (std::getline(Stream, Line))
		{
			std::istringstream LineStream(Line);
			std::vector<std::string> Tokens;
			std::string Tok;
			while (LineStream >> Tok)
			{
				Tokens.push_back(Tok);
			}
			if (Tokens.size() < 3)
			{
				continue;
			}
			if (Tokens[0] == "OpMemberName" && Tokens.size() >= 4)
			{
				std::string NameLiteral;
				for (size_t i = 3; i < Tokens.size(); ++i)
				{
					if (i > 3) NameLiteral += " ";
					NameLiteral += Tokens[i];
				}
				if (!NameLiteral.empty() && NameLiteral.front() == '"') NameLiteral.erase(0, 1);
				if (!NameLiteral.empty() && NameLiteral.back() == '"') NameLiteral.pop_back();
				MemberNamesByType[Tokens[1]][std::atoi(Tokens[2].c_str())] = NameLiteral;
			}
			else if (Tokens[0] == "OpMemberDecorate" && Tokens.size() >= 5 && Tokens[3] == "Offset")
			{
				MemberOffsetsByType[Tokens[1]][std::atoi(Tokens[2].c_str())] = (unsigned)std::atoi(Tokens[4].c_str());
			}
			else if (Tokens[0] == "OpName" && Tokens.size() >= 3)
			{
				std::string NameLiteral;
				for (size_t i = 2; i < Tokens.size(); ++i)
				{
					if (i > 2) NameLiteral += " ";
					NameLiteral += Tokens[i];
				}
				if (!NameLiteral.empty() && NameLiteral.front() == '"') NameLiteral.erase(0, 1);
				if (!NameLiteral.empty() && NameLiteral.back() == '"') NameLiteral.pop_back();
				FindOrAdd(Tokens[1]).Name = NameLiteral;
			}
			else if (Tokens[0] == "OpDecorate" && Tokens.size() >= 3)
			{
				// NOTE: "Block" is a bare decoration (no trailing literal
				// operand -- "OpDecorate %id Block", 3 tokens), unlike
				// DescriptorSet/Binding ("OpDecorate %id DescriptorSet N",
				// 4 tokens) -- a >=4 guard on the whole branch (a real bug,
				// found via DAWN_DEBUG_REFLECT=1 tracing a real cooked
				// shader) silently dropped every Block decoration, which
				// made every real uniform buffer misclassify as Unknown
				// instead of UniformBuffer (BlockDecoratedType stayed empty).
				if (Tokens.size() >= 4 && Tokens[2] == "DescriptorSet")
				{
					FindOrAdd(Tokens[1]).Set = std::atoi(Tokens[3].c_str());
				}
				else if (Tokens.size() >= 4 && Tokens[2] == "Binding")
				{
					FindOrAdd(Tokens[1]).Binding = std::atoi(Tokens[3].c_str());
				}
				else if (Tokens[2] == "Block")
				{
					BlockDecoratedType[Tokens[1]] = true;
				}
				else if (Tokens.size() >= 4 && Tokens[2] == "ArrayStride")
				{
					ArrayStrideByType[Tokens[1]] = (unsigned)std::atoi(Tokens[3].c_str());
				}
				else if (Tokens[2] == "BufferBlock")
				{
					BufferBlockDecoratedType[Tokens[1]] = true;
				}
				else if (Tokens[2] == "NonWritable")
				{
					NonWritableVar[Tokens[1]] = true;
				}
			}
			else if (Tokens.size() >= 3 && Tokens[1] == "=" && !Tokens[0].empty() && Tokens[0][0] == '%')
			{
				// "%id = OpXxx <operands...>" form (OpVariable, OpTypePointer,
				// OpTypeImage, OpTypeSampler, OpTypeStruct, ...).
				DefById[Tokens[0]] = Tokens;
			}
		}

		// Resolve Kind for every binding actually decorated with a real
		// DescriptorSet/Binding pair, by walking OpVariable -> OpTypePointer
		// -> pointee type.
		const bool bDebug = std::getenv("DAWN_DEBUG_REFLECT") != nullptr;
		for (auto& Pair : ByIdOrdered)
		{
			FBinding& B = Pair.second;
			if (bDebug) { std::fprintf(stderr, "[reflect] id=%s name=%s set=%d binding=%d\n", Pair.first.c_str(), B.Name.c_str(), B.Set, B.Binding); }
			auto VarIt = DefById.find(Pair.first);
			if (VarIt == DefById.end() || VarIt->second.size() < 5 || VarIt->second[2] != "OpVariable")
			{
				if (bDebug) { std::fprintf(stderr, "  -> no OpVariable def found (DefById has %zu entries; found=%d)\n", DefById.size(), VarIt != DefById.end()); }
				continue;
			}
			const std::string& PtrTypeId = VarIt->second[3];
			const std::string& StorageClass = VarIt->second[4];

			auto PtrIt = DefById.find(PtrTypeId);
			if (PtrIt == DefById.end() || PtrIt->second.size() < 5 || PtrIt->second[2] != "OpTypePointer")
			{
				continue;
			}
			const std::string& BaseTypeId = PtrIt->second[4];

			auto BaseIt = DefById.find(BaseTypeId);
			if (BaseIt == DefById.end() || BaseIt->second.size() < 3)
			{
				continue;
			}
			const std::string& BaseOp = BaseIt->second[2];

			if (BaseOp == "OpTypeImage")
			{
				// OpTypeImage operands: %id = OpTypeImage %sampledType Dim
				// depth arrayed MS Sampled format -- Sampled==2 marks a
				// storage image (HLSL RWTexture*), Sampled==1 a sampled one.
				const std::vector<std::string>& ImgTokens = BaseIt->second;
				const bool bStorageImage = ImgTokens.size() >= 9 && ImgTokens[8] == "2";
				B.Kind = bStorageImage ? DawnBindingKind_StorageImage : DawnBindingKind_Texture;
			}
			else if (BaseOp == "OpTypeSampler")
			{
				B.Kind = DawnBindingKind_Sampler;
			}
			else if (BaseOp == "OpTypeStruct" && StorageClass == "Uniform" && BlockDecoratedType[BaseTypeId])
			{
				B.Kind = DawnBindingKind_UniformBuffer;
			}
			else if (BaseOp == "OpTypeStruct" &&
				(StorageClass == "StorageBuffer" || (StorageClass == "Uniform" && BufferBlockDecoratedType[BaseTypeId])))
			{
				B.Kind = NonWritableVar[Pair.first] ? DawnBindingKind_StorageBufferRO : DawnBindingKind_StorageBufferRW;
			}
			if (bDebug) { std::fprintf(stderr, "  -> BaseOp=%s StorageClass=%s BlockDecorated=%d Kind=%d\n", BaseOp.c_str(), StorageClass.c_str(), (int)BlockDecoratedType[BaseTypeId], (int)B.Kind); }
		}
		if (bDebug)
		{
			int32_t FinalCount = 0;
			for (auto& Pair : ByIdOrdered) { if (Pair.second.Set >= 0 || Pair.second.Binding >= 0) { ++FinalCount; } }
			std::fprintf(stderr, "[reflect] final candidate count (Set>=0||Binding>=0) = %d\n", FinalCount);
		}

		// Member-level reflection of the DXC loose-global block "$Globals"
		// (where HLSL file-scope globals -- UE's legacy FShaderParameter loose
		// parameters -- land). UE requires per-member LooseData parameter-map
		// entries or FShaderParameter::Bind fatals on every non-optional loose
		// parameter. Sizes: next-offset delta (layout-true, padding included);
		// last member: derived from its SPIR-V type (scalar/vector/matrix/
		// array via ArrayStride), 16-byte fallback.
		if (OutGlobals)
		{
			// Recursive-ish type-size helper over the token table.
			std::function<unsigned(const std::string&)> TypeSize = [&](const std::string& TypeId) -> unsigned
			{
				auto It = DefById.find(TypeId);
				if (It == DefById.end() || It->second.size() < 3) return 0;
				const std::vector<std::string>& T = It->second;
				const std::string& Op = T[2];
				if (Op == "OpTypeFloat" || Op == "OpTypeInt")
				{
					return T.size() >= 4 ? (unsigned)std::atoi(T[3].c_str()) / 8u : 4u;
				}
				if (Op == "OpTypeVector")
				{
					return T.size() >= 5 ? TypeSize(T[3]) * (unsigned)std::atoi(T[4].c_str()) : 0u;
				}
				if (Op == "OpTypeMatrix")
				{
					// DXC cbuffer layout: 16-byte column stride.
					if (T.size() < 5) return 0u;
					unsigned Col = TypeSize(T[3]);
					Col = ((Col + 15u) / 16u) * 16u;
					return Col * (unsigned)std::atoi(T[4].c_str());
				}
				if (Op == "OpTypeArray")
				{
					if (T.size() < 5) return 0u;
					unsigned Stride = ArrayStrideByType.count(TypeId) ? ArrayStrideByType[TypeId] : TypeSize(T[3]);
					unsigned Len = 0;
					auto CIt = DefById.find(T[4]);
					if (CIt != DefById.end() && CIt->second.size() >= 5 && CIt->second[2] == "OpConstant")
					{
						Len = (unsigned)std::atoi(CIt->second[4].c_str());
					}
					return Stride * Len;
				}
				return 0u;
			};

			for (auto& Pair : ByIdOrdered)
			{
				FBinding& B = Pair.second;
				if (B.Kind != DawnBindingKind_UniformBuffer) continue;
				if (B.Name != "$Globals" && B.Name != "_Globals") continue;

				// Re-walk OpVariable -> OpTypePointer -> struct type id.
				auto VarIt = DefById.find(Pair.first);
				if (VarIt == DefById.end() || VarIt->second.size() < 5) continue;
				auto PtrIt = DefById.find(VarIt->second[3]);
				if (PtrIt == DefById.end() || PtrIt->second.size() < 5) continue;
				const std::string& StructTypeId = PtrIt->second[4];

				const std::map<int, std::string>& Names = MemberNamesByType[StructTypeId];
				const std::map<int, unsigned>& Offsets = MemberOffsetsByType[StructTypeId];
				auto StructIt = DefById.find(StructTypeId);

				OutGlobals->bFound = true;
				OutGlobals->Set = B.Set;
				OutGlobals->Binding = B.Binding;

				for (auto It = Names.begin(); It != Names.end(); ++It)
				{
					const int Idx = It->first;
					FLooseMember M;
					M.Name = It->second;
					auto OffIt = Offsets.find(Idx);
					M.Offset = OffIt != Offsets.end() ? OffIt->second : 0u;
					// Size must be the member's REAL type size (no trailing
					// cbuffer padding): UE's FShaderParameterStructBindingContext
					// ::Bind FATALS when the reported size exceeds the C++
					// member's size ("...is 12 bytes, smaller than EOTF's 4
					// bytes" seen live when this used next-offset deltas, which
					// include padding). Next-offset delta is only the fallback
					// when the type walk fails, capped at 16 to never exceed a
					// register; then 16.
					unsigned FromType = 0;
					if (StructIt != DefById.end() && StructIt->second.size() > (size_t)(3 + Idx))
					{
						FromType = TypeSize(StructIt->second[3 + Idx]);
					}
					if (FromType)
					{
						M.Size = FromType;
					}
					else
					{
						auto NextOffIt = Offsets.upper_bound(Idx);
						unsigned Delta = (NextOffIt != Offsets.end() && NextOffIt->second > M.Offset) ? NextOffIt->second - M.Offset : 0u;
						M.Size = Delta ? (Delta < 16u ? Delta : 16u) : 16u;
					}
					if (bDebug) { std::fprintf(stderr, "[reflect] $Globals member idx=%d name=%s offset=%u size=%u\n", Idx, M.Name.c_str(), M.Offset, M.Size); }
					OutGlobals->Members.push_back(std::move(M));
				}
				break; // one $Globals block per shader
			}
		}

		std::vector<FBinding> Bindings;
		for (auto& Pair : ByIdOrdered)
		{
			if (Pair.second.Set >= 0 || Pair.second.Binding >= 0)
			{
				Bindings.push_back(Pair.second);
			}
		}
		std::sort(Bindings.begin(), Bindings.end(), [](const FBinding& A, const FBinding& B)
		{
			return A.Set != B.Set ? A.Set < B.Set : A.Binding < B.Binding;
		});
		return Bindings;
	}

	std::string SummarizeBindings(const std::vector<FBinding>& Bindings)
	{
		auto KindName = [](EDawnReflectedBindingKind K) -> const char*
		{
			switch (K)
			{
			case DawnBindingKind_UniformBuffer: return "UniformBuffer";
			case DawnBindingKind_Texture:       return "Texture";
			case DawnBindingKind_Sampler:       return "Sampler";
			default:                            return "Unknown";
			}
		};
		std::ostringstream Out;
		Out << Bindings.size() << " bound resource(s):\n";
		for (const FBinding& B : Bindings)
		{
			Out << "  set=" << B.Set << " binding=" << B.Binding << " kind=" << KindName(B.Kind) << " name=\"" << B.Name << "\"\n";
		}
		return Out.str();
	}
}


namespace
{
	// --- Crash guard --------------------------------------------------------
	// tint reports internal compiler errors (TINT_ICE / TINT_ASSERT /
	// TINT_UNREACHABLE) by printing a banner to stderr and then
	// __builtin_trap()'ing (SIGILL) -- see src/tint/utils/ice/ice.cc. This tint
	// snapshot has NO global ICE-callback registration (the per-callsite
	// callback parameter is not reachable from the public reader/writer entry
	// points), and spirv-tools can likewise assert() -> SIGABRT. In-process
	// that kills the entire cook: verified live 2026-08-11 (cook13 died on
	// signal 4 from "scalar.h:59 TINT_ASSERT(std::isfinite(v.value))" and
	// "parser.cc:785 Unsupported texture dimension: 5" while compiling the UT
	// global shader map). Convert such crashes into per-shader compile
	// failures instead: lazily install chained signal handlers; when a guarded
	// call crashes, siglongjmp back and report Success=0 with the signal
	// number (the tint ICE banner naming the exact assert still lands on
	// stderr, i.e. in the cook/worker log, for failure-mode bucketing).
	// Thread-local guard state keeps concurrent shader-compile threads
	// independent; crashes on non-guarded threads (or outside the guarded
	// call) restore + re-raise so the host's own crash handling runs.
	// Deliberate tradeoff: longjmp'ing out of a trap skips unwinding, so a
	// crashed compile may leak a few allocations -- acceptable for a cook.
	thread_local sigjmp_buf GCrashJmp;
	thread_local volatile sig_atomic_t GCrashGuardActive = 0;
	thread_local volatile sig_atomic_t GCrashSignal = 0;

	// SIGTRAP included: tint's ICE path calls debugger::Break() (a trap
	// instruction / SIGTRAP) BEFORE __builtin_trap(); un-guarded, that killed
	// whole ShaderCompileWorker batches ("Internal Error!" jobs with no error
	// text -- 297 of them in the second full UT cook) even though the banner
	// was printed and SIGILL was guarded.
	constexpr int GGuardedSignals[] = { SIGILL, SIGTRAP, SIGABRT, SIGSEGV, SIGBUS, SIGFPE };
	constexpr size_t GNumGuardedSignals = sizeof(GGuardedSignals) / sizeof(GGuardedSignals[0]);
	struct sigaction GPrevActions[GNumGuardedSignals];

	void CrashGuardHandler(int Sig)
	{
		if (GCrashGuardActive)
		{
			GCrashSignal = Sig;
			siglongjmp(GCrashJmp, 1);
		}
		// Not a guarded tint/spirv-tools call: put the previous handler back
		// and re-raise (blocked until we return, or re-faults for SEGV/ILL)
		// so the engine's crash handler still owns real crashes.
		for (size_t i = 0; i < GNumGuardedSignals; ++i)
		{
			if (GGuardedSignals[i] == Sig)
			{
				sigaction(Sig, &GPrevActions[i], nullptr);
				break;
			}
		}
		raise(Sig);
	}

	void InstallCrashGuardOnce()
	{
		static std::once_flag OnceFlag;
		std::call_once(OnceFlag, []()
		{
			struct sigaction SA;
			std::memset(&SA, 0, sizeof(SA));
			SA.sa_handler = CrashGuardHandler;
			sigemptyset(&SA.sa_mask);
			SA.sa_flags = SA_ONSTACK; // ride the host's per-thread altstack if present
			for (size_t i = 0; i < GNumGuardedSignals; ++i)
			{
				sigaction(GGuardedSignals[i], &SA, &GPrevActions[i]);
			}
		});
	}
}


namespace
{
	// WGSL has no inf/nan literals; tint hard-ICEs on non-finite constants
	// (scalar.h TINT_ASSERT(std::isfinite(v.value)) -- 642 banners across the
	// first full UT cook; UE shaders really do use inf, e.g. POSITIVE_INFINITY
	// clears and FLT_MAX-vs-inf depth sentinels). Legalize at the SPIR-V word
	// level before tint: rewrite every 32-bit-float OpConstant whose literal is
	// +/-Inf to +/-FLT_MAX and NaN to 0. This is the same clamp other
	// production backends apply when the target language cannot express
	// non-finite literals.
	// tint cannot express SPIR-V's EarlyFragmentTests execution mode in WGSL
	// (parser.cc:1619 ICE, 178 banners in the first full UT cook -- DXC emits
	// it for HLSL [earlydepthstencil]). Dropping it only loses an early-Z
	// optimization hint; rendering semantics are unchanged. Rebuilds the word
	// stream without the offending OpExecutionMode instructions.
	void StripEarlyFragmentTests(std::vector<uint32_t>& Spirv)
	{
		if (Spirv.size() < 5) return;
		const uint32_t OpExecutionModeOp = 16;
		const uint32_t EarlyFragmentTestsMode = 9;
		std::vector<uint32_t> Out;
		Out.reserve(Spirv.size());
		Out.insert(Out.end(), Spirv.begin(), Spirv.begin() + 5);
		size_t i = 5;
		while (i < Spirv.size())
		{
			const uint32_t Word0 = Spirv[i];
			const uint32_t Opcode = Word0 & 0xFFFFu;
			const uint32_t WordCount = Word0 >> 16;
			if (WordCount == 0 || i + WordCount > Spirv.size()) { Out.insert(Out.end(), Spirv.begin() + i, Spirv.end()); break; }
			const bool bDrop = (Opcode == OpExecutionModeOp && WordCount >= 3 && Spirv[i + 2] == EarlyFragmentTestsMode);
			if (!bDrop)
			{
				Out.insert(Out.end(), Spirv.begin() + i, Spirv.begin() + i + WordCount);
			}
			i += WordCount;
		}
		Spirv = std::move(Out);
	}

	void StripViewportIndexLayer(std::vector<uint32_t>& Spirv)
	{
		if (Spirv.size() < 5) return;
		const uint32_t OpExtensionOp = 10, OpCapabilityOp = 17, OpDecorateOp = 71;
		const uint32_t DecoBuiltIn = 11, DecoLocation = 30, DecoFlat = 14;
		const uint32_t BuiltInLayer = 9, BuiltInViewportIndex = 10;
		const uint32_t CapViewportIndexLayerEXT = 5254;
		// Pass 1: collect BuiltIn Layer/ViewportIndex target ids + highest Location in use.
		std::vector<uint32_t> Targets; uint32_t MaxLoc = 0; bool AnyLoc = false;
		size_t i = 5;
		while (i < Spirv.size()) {
			const uint32_t w0 = Spirv[i], op = w0 & 0xFFFFu, wc = w0 >> 16;
			if (wc == 0 || i + wc > Spirv.size()) break;
			if (op == OpDecorateOp && wc >= 4) {
				const uint32_t deco = Spirv[i + 2];
				if (deco == DecoBuiltIn && (Spirv[i + 3] == BuiltInLayer || Spirv[i + 3] == BuiltInViewportIndex))
					Targets.push_back(Spirv[i + 1]);
				else if (deco == DecoLocation) { AnyLoc = true; if (Spirv[i + 3] > MaxLoc) MaxLoc = Spirv[i + 3]; }
			}
			i += wc;
		}
		if (Targets.empty()) return;
		uint32_t NextLoc = AnyLoc ? MaxLoc + 1u : 0u;
		std::unordered_map<uint32_t, uint32_t> TargetLoc;
		for (uint32_t t : Targets) if (!TargetLoc.count(t)) TargetLoc[t] = NextLoc++;
		// Pass 2: rebuild, dropping the ext/cap and rewriting the builtin decorations.
		std::vector<uint32_t> Out; Out.reserve(Spirv.size() + Targets.size() * 4);
		Out.insert(Out.end(), Spirv.begin(), Spirv.begin() + 5);
		i = 5;
		while (i < Spirv.size()) {
			const uint32_t w0 = Spirv[i], op = w0 & 0xFFFFu, wc = w0 >> 16;
			if (wc == 0 || i + wc > Spirv.size()) { Out.insert(Out.end(), Spirv.begin() + i, Spirv.end()); break; }
			bool drop = false;
			if (op == OpCapabilityOp && wc == 2 && Spirv[i + 1] == CapViewportIndexLayerEXT) drop = true;
			if (op == OpExtensionOp) {
				std::string s; bool done = false;
				for (size_t k = i + 1; k < i + wc && !done; ++k) {
					const uint32_t word = Spirv[k];
					for (int b = 0; b < 4; ++b) { const char c = (char)((word >> (b * 8)) & 0xFFu); if (!c) { done = true; break; } s.push_back(c); }
				}
				if (s == "SPV_EXT_shader_viewport_index_layer") drop = true;
			}
			if (op == OpDecorateOp && wc >= 4 && Spirv[i + 2] == DecoBuiltIn) {
				auto it = TargetLoc.find(Spirv[i + 1]);
				if (it != TargetLoc.end() && (Spirv[i + 3] == BuiltInLayer || Spirv[i + 3] == BuiltInViewportIndex)) {
					Out.push_back((4u << 16) | OpDecorateOp); Out.push_back(Spirv[i + 1]); Out.push_back(DecoLocation); Out.push_back(it->second);
					Out.push_back((3u << 16) | OpDecorateOp); Out.push_back(Spirv[i + 1]); Out.push_back(DecoFlat);
					i += wc; continue;
				}
			}
			if (!drop) Out.insert(Out.end(), Spirv.begin() + i, Spirv.begin() + i + wc);
			i += wc;
		}
		Spirv = std::move(Out);
	}

	void LegalizeNonFiniteConstants(std::vector<uint32_t>& Spirv)
	{
		if (Spirv.size() < 5) return;
		const uint32_t OpTypeFloatOp = 22;
		const uint32_t OpConstantOp = 43;
		std::unordered_map<uint32_t, bool> Float32Types;
		size_t i = 5; // skip header
		while (i < Spirv.size())
		{
			const uint32_t Word0 = Spirv[i];
			const uint32_t Opcode = Word0 & 0xFFFFu;
			const uint32_t WordCount = Word0 >> 16;
			if (WordCount == 0 || i + WordCount > Spirv.size()) break;
			if (Opcode == OpTypeFloatOp && WordCount >= 3)
			{
				Float32Types[Spirv[i + 1]] = (Spirv[i + 2] == 32);
			}
			else if (Opcode == OpConstantOp && WordCount == 4)
			{
				auto It = Float32Types.find(Spirv[i + 1]);
				if (It != Float32Types.end() && It->second)
				{
					const uint32_t Bits = Spirv[i + 3];
					const uint32_t Exp = (Bits >> 23) & 0xFFu;
					const uint32_t Mant = Bits & 0x7FFFFFu;
					if (Exp == 0xFFu)
					{
						if (Mant == 0) // +/-Inf -> +/-FLT_MAX
						{
							Spirv[i + 3] = (Bits & 0x80000000u) | 0x7F7FFFFFu;
						}
						else // NaN -> 0
						{
							Spirv[i + 3] = 0u;
						}
					}
				}
			}
			i += WordCount;
		}
	}
}

	// WGSL forbids implicit-LOD texture sampling (textureSample) outside uniform
	// control flow, and Dawn enforces it at runtime too -- UE material shaders
	// call Texture2DSample inside if/loop branches constantly, so DXC's
	// OpImageSample*ImplicitLod lands in non-uniform flow and Tint's WgslFromIR
	// rejects the module ("'textureSample' must only be called from uniform
	// control flow"), the dominant reason material shader maps failed to cook for
	// WebGPU. Rewrite every implicit-LOD image sample to an explicit-LOD sample at
	// Lod 0.0 (textureSampleLevel), which needs no derivatives and is legal
	// anywhere. Menu/UI content samples full-res textures so losing derivative
	// mip-selection is visually moot. Handles Sample/SampleDref/SampleProj/
	// SampleProjDref; preserves ConstOffset/Offset; drops Bias/MinLod; injects a
	// `float 0.0` constant if none exists.
	void ForceExplicitLodSamples(std::vector<uint32_t>& Spirv)
	{
		if (Spirv.size() < 5) return;
		const uint32_t OpTypeFloat = 22, OpConstant = 43, OpFunction = 54;
		auto ClassifyImplicit = [](uint32_t op, uint32_t& TargetOp, uint32_t& NFixed) -> bool {
			switch (op) {
				case 87: TargetOp = 88; NFixed = 4; return true; // OpImageSampleImplicitLod
				case 89: TargetOp = 90; NFixed = 5; return true; // OpImageSampleDrefImplicitLod (+Dref)
				case 91: TargetOp = 92; NFixed = 4; return true; // OpImageSampleProjImplicitLod
				case 93: TargetOp = 94; NFixed = 5; return true; // OpImageSampleProjDrefImplicitLod (+Dref)
				default: return false;
			}
		};
		// Pass 1: float32 type id, an existing float 0.0 constant, presence of a
		// function + any implicit sample.
		uint32_t FloatTypeId = 0, ZeroConstId = 0, T = 0, N = 0;
		bool AnyImplicit = false, HasFunc = false;
		size_t i = 5;
		while (i < Spirv.size()) {
			const uint32_t w0 = Spirv[i], op = w0 & 0xFFFFu, wc = w0 >> 16;
			if (wc == 0 || i + wc > Spirv.size()) break;
			if (op == OpTypeFloat && wc >= 3 && Spirv[i + 2] == 32u) FloatTypeId = Spirv[i + 1];
			if (op == OpConstant && wc == 4 && FloatTypeId != 0 && Spirv[i + 1] == FloatTypeId && Spirv[i + 3] == 0u) ZeroConstId = Spirv[i + 2];
			if (op == OpFunction) HasFunc = true;
			if (ClassifyImplicit(op, T, N)) AnyImplicit = true;
			i += wc;
		}
		if (!AnyImplicit || FloatTypeId == 0 || !HasFunc) return;
		const bool bInject = (ZeroConstId == 0);
		if (bInject) { ZeroConstId = Spirv[3]; Spirv[3] = ZeroConstId + 1u; } // new id from bound, bump bound
		// Pass 2: rebuild -- inject the constant before the first function and
		// rewrite each implicit sample to explicit Lod 0.0.
		std::vector<uint32_t> Out; Out.reserve(Spirv.size() + 8);
		Out.insert(Out.end(), Spirv.begin(), Spirv.begin() + 5);
		bool bInjected = !bInject;
		i = 5;
		while (i < Spirv.size()) {
			const uint32_t w0 = Spirv[i], op = w0 & 0xFFFFu, wc = w0 >> 16;
			if (wc == 0 || i + wc > Spirv.size()) { Out.insert(Out.end(), Spirv.begin() + i, Spirv.end()); break; }
			if (!bInjected && op == OpFunction) {
				Out.push_back((4u << 16) | OpConstant); Out.push_back(FloatTypeId); Out.push_back(ZeroConstId); Out.push_back(0u);
				bInjected = true;
			}
			uint32_t TargetOp = 0, NFixed = 0;
			if (ClassifyImplicit(op, TargetOp, NFixed) && wc >= 1 + NFixed) {
				uint32_t constOffId = 0, offId = 0;
				const size_t maskIdx = i + 1 + NFixed;
				if (wc > 1 + NFixed) {
					const uint32_t mask = Spirv[maskIdx];
					size_t opnd = maskIdx + 1;
					if (mask & 0x1u) opnd += 1;                                              // Bias -> drop
					if (mask & 0x2u) opnd += 1;                                              // Lod (unexpected)
					if (mask & 0x4u) opnd += 2;                                              // Grad (unexpected)
					if ((mask & 0x8u)  && opnd < i + wc) { constOffId = Spirv[opnd]; opnd += 1; } // ConstOffset -> keep
					if ((mask & 0x10u) && opnd < i + wc) { offId      = Spirv[opnd]; opnd += 1; } // Offset -> keep
					// higher bits (ConstOffsets/Sample/MinLod) unread -> dropped
				}
				uint32_t newMask = 0x2u; // Lod
				std::vector<uint32_t> ops; ops.push_back(ZeroConstId);
				if (constOffId) { newMask |= 0x8u;  ops.push_back(constOffId); }
				if (offId)      { newMask |= 0x10u; ops.push_back(offId); }
				const uint32_t newWc = 1u + NFixed + 1u + (uint32_t)ops.size();
				Out.push_back((newWc << 16) | TargetOp);
				for (uint32_t k = 1; k <= NFixed; ++k) Out.push_back(Spirv[i + k]);
				Out.push_back(newMask);
				for (uint32_t o : ops) Out.push_back(o);
				i += wc; continue;
			}
			Out.insert(Out.end(), Spirv.begin() + i, Spirv.begin() + i + wc);
			i += wc;
		}
		Spirv = std::move(Out);
	}

	// WGSL requires derivative builtins (dpdx/dpdy/fwidth, SPIR-V opcodes
	// 207..215) in uniform control flow, with no explicit-LOD escape hatch. UE's
	// ES3.1 mobile base-pass PS uses them in non-uniform flow, which failed every
	// material's shader map (WorldGridMaterial included) and stopped the shader
	// library from being emitted. Rewrite each derivative to OpCopyObject of its
	// operand (dpdx(x) -> x): the derivative's Result Type equals the operand
	// type, so this is a type-valid single-word opcode swap. The menu needs no
	// real derivatives -- texture samples are already forced to LOD 0.
	void NeutralizeDerivatives(std::vector<uint32_t>& Spirv)
	{
		if (Spirv.size() < 5) return;
		const uint32_t OpCopyObject = 83;
		size_t i = 5;
		while (i < Spirv.size()) {
			const uint32_t w0 = Spirv[i], op = w0 & 0xFFFFu, wc = w0 >> 16;
			if (wc == 0 || i + wc > Spirv.size()) break;
			if (op >= 207u && op <= 215u && wc == 4) {
				Spirv[i] = (4u << 16) | OpCopyObject;
			}
			i += wc;
		}
	}

// PS binding-namespace separation: VS and PS are cross-compiled INDEPENDENTLY, so each
// stage's first uniform buffer lands at @binding(0). Merged into one WebGPU bind group
// they collide (one binding = one buffer) and the VS ViewProjection is lost -> geometry
// off-screen. Offset the FRAGMENT shader's SPIR-V Binding decorations so VS (unchanged)
// and PS never share a binding number. Applied BEFORE legalize (-> WGSL @binding) and
// reflection (-> UE ParameterMap) so WGSL, ParameterMap and the runtime stay consistent.
static bool SpirvIsFragment(const std::vector<uint32_t>& Spirv)
{
	if (Spirv.size() < 5) return false;
	const uint32_t OpEntryPointOp = 15, ExecModelFragment = 4;
	size_t i = 5;
	while (i < Spirv.size())
	{
		const uint32_t w0 = Spirv[i], op = w0 & 0xFFFFu, wc = w0 >> 16;
		if (wc == 0 || i + wc > Spirv.size()) break;
		if (op == OpEntryPointOp && wc >= 2) return Spirv[i + 1] == ExecModelFragment;
		i += wc;
	}
	return false;
}

static void OffsetSpirvBindings(std::vector<uint32_t>& Spirv, uint32_t Offset)
{
	if (Spirv.size() < 5) return;
	const uint32_t OpDecorateOp = 71, DecoBinding = 33;
	size_t i = 5;
	while (i < Spirv.size())
	{
		const uint32_t w0 = Spirv[i], op = w0 & 0xFFFFu, wc = w0 >> 16;
		if (wc == 0 || i + wc > Spirv.size()) break;
		if (op == OpDecorateOp && wc >= 4 && Spirv[i + 2] == DecoBinding) { Spirv[i + 3] += Offset; }
		i += wc;
	}
}

static FDawnTintCookResult DawnLegalizeAndCookImpl(const unsigned int* SpirvWords, unsigned int SpirvWordCount)
{
	FDawnTintCookResult Out = {};

	std::vector<uint32_t> Spirv(SpirvWords, SpirvWords + SpirvWordCount);
	// Keep the pre-legalization module: DECLARED-resource reflection must run
	// on it (legalization's dead-resource elimination removes unused
	// resources, but UE's parameter map needs entries for every DECLARED
	// non-optional parameter -- Bind() fatals otherwise, seen live on
	// FLumenCardCS's LumenCardOutputs). Post-legalization reflection then
	// marks which of those survive into the actual WGSL (bDeclaredOnly=0).
	// Separate the PS binding namespace from the VS (100 offset; binding numbers stay < 1000).
	if (SpirvIsFragment(Spirv)) { OffsetSpirvBindings(Spirv, 100u); }
	const std::vector<uint32_t> PreLegalizeSpirv = Spirv;

	std::string LegalizeLog;
	if (!Legalize(Spirv, LegalizeLog))
	{
		std::string Msg = "spirv-opt legalize failed: " + LegalizeLog;
		Out.Success = 0;
		Out.Diagnostic = DupToMalloc(Msg, Out.DiagnosticLen);
		return Out;
	}

	// Reflect AFTER legalization (dead-resource elimination already ran)
	// but BEFORE StripReflectInfo removes the OpName/OpDecorate info this
	// needs — see the comment on Legalize()/StripReflectInfo() above for
	// why this exact ordering matters (a real UE uniform buffer like
	// `View` declares ~100 individually-bound resources; only the handful
	// an actual shader uses should survive to be reported/bound).
	std::string DisassemblyError;
	FGlobalsInfo Globals;
	std::vector<FBinding> ReflectedBindings = ReflectBindings(PreLegalizeSpirv, DisassemblyError, &Globals);
	{
		std::string UsedError;
		const std::vector<FBinding> UsedBindings = ReflectBindings(Spirv, UsedError, nullptr);
		for (FBinding& Declared : ReflectedBindings)
		{
			bool bUsed = false;
			for (const FBinding& U : UsedBindings)
			{
				if (U.Set == Declared.Set && U.Binding == Declared.Binding) { bUsed = true; break; }
			}
			Declared.bDeclaredOnly = !bUsed;
		}
	}
	std::string ReflectionSummary = DisassemblyError.empty() ? SummarizeBindings(ReflectedBindings) : DisassemblyError;

	// Debug aid (env-var gated): dump the post-legalization, pre-strip
	// disassembly to a file, for diagnosing reflection mismatches directly
	// against the real SPIR-V text instead of guessing.
	if (const char* DumpPath = std::getenv("DAWN_DUMP_SPIRV_DIS"))
	{
		spvtools::SpirvTools DumpTools(SPV_ENV_VULKAN_1_1);
		std::string DumpText;
		DumpTools.Disassemble(Spirv, &DumpText, SPV_BINARY_TO_TEXT_OPTION_NO_HEADER | SPV_BINARY_TO_TEXT_OPTION_FRIENDLY_NAMES);
		FILE* F = std::fopen(DumpPath, "w");
		if (F) { std::fwrite(DumpText.data(), 1, DumpText.size(), F); std::fclose(F); }
	}

	std::string StripLog;
	if (!StripReflectInfo(Spirv, StripLog))
	{
		std::string Msg = "spirv-opt strip-reflect failed: " + StripLog;
		Out.Success = 0;
		Out.Diagnostic = DupToMalloc(Msg, Out.DiagnosticLen);
		return Out;
	}

	LegalizeNonFiniteConstants(Spirv);
	StripEarlyFragmentTests(Spirv);
	StripViewportIndexLayer(Spirv);
	ForceExplicitLodSamples(Spirv);
	NeutralizeDerivatives(Spirv);

	std::string Wgsl, TintError;
	if (!SpirvToWgsl(Spirv, Wgsl, TintError))
	{
		std::string Msg = "Tint SPIR-V->WGSL failed: " + TintError + "\nReflected bindings before failure:\n" + ReflectionSummary;
		Out.Success = 0;
		Out.Diagnostic = DupToMalloc(Msg, Out.DiagnosticLen);
		return Out;
	}

	Out.Success = 1;
	Out.Wgsl = DupToMalloc(Wgsl, Out.WgslLen);
	Out.Diagnostic = DupToMalloc(ReflectionSummary, Out.DiagnosticLen);

	Out.GlobalsSet = Globals.bFound ? Globals.Set : -1;
	Out.GlobalsBinding = Globals.bFound ? Globals.Binding : -1;
	Out.NumLooseMembers = static_cast<unsigned int>(Globals.Members.size());
	if (Out.NumLooseMembers > 0)
	{
		Out.LooseMembers = static_cast<FDawnReflectedLooseMember*>(std::malloc(sizeof(FDawnReflectedLooseMember) * Out.NumLooseMembers));
		for (unsigned int i = 0; i < Out.NumLooseMembers; ++i)
		{
			const FLooseMember& Src = Globals.Members[i];
			FDawnReflectedLooseMember& Dst = Out.LooseMembers[i];
			Dst.ByteOffset = Src.Offset;
			Dst.ByteSize = Src.Size;
			std::memset(Dst.Name, 0, sizeof(Dst.Name));
			std::strncpy(Dst.Name, Src.Name.c_str(), sizeof(Dst.Name) - 1);
		}
	}

	Out.NumBindings = static_cast<unsigned int>(ReflectedBindings.size());
	if (std::getenv("DAWN_DEBUG_REFLECT")) { std::fprintf(stderr, "[reflect] Dawn_LegalizeAndCookSpirvToWgsl: ReflectedBindings.size()=%zu\n", ReflectedBindings.size()); }
	if (Out.NumBindings > 0)
	{
		Out.Bindings = static_cast<FDawnReflectedBinding*>(std::malloc(sizeof(FDawnReflectedBinding) * Out.NumBindings));
		for (unsigned int i = 0; i < Out.NumBindings; ++i)
		{
			const FBinding& Src = ReflectedBindings[i];
			FDawnReflectedBinding& Dst = Out.Bindings[i];
			Dst.Set = static_cast<unsigned int>(Src.Set);
			Dst.Binding = static_cast<unsigned int>(Src.Binding);
			Dst.Kind = static_cast<unsigned int>(Src.Kind);
			Dst.bDeclaredOnly = Src.bDeclaredOnly ? 1u : 0u;
			std::memset(Dst.Name, 0, sizeof(Dst.Name));
			std::strncpy(Dst.Name, Src.Name.c_str(), sizeof(Dst.Name) - 1);
		}
	}

	return Out;
}

extern "C" FDawnTintCookResult Dawn_LegalizeAndCookSpirvToWgsl(const unsigned int* SpirvWords, unsigned int SpirvWordCount)
{
	InstallCrashGuardOnce();

	GCrashSignal = 0;
	if (sigsetjmp(GCrashJmp, 1) == 0)
	{
		GCrashGuardActive = 1;
		FDawnTintCookResult Out = DawnLegalizeAndCookImpl(SpirvWords, SpirvWordCount);
		GCrashGuardActive = 0;
		return Out;
	}

	// A guarded tint/spirv-tools call crashed; report it as a per-shader
	// compile failure instead of taking the whole cook process down.
	GCrashGuardActive = 0;
	FDawnTintCookResult Crash = {};
	Crash.Success = 0;
	Crash.GlobalsSet = -1;
	Crash.GlobalsBinding = -1;
	std::string Msg = "tint/spirv-tools INTERNAL COMPILER CRASH (signal " + std::to_string((int)GCrashSignal)
		+ ") captured by dawn_tint_bridge crash guard; see the tint ICE banner on stderr for the exact assert";
	Crash.Diagnostic = DupToMalloc(Msg, Crash.DiagnosticLen);
	return Crash;
}

extern "C" void Dawn_FreeTintCookResult(FDawnTintCookResult* Result)
{
	if (!Result) return;
	std::free(Result->Wgsl);
	std::free(Result->Diagnostic);
	std::free(Result->Bindings);
	std::free(Result->LooseMembers);
	Result->Wgsl = nullptr;
	Result->Diagnostic = nullptr;
	Result->Bindings = nullptr;
	Result->NumBindings = 0;
	Result->LooseMembers = nullptr;
	Result->NumLooseMembers = 0;
}
