// DawnRHI — concrete FRHI* resource wrappers holding Dawn (webgpu.h) objects.
#pragma once

#include "DawnRHIPrivate.h"

class FDawnSamplerState : public FRHISamplerState
{
public:
	FSamplerStateInitializerRHI Initializer;
	explicit FDawnSamplerState(const FSamplerStateInitializerRHI& InInit) : Initializer(InInit) {}
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
	static constexpr const TCHAR* EntryPoint = TEXT("vs_main");
};

class FDawnPixelShader : public FRHIPixelShader
{
public:
	FString WGSLSource;
	WGPUShaderModule ShaderModule = nullptr;
	static constexpr const TCHAR* EntryPoint = TEXT("fs_main");
};

class FDawnGraphicsPipelineState : public FRHIGraphicsPipelineState
{
public:
	WGPURenderPipeline Pipeline = nullptr;
	TRefCountPtr<FDawnVertexShader> VertexShader;
	TRefCountPtr<FDawnPixelShader> PixelShader;

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
