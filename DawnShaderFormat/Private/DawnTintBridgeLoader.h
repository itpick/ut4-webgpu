// Copyright: DawnRHI project (itpick/ut4-webgpu).
//
// Wall #3 (found this session): even a plain-C-ABI static archive
// (libDawnTintBridge.a, compiled with Tint's own matching flags per
// tools/dawn_tint_bridge.h's header comment) reproduces a fresh
// "bad_variant_access" crash once linked directly into a UE
// executable — NOT an ABI-layout mismatch this time (proven: the exact
// same bridge code, called on the exact same SPIR-V bytes dumped from
// inside the UE process, succeeds perfectly as a bare standalone binary
// outside UE). Root cause: static linking gives the WHOLE final
// executable exactly one copy of `operator new`/`delete`/`malloc` — UE's
// own Mimalloc-backed override wins for every allocation, including ones
// made deep inside Tint's/SPIRV-Tools' statically-linked code. Something
// about that override (likely aligned-new / large-allocation edge cases
// libc++'s std::variant-backed tint::Result<T> hits) corrupts state
// invisibly rather than crashing outright, producing exactly the
// "variant looks live but neither index matches" symptom.
//
// THE FIX: the same pattern that fixed wall #1 (ShaderConductor), applied
// to a static archive instead of a vendored .so this time — build the
// bridge as its OWN fully self-contained shared object (statically
// linking libc++/libc++abi INTO libDawnTintBridge.so — see
// tools/build_dawn_tint_thirdparty.sh) and dlopen(RTLD_DEEPBIND) it at
// runtime instead of a compile-time link. RTLD_DEEPBIND makes the .so
// prefer its OWN embedded operator new/delete/allocator machinery over
// the host process's weak-symbol override for calls made *from within*
// the .so — exactly the same mechanism, for the same reason, as
// DawnShaderConductorLoader.h.
#pragma once

#include "CoreMinimal.h"

extern "C"
{
	#include "dawn_tint_bridge.h"
}

class FDawnTintBridgeLoader
{
public:
	FDawnTintBridgeLoader() = default;
	~FDawnTintBridgeLoader();

	FDawnTintBridgeLoader(const FDawnTintBridgeLoader&) = delete;
	FDawnTintBridgeLoader& operator=(const FDawnTintBridgeLoader&) = delete;

	bool Init(FString& OutError);
	bool IsInitialized() const { return bInitialized; }

	// Thin wrapper around dlsym'd Dawn_LegalizeAndCookSpirvToWgsl /
	// Dawn_FreeTintCookResult. Caller must eventually call
	// Dawn_FreeTintCookResult on the (POD, plain-malloc'd) result — no
	// special ABI handling needed since FDawnTintCookResult is a trivial
	// C struct.
	FDawnTintCookResult LegalizeAndCookSpirvToWgsl(const uint32* SpirvWords, uint32 WordCount);
	void FreeResult(FDawnTintCookResult* Result);

private:
	void* Handle = nullptr;
	void* CookFnPtr = nullptr;
	void* FreeFnPtr = nullptr;
	bool bInitialized = false;
};

FDawnTintBridgeLoader& GetDawnTintBridgeLoader();
