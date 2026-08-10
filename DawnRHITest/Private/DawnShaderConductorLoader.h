// Copyright: DawnRHI project (itpick/ut4-webgpu). Not part of, and does not
// touch, SimplyStream's closed WebGPURHI/WebGPUShaderFormat code.
//
// Runtime dlopen() wrapper around Epic's own open ThirdParty ShaderConductor
// (Engine/Source/ThirdParty/ShaderConductor — Microsoft's HLSL->SPIR-V/DXIL
// compiler, MIT licensed, NOT SimplyStream code).
//
// WHY THIS EXISTS (root cause of the previously-documented SPIRV-Tools
// SIGSEGV, see HANDOFF.md "shader-cook path: blocked" section):
//
// libShaderConductor.so is a fully self-contained shared object: it
// statically links its own private copy of libc++ (confirmed via
// `readelf --dyn-syms`: ~893 default-visibility, GLOBAL/WEAK-bound
// "std::__1::..." symbols, and NEITHER libc++.so NOR libstdc++.so appears
// in its NEEDED list — it never dynamically depends on an external C++
// runtime). DawnRHITest, once it links Core/RHI/etc, is ALSO a fully
// self-contained libc++ binary of its own (~1188 duplicate default-vis
// libc++ symbols, likewise zero external libc++/libstdc++ NEEDED entries).
//
// When libShaderConductor.so is linked normally (a compile-time
// PublicAdditionalLibraries dependency -> ELF DT_NEEDED), the dynamic
// linker loads it into the SAME global symbol scope as the executable at
// process startup. Per standard ELF symbol resolution, default-visibility
// GLOBAL/WEAK symbols can be interposed across that scope: internal calls
// made *inside* libShaderConductor.so (e.g. SPIRV-Tools' internal
// `spvtools::opt::IRContext`/`std::unordered_map` destructors) can resolve
// to the EXECUTABLE's copy of the same weak template instantiation instead
// of the .so's own copy. Confirmed by direct reproduction: a minimal
// standalone probe with ~0 duplicate libc++ symbols of its own calls
// `ShaderConductor::Compiler::Compile` (both with default Options and with
// `disableOptimizations=true`, i.e. both previously-crashing code paths)
// with ZERO crashes when the .so is loaded via dlopen() instead of a
// compile-time link — even without RTLD_DEEPBIND. We still request
// RTLD_DEEPBIND here (glibc: prefer the loaded object's own symbols over
// the global scope for its internal references) as belt-and-suspenders,
// since DawnRHITest itself has ~1200 duplicate libc++ symbols once linked,
// unlike the minimal repro probe.
//
// Because we never let the .so be a link-time dependency, every single
// symbol we need from it — including ShaderConductor::Blob's constructor/
// destructor/Data()/Size(), which are ordinary exported (non-template,
// non-inline) functions — must be resolved via dlsym(), never via the
// linker. See DawnShaderConductorLoader.cpp for the exact mangled names
// and the RawResultDesc/RawBlob memory-layout mirror this requires (Blob's
// only data member is a private `BlobImpl*`, so its layout is just a
// pointer — safe to mirror without touching the true class definition).

#pragma once

#include "CoreMinimal.h"

THIRD_PARTY_INCLUDES_START
#include <ShaderConductor/ShaderConductor.hpp>
THIRD_PARTY_INCLUDES_END

// Isolated, dlopen-based access to ShaderConductor's HLSL->SPIR-V compiler.
// Deliberately NOT a compile-time link dependency of anything (see header
// comment above for why) — construct one instance, call Init() once, then
// CompileHlslToSpirv() as needed.
//
// Lives in DawnRHITest for now (only consumer today is the shader-cook
// smoke test / offline probe). Promote into the DawnRHI module proper
// (as a real IShaderFormat-facing utility) once the HLSL->SPIR-V->WGSL
// chain is fully wired — see HANDOFF.md next steps.
class FDawnShaderConductorLoader
{
public:
	FDawnShaderConductorLoader() = default;
	~FDawnShaderConductorLoader();

	FDawnShaderConductorLoader(const FDawnShaderConductorLoader&) = delete;
	FDawnShaderConductorLoader& operator=(const FDawnShaderConductorLoader&) = delete;

	// Loads libdxcompiler.so + libShaderConductor.so via dlopen(RTLD_DEEPBIND)
	// and resolves the handful of symbols we need via dlsym. Returns false
	// (with OutError set) on any failure. Safe to call more than once
	// (idempotent once successfully initialised).
	bool Init(FString& OutError);

	bool IsInitialized() const { return bInitialized; }

	// Compiles HLSL source to SPIR-V using ShaderConductor::Compiler::Compile
	// under the hood, entirely through dlsym'd function pointers.
	bool CompileHlslToSpirv(
		const char* HlslSource,
		const char* FileName,
		const char* EntryPoint,
		ShaderConductor::ShaderStage Stage,
		bool bDisableOptimizations,
		TArray<uint32>& OutSpirv,
		FString& OutError);

private:
	void* DxcHandle = nullptr;
	void* ScHandle = nullptr;
	void* CompileFnPtr = nullptr;   // ShaderConductor::Compiler::Compile(SourceDesc,Options,TargetDesc) -> ResultDesc (sret ABI)
	void* BlobDataFnPtr = nullptr;  // ShaderConductor::Blob::Data() const
	void* BlobSizeFnPtr = nullptr;  // ShaderConductor::Blob::Size() const
	bool bInitialized = false;
};
