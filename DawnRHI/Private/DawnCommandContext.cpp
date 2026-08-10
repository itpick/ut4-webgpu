#include "DawnCommandContext.h"
#include "DawnDynamicRHI.h"
#include "DawnResources.h"

FDawnCommandContext::FDawnCommandContext(FDawnDynamicRHI* InOwner)
	: Owner(InOwner)
{
}

FDawnCommandContext::~FDawnCommandContext()
{
	if (ActiveRenderPass) { wgpuRenderPassEncoderRelease(ActiveRenderPass); }
	if (CommandEncoder) { wgpuCommandEncoderRelease(CommandEncoder); }
}

void FDawnCommandContext::EnsureCommandEncoder()
{
	if (!CommandEncoder)
	{
		WGPUCommandEncoderDescriptor Desc = {};
		CommandEncoder = wgpuDeviceCreateCommandEncoder(Owner->GetDevice(), &Desc);
	}
}

// ============================================================================
// REAL: minimum draw path (set pipeline, set vertex buffer, viewport, draw)
// ============================================================================

void FDawnCommandContext::RHISetStreamSource(uint32 StreamIndex, FRHIBuffer* VertexBuffer, uint32 Offset)
{
	// Stage 1: single vertex stream (index 0).
	BoundVertexBuffer = static_cast<FDawnBuffer*>(VertexBuffer);
	BoundVertexBufferOffset = Offset;
}

void FDawnCommandContext::RHISetViewport(float MinX, float MinY, float MinZ, float MaxX, float MaxY, float MaxZ)
{
	if (ActiveRenderPass)
	{
		wgpuRenderPassEncoderSetViewport(ActiveRenderPass, MinX, MinY, MaxX - MinX, MaxY - MinY, MinZ, MaxZ);
	}
}

void FDawnCommandContext::RHISetScissorRect(bool bEnable, uint32 MinX, uint32 MinY, uint32 MaxX, uint32 MaxY)
{
	// Stage 1 simplification: only the "enabled" case is wired up. Disabling
	// the scissor after having set one is a later-stage concern (we'd need
	// to remember the render target size to reset to full-rect).
	if (ActiveRenderPass && bEnable)
	{
		wgpuRenderPassEncoderSetScissorRect(ActiveRenderPass, MinX, MinY, MaxX - MinX, MaxY - MinY);
	}
}

void FDawnCommandContext::RHISetGraphicsPipelineState(FRHIGraphicsPipelineState* GraphicsState, uint32 StencilRef, bool bApplyAdditionalState)
{
	checkf(ActiveRenderPass, TEXT("DawnRHI Stage 1: RHISetGraphicsPipelineState must be called between RHIBeginRenderPass/RHIEndRenderPass"));
	FDawnGraphicsPipelineState* PSO = static_cast<FDawnGraphicsPipelineState*>(GraphicsState);
	wgpuRenderPassEncoderSetPipeline(ActiveRenderPass, PSO->Pipeline);
}

void FDawnCommandContext::RHIDrawPrimitive(uint32 BaseVertexIndex, uint32 NumPrimitives, uint32 NumInstances)
{
	checkf(ActiveRenderPass, TEXT("DawnRHI Stage 1: RHIDrawPrimitive requires an active render pass"));
	if (BoundVertexBuffer)
	{
		wgpuRenderPassEncoderSetVertexBuffer(ActiveRenderPass, 0, BoundVertexBuffer->Buffer, BoundVertexBufferOffset, WGPU_WHOLE_SIZE);
	}
	// Stage 1 only wires up triangle lists (matches FGraphicsPipelineStateInitializer::PrimitiveType == PT_TriangleList).
	const uint32 VertexCount = NumPrimitives * 3;
	wgpuRenderPassEncoderDraw(ActiveRenderPass, VertexCount, NumInstances, BaseVertexIndex, 0);
}

void FDawnCommandContext::RHIBeginRenderPass(const FRHIRenderPassInfo& InInfo, const TCHAR* InName)
{
	EnsureCommandEncoder();

	FDawnTexture* ColorTex = static_cast<FDawnTexture*>(InInfo.ColorRenderTargets[0].RenderTarget);
	checkf(ColorTex && ColorTex->View, TEXT("DawnRHI Stage 1: RHIBeginRenderPass requires ColorRenderTargets[0]"));

	WGPURenderPassColorAttachment ColorAttach = {};
	ColorAttach.view = ColorTex->View;
	// Stage 1 simplification: always clear-then-store. Honouring
	// InInfo.ColorRenderTargets[0].Action's load/store combinations is
	// Stage 2 scope (needs the full ERenderTargetActions decode table).
	ColorAttach.loadOp = WGPULoadOp_Clear;
	ColorAttach.storeOp = WGPUStoreOp_Store;
	ColorAttach.clearValue = { 0.0, 0.0, 0.0, 1.0 };
	ColorAttach.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

	WGPURenderPassDescriptor RpDesc = {};
	RpDesc.colorAttachmentCount = 1;
	RpDesc.colorAttachments = &ColorAttach;

	ActiveRenderPass = wgpuCommandEncoderBeginRenderPass(CommandEncoder, &RpDesc);
}

void FDawnCommandContext::RHIEndRenderPass()
{
	checkf(ActiveRenderPass, TEXT("DawnRHI: RHIEndRenderPass without a matching RHIBeginRenderPass"));
	wgpuRenderPassEncoderEnd(ActiveRenderPass);
	wgpuRenderPassEncoderRelease(ActiveRenderPass);
	ActiveRenderPass = nullptr;
}

