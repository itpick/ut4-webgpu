// Copyright: DawnRHI project (itpick/ut4-webgpu).
#pragma once

#include "CoreMinimal.h"

struct FShaderCompilerInput;
struct FShaderCompilerOutput;
class FShaderPreprocessOutput;

// The real cook chain, promoted from tools/hlsl_to_wgsl.cpp into the
// module: preprocessed HLSL (already macro/#include-expanded by UE's
// shared shader preprocessor, via FBaseShaderFormat::PreprocessShader) ->
// ShaderConductor::Compiler::Compile (dlopen/RTLD_DEEPBIND-isolated) ->
// SPIR-V -> spvtools::Optimizer (self-built SPIRV-Tools;
// RegisterLegalizationPasses() + CreateStripReflectInfoPass()) ->
// tint::spirv::reader::ReadIR -> tint::wgsl::writer::WgslFromIR (self-built
// Tint) -> WGSL text, packed as our own plain-UTF8-text shader-code
// container into Output.ShaderCode. On any failure, sets
// Output.bSucceeded = false and appends a precise FShaderCompilerError
// naming which stage failed and why.
void CompileDawnShader(
	const FShaderCompilerInput& Input,
	const FShaderPreprocessOutput& PreprocessOutput,
	FShaderCompilerOutput& Output);
