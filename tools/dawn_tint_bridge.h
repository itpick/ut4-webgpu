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

// Explicit default visibility for the two real ABI entry points below —
// needed because the .so is built with -fvisibility=hidden (deliberately,
// to keep Tint/SPIRV-Tools/libc++ symbols this TU pulls in OFF the dynamic
// symbol table — see dawn_tint_bridge.h's top-of-file comment and
// tools/build_dawn_tint_thirdparty.sh's sibling bridge-.so build step).
// Without this, -fvisibility=hidden hides these two functions too and
// dlsym("Dawn_LegalizeAndCookSpirvToWgsl"/"Dawn_FreeTintCookResult") fails.
#if defined(__GNUC__) || defined(__clang__)
#define DAWN_TINT_BRIDGE_API __attribute__((visibility("default")))
#else
#define DAWN_TINT_BRIDGE_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Real (not hardcoded/guessed) resource-kind classification for a single
// reflected SPIR-V binding — milestone step 1 ("wire real SPIR-V
// reflection... replacing any hardcoded @group(0){0,1,2}"). Derived by
// walking the real SPIR-V module's OpVariable (StorageClass) ->
// OpTypePointer -> pointee-type chain (OpTypeStruct+Block == uniform
// buffer, OpTypeImage == texture, OpTypeSampler == sampler) — see
// ClassifyBindings() in dawn_tint_bridge.cpp.
typedef enum EDawnReflectedBindingKind
{
	DawnBindingKind_Unknown = 0,
	DawnBindingKind_UniformBuffer = 1,
	DawnBindingKind_Texture = 2,
	DawnBindingKind_Sampler = 3,
	// Storage resources (2026-08-11): required for compute (and any VS/PS
	// using StructuredBuffer SRVs). Without these every UAV/structured-buffer
	// parameter was classified Unknown, skipped from the parameter map, and
	// UE fataled with "Failure to bind non-optional shader resource parameter"
	// (seen live on TClearReplacementCS's ClearResource in the first
	// compute-enabled UT cook).
	DawnBindingKind_StorageBufferRW = 4, // HLSL RW*Buffer -> SPIR-V StorageBuffer/BufferBlock, writable
	DawnBindingKind_StorageBufferRO = 5, // HLSL StructuredBuffer/ByteAddressBuffer SRV (NonWritable)
	DawnBindingKind_StorageImage = 6,    // HLSL RWTexture* -> OpTypeImage Sampled=2
} EDawnReflectedBindingKind;

typedef struct FDawnReflectedBinding
{
	unsigned int Set;
	unsigned int Binding;
	unsigned int Kind; // EDawnReflectedBindingKind
	// 1 = declared in the HLSL/pre-legalization SPIR-V but eliminated by
	// legalization's dead-resource pass, i.e. NOT present in the cooked WGSL.
	// UE's parameter map must still contain such entries (FShaderParameter/
	// FShaderResourceParameter::Bind FATALS on missing non-optional
	// parameters even when a permutation does not use them -- seen live on
	// FLumenCardCS's LumenCardOutputs, 2026-08-11); runtime bind-group
	// construction must SKIP them (they have no WGSL binding).
	unsigned int bDeclaredOnly;
	char Name[128];    // real SPIR-V OpName text, truncated; always NUL-terminated
} FDawnReflectedBinding;

typedef struct FDawnReflectedLooseMember
{
	unsigned int ByteOffset; // real OpMemberDecorate Offset (DXC cbuffer layout)
	unsigned int ByteSize;   // next-offset delta; last member: derived from its SPIR-V type
	char Name[128];          // real OpMemberName text, truncated; always NUL-terminated
} FDawnReflectedLooseMember;

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

	// On success: malloc'd array of NumBindings real reflected resource
	// bindings (see FDawnReflectedBinding above) — every @group/@binding
	// the cooked WGSL actually declares, with its real classified resource
	// kind. Empty (Bindings==NULL, NumBindings==0) is legitimate for a
	// shader that binds no resources (e.g. NullPixelShader.usf).
	FDawnReflectedBinding* Bindings;
	unsigned int NumBindings;

	// On success: member-level reflection of the DXC loose-global uniform
	// buffer ("$Globals" -- where HLSL file-scope globals like UE's legacy
	// FShaderParameter loose parameters land). Each entry is one member with
	// its real std140/DXC cbuffer byte offset (from OpMemberDecorate Offset)
	// and size (next-offset delta; last member from its SPIR-V type). UE's
	// FShaderParameter::Bind requires these as LooseData parameter-map
	// entries -- without them every non-optional loose parameter fatals with
	// "Failure to bind non-optional shader parameter X" (seen live 2026-08-11
	// on FSimpleElementMaskedGammaPS's ClipRef in the first full UT cook).
	// GlobalsSet/GlobalsBinding give the $Globals buffer's own set/binding
	// (-1 if the shader has no $Globals block; then NumLooseMembers==0).
	FDawnReflectedLooseMember* LooseMembers;
	unsigned int NumLooseMembers;
	int GlobalsSet;
	int GlobalsBinding;
} FDawnTintCookResult;

// Full pipeline stage 2+3: legalized-SPIR-V-eligible input (still has
// ShaderConductor's UE-fork -fspv-reflect decorations — this function
// strips them itself via spvtools::CreateStripReflectInfoPass(), matching
// tools/hlsl_to_wgsl.cpp's LegalizeAndStrip+SpirvToWgsl steps) -> WGSL text
// + a human-readable resource-binding reflection summary (set/binding/name
// triples, read off the pre-legalization SPIR-V's OpName/OpDecorate) + a
// real structured binding manifest (FDawnReflectedBinding[]) for programmatic
// consumption (milestone step 1 — DawnShaderCompiler.cpp/DawnRHI use this,
// not the text summary, to build reflection-driven bind group layouts).
// Frees any previously-returned result's buffers are the CALLER's
// responsibility via Dawn_FreeTintCookResult.
DAWN_TINT_BRIDGE_API FDawnTintCookResult Dawn_LegalizeAndCookSpirvToWgsl(const unsigned int* SpirvWords, unsigned int SpirvWordCount);

DAWN_TINT_BRIDGE_API void Dawn_FreeTintCookResult(FDawnTintCookResult* Result);

#ifdef __cplusplus
}
#endif