void FDawnCommandContext::SubmitToQueue()
{
	if (CommandEncoder)
	{
		WGPUCommandBufferDescriptor CbDesc = {};
		WGPUCommandBuffer CmdBuf = wgpuCommandEncoderFinish(CommandEncoder, &CbDesc);
		wgpuQueueSubmit(Owner->GetQueue(), 1, &CmdBuf);
		wgpuCommandBufferRelease(CmdBuf);
		wgpuCommandEncoderRelease(CommandEncoder);
		CommandEncoder = nullptr;
	}
}

// ============================================================================
// Stub bodies (checkNoEntry) for everything a bare triangle never hits —
// compute, transitions/barriers, UAVs, breadcrumbs, indexed/indirect draws,
// depth-bounds, texture copy, legacy bound-shader-state path.
// ============================================================================

void FDawnCommandContext::RHISetComputePipelineState(FRHIComputePipelineState* ComputePipelineState)
{
	checkNoEntry();
}

void FDawnCommandContext::RHIDispatchComputeShader(uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ)
{
	checkNoEntry();
}

void FDawnCommandContext::RHIDispatchIndirectComputeShader(FRHIBuffer* ArgumentBuffer, uint32 ArgumentOffset)
{
	checkNoEntry();
}

void FDawnCommandContext::RHIBeginTransitions(TArrayView<const FRHITransition*> Transitions)
{
	checkNoEntry();
}

void FDawnCommandContext::RHIEndTransitions(TArrayView<const FRHITransition*> Transitions)
{
	checkNoEntry();
}

void FDawnCommandContext::RHIClearUAVFloat(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FVector4f& Values)
{
	checkNoEntry();
}

void FDawnCommandContext::RHIClearUAVUint(FRHIUnorderedAccessView* UnorderedAccessViewRHI, const FUintVector4& Values)
{
	checkNoEntry();
}

void FDawnCommandContext::RHISetShaderParameters(FRHIComputeShader* ComputeShader, TConstArrayView<uint8> InParametersData, TConstArrayView<FRHIShaderParameter> InParameters, TConstArrayView<FRHIShaderParameterResource> InResourceParameters, TConstArrayView<FRHIShaderParameterResource> InBindlessParameters)
{
	checkNoEntry();
}

void FDawnCommandContext::RHISetStaticUniformBuffers(const FUniformBufferStaticBindings& InUniformBuffers)
{
	checkNoEntry();
}

void FDawnCommandContext::RHISetStaticUniformBuffer(FUniformBufferStaticSlot Slot, FRHIUniformBuffer* UniformBuffer)
{
	checkNoEntry();
}

void FDawnCommandContext::RHIBeginBreadcrumbGPU(FRHIBreadcrumbNode* Breadcrumb)
{
	checkNoEntry();
}

void FDawnCommandContext::RHIEndBreadcrumbGPU(FRHIBreadcrumbNode* Breadcrumb)
{
	checkNoEntry();
}

void FDawnCommandContext::RHISetMultipleViewports(uint32 Count, const FViewportBounds* Data)
{
	checkNoEntry();
}

void FDawnCommandContext::RHIBeginRenderQuery(FRHIRenderQuery* RenderQuery)
{
	checkNoEntry();
}

void FDawnCommandContext::RHIEndRenderQuery(FRHIRenderQuery* RenderQuery)
{
	checkNoEntry();
}

void FDawnCommandContext::RHISetShaderParameters(FRHIGraphicsShader* Shader, TConstArrayView<uint8> InParametersData, TConstArrayView<FRHIShaderParameter> InParameters, TConstArrayView<FRHIShaderParameterResource> InResourceParameters, TConstArrayView<FRHIShaderParameterResource> InBindlessParameters)
{
	checkNoEntry();
}

void FDawnCommandContext::RHIDrawPrimitiveIndirect(FRHIBuffer* ArgumentBuffer, uint32 ArgumentOffset)
{
	checkNoEntry();
}

void FDawnCommandContext::RHIDrawIndexedPrimitive(FRHIBuffer* IndexBuffer, int32 BaseVertexIndex, uint32 FirstInstance, uint32 NumVertices, uint32 StartIndex, uint32 NumPrimitives, uint32 NumInstances)
{
	checkNoEntry();
}

void FDawnCommandContext::RHIDrawIndexedPrimitiveIndirect(FRHIBuffer* IndexBuffer, FRHIBuffer* ArgumentBuffer, uint32 ArgumentOffset)
{
	checkNoEntry();
}

void FDawnCommandContext::RHISetDepthBounds(float MinDepth, float MaxDepth)
{
	checkNoEntry();
}

void FDawnCommandContext::RHICopyTexture(FRHITexture* SourceTexture, FRHITexture* DestTexture, const FRHICopyTextureInfo& CopyInfo)
{
	checkNoEntry();
}

void FDawnCommandContext::RHICopyBufferRegion(FRHIBuffer* DestBuffer, uint64 DstOffset, FRHIBuffer* SourceBuffer, uint64 SrcOffset, uint64 NumBytes)
{
	checkNoEntry();
}

// RHISetBoundShaderState / RHISetDepthStencilState / RHISetRasterizerState /
// RHISetBlendState / RHIEnableDepthBoundsTest / RHISetComputeShader removed —
// see the note in DawnCommandContext.h (they belong to IRHICommandContextPSOFallback).
