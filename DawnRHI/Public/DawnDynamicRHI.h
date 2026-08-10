// DawnRHI — FDynamicRHI backed by Dawn native (webgpu.h), Vulkan backend.
//
// Override list below is mechanically derived from
// Engine/Source/Runtime/RHI/Public/DynamicRHI.h (extracted via a small
// script, not hand-transcribed) so signatures match exactly. Methods marked
// REAL in DawnDynamicRHI.cpp have working Dawn-backed bodies; everything
// else is checkNoEntry() stubbed and listed in the module's stub manifest.
#pragma once

#include "DawnRHIPrivate.h"

class FDawnCommandContext;

class FDawnDynamicRHI final : public FDynamicRHI
{
public:
	FDawnDynamicRHI();
	virtual ~FDawnDynamicRHI();

	// --- Mechanically-derived overrides of every FDynamicRHI pure virtual (55) ---
	virtual void Init() override;
	virtual void Shutdown() override;
	virtual const TCHAR* GetName() override;
	virtual void RHIEndFrame(const FRHIEndFrameArgs& Args) override;
	virtual FSamplerStateRHIRef RHICreateSamplerState(const FSamplerStateInitializerRHI& Initializer) override;
	virtual FRasterizerStateRHIRef RHICreateRasterizerState(const FRasterizerStateInitializerRHI& Initializer) override;
	virtual FDepthStencilStateRHIRef RHICreateDepthStencilState(const FDepthStencilStateInitializerRHI& Initializer) override;
	virtual FBlendStateRHIRef RHICreateBlendState(const FBlendStateInitializerRHI& Initializer) override;
	virtual FVertexDeclarationRHIRef RHICreateVertexDeclaration(const FVertexDeclarationElementList& Elements) override;
	virtual FPixelShaderRHIRef RHICreatePixelShader(const FRHICreateShaderDesc& CreateShaderDesc) override;
	virtual FVertexShaderRHIRef RHICreateVertexShader(const FRHICreateShaderDesc& CreateShaderDesc) override;
	virtual FGeometryShaderRHIRef RHICreateGeometryShader(const FRHICreateShaderDesc& CreateShaderDesc) override;
	virtual FComputeShaderRHIRef RHICreateComputeShader(const FRHICreateShaderDesc& CreateShaderDesc) override;
	virtual FGPUFenceRHIRef RHICreateGPUFence(const FName &Name) override;
	virtual FBoundShaderStateRHIRef RHICreateBoundShaderState(FRHIVertexDeclaration* VertexDeclaration, FRHIVertexShader* VertexShader, FRHIPixelShader* PixelShader, FRHIGeometryShader* GeometryShader) override;
	// NOTE: the mesh-shader RHICreateBoundShaderState overload and
	// RHIUpdateAllocationTags are gated behind platform macros
	// (PLATFORM_SUPPORTS_MESH_SHADERS / similar) that are off for this
	// build config, so they aren't part of FDynamicRHI's actual vtable here
	// — UBT confirmed (`does not override any member functions`). Omitted
	// rather than stubbed.
	virtual FGraphicsPipelineStateRHIRef RHICreateGraphicsPipelineState(const FGraphicsPipelineStateInitializer& Initializer) override;
	virtual FComputePipelineStateRHIRef RHICreateComputePipelineState(const FComputePipelineStateInitializer& Initializer) override;
	virtual FUniformBufferRHIRef RHICreateUniformBuffer(const void* Contents, const FRHIUniformBufferLayout* Layout, EUniformBufferUsage Usage, EUniformBufferValidation Validation) override;
	virtual void RHIUpdateUniformBuffer(FRHICommandListBase& RHICmdList, FRHIUniformBuffer* UniformBufferRHI, const void* Contents) override;
	virtual void RHIReplaceResources(FRHICommandListBase& RHICmdList, TArray<FRHIResourceReplaceInfo>&& ReplaceInfos) override;
	virtual FRHIBufferInitializer RHICreateBufferInitializer(FRHICommandListBase& RHICmdList, const FRHIBufferCreateDesc& CreateDesc) override;
	virtual FRHICalcTextureSizeResult RHICalcTexturePlatformSize(FRHITextureDesc const& Desc, uint32 FirstMipIndex) override;
	virtual void RHIGetTextureMemoryStats(FTextureMemoryStats& OutStats) override;
	virtual bool RHIGetTextureMemoryVisualizeData(FColor* TextureData, int32 SizeX, int32 SizeY, int32 Pitch, int32 PixelSize) override;
	virtual FRHITextureInitializer RHICreateTextureInitializer(FRHICommandListBase& RHICmdList, const FRHITextureCreateDesc& CreateDesc) override;
	virtual FTextureRHIRef RHIAsyncCreateTexture2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, ERHIAccess InResourceState, void** InitialMipData, uint32 NumInitialMips, const TCHAR* DebugName, FGraphEventRef& OutCompletionEvent) override;
	virtual FShaderResourceViewRHIRef RHICreateShaderResourceView(class FRHICommandListBase& RHICmdList, FRHIViewableResource* Resource, FRHIViewDesc const& ViewDesc) override;
	virtual FUnorderedAccessViewRHIRef RHICreateUnorderedAccessView(class FRHICommandListBase& RHICmdList, FRHIViewableResource* Resource, FRHIViewDesc const& ViewDesc) override;
	virtual uint32 RHIComputeMemorySize(FRHITexture* TextureRHI) override;
	virtual FRHILockTextureResult RHILockTexture(FRHICommandListImmediate& RHICmdList, const FRHILockTextureArgs& Arguments) override;
	virtual void RHIUnlockTexture(FRHICommandListImmediate& RHICmdList, const FRHILockTextureArgs& Arguments) override;
	virtual void RHIUpdateTexture2D(FRHICommandListBase& RHICmdList, FRHITexture* Texture, uint32 MipIndex, const struct FUpdateTextureRegion2D& UpdateRegion, uint32 SourcePitch, const uint8* SourceData) override;
	virtual void RHIUpdateTexture3D(FRHICommandListBase& RHICmdList, FRHITexture* Texture, uint32 MipIndex, const struct FUpdateTextureRegion3D& UpdateRegion, uint32 SourceRowPitch, uint32 SourceDepthPitch, const uint8* SourceData) override;
	virtual void RHIReadSurfaceData(FRHITexture* Texture, FIntRect Rect, TArray<FColor>& OutData, FReadSurfaceDataFlags InFlags) override;
	virtual void RHIMapStagingSurface(FRHITexture* Texture, FRHIGPUFence* Fence, void*& OutData, int32& OutWidth, int32& OutHeight, uint32 GPUIndex = 0) override;
	virtual void RHIUnmapStagingSurface(FRHITexture* Texture, uint32 GPUIndex = 0) override;
	virtual void RHIReadSurfaceFloatData(FRHITexture* Texture, FIntRect Rect, TArray<FFloat16Color>& OutData, ECubeFace CubeFace, int32 ArrayIndex, int32 MipIndex) override;
	virtual void RHIRead3DSurfaceFloatData(FRHITexture* Texture, FIntRect Rect, FIntPoint ZMinMax, TArray<FFloat16Color>& OutData) override;
	virtual FRenderQueryRHIRef RHICreateRenderQuery(ERenderQueryType QueryType) override;
	virtual bool RHIGetRenderQueryResult(FRHIRenderQuery* RenderQuery, uint64& OutResult, bool bWait, uint32 GPUIndex = INDEX_NONE) override;
	virtual FTextureRHIRef RHIGetViewportBackBuffer(FRHIViewport* Viewport) override;
	virtual FViewportRHIRef RHICreateViewport(void* WindowHandle, uint32 SizeX, uint32 SizeY, bool bIsFullscreen, EPixelFormat PixelFormat) override;
	virtual void RHIResizeViewport(FRHIViewport* Viewport, uint32 SizeX, uint32 SizeY, bool bIsFullscreen, EPixelFormat PixelFormat) override;
	virtual void RHIEndDrawingViewport(FRHICommandListImmediate& RHICmdList, FRHIViewport* Viewport, FRHIPresentArgs const& PresentArgs) override;
	virtual void RHITick(float DeltaTime) override;
	virtual void RHIBlockUntilGPUIdle() override;
	virtual bool RHIGetAvailableResolutions(FScreenResolutionArray& Resolutions, bool bIgnoreRefreshRate) override;
	virtual void RHIGetSupportedResolution(uint32& Width, uint32& Height) override;
	virtual void* RHIGetNativeDevice() override;
	virtual IRHICommandContext* RHIGetDefaultContext() override;
	virtual IRHIComputeContext* RHIGetCommandContext(ERHIPipeline Pipeline) override;
	virtual FRHIFinalizeContextsResult RHIFinalizeContexts(FRHIFinalizeContextsArgs&& Args) override;
	virtual void RHISubmitCommandLists(FRHISubmitCommandListsArgs&& Args) override;

	// --- Dawn device access (used by FDawnCommandContext / resource creation) ---
	WGPUInstance GetInstance() const { return Instance; }
	WGPUAdapter  GetAdapter()  const { return Adapter; }
	WGPUDevice   GetDevice()   const { return Device; }
	WGPUQueue    GetQueue()    const { return Queue; }

private:
	WGPUInstance Instance = nullptr;
	WGPUAdapter Adapter = nullptr;
	WGPUDevice Device = nullptr;
	WGPUQueue Queue = nullptr;

	TUniquePtr<FDawnCommandContext> CommandContext;

	void InitDawnDevice();
};
