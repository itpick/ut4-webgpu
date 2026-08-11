// DawnRHI — concrete FRHI* resource wrappers holding Dawn (webgpu.h) objects.
#pragma once

#include "DawnRHIPrivate.h"

class FDawnSamplerState : public FRHISamplerState
{
public:
	FSamplerStateInitializerRHI Initializer;
	WGPUSampler Sampler = nullptr;
	explicit FDawnSamplerState(const FSamplerStateInitializerRHI& InInit) : Initializer(InInit) {}
	~FDawnSamplerState()
	{
		if (Sampler) { wgpuSamplerRelease(Sampler); }
	}
};

class FDawnRasterizerState : public FRHIRasterizerState
{
public:
	FRasterizerStateInitializerRHI Initializer;
	explicit FDawnRasterizerState(const FRasterizerStateInitializerRHI& InInit) : Initializer(InInit) {}
	virtual bool GetInitializer(FRasterizerStateInitializerRHI& Init) override { Init = Initializer; return true; }
};

class FDawnDepthStencilState : public FRHIDepthStencilState
{
public:
	FDepthStencilStateInitializerRHI Initializer;
	explicit FDawnDepthStencilState(const FDepthStencilStateInitializerRHI& InInit) : Initializer(InInit) {}
	virtual bool GetInitializer(FDepthStencilStateInitializerRHI& Init) override { Init = Initializer; return true; }
};

class FDawnBlendState : public FRHIBlendState
{
public:
	FBlendStateInitializerRHI Initializer;
	explicit FDawnBlendState(const FBlendStateInitializerRHI& InInit) : Initializer(InInit) {}
	virtual bool GetInitializer(FBlendStateInitializerRHI& Init) override { Init = Initializer; return true; }
};

class FDawnVertexDeclaration : public FRHIVertexDeclaration
{
public:
	FVertexDeclarationElementList Elements;
	explicit FDawnVertexDeclaration(const FVertexDeclarationElementList& InElements) : Elements(InElements) {}
	virtual bool GetInitializer(FVertexDeclarationElementList& Init) override { Init = Elements; return true; }
};

// Milestone step 1: a shader's real reflected resource bindings, as read
// off the actual cooked SPIR-V/WGSL by DawnShaderCompiler.cpp/
// tools/dawn_tint_bridge.cpp's ReflectBindings (see DawnShaderCompiler.cpp's
// ParameterMap population) — replaces the old fixed
// {UB@0,Texture@1,Sampler@2} @group(0) assumption. Populated directly on
// FDawnVertexShader/FDawnPixelShader by whatever creates them from a real
// cook (today: DawnRHITestMain's real-shader render path, reading the
// sidecar reflection DawnCookProbe writes; tomorrow: a real engine
// shader-map loader reading Output.ParameterMap back out). Empty is
// legitimate for a shader that binds no resources.
enum class EDawnShaderBindingKind : uint8
{
	Unknown = 0,
	UniformBuffer = 1,
	Texture = 2,
	Sampler = 3,
};

struct FDawnShaderBinding
{
	uint32 Group = 0;
	uint32 Binding = 0;
	EDawnShaderBindingKind Kind = EDawnShaderBindingKind::Unknown;
	FString Name;
};

// Stage 1 shader contract (OUR OWN, not SimplyStream's): FRHICreateShaderDesc::Code
// is raw UTF-8 WGSL *source text* for a single entry point. The fixed entry
// point names below ("vs_main" / "fs_main") are this module's convention.
// A later stage replaces hand-authored WGSL with HLSL->SPIR-V->WGSL (Tint)
// output using the same Code-is-WGSL-text contract.
class FDawnVertexShader : public FRHIVertexShader
{
public:
	FString WGSLSource;
	WGPUShaderModule ShaderModule = nullptr;

	// Milestone step 1/2: real WGSL entry points keep their real name (e.g.
	// "ScreenPassVS" from a real cooked UE shader), not the Stage 1
	// hand-authored convention below -- see ParseWgslEntryPoint() in
	// DawnDynamicRHI.cpp, which sets this from the actual cooked WGSL text
	// (@vertex fn <Name>) at RHICreateVertexShader time. Defaults to the old
	// fixed name so the still-supported Stage 1/2 hand-authored WGSL test
	// paths (whose shaders really are named vs_main) need no changes.
	FString EntryPoint = TEXT("vs_main");

