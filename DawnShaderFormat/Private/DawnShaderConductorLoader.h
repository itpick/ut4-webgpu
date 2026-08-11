// Copyright: DawnRHI project (itpick/ut4-webgpu). Not part of, and does not
// touch, SimplyStream's closed WebGPURHI/WebGPUShaderFormat code.
//
// Promoted, verbatim in behaviour, from
// Engine/Source/Programs/DawnRHITest/Private/DawnShaderConductorLoader.h
// (the DawnRHITest shader-cook milestone) into the real IShaderFormat
// module. See that file's history / HANDOFF.md's "shader-cook path: wall
// #1" section for the full root-cause writeup of why ShaderConductor must
// be dlopen()'d (RTLD_DEEPBIND) rather than compile-time linked: linking
// libShaderConductor.so normally makes it an ELF DT_NEEDED dependency in
// the same global symbol scope as this module's host process, and default-
// visibility libc++ template symbols inside the .so's own statically-
// linked libc++ copy get interposed by the host's duplicate copies of the
// same weak symbols — confirmed via readelf, reproduced/fixed via a
// minimal standalone probe (tools/sc_deepbind_probe.cpp in the repo).
#pragma once

#include "CoreMinimal.h"

THIRD_PARTY_INCLUDES_START
#include <ShaderConductor/ShaderConductor.hpp>
THIRD_PARTY_INCLUDES_END

// Isolated, dlopen-based access to ShaderConductor's HLSL->SPIR-V compiler.
// Deliberately NOT a compile-time link dependency of anything. Construct
// one instance (the module keeps a single lazily-initialized instance —
// see DawnShaderCompiler.cpp), call Init() once, then CompileHlslToSpirv()
// as needed.
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
	// under the hood, entirely through dlsym'd function pointers. The
	// returned SPIR-V still carries ShaderConductor's UE-fork
	// SPV_GOOGLE_hlsl_functionality1 reflection decorations (see
	// DawnShaderCompiler.cpp) — DawnShaderCompiler's own binding-reflection
	// pass reads bindings straight off the *legalized* SPIR-V's ordinary
	// OpDecorate Binding/DescriptorSet words (via SPIRV-Tools' binary
	// parser), so no separate ShaderConductor reflection-language pass is
	// needed here.
	bool CompileHlslToSpirv(
		const char* HlslSource,
		const char* FileName,
		const char* EntryPoint,
		ShaderConductor::ShaderStage Stage,
		bool bDisableOptimizations,
		bool bHlsl2021,
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

// Process-wide lazily-initialized loader (ShaderConductor/DXC keep
// internal global state that isn't safe to duplicate per shader-compile
// job — see the .cpp's shutdown comment). Shared by every
// CompileDawnShader() call in this module.
FDawnShaderConductorLoader& GetDawnShaderConductorLoader();
