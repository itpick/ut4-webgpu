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
	CurrentPSO = PSO;
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
	// Stage 1/2 simplification: always clear-then-store. Honouring
	// InInfo.ColorRenderTargets[0].Action's load/store combinations is
	// later-stage scope (needs the full ERenderTargetActions decode table).
	ColorAttach.loadOp = WGPULoadOp_Clear;
	ColorAttach.storeOp = WGPUStoreOp_Store;
	ColorAttach.clearValue = { 0.0, 0.0, 0.0, 1.0 };
	ColorAttach.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

	WGPURenderPassDescriptor RpDesc = {};
	RpDesc.colorAttachmentCount = 1;
	RpDesc.colorAttachments = &ColorAttach;

	WGPURenderPassDepthStencilAttachment DepthAttach = {};
	FDawnTexture* DepthTex = static_cast<FDawnTexture*>(InInfo.DepthStencilRenderTarget.DepthStencilTarget);
	if (DepthTex && DepthTex->View)
	{
		DepthAttach.view = DepthTex->View;
		DepthAttach.depthLoadOp = WGPULoadOp_Clear;
		DepthAttach.depthStoreOp = WGPUStoreOp_Store;
		DepthAttach.depthClearValue = 1.0f;
		DepthAttach.depthReadOnly = false;
		RpDesc.depthStencilAttachment = &DepthAttach;
	}

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

#if WITH_RHI_BREADCRUMBS
void FDawnCommandContext::RHIBeginBreadcrumbGPU(FRHIBreadcrumbNode* Breadcrumb)
{
	checkNoEntry();
}

void FDawnCommandContext::RHIEndBreadcrumbGPU(FRHIBreadcrumbNode* Breadcrumb)
{
	checkNoEntry();
}
#endif

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
	// Stage 2 fallback / multi-@group fix (this session): when the bound PSO
	// carries real reflection data (FDawnVertexShader/FDawnPixelShader::
	// Bindings — see DawnResources.h/RHICreateGraphicsPipelineState in
	// DawnDynamicRHI.cpp), each FRHIShaderParameterResource::Index is a real
	// flattened WGSL @binding number, but NOT necessarily @group(0) — a
	// caller (like DawnRHITestMain's real-shader render path) can pass
	// resources belonging to more than one real @group in a single call, so
	// this now looks up each resource's real Group by its Binding number
	// (scanning the bound VS+PS's reflected ::Bindings — flattened binding
	// numbers are unique across a whole cooked shader, see HANDOFF.md
	// "Update 12", so this is unambiguous) and issues one
	// wgpuRenderPassEncoderSetBindGroup call per real group actually
	// touched, against CurrentPSO->BindGroupLayouts[Group] (one layout per
	// group — see the NOTE in DawnResources.h). Falls back to the old
	// single-@group(0) behaviour (trusting Param.Index directly, no lookup)
	// when the PSO has no reflection data at all, matching the Stage 1/2
	// hand-authored WGSL paths' fixed convention.
	checkf(CurrentPSO && CurrentPSO->BindGroupLayouts.Num() > 0, TEXT("DawnRHI: RHISetShaderParameters requires RHISetGraphicsPipelineState to have run first"));
	if (InResourceParameters.Num() == 0)
	{
		return;
	}

	auto FindGroupForBinding = [this](uint32 Binding) -> uint32
	{
		for (const FDawnShaderBinding& B : CurrentPSO->VertexShader->Bindings)
		{
			if (B.Binding == Binding) { return B.Group; }
		}
		for (const FDawnShaderBinding& B : CurrentPSO->PixelShader->Bindings)
		{
			if (B.Binding == Binding) { return B.Group; }
		}
		return 0; // no reflection data (or an unreflected binding) -- Stage 2 fixed convention is always @group(0)
	};

	TMap<uint32, TArray<WGPUBindGroupEntry>> EntriesByGroup;
	for (const FRHIShaderParameterResource& Param : InResourceParameters)
	{
		WGPUBindGroupEntry Entry = {};
		Entry.binding = Param.Index;
		switch (Param.Type)
		{
		case FRHIShaderParameterResource::EType::UniformBuffer:
		{
			FDawnUniformBuffer* UB = static_cast<FDawnUniformBuffer*>(Param.Resource);
			Entry.buffer = UB->Buffer;
			Entry.size = UB->GetLayout().ConstantBufferSize;
			break;
		}
		case FRHIShaderParameterResource::EType::Texture:
		{
			FDawnTexture* Tex = static_cast<FDawnTexture*>(Param.Resource);
			Entry.textureView = Tex->View;
			break;
		}
		case FRHIShaderParameterResource::EType::Sampler:
		{
			FDawnSamplerState* Sampler = static_cast<FDawnSamplerState*>(Param.Resource);
			Entry.sampler = Sampler->Sampler;
			break;
		}
		default:
			checkf(false, TEXT("DawnRHI Stage 2: unsupported shader parameter resource type %d"), (int32)Param.Type);
			continue;
		}
		const uint32 Group = FindGroupForBinding(Param.Index);
		EntriesByGroup.FindOrAdd(Group).Add(Entry);
	}

	checkf(ActiveRenderPass, TEXT("DawnRHI: RHISetShaderParameters requires an active render pass"));
	for (TPair<uint32, TArray<WGPUBindGroupEntry>>& Pair : EntriesByGroup)
	{
		checkf(Pair.Key < (uint32)CurrentPSO->BindGroupLayouts.Num(), TEXT("DawnRHI: resource reflected @group(%u) but PSO only built %d bind group layout(s)"), Pair.Key, CurrentPSO->BindGroupLayouts.Num());

		WGPUBindGroupDescriptor BgDesc = {};
		BgDesc.layout = CurrentPSO->BindGroupLayouts[Pair.Key];
		BgDesc.entryCount = Pair.Value.Num();
		BgDesc.entries = Pair.Value.GetData();
		WGPUBindGroup BindGroup = wgpuDeviceCreateBindGroup(Owner->GetDevice(), &BgDesc);
		checkf(BindGroup, TEXT("DawnRHI: bind group creation failed for @group(%u)"), Pair.Key);

		wgpuRenderPassEncoderSetBindGroup(ActiveRenderPass, Pair.Key, BindGroup, 0, nullptr);
		// The render pass encoder retains its own reference while recording;
		// safe to release our handle once the call above returns.
		wgpuBindGroupRelease(BindGroup);
	}
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