	// Milestone step 1: real reflected bindings (see FDawnShaderBinding above).
	// Empty for the Stage 1/2 hand-authored WGSL paths (those already agree
	// with the old fixed group(0){0,1,2} layout by construction); populated
	// for real-cooked shaders so RHICreateGraphicsPipelineState can build a
	// reflection-driven BindGroupLayout instead of assuming fixed slots.
	TArray<FDawnShaderBinding> Bindings;
};

class FDawnPixelShader : public FRHIPixelShader
{
public:
	FString WGSLSource;
	WGPUShaderModule ShaderModule = nullptr;
	FString EntryPoint = TEXT("fs_main"); // see FDawnVertexShader::EntryPoint above

	TArray<FDawnShaderBinding> Bindings;
};

class FDawnGraphicsPipelineState : public FRHIGraphicsPipelineState
{
public:
	WGPURenderPipeline Pipeline = nullptr;
	TRefCountPtr<FDawnVertexShader> VertexShader;
	TRefCountPtr<FDawnPixelShader> PixelShader;

	// Stage 2 convention (no shader reflection yet — that lands with the real
	// shader-cook pipeline): a single, fixed @group(0) bind group layout used
	// by every PSO — binding 0 = uniform buffer (vertex+fragment visible),
	// binding 1 = texture (fragment), binding 2 = sampler (fragment). Shaders
	// that don't use all three simply don't declare the unused bindings in
	// WGSL; Dawn only requires the *bind group* contents to satisfy whatever
	// the shader actually references, not the other way around.
	//
	// Multi-@group fix (this session): real reflected bindings carry their
	// own FDawnShaderBinding::Group (see above), but until now
	// RHICreateGraphicsPipelineState silently ignored it and always built
	// exactly one WGPUBindGroupLayout/PipelineLayout with
	// bindGroupLayoutCount=1 — every reflected binding was folded into
	// @group(0) regardless of what the real cooked WGSL declared. Empirically
	// every real UE global shader cooked through this toolchain so far
	// (ScreenPass.usf, DistortApplyScreenPS.usf — see HANDOFF.md "Update 12")
	// DOES land entirely in @group(0) (ShaderConductor's DXC->SPIR-V path
	// assigns one flat descriptor set here), so the bug was latent/untested
	// rather than a live symptom — but it was still a real correctness gap
	// for any future shader that legitimately reflects >1 group. Now:
	// BindGroupLayouts/PipelineLayout are keyed by the real distinct Group
	// values found across VS+PS bindings (still exactly 1 entry for every
	// real shader cooked so far, 0-length falls back to the old fixed
	// single-@group(0) 3-slot convention below for the Stage 1/2
	// hand-authored WGSL paths).
	TArray<WGPUBindGroupLayout> BindGroupLayouts;
	WGPUPipelineLayout PipelineLayout = nullptr;

	virtual FRHIGraphicsShader* GetShader(EShaderFrequency Frequency) const override
	{
		switch (Frequency)
		{
		case SF_Vertex: return VertexShader.GetReference();
		case SF_Pixel:  return PixelShader.GetReference();
		default:        return nullptr;
		}
	}

	~FDawnGraphicsPipelineState()
	{
		if (Pipeline) { wgpuRenderPipelineRelease(Pipeline); }
		if (PipelineLayout) { wgpuPipelineLayoutRelease(PipelineLayout); }
		for (WGPUBindGroupLayout Layout : BindGroupLayouts) { if (Layout) { wgpuBindGroupLayoutRelease(Layout); } }
	}
};

class FDawnBuffer : public FRHIBuffer
{
public:
	WGPUBuffer Buffer = nullptr;

	explicit FDawnBuffer(const FRHIBufferCreateDesc& CreateDesc) : FRHIBuffer(CreateDesc) {}
	~FDawnBuffer()
	{
		if (Buffer) { wgpuBufferRelease(Buffer); }
	}
};

class FDawnUniformBuffer : public FRHIUniformBuffer
{
public:
	WGPUBuffer Buffer = nullptr;

