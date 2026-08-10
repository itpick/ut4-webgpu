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
#include <dawn/dawn_proc.h>
#include <dawn/native/DawnNative.h>
THIRD_PARTY_INCLUDES_END

DECLARE_LOG_CATEGORY_EXTERN(LogDawnRHI, Log, All);
