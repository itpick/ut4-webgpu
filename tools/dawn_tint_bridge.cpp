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
#include <cstdlib>
#include <cstring>
#include <string>
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

	bool LegalizeAndStrip(std::vector<uint32_t>& Spirv, std::string& OutLog)
	{
		spvtools::Optimizer Opt(SPV_ENV_VULKAN_1_1);
		Opt.SetMessageConsumer([&OutLog](spv_message_level_t, const char*, const spv_position_t&, const char* Msg)
		{
			OutLog += Msg;
			OutLog += "\n";
		});
		Opt.RegisterLegalizationPasses();
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
		auto IrResult = tint::spirv::reader::ReadIR(Spirv, ReadOpts);
		if (IrResult != tint::Success)
		{
			OutError = "ReadIR: " + IrResult.Failure().reason;
			return false;
		}

		tint::wgsl::writer::Options WriteOpts = {};
		auto WgslResult = tint::wgsl::writer::WgslFromIR(IrResult.Get(), WriteOpts);
		if (WgslResult != tint::Success)
		{
			OutError = "WgslFromIR: " + WgslResult.Failure().reason;
			return false;
		}

		OutWgsl = WgslResult.Get().wgsl;
		return true;
	}

	// Best-effort textual reflection: disassemble the pre-legalization
	// SPIR-V (still has OpName/OpDecorate DescriptorSet/Binding) and scan
	// for those triples. Not a real SPIR-V walker — good enough to report
	// what a real UT4 shader actually binds, versus DawnRHI's current
	// fixed group(0){0,1,2} runtime assumption.
	std::string ReflectBindingsSummary(const std::vector<uint32_t>& Spirv)
	{
		spvtools::SpirvTools Tools(SPV_ENV_VULKAN_1_1);
		std::string Text;
		if (!Tools.Disassemble(Spirv, &Text, SPV_BINARY_TO_TEXT_OPTION_NO_HEADER | SPV_BINARY_TO_TEXT_OPTION_FRIENDLY_NAMES))
		{
			return "(disassembly failed, no reflection available)";
		}

		struct FBinding { std::string Name; int Set = -1; int Binding = -1; };
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
			if (Tokens[0] == "OpName" && Tokens.size() >= 3)
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
			else if (Tokens[0] == "OpDecorate" && Tokens.size() >= 4)
			{
				if (Tokens[2] == "DescriptorSet")
				{
					FindOrAdd(Tokens[1]).Set = std::atoi(Tokens[3].c_str());
				}
				else if (Tokens[2] == "Binding")
				{
					FindOrAdd(Tokens[1]).Binding = std::atoi(Tokens[3].c_str());
				}
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

		std::ostringstream Out;
		Out << Bindings.size() << " bound resource(s):\n";
		for (const FBinding& B : Bindings)
		{
			Out << "  set=" << B.Set << " binding=" << B.Binding << " name=\"" << B.Name << "\"\n";
		}
		return Out.str();
	}
}

extern "C" FDawnTintCookResult Dawn_LegalizeAndCookSpirvToWgsl(const unsigned int* SpirvWords, unsigned int SpirvWordCount)
{
	FDawnTintCookResult Out = {};

	std::vector<uint32_t> Spirv(SpirvWords, SpirvWords + SpirvWordCount);
	std::string ReflectionSummary = ReflectBindingsSummary(Spirv);

	std::string OptLog;
	if (!LegalizeAndStrip(Spirv, OptLog))
	{
		std::string Msg = "spirv-opt legalize+strip-reflect failed: " + OptLog;
		Out.Success = 0;
		Out.Diagnostic = DupToMalloc(Msg, Out.DiagnosticLen);
		return Out;
	}

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
	return Out;
}

extern "C" void Dawn_FreeTintCookResult(FDawnTintCookResult* Result)
{
	if (!Result) return;
	std::free(Result->Wgsl);
	std::free(Result->Diagnostic);
	Result->Wgsl = nullptr;
	Result->Diagnostic = nullptr;
}