	explicit FDawnUniformBuffer(const FRHIUniformBufferLayout* InLayout) : FRHIUniformBuffer(InLayout) {}
	~FDawnUniformBuffer()
	{
		if (Buffer) { wgpuBufferRelease(Buffer); }
	}
};

class FDawnTexture : public FRHITexture
{
public:
	WGPUTexture Texture = nullptr;
	WGPUTextureView View = nullptr;

	explicit FDawnTexture(const FRHITextureCreateDesc& CreateDesc) : FRHITexture(CreateDesc) {}
	~FDawnTexture()
	{
		if (View) { wgpuTextureViewRelease(View); }
		if (Texture) { wgpuTextureRelease(Texture); }
	}

	virtual void* GetNativeResource() const override { return Texture; }
};

// Helper: EPixelFormat -> WGPUTextureFormat. Stage 1 only needs RGBA8Unorm;
// this is intentionally narrow and will grow as real content lands (Stage 2).
inline WGPUTextureFormat DawnTextureFormatFromPixelFormat(EPixelFormat Format)
{
	switch (Format)
	{
	case PF_R8G8B8A8:      return WGPUTextureFormat_RGBA8Unorm;
	case PF_B8G8R8A8:      return WGPUTextureFormat_BGRA8Unorm;
	case PF_FloatRGBA:     return WGPUTextureFormat_RGBA16Float;
	default:               return WGPUTextureFormat_RGBA8Unorm;
	}
}

inline WGPUVertexFormat DawnVertexFormatFromElementType(EVertexElementType Type)
{
	switch (Type)
	{
	case VET_Float1: return WGPUVertexFormat_Float32;
	case VET_Float2: return WGPUVertexFormat_Float32x2;
	case VET_Float3: return WGPUVertexFormat_Float32x3;
	case VET_Float4: return WGPUVertexFormat_Float32x4;
	default:         return WGPUVertexFormat_Float32x4;
	}
}

// Stage 2: the fixed depth-buffer format DawnRHI uses whenever a PSO/render
// pass asks for depth/stencil. Narrow on purpose (no stencil-capable variant
// selection yet) — broadened alongside real content needs.
inline WGPUTextureFormat DawnDepthStencilFormat()
{
	return WGPUTextureFormat_Depth24Plus;
}

inline WGPUCompareFunction DawnCompareFunctionFromRHI(ECompareFunction Fn)
{
	switch (Fn)
	{
	case CF_Less:         return WGPUCompareFunction_Less;
	case CF_LessEqual:    return WGPUCompareFunction_LessEqual;
	case CF_Greater:      return WGPUCompareFunction_Greater;
	case CF_GreaterEqual: return WGPUCompareFunction_GreaterEqual;
	case CF_Equal:        return WGPUCompareFunction_Equal;
	case CF_NotEqual:     return WGPUCompareFunction_NotEqual;
	case CF_Never:        return WGPUCompareFunction_Never;
	case CF_Always:       return WGPUCompareFunction_Always;
	default:              return WGPUCompareFunction_Always;
	}
}

inline WGPUFilterMode DawnFilterModeFromRHI(ESamplerFilter Filter)
{
	switch (Filter)
	{
	case SF_Point:              return WGPUFilterMode_Nearest;
	default:                    return WGPUFilterMode_Linear; // Bilinear/Trilinear/Anisotropic*
	}
}

inline WGPUMipmapFilterMode DawnMipFilterModeFromRHI(ESamplerFilter Filter)
{
	switch (Filter)
	{
	case SF_Point:  return WGPUMipmapFilterMode_Nearest;
	default:        return WGPUMipmapFilterMode_Linear;
	}
}

inline WGPUAddressMode DawnAddressModeFromRHI(ESamplerAddressMode Mode)
{
	switch (Mode)
	{
	case AM_Wrap:   return WGPUAddressMode_Repeat;
	case AM_Clamp:  return WGPUAddressMode_ClampToEdge;
	case AM_Mirror: return WGPUAddressMode_MirrorRepeat;
	case AM_Border: return WGPUAddressMode_ClampToEdge; // Dawn has no border-color address mode
	default:        return WGPUAddressMode_ClampToEdge;
	}
}
