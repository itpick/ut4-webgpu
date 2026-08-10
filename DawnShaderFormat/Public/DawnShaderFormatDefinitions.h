// Copyright: DawnRHI project (itpick/ut4-webgpu).
//
// Our own shader-format name, deliberately NOT one of the centrally
// registered NAME_VULKAN_*/NAME_PCD3D_*/... constants in
// RHIShaderFormatDefinitions.inl (those live in RHIDefinitions.h and are
// shared across every platform's shader format module) — this is a new,
// independent format we own end-to-end. Wiring it into a real
// ITargetPlatform's GetAllTargetedShaderFormats() (so a normal cook
// automatically requests it) is future work; see HANDOFF.md. Today it is
// discovered by FTargetPlatformManagerModule's generic
// "*ShaderFormat*"-wildcard module scan (this module is named
// "DawnShaderFormat", which matches) and can be driven directly through
// the real IShaderFormat/IShaderFormatModule interfaces, e.g. by
// DawnShaderFormatTest.
#pragma once

#include "CoreMinimal.h"

// The one shader format this module supports: HLSL cooked to our own
// plain-UTF8-WGSL-text container via ShaderConductor -> SPIRV-Tools ->
// Tint. Matches what FDawnDynamicRHI::RHICreateVertexShader/
// RHICreatePixelShader already expect (see DawnRHI/Private/DawnDynamicRHI.cpp).
#define DAWN_SHADER_FORMAT_NAME TEXT("SF_DAWN_WGSL")

inline const FName& GetDawnWgslShaderFormatName()
{
	static FName Name(DAWN_SHADER_FORMAT_NAME);
	return Name;
}
