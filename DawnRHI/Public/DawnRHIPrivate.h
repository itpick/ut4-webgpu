// DawnRHI — shared includes for the Dawn-native WebGPU RHI backend.
//
// Only Dawn's public API (webgpu.h, dawn/native/DawnNative.h, dawn/dawn_proc.h)
// and Epic's own open RHI headers are used here. Nothing from SimplyStream's
// closed WebGPURHI or WebGPUShaderFormat modules is included or referenced.
#pragma once

#include "CoreMinimal.h"
#include "RHI.h"
#include "RHIResources.h"
#include "DynamicRHI.h"
#include "RHIContext.h"
#include "RHITextureInitializer.h"
#include "RHIBufferInitializer.h"
#include "RHITypes.h"

THIRD_PARTY_INCLUDES_START
#include <webgpu/webgpu.h>
#if !DAWNRHI_WASM
// dawn/native/DawnNative.h + dawn/dawn_proc.h are native-Dawn-only: they
// declare dawn::native::GetProcs()/dawnProcSetProcs(), the manual proc-table
// install that a statically-linked native Dawn build needs. Under
// emscripten's emdawnwebgpu port, there is no dawn::native at all — every
// webgpu.h call resolves straight to the port's JS glue / the browser's
// implicit navigator.gpu-backed device, so no proc table exists to install.
#include <dawn/dawn_proc.h>
#include <dawn/native/DawnNative.h>
#endif
THIRD_PARTY_INCLUDES_END

DECLARE_LOG_CATEGORY_EXTERN(LogDawnRHI, Log, All);
