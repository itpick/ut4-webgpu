// DawnRHI — IRHICommandContext backed by Dawn native (webgpu.h).
//
// Runs in "immediate" mode: every RHI* call below translates straight into a
// Dawn command-encoder/render-pass-encoder call (no deferred command-list
// recording/parallel translation). That is the deliberate Stage 1
// simplification — real parallel translation is Stage 2+ work.
//
// Override list mechanically derived from RHIContext.h (both IRHIComputeContext
// and IRHICommandContext pure virtuals, since IRHICommandContext : public
// IRHIComputeContext). Methods marked REAL in the .cpp have working bodies;
// the rest are checkNoEntry() stubs — see the module's stub manifest.
#pragma once

#include "DawnRHIPrivate.h"
#include "DawnResources.h"

class FDawnDynamicRHI;

class FDawnCommandContext final : public IRHICommandContext
{
public:
	explicit FDawnCommandContext(FDawnDynamicRHI* InOwner);
	virtual ~FDawnCommandContext();

	virtual void RHISetComputePipelineState(FRHIComputePipelineState* ComputePipelineState) override;
	virtual void RHIDispatchComputeShader(uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ) override;
	virtual void RHIDispatchIndirectComputeShader(FRHIBuffer* ArgumentBuffer, uint32 ArgumentOffset) override;
	virtual void RHIBeginTransitions(TArrayView<const FRHITransition*> Transitions) override;
	virtual void RHIEndTransitions(TArrayView<const FRHITransition*> Transitions) override;
	virtual void RHIClearUAVFloat(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FVector4f& Values) override;
	virtual void RHIClearUAVUint(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FUintVector4& Values) override;
	virtual void RHISetShaderParameters(FRHIComputeShader* ComputeShader, TConstArrayView<uint8> InParametersData, TConstArrayView<FRHIShaderParameter> InParameters, TConstArrayView<FRHIShaderParameterResource> InResourceParameters, TConstArrayView<FRHIShaderParameterResource> InBindlessParameters) override;
	virtual void RHISetStaticUniformBuffers(const FUniformBufferStaticBindings& InUniformBuffers) override;
	virtual void RHISetStaticUniformBuffer(FUniformBufferStaticSlot Slot, FRHIUniformBuffer* UniformBuffer) override;
	virtual void RHIBeginBreadcrumbGPU(FRHIBreadcrumbNode* Breadcrumb) override;
	virtual void RHIEndBreadcrumbGPU(FRHIBreadcrumbNode* Breadcrumb) override;
	virtual void RHISetMultipleViewports(uint32 Count, const FViewportBounds* Data) override;
	virtual void RHIBeginRenderQuery(FRHIRenderQuery* RenderQuery) override;
	virtual void RHIEndRenderQuery(FRHIRenderQuery* RenderQuery) override;
	virtual void RHISetStreamSource(uint32 StreamIndex, FRHIBuffer* VertexBuffer, uint32 Offset) override;
	virtual void RHISetViewport(float MinX, float MinY, float MinZ, float MaxX, float MaxY, float MaxZ) override;
	virtual void RHISetScissorRect(bool bEnable, uint32 MinX, uint32 MinY, uint32 MaxX, uint32 MaxY) override;
	virtual void RHISetGraphicsPipelineState(FRHIGraphicsPipelineState* GraphicsState, uint32 StencilRef, bool bApplyAdditionalState) override;
	virtual void RHISetShaderParameters(FRHIGraphicsShader* Shader, TConstArrayView<uint8> InParametersData, TConstArrayView<FRHIShaderParameter> InParameters, TConstArrayView<FRHIShaderParameterResource> InResourceParameters, TConstArrayView<FRHIShaderParameterResource> InBindlessParameters) override;
	virtual void RHIDrawPrimitive(uint32 BaseVertexIndex, uint32 NumPrimitives, uint32 NumInstances) override;
	virtual void RHIDrawPrimitiveIndirect(FRHIBuffer* ArgumentBuffer, uint32 ArgumentOffset) override;
	virtual void RHIDrawIndexedPrimitive(FRHIBuffer* IndexBuffer, int32 BaseVertexIndex, uint32 FirstInstance, uint32 NumVertices, uint32 StartIndex, uint32 NumPrimitives, uint32 NumInstances) override;
	virtual void RHIDrawIndexedPrimitiveIndirect(FRHIBuffer* IndexBuffer, FRHIBuffer* ArgumentBuffer, uint32 ArgumentOffset) override;
	virtual void RHISetDepthBounds(float MinDepth, float MaxDepth) override;
	virtual void RHIBeginRenderPass(const FRHIRenderPassInfo& InInfo, const TCHAR* InName) override;
	virtual void RHIEndRenderPass() override;
	virtual void RHICopyTexture(FRHITexture* SourceTexture, FRHITexture* DestTexture, const FRHICopyTextureInfo& CopyInfo) override;
	virtual void RHICopyBufferRegion(FRHIBuffer* DestBuffer, uint64 DstOffset, FRHIBuffer* SourceBuffer, uint64 SrcOffset, uint64 NumBytes) override;

	// NOTE: RHISetBoundShaderState / RHISetDepthStencilState / RHISetRasterizerState /
	// RHISetBlendState / RHIEnableDepthBoundsTest / RHISetComputeShader / the
	// FGraphicsPipelineStateInitializer overload of RHISetGraphicsPipelineState
	// are declared on IRHICommandContextPSOFallback (the legacy non-PSO
	// state-setting path), not on IRHICommandContext itself — UBT/clang
	// confirmed this (`does not override any member functions`) when they
	// were mechanically included from the same header text. Stage 1 uses
	// the modern baked-PSO path exclusively, so they're intentionally
	// omitted rather than stubbed.

	// Called by FDawnDynamicRHI::RHIFinalizeContexts/RHISubmitCommandLists.
	void SubmitToQueue();

private:
	FDawnDynamicRHI* Owner = nullptr;

	WGPUCommandEncoder CommandEncoder = nullptr;
	WGPURenderPassEncoder ActiveRenderPass = nullptr;
	TRefCountPtr<FDawnBuffer> BoundVertexBuffer;
	uint32 BoundVertexBufferOffset = 0;
	FDawnGraphicsPipelineState* CurrentPSO = nullptr; // non-owning; set by RHISetGraphicsPipelineState

	void EnsureCommandEncoder();
};
