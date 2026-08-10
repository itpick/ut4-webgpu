// Copyright: DawnRHI project (itpick/ut4-webgpu).
//
// Plain C ABI boundary around Tint/SPIRV-Tools. WHY THIS EXISTS: even with
// the self-built (correctly source-matched) Tint/SPIRV-Tools static libs,
// calling tint::Result<T> (a header-only template wrapping std::variant)
// directly from a TU compiled with UE's UBT flags reproduces a wall-#2-
// shaped crash ("bad_variant_access") — a *second*, distinct ABI-matching
// problem from wall #2's original "vendored vs self-built libtint.a"
// question. Root cause: tint::Result<T> is defined in a *header*
// (src/tint/utils/result.h), so UE's TU (DawnShaderCompiler.cpp,
// compiled with UBT's own flags/IncludeOrderVersion/BuildSettings)
// re-instantiates the same template locally with whatever ABI-relevant
// macros UBT's compile line implies, while Tint's own .a was built via a
// bare nix-shell clang++ invocation (tools/build_dawn_tint_thirdparty.sh)
// with different flags — an ODR violation: "the same" template ends up
// with two different memory layouts across the two TUs. Proven present
// even with bEnableExceptions=true (that only fixed the -fno-exceptions
// abort variant of the symptom, not the underlying layout mismatch); ruled
// out as a "wrong libs" issue by confirming the exact same self-built libs
// still work perfectly through the original standalone tools/hlsl_to_wgsl
// binary (compiled with the matching flags) on the identical input.
//
// THE FIX: never let a UBT-compiled TU touch tint::Result<T> (or any other
// Tint/SPIRV-Tools C++ type) directly. This header + dawn_tint_bridge.cpp
// are compiled ONCE, by tools/build_dawn_tint_thirdparty.sh, with the
// exact same flags used to self-build Tint/SPIRV-Tools, into
// libDawnTintBridge.a. DawnShaderCompiler.cpp (built by UBT with whatever
// flags the DawnShaderFormat module uses) only ever calls these plain-C
// functions — no template instantiation of any Tint type crosses the ABI
// boundary, so no layout mismatch is possible regardless of what compile
// flags UBT uses for the rest of the module.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FDawnTintCookResult
{
	int Success; // 0/1

	// On success: malloc'd UTF-8 WGSL text (WgslLen bytes, NOT null-terminated
	// guaranteed beyond the allocation — treat WgslLen as authoritative).
	char* Wgsl;
	unsigned int WgslLen;

	// On failure (or partial info on success, e.g. reflection log): malloc'd
	// UTF-8 diagnostic text.
	char* Diagnostic;
	unsigned int DiagnosticLen;
} FDawnTintCookResult;

// Full pipeline stage 2+3: legalized-SPIR-V-eligible input (still has
// ShaderConductor's UE-fork -fspv-reflect decorations — this function
// strips them itself via spvtools::CreateStripReflectInfoPass(), matching
// tools/hlsl_to_wgsl.cpp's LegalizeAndStrip+SpirvToWgsl steps) -> WGSL text
// + a human-readable resource-binding reflection summary (set/binding/name
// triples, read off the pre-legalization SPIR-V's OpName/OpDecorate).
// Frees any previously-returned result's buffers are the CALLER's
// responsibility via Dawn_FreeTintCookResult.
FDawnTintCookResult Dawn_LegalizeAndCookSpirvToWgsl(const unsigned int* SpirvWords, unsigned int SpirvWordCount);

void Dawn_FreeTintCookResult(FDawnTintCookResult* Result);

#ifdef __cplusplus
}
#endif
