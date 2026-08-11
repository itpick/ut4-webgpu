#include "DawnDynamicRHI.h"
#include "DawnCommandContext.h"
#include "DawnResources.h"

DEFINE_LOG_CATEGORY(LogDawnRHI);

// ============================================================================
// Small helpers for constructing FRHITextureInitializer / FRHIBufferInitializer,
// whose real constructors are `protected` and only reachable from a derived
// type (this is the pattern the engine expects backends to use).
// ============================================================================
struct FDawnTextureInitializerHelper final : public FRHITextureInitializer
{
	FDawnTextureInitializerHelper(FRHICommandListBase& RHICmdList, FRHITexture* InTexture, FFinalizeCallback&& InFinalize,
		FGetSubresourceCallback&& InGetSubresource = FGetSubresourceCallback{})
		: FRHITextureInitializer(RHICmdList, InTexture, nullptr, 0, MoveTemp(InFinalize), MoveTemp(InGetSubresource))
	{
	}
};

struct FDawnBufferInitializerHelper final : public FRHIBufferInitializer
{
	FDawnBufferInitializerHelper(FRHICommandListBase& RHICmdList, FRHIBuffer* InBuffer, void* InWritableData, uint64 InWritableSize, FFinalizeCallback&& InFinalize)
		: FRHIBufferInitializer(RHICmdList, InBuffer, InWritableData, InWritableSize, MoveTemp(InFinalize))
	{
	}
};

static void WaitForQueueIdle(WGPUInstance Instance, WGPUQueue Queue)
{
	bool bDone = false;
	WGPUQueueWorkDoneCallbackInfo CbInfo = {};
	CbInfo.mode = WGPUCallbackMode_AllowSpontaneous;
	CbInfo.callback = [](WGPUQueueWorkDoneStatus, WGPUStringView, void* Userdata1, void*)
	{
		*reinterpret_cast<bool*>(Userdata1) = true;
	};
	CbInfo.userdata1 = &bDone;
	WGPUFuture F = wgpuQueueOnSubmittedWorkDone(Queue, CbInfo);
	for (int32 i = 0; i < 200000 && !bDone; ++i)
	{
		WGPUFutureWaitInfo WaitInfo = {};
		WaitInfo.future = F;
		wgpuInstanceWaitAny(Instance, 1, &WaitInfo, 1000000 /* 1ms */);
	}
}

// ============================================================================
// FDawnDynamicRHI
// ============================================================================

FDawnDynamicRHI::FDawnDynamicRHI()
{
}

FDawnDynamicRHI::~FDawnDynamicRHI()
{
}

void FDawnDynamicRHI::InitDawnDevice()
{
#if !DAWNRHI_WASM
	// Static linking of Dawn native requires installing the proc table
	// ourselves — standard documented Dawn native usage (dawn/native/DawnNative.h),
	// not anything SimplyStream-specific.
	dawnProcSetProcs(&dawn::native::GetProcs());
#endif

	// Dawn requires explicitly opting in to finite-timeout wgpuInstanceWaitAny()
	// calls via the TimedWaitAny instance feature (an unrequested instance
	// otherwise only supports timeout==0 / timeout==UINT64_MAX waits, and
	// finite polling waits fail with "Timeout waits are either not enabled
	// or not supported"). We poll with short finite timeouts throughout this
	// module (buffer-map / queue-idle waits), so request it up front.
	//
	// DAWNRHI_WASM: emdawnwebgpu's instance does not support requesting
	// TimedWaitAny (DawnRHIWasmProbe's real_shader_wasm.cpp never requests
	// it and never calls wgpuInstanceWaitAny at all — it is purely
	// callback/event-loop driven, matching how every other wasm WebGPU
	// binding works: JS promises only resolve between synchronous C/wasm
	// calls yielding control back to the browser event loop, so a spin-wait
	// inside wasm code would block that very event loop and deadlock
	// instead of ever seeing the callback fire). Left unrequested here.
	// TimedWaitAny is required for BOTH native (finite polling waits) AND wasm/emdawnwebgpu:
	// emdawnwebgpu treats even UINT64_MAX as a finite timed wait (not the infinite sentinel), so
	// wgpuInstanceWaitAny fails 'TimedWaitAny not enabled' without it. On wasm this is only
	// serviceable because main() runs on a worker (PROXY_TO_PTHREAD): the worker blocks on the
	// future's futex while the browser main thread services the WebGPU promise and signals it.
	WGPUInstanceFeatureName RequiredFeatures[] = { WGPUInstanceFeatureName_TimedWaitAny };
	WGPUInstanceDescriptor InstanceDesc = {};
	InstanceDesc.requiredFeatureCount = 1;
	InstanceDesc.requiredFeatures = RequiredFeatures;
	Instance = wgpuCreateInstance(&InstanceDesc);
	checkf(Instance, TEXT("DawnRHI: wgpuCreateInstance failed"));

	WGPURequestAdapterOptions AdapterOpts = {};
	AdapterOpts.powerPreference = WGPUPowerPreference_HighPerformance;
#if !DAWNRHI_WASM
	// The browser's WebGPU implementation picks its own backend (Vulkan/
	// Metal/D3D12/ANGLE) internally; there is no such choice to make from
	// wasm — DawnRHIWasmProbe's probes never set backendType and worked
	// correctly against the real GPU via Chrome's own backend selection.
	AdapterOpts.backendType = WGPUBackendType_Vulkan;
#endif

	WGPURequestAdapterCallbackInfo AdapterCbInfo = {};
	AdapterCbInfo.mode = WGPUCallbackMode_AllowSpontaneous;
	AdapterCbInfo.callback = [](WGPURequestAdapterStatus Status, WGPUAdapter InAdapter, WGPUStringView Message, void* Userdata1, void*)
	{
		if (Status == WGPURequestAdapterStatus_Success)
		{
			*reinterpret_cast<WGPUAdapter*>(Userdata1) = InAdapter;
		}
		else
		{
			UE_LOG(LogDawnRHI, Error, TEXT("RequestAdapter failed: %.*s"), (int32)Message.length, ANSI_TO_TCHAR(Message.data));
		}
	};
	AdapterCbInfo.userdata1 = &Adapter;
	WGPUFuture AdapterFuture = wgpuInstanceRequestAdapter(Instance, &AdapterOpts, AdapterCbInfo);
	WGPUFutureWaitInfo AdapterWait = {};
	AdapterWait.future = AdapterFuture;
	wgpuInstanceWaitAny(Instance, 1, &AdapterWait, UINT64_MAX);
	checkf(Adapter, TEXT("DawnRHI: no adapter"));

	WGPUAdapterInfo Info = {};
	wgpuAdapterGetInfo(Adapter, &Info);
	UE_LOG(LogDawnRHI, Log, TEXT("DawnRHI adapter: %.*s / %.*s"),
		(int32)Info.vendor.length, ANSI_TO_TCHAR(Info.vendor.data),
		(int32)Info.device.length, ANSI_TO_TCHAR(Info.device.data));
	wgpuAdapterInfoFreeMembers(Info);

	WGPUDeviceDescriptor DeviceDesc = {};
	DeviceDesc.uncapturedErrorCallbackInfo.callback = [](WGPUDevice const*, WGPUErrorType Type, WGPUStringView Message, void*, void*)
	{
		UE_LOG(LogDawnRHI, Error, TEXT("Dawn uncaptured error (type %d): %.*s"), (int32)Type, (int32)Message.length, ANSI_TO_TCHAR(Message.data));
	};
	WGPURequestDeviceCallbackInfo DeviceCbInfo = {};
	DeviceCbInfo.mode = WGPUCallbackMode_AllowSpontaneous;
	DeviceCbInfo.callback = [](WGPURequestDeviceStatus Status, WGPUDevice InDevice, WGPUStringView Message, void* Userdata1, void*)
	{
		if (Status == WGPURequestDeviceStatus_Success)
		{
			*reinterpret_cast<WGPUDevice*>(Userdata1) = InDevice;
		}
		else
		{
			UE_LOG(LogDawnRHI, Error, TEXT("RequestDevice failed: %.*s"), (int32)Message.length, ANSI_TO_TCHAR(Message.data));
		}
	};
	DeviceCbInfo.userdata1 = &Device;
	WGPUFuture DeviceFuture = wgpuAdapterRequestDevice(Adapter, &DeviceDesc, DeviceCbInfo);
	WGPUFutureWaitInfo DeviceWait = {};
	DeviceWait.future = DeviceFuture;
	wgpuInstanceWaitAny(Instance, 1, &DeviceWait, UINT64_MAX);
	checkf(Device, TEXT("DawnRHI: no device"));

	Queue = wgpuDeviceGetQueue(Device);
}

void FDawnDynamicRHI::Init()
{
	InitDawnDevice();
	CommandContext = MakeUnique<FDawnCommandContext>(this);
	GRHISupportsRHIThread = false;
	GRHISupportsParallelRHIExecute = false;
	GRHIAdapterName = TEXT("Dawn (Vulkan)");
	GMaxRHIFeatureLevel = ERHIFeatureLevel::SM6;
	GMaxRHIShaderPlatform = SP_NumPlatforms; // Stage 1: no engine shader-platform mapping yet.
	GIsRHIInitialized = true;
}

void FDawnDynamicRHI::Shutdown()
{
	CommandContext.Reset();
	if (Queue) { wgpuQueueRelease(Queue); Queue = nullptr; }
	if (Device) { wgpuDeviceRelease(Device); Device = nullptr; }
	if (Adapter) { wgpuAdapterRelease(Adapter); Adapter = nullptr; }
	if (Instance) { wgpuInstanceRelease(Instance); Instance = nullptr; }
}

const TCHAR* FDawnDynamicRHI::GetName()
{
	return TEXT("Dawn");
}

FSamplerStateRHIRef FDawnDynamicRHI::RHICreateSamplerState(const FSamplerStateInitializerRHI& Initializer)
{
	FDawnSamplerState* State = new FDawnSamplerState(Initializer);

	WGPUSamplerDescriptor Desc = {};
	Desc.addressModeU = DawnAddressModeFromRHI((ESamplerAddressMode)Initializer.AddressU);
	Desc.addressModeV = DawnAddressModeFromRHI((ESamplerAddressMode)Initializer.AddressV);
	Desc.addressModeW = DawnAddressModeFromRHI((ESamplerAddressMode)Initializer.AddressW);
	Desc.magFilter = DawnFilterModeFromRHI(Initializer.Filter);
	Desc.minFilter = DawnFilterModeFromRHI(Initializer.Filter);
	Desc.mipmapFilter = DawnMipFilterModeFromRHI(Initializer.Filter);
	Desc.lodMinClamp = Initializer.MinMipLevel;
	Desc.lodMaxClamp = Initializer.MaxMipLevel;
	Desc.maxAnisotropy = (uint16)FMath::Max(1, Initializer.MaxAnisotropy);

	State->Sampler = wgpuDeviceCreateSampler(Device, &Desc);
	checkf(State->Sampler, TEXT("DawnRHI: sampler creation failed"));
	return State;
}

FRasterizerStateRHIRef FDawnDynamicRHI::RHICreateRasterizerState(const FRasterizerStateInitializerRHI& Initializer)
{
	return new FDawnRasterizerState(Initializer);
}

FDepthStencilStateRHIRef FDawnDynamicRHI::RHICreateDepthStencilState(const FDepthStencilStateInitializerRHI& Initializer)
{
	return new FDawnDepthStencilState(Initializer);
}

FBlendStateRHIRef FDawnDynamicRHI::RHICreateBlendState(const FBlendStateInitializerRHI& Initializer)
{
	return new FDawnBlendState(Initializer);
}

FVertexDeclarationRHIRef FDawnDynamicRHI::RHICreateVertexDeclaration(const FVertexDeclarationElementList& Elements)
{
	return new FDawnVertexDeclaration(Elements);
}

static WGPUShaderModule CompileWGSL(WGPUDevice Device, const FRHICreateShaderDesc& CreateShaderDesc, const TCHAR* DebugLabel)
{
	// Stage 1 shader contract (ours, not SimplyStream's): Code is UTF-8 WGSL
	// source text. Stage 3 replaces the *producer* of this text (hand-authored
	// -> HLSL->SPIR-V->WGSL via ShaderConductor+Tint) without changing this
	// consumer. Pass the bytes straight through with an explicit length —
	// no TCHAR round-trip, no reliance on WGPU_STRLEN's null-termination
	// assumption (an earlier version round-tripped through FString/UTF8CHAR
	// and fed Dawn's WGSL parser a corrupted/mis-terminated buffer).
	WGPUShaderSourceWGSL WgslDesc = {};
	WgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
	WgslDesc.code = { reinterpret_cast<const char*>(CreateShaderDesc.Code.GetData()), (size_t)CreateShaderDesc.Code.Num() };

	WGPUShaderModuleDescriptor ModuleDesc = {};
	ModuleDesc.nextInChain = &WgslDesc.chain;

	auto LabelUtf8 = StringCast<UTF8CHAR>(DebugLabel);
	ModuleDesc.label = { reinterpret_cast<const char*>(LabelUtf8.Get()), WGPU_STRLEN };

	return wgpuDeviceCreateShaderModule(Device, &ModuleDesc);
}

// Milestone step 1/2: real cooked WGSL keeps the real UE shader's own entry
// point name (e.g. "ScreenPassVS"/"CopyRectPS"), NOT the Stage 1 fixed
// "vs_main"/"fs_main" convention (see FDawnVertexShader/FDawnPixelShader
// ::EntryPoint in DawnResources.h). Scan for the real @vertex/@fragment
// attribute's following "fn <Name>(" -- this is real WGSL grammar (the
// attribute always immediately precedes the function it decorates), not a
// guess. Falls back to the old fixed name if not found, so the still-
// supported Stage 1/2 hand-authored WGSL paths (whose source has no
// @vertex/@fragment attribute at all -- see GVertexWGSL/GPixelWGSL in
// DawnRHITestMain.cpp) are unaffected.
static FString ParseWgslEntryPoint(const FString& Wgsl, const TCHAR* Attribute, const TCHAR* FallbackName)
{
	int32 AttrIdx = Wgsl.Find(Attribute);
	if (AttrIdx == INDEX_NONE)
	{
		return FallbackName;
	}
	int32 FnIdx = Wgsl.Find(TEXT("fn "), ESearchCase::CaseSensitive, ESearchDir::FromStart, AttrIdx);
	if (FnIdx == INDEX_NONE)
	{
		return FallbackName;
	}
	int32 NameStart = FnIdx + 3;
	int32 ParenIdx = Wgsl.Find(TEXT("("), ESearchCase::CaseSensitive, ESearchDir::FromStart, NameStart);
	if (ParenIdx == INDEX_NONE || ParenIdx <= NameStart)
	{
		return FallbackName;
	}
	return Wgsl.Mid(NameStart, ParenIdx - NameStart).TrimStartAndEnd();
}

FPixelShaderRHIRef FDawnDynamicRHI::RHICreatePixelShader(const FRHICreateShaderDesc& CreateShaderDesc)
{
	FDawnPixelShader* Shader = new FDawnPixelShader();
	Shader->WGSLSource = FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(CreateShaderDesc.Code.GetData()), CreateShaderDesc.Code.Num()).Get();
	Shader->EntryPoint = ParseWgslEntryPoint(Shader->WGSLSource, TEXT("@fragment"), TEXT("fs_main"));
	Shader->ShaderModule = CompileWGSL(Device, CreateShaderDesc, TEXT("DawnRHI.PixelShader"));
	checkf(Shader->ShaderModule, TEXT("DawnRHI: pixel shader WGSL compile failed"));
	return Shader;
}

FVertexShaderRHIRef FDawnDynamicRHI::RHICreateVertexShader(const FRHICreateShaderDesc& CreateShaderDesc)
{
	FDawnVertexShader* Shader = new FDawnVertexShader();
	Shader->WGSLSource = FUTF8ToTCHAR(reinterpret_cast<const ANSICHAR*>(CreateShaderDesc.Code.GetData()), CreateShaderDesc.Code.Num()).Get();
	Shader->EntryPoint = ParseWgslEntryPoint(Shader->WGSLSource, TEXT("@vertex"), TEXT("vs_main"));
	Shader->ShaderModule = CompileWGSL(Device, CreateShaderDesc, TEXT("DawnRHI.VertexShader"));
	checkf(Shader->ShaderModule, TEXT("DawnRHI: vertex shader WGSL compile failed"));
	return Shader;
}

FGraphicsPipelineStateRHIRef FDawnDynamicRHI::RHICreateGraphicsPipelineState(const FGraphicsPipelineStateInitializer& Initializer)
{
	FDawnVertexShader* VS = static_cast<FDawnVertexShader*>(Initializer.BoundShaderState.VertexShaderRHI);
	FDawnPixelShader* PS = static_cast<FDawnPixelShader*>(Initializer.BoundShaderState.PixelShaderRHI);
	FDawnVertexDeclaration* VertexDecl = static_cast<FDawnVertexDeclaration*>(Initializer.BoundShaderState.VertexDeclarationRHI);
	checkf(VS && PS, TEXT("DawnRHI: PSO requires vertex+pixel shaders (Stage 1 scope)"));

	// Vertex buffer layout (single stream 0) derived from the vertex declaration.
	TArray<WGPUVertexAttribute> Attributes;
	uint64 Stride = 0;
	if (VertexDecl)
	{
		for (const FVertexElement& Elem : VertexDecl->Elements)
		{
			WGPUVertexAttribute Attr = {};
			Attr.format = DawnVertexFormatFromElementType((EVertexElementType)Elem.Type.GetValue());
			Attr.offset = Elem.Offset;
			Attr.shaderLocation = Elem.AttributeIndex;
			Attributes.Add(Attr);
			Stride = FMath::Max<uint64>(Stride, Elem.Stride);
		}
	}

	WGPUVertexBufferLayout BufferLayout = {};
	BufferLayout.arrayStride = Stride;
	BufferLayout.stepMode = WGPUVertexStepMode_Vertex;
	BufferLayout.attributeCount = Attributes.Num();
	BufferLayout.attributes = Attributes.GetData();

	WGPUColorTargetState ColorTarget = {};
	ColorTarget.format = DawnTextureFormatFromPixelFormat((EPixelFormat)Initializer.RenderTargetFormats[0]);
	ColorTarget.writeMask = WGPUColorWriteMask_All;

	WGPUFragmentState FragState = {};
	FragState.module = PS->ShaderModule;
	auto FsEntry = StringCast<UTF8CHAR>(*PS->EntryPoint);
	FragState.entryPoint = { reinterpret_cast<const char*>(FsEntry.Get()), (size_t)FsEntry.Length() };
	FragState.targetCount = 1;
	FragState.targets = &ColorTarget;

	// Milestone step 1: reflection-driven @group(0) bind group layout.
	// If either shader carries real reflected bindings (FDawnVertexShader/
	// FDawnPixelShader::Bindings, populated from the real cooked SPIR-V —
	// see DawnResources.h/DawnShaderCompiler.cpp), build the layout from
	// THAT (real @binding numbers, real resource kinds) instead of the old
	// fixed {UB@0,Texture@1,Sampler@2} table. Falls back to the fixed table
	// only when neither shader has reflection data — the Stage 1/2
	// hand-authored WGSL test paths (DawnRHITestMain's synthetic
	// scene_vs/scene_ps) still rely on that fixed convention by
	// construction and never populate ::Bindings.
	TArray<WGPUBindGroupLayoutEntry> LayoutEntries;
	auto AppendShaderBindings = [&LayoutEntries](const TArray<FDawnShaderBinding>& Bindings, WGPUShaderStage Stage)
	{
		for (const FDawnShaderBinding& B : Bindings)
		{
			// Multiple shader stages can reflect the SAME @group(0)/@binding
			// (e.g. a uniform buffer read by both VS and PS) — merge visibility
			// instead of adding a duplicate WGPUBindGroupLayoutEntry (Dawn
			// rejects a layout with two entries at the same binding number).
			for (WGPUBindGroupLayoutEntry& Existing : LayoutEntries)
			{
				if (Existing.binding == B.Binding)
				{
					Existing.visibility |= Stage;
					return;
				}
			}
			WGPUBindGroupLayoutEntry Entry = {};
			Entry.binding = B.Binding;
			Entry.visibility = Stage;
			switch (B.Kind)
			{
			case EDawnShaderBindingKind::UniformBuffer:
				Entry.buffer.type = WGPUBufferBindingType_Uniform;
				break;
			case EDawnShaderBindingKind::Texture:
				Entry.texture.sampleType = WGPUTextureSampleType_Float;
				Entry.texture.viewDimension = WGPUTextureViewDimension_2D;
				break;
			case EDawnShaderBindingKind::Sampler:
				Entry.sampler.type = WGPUSamplerBindingType_Filtering;
				break;
			default:
				continue; // unclassified resource kind — do not fabricate a layout entry
			}
			LayoutEntries.Add(Entry);
		}
	};
	AppendShaderBindings(VS->Bindings, WGPUShaderStage_Vertex);
	AppendShaderBindings(PS->Bindings, WGPUShaderStage_Fragment);

	WGPUBindGroupLayoutEntry FixedLayoutEntries[3] = {};
	if (LayoutEntries.Num() == 0)
	{
		// No reflection data on either shader — fall back to Stage 2's
		// original fixed convention (see the NOTE on
		// FDawnGraphicsPipelineState::BindGroupLayout in DawnResources.h).
		FixedLayoutEntries[0].binding = 0;
		FixedLayoutEntries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
		FixedLayoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
		FixedLayoutEntries[1].binding = 1;
		FixedLayoutEntries[1].visibility = WGPUShaderStage_Fragment;
		FixedLayoutEntries[1].texture.sampleType = WGPUTextureSampleType_Float;
		FixedLayoutEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
		FixedLayoutEntries[2].binding = 2;
		FixedLayoutEntries[2].visibility = WGPUShaderStage_Fragment;
		FixedLayoutEntries[2].sampler.type = WGPUSamplerBindingType_Filtering;
		LayoutEntries.Append(FixedLayoutEntries, 3);
	}

	WGPUBindGroupLayoutDescriptor BglDesc = {};
	BglDesc.entryCount = LayoutEntries.Num();
	BglDesc.entries = LayoutEntries.GetData();
	WGPUBindGroupLayout BindGroupLayout = wgpuDeviceCreateBindGroupLayout(Device, &BglDesc);

	WGPUPipelineLayoutDescriptor PlDesc = {};
	PlDesc.bindGroupLayoutCount = 1;
	PlDesc.bindGroupLayouts = &BindGroupLayout;
	WGPUPipelineLayout PipelineLayout = wgpuDeviceCreatePipelineLayout(Device, &PlDesc);

	WGPUDepthStencilState DepthStencil = {};
	bool bHasDepth = Initializer.DepthStencilTargetFormat != PF_Unknown;
	if (bHasDepth)
	{
		FDawnDepthStencilState* DSState = static_cast<FDawnDepthStencilState*>(Initializer.DepthStencilState);
		DepthStencil.format = DawnDepthStencilFormat();
		DepthStencil.depthWriteEnabled = DSState ? (DSState->Initializer.bEnableDepthWrite ? WGPUOptionalBool_True : WGPUOptionalBool_False) : WGPUOptionalBool_True;
		DepthStencil.depthCompare = DSState ? DawnCompareFunctionFromRHI(DSState->Initializer.DepthTest) : WGPUCompareFunction_LessEqual;
		DepthStencil.stencilFront.compare = WGPUCompareFunction_Always;
		DepthStencil.stencilBack.compare = WGPUCompareFunction_Always;
		DepthStencil.stencilReadMask = 0xFF;
		DepthStencil.stencilWriteMask = 0xFF;
	}

	WGPURenderPipelineDescriptor PipeDesc = {};
	PipeDesc.layout = PipelineLayout;
	PipeDesc.vertex.module = VS->ShaderModule;
	auto VsEntry = StringCast<UTF8CHAR>(*VS->EntryPoint);
	PipeDesc.vertex.entryPoint = { reinterpret_cast<const char*>(VsEntry.Get()), (size_t)VsEntry.Length() };
	if (Attributes.Num() > 0)
	{
		PipeDesc.vertex.bufferCount = 1;
		PipeDesc.vertex.buffers = &BufferLayout;
	}
	PipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
	PipeDesc.multisample.count = 1;
	PipeDesc.multisample.mask = 0xFFFFFFFF;
	PipeDesc.fragment = &FragState;
	if (bHasDepth)
	{
		PipeDesc.depthStencil = &DepthStencil;
	}

	WGPURenderPipeline Pipeline = wgpuDeviceCreateRenderPipeline(Device, &PipeDesc);
	checkf(Pipeline, TEXT("DawnRHI: render pipeline creation failed"));

	FDawnGraphicsPipelineState* PSO = new FDawnGraphicsPipelineState();
	PSO->Pipeline = Pipeline;
	PSO->VertexShader = VS;
	PSO->PixelShader = PS;
	PSO->BindGroupLayout = BindGroupLayout;
	PSO->PipelineLayout = PipelineLayout;
	return PSO;
}

FRHIBufferInitializer FDawnDynamicRHI::RHICreateBufferInitializer(FRHICommandListBase& RHICmdList, const FRHIBufferCreateDesc& InCreateDesc)
{
	// RHIValidation asserts every resource is created with a known initial
	// ERHIAccess state (matches VulkanRHI's FVulkanDynamicRHI::CreateBuffer,
	// which calls DetermineInitialState() before constructing the resource).
	FRHIBufferCreateDesc CreateDesc = InCreateDesc;
	CreateDesc.DetermineInitialState();

	TRefCountPtr<FDawnBuffer> NewBuffer = new FDawnBuffer(CreateDesc);

	WGPUBufferUsage Usage = WGPUBufferUsage_CopyDst;
	if (EnumHasAnyFlags(CreateDesc.Usage, EBufferUsageFlags::VertexBuffer)) { Usage |= WGPUBufferUsage_Vertex; }
	if (EnumHasAnyFlags(CreateDesc.Usage, EBufferUsageFlags::IndexBuffer))  { Usage |= WGPUBufferUsage_Index; }
	if (EnumHasAnyFlags(CreateDesc.Usage, EBufferUsageFlags::UniformBuffer)) { Usage |= WGPUBufferUsage_Uniform; }

	WGPUBufferDescriptor BufDesc = {};
	BufDesc.usage = Usage;
	BufDesc.size = FMath::Max<uint32>(CreateDesc.Size, 4);
	BufDesc.mappedAtCreation = true;

	WGPUBuffer Buffer = wgpuDeviceCreateBuffer(Device, &BufDesc);
	checkf(Buffer, TEXT("DawnRHI: buffer creation failed"));
	NewBuffer->Buffer = Buffer;

	void* WritableData = wgpuBufferGetMappedRange(Buffer, 0, BufDesc.size);

	// NOTE: FRHIBufferInitializer::FFinalizeCallback is `protected` — only
	// nameable from a derived type (FDawnBufferInitializerHelper). From this
	// (non-derived) function we pass a bare lambda as the constructor
	// argument instead of spelling the protected alias out ourselves; it
	// converts implicitly at the call site.
	return FDawnBufferInitializerHelper(RHICmdList, NewBuffer.GetReference(), WritableData, BufDesc.size,
		[NewBuffer](FRHICommandListBase&) mutable -> FBufferRHIRef
		{
			wgpuBufferUnmap(NewBuffer->Buffer);
			return FBufferRHIRef(NewBuffer);
		});
}

FRHITextureInitializer FDawnDynamicRHI::RHICreateTextureInitializer(FRHICommandListBase& RHICmdList, const FRHITextureCreateDesc& InCreateDesc)
{
	// Same DetermineInitialState() requirement as RHICreateBufferInitializer above.
	FRHITextureCreateDesc CreateDesc = InCreateDesc;
	if (CreateDesc.InitialState == ERHIAccess::Unknown)
	{
		CreateDesc.SetInitialState(RHIGetDefaultResourceState(CreateDesc.Flags, false));
	}

	TRefCountPtr<FDawnTexture> NewTexture = new FDawnTexture(CreateDesc);

	const bool bIsDepthStencil = EnumHasAnyFlags(CreateDesc.Flags, ETextureCreateFlags::DepthStencilTargetable);

	WGPUTextureUsage Usage = WGPUTextureUsage_CopySrc | WGPUTextureUsage_CopyDst;
	if (bIsDepthStencil)
	{
		Usage |= WGPUTextureUsage_RenderAttachment;
	}
	if (EnumHasAnyFlags(CreateDesc.Flags, ETextureCreateFlags::RenderTargetable))
	{
		Usage |= WGPUTextureUsage_RenderAttachment;
	}
	if (EnumHasAnyFlags(CreateDesc.Flags, ETextureCreateFlags::ShaderResource))
	{
		Usage |= WGPUTextureUsage_TextureBinding;
	}

	WGPUTextureDescriptor TexDesc = {};
	TexDesc.usage = Usage;
	TexDesc.dimension = WGPUTextureDimension_2D;
	TexDesc.size = { (uint32)CreateDesc.Extent.X, (uint32)CreateDesc.Extent.Y, 1 };
	TexDesc.format = bIsDepthStencil ? DawnDepthStencilFormat() : DawnTextureFormatFromPixelFormat(CreateDesc.Format);
	TexDesc.mipLevelCount = 1;
	TexDesc.sampleCount = 1;

	WGPUTexture Texture = wgpuDeviceCreateTexture(Device, &TexDesc);
	checkf(Texture, TEXT("DawnRHI: texture creation failed"));
	NewTexture->Texture = Texture;
	NewTexture->View = wgpuTextureCreateView(Texture, nullptr);

	// Initial pixel data (Stage 2: sampled textures, e.g. a material/UI
	// texture). GetSubresourceCallback hands the caller a CPU-side staging
	// buffer to WriteData() into; FinalizeCallback uploads it via
	// wgpuQueueWriteTexture and frees the staging memory. WebGPU has no
	// buffer-style mappedAtCreation for textures, so this (rather than the
	// buffer path's direct-mapped-pointer trick) is the correct shape here.
	const uint32 Width = (uint32)CreateDesc.Extent.X;
	const uint32 Height = (uint32)CreateDesc.Extent.Y;
	const uint32 BytesPerPixel = 4; // Stage 2 only handles 4-byte formats (RGBA8/BGRA8).
	const uint64 StagingSize = (uint64)Width * Height * BytesPerPixel;
	TSharedPtr<TArray<uint8>> Staging = MakeShared<TArray<uint8>>();

	// NOTE: same protected-typedef situation as FFinalizeCallback above —
	// FGetSubresourceCallback can't be named from this (non-derived)
	// function, so both lambdas below are passed as bare arguments rather
	// than assigned to a named variable of that type first.
	WGPUQueue QueueCopy = Queue;
	return FDawnTextureInitializerHelper(RHICmdList, NewTexture.GetReference(),
		[NewTexture, Staging, Width, Height, BytesPerPixel, QueueCopy](FRHICommandListBase&) mutable -> FTextureRHIRef
		{
			if (Staging->Num() > 0)
			{
				WGPUTexelCopyTextureInfo Dst = {};
				Dst.texture = NewTexture->Texture;
				WGPUTexelCopyBufferLayout Layout = {};
				Layout.bytesPerRow = Width * BytesPerPixel;
				Layout.rowsPerImage = Height;
				WGPUExtent3D Size = { Width, Height, 1 };
				wgpuQueueWriteTexture(QueueCopy, &Dst, Staging->GetData(), Staging->Num(), &Layout, &Size);
			}
			return FTextureRHIRef(NewTexture);
		},
		[Staging, StagingSize, Width, BytesPerPixel](FRHITextureInitializer::FSubresourceIndex) -> FRHITextureSubresourceInitializer
		{
			Staging->SetNumUninitialized((int64)StagingSize);
			FRHITextureSubresourceInitializer SubresourceInit;
			SubresourceInit.Data = Staging->GetData();
			SubresourceInit.Size = StagingSize;
			SubresourceInit.Stride = (uint64)Width * BytesPerPixel;
			return SubresourceInit;
		});
}

uint32 FDawnDynamicRHI::RHIComputeMemorySize(FRHITexture* TextureRHI)
{
	if (!TextureRHI) { return 0; }
	const FRHITextureDesc& Desc = TextureRHI->GetDesc();
	return Desc.Extent.X * Desc.Extent.Y * 4;
}

void FDawnDynamicRHI::RHIReadSurfaceData(FRHITexture* Texture, FIntRect Rect, TArray<FColor>& OutData, FReadSurfaceDataFlags InFlags)
{
	FDawnTexture* Tex = static_cast<FDawnTexture*>(Texture);
	checkf(Tex && Tex->Texture, TEXT("DawnRHI: RHIReadSurfaceData needs a valid Dawn texture"));

	const uint32 Width = Rect.Width();
	const uint32 Height = Rect.Height();
	const uint32 BytesPerRow = Align(Width * 4, 256);

	WGPUBufferDescriptor BufDesc = {};
	BufDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
	BufDesc.size = BytesPerRow * Height;
	WGPUBuffer ReadbackBuf = wgpuDeviceCreateBuffer(Device, &BufDesc);

	WGPUCommandEncoderDescriptor EncDesc = {};
	WGPUCommandEncoder Encoder = wgpuDeviceCreateCommandEncoder(Device, &EncDesc);

	WGPUTexelCopyTextureInfo SrcTex = {};
	SrcTex.texture = Tex->Texture;
	SrcTex.origin = { (uint32)Rect.Min.X, (uint32)Rect.Min.Y, 0 };
	WGPUTexelCopyBufferInfo DstBuf = {};
	DstBuf.buffer = ReadbackBuf;
	DstBuf.layout.bytesPerRow = BytesPerRow;
	DstBuf.layout.rowsPerImage = Height;
	WGPUExtent3D CopySize = { Width, Height, 1 };
	wgpuCommandEncoderCopyTextureToBuffer(Encoder, &SrcTex, &DstBuf, &CopySize);

	WGPUCommandBufferDescriptor CbDesc = {};
	WGPUCommandBuffer CmdBuf = wgpuCommandEncoderFinish(Encoder, &CbDesc);
	wgpuQueueSubmit(Queue, 1, &CmdBuf);
	wgpuCommandEncoderRelease(Encoder);
	wgpuCommandBufferRelease(CmdBuf);

	bool bMapped = false;
	WGPUBufferMapCallbackInfo MapCbInfo = {};
	MapCbInfo.mode = WGPUCallbackMode_AllowSpontaneous;
	MapCbInfo.callback = [](WGPUMapAsyncStatus, WGPUStringView, void* Userdata1, void*)
	{
		*reinterpret_cast<bool*>(Userdata1) = true;
	};
	MapCbInfo.userdata1 = &bMapped;
	WGPUFuture MapFuture = wgpuBufferMapAsync(ReadbackBuf, WGPUMapMode_Read, 0, BufDesc.size, MapCbInfo);
	for (int32 i = 0; i < 200000 && !bMapped; ++i)
	{
		WGPUFutureWaitInfo WaitInfo = {};
		WaitInfo.future = MapFuture;
		wgpuInstanceWaitAny(Instance, 1, &WaitInfo, 1000000);
	}
	checkf(bMapped, TEXT("DawnRHI: readback buffer never mapped"));

	const uint8* Mapped = reinterpret_cast<const uint8*>(wgpuBufferGetConstMappedRange(ReadbackBuf, 0, BufDesc.size));
	OutData.SetNumUninitialized(Width * Height);
	for (uint32 Y = 0; Y < Height; ++Y)
	{
		const uint8* Row = Mapped + Y * BytesPerRow;
		for (uint32 X = 0; X < Width; ++X)
		{
			OutData[Y * Width + X] = FColor(Row[X * 4 + 0], Row[X * 4 + 1], Row[X * 4 + 2], Row[X * 4 + 3]);
		}
	}
	wgpuBufferUnmap(ReadbackBuf);
	wgpuBufferRelease(ReadbackBuf);
}

void FDawnDynamicRHI::RHIBlockUntilGPUIdle()
{
	WaitForQueueIdle(Instance, Queue);
}

void* FDawnDynamicRHI::RHIGetNativeDevice()
{
	return Device;
}

IRHICommandContext* FDawnDynamicRHI::RHIGetDefaultContext()
{
	return CommandContext.Get();
}

IRHIComputeContext* FDawnDynamicRHI::RHIGetCommandContext(ERHIPipeline Pipeline)
{
	// Stage 1: graphics-only. Async compute is a later-stage concern.
	return (Pipeline == ERHIPipeline::Graphics) ? CommandContext.Get() : nullptr;
}

// NOTE: the return type must be qualified here (FDynamicRHI::...) even
// though FDawnDynamicRHI inherits it — for an out-of-line member function
// definition, the leading return type is looked up in the enclosing scope
// *before* `FDawnDynamicRHI::` establishes class-member lookup.
FDynamicRHI::FRHIFinalizeContextsResult FDawnDynamicRHI::RHIFinalizeContexts(FDynamicRHI::FRHIFinalizeContextsArgs&& Args)
{
	// Stage 1 runs in immediate mode (see FDawnCommandContext) — every RHI*
	// call already executed against the Dawn command encoder by the time
	// this is reached, so there is nothing to translate/finalize here.
	return {};
}

void FDawnDynamicRHI::RHISubmitCommandLists(FRHISubmitCommandListsArgs&& Args)
{
	if (CommandContext)
	{
		CommandContext->SubmitToQueue();
	}
}

// ============================================================================
// Stub bodies (checkNoEntry) for FDynamicRHI methods a bare triangle never
// hits — compute, ray tracing, bindless views, viewport/present, texture
// upload/lock, uniform buffers, async texture streaming, queries, resolution
// enumeration. See module README / task report for the full real-vs-stub list.
// ============================================================================

void FDawnDynamicRHI::RHIEndFrame(const FRHIEndFrameArgs& Args)
{
	checkNoEntry();
}

FGeometryShaderRHIRef FDawnDynamicRHI::RHICreateGeometryShader(const FRHICreateShaderDesc& CreateShaderDesc)
{
	checkNoEntry();
	return {};
}

FComputeShaderRHIRef FDawnDynamicRHI::RHICreateComputeShader(const FRHICreateShaderDesc& CreateShaderDesc)
{
	checkNoEntry();
	return {};
}

FGPUFenceRHIRef FDawnDynamicRHI::RHICreateGPUFence(const FName &Name)
{
	checkNoEntry();
	return {};
}

FBoundShaderStateRHIRef FDawnDynamicRHI::RHICreateBoundShaderState(FRHIVertexDeclaration* VertexDeclaration, FRHIVertexShader* VertexShader, FRHIPixelShader* PixelShader, FRHIGeometryShader* GeometryShader)
{
	checkNoEntry();
	return {};
}

FComputePipelineStateRHIRef FDawnDynamicRHI::RHICreateComputePipelineState(const FComputePipelineStateInitializer& Initializer)
{
	checkNoEntry();
	return {};
}

FUniformBufferRHIRef FDawnDynamicRHI::RHICreateUniformBuffer(const void* Contents, const FRHIUniformBufferLayout* Layout, EUniformBufferUsage Usage, EUniformBufferValidation Validation)
{
	FDawnUniformBuffer* UB = new FDawnUniformBuffer(Layout);

	// WebGPU buffers must be a multiple of 4 bytes for wgpuQueueWriteBuffer;
	// UE's constant buffers are already 16-byte aligned in practice, but
	// round up defensively.
	const uint32 Size = Align(FMath::Max<uint32>(Layout->ConstantBufferSize, 4), 4);

	WGPUBufferDescriptor BufDesc = {};
	BufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
	BufDesc.size = Size;
	UB->Buffer = wgpuDeviceCreateBuffer(Device, &BufDesc);
	checkf(UB->Buffer, TEXT("DawnRHI: uniform buffer creation failed"));

	if (Contents && Layout->ConstantBufferSize > 0)
	{
		wgpuQueueWriteBuffer(Queue, UB->Buffer, 0, Contents, Layout->ConstantBufferSize);
	}
	return UB;
}

void FDawnDynamicRHI::RHIUpdateUniformBuffer(FRHICommandListBase& RHICmdList, FRHIUniformBuffer* UniformBufferRHI, const void* Contents)
{
	checkNoEntry();
}

void FDawnDynamicRHI::RHIReplaceResources(FRHICommandListBase& RHICmdList, TArray<FRHIResourceReplaceInfo>&& ReplaceInfos)
{
	checkNoEntry();
}

FRHICalcTextureSizeResult FDawnDynamicRHI::RHICalcTexturePlatformSize(FRHITextureDesc const& Desc, uint32 FirstMipIndex)
{
	checkNoEntry();
	return {};
}

void FDawnDynamicRHI::RHIGetTextureMemoryStats(FTextureMemoryStats& OutStats)
{
	checkNoEntry();
}

bool FDawnDynamicRHI::RHIGetTextureMemoryVisualizeData(FColor* TextureData, int32 SizeX, int32 SizeY, int32 Pitch, int32 PixelSize)
{
	checkNoEntry();
	return false;
}

FTextureRHIRef FDawnDynamicRHI::RHIAsyncCreateTexture2D(uint32 SizeX, uint32 SizeY, uint8 Format, uint32 NumMips, ETextureCreateFlags Flags, ERHIAccess InResourceState, void** InitialMipData, uint32 NumInitialMips, const TCHAR* DebugName, FGraphEventRef& OutCompletionEvent)
{
	checkNoEntry();
	return {};
}

FShaderResourceViewRHIRef FDawnDynamicRHI::RHICreateShaderResourceView(class FRHICommandListBase& RHICmdList, FRHIViewableResource* Resource, FRHIViewDesc const& ViewDesc)
{
	checkNoEntry();
	return {};
}

FUnorderedAccessViewRHIRef FDawnDynamicRHI::RHICreateUnorderedAccessView(class FRHICommandListBase& RHICmdList, FRHIViewableResource* Resource, FRHIViewDesc const& ViewDesc)
{
	checkNoEntry();
	return {};
}

FRHILockTextureResult FDawnDynamicRHI::RHILockTexture(FRHICommandListImmediate& RHICmdList, const FRHILockTextureArgs& Arguments)
{
	checkNoEntry();
	return {};
}

void FDawnDynamicRHI::RHIUnlockTexture(FRHICommandListImmediate& RHICmdList, const FRHILockTextureArgs& Arguments)
{
	checkNoEntry();
}

void FDawnDynamicRHI::RHIUpdateTexture2D(FRHICommandListBase& RHICmdList, FRHITexture* Texture, uint32 MipIndex, const struct FUpdateTextureRegion2D& UpdateRegion, uint32 SourcePitch, const uint8* SourceData)
{
	checkNoEntry();
}

void FDawnDynamicRHI::RHIUpdateTexture3D(FRHICommandListBase& RHICmdList, FRHITexture* Texture, uint32 MipIndex, const struct FUpdateTextureRegion3D& UpdateRegion, uint32 SourceRowPitch, uint32 SourceDepthPitch, const uint8* SourceData)
{
	checkNoEntry();
}

void FDawnDynamicRHI::RHIMapStagingSurface(FRHITexture* Texture, FRHIGPUFence* Fence, void*& OutData, int32& OutWidth, int32& OutHeight, uint32 GPUIndex)
{
	checkNoEntry();
}

void FDawnDynamicRHI::RHIUnmapStagingSurface(FRHITexture* Texture, uint32 GPUIndex)
{
	checkNoEntry();
}

void FDawnDynamicRHI::RHIReadSurfaceFloatData(FRHITexture* Texture, FIntRect Rect, TArray<FFloat16Color>& OutData, ECubeFace CubeFace, int32 ArrayIndex, int32 MipIndex)
{
	checkNoEntry();
}

void FDawnDynamicRHI::RHIRead3DSurfaceFloatData(FRHITexture* Texture, FIntRect Rect, FIntPoint ZMinMax, TArray<FFloat16Color>& OutData)
{
	checkNoEntry();
}

FRenderQueryRHIRef FDawnDynamicRHI::RHICreateRenderQuery(ERenderQueryType QueryType)
{
	checkNoEntry();
	return {};
}

bool FDawnDynamicRHI::RHIGetRenderQueryResult(FRHIRenderQuery* RenderQuery, uint64& OutResult, bool bWait, uint32 GPUIndex)
{
	checkNoEntry();
	return false;
}

FTextureRHIRef FDawnDynamicRHI::RHIGetViewportBackBuffer(FRHIViewport* Viewport)
{
	checkNoEntry();
	return {};
}

FViewportRHIRef FDawnDynamicRHI::RHICreateViewport(void* WindowHandle, uint32 SizeX, uint32 SizeY, bool bIsFullscreen, EPixelFormat PixelFormat)
{
	checkNoEntry();
	return {};
}

void FDawnDynamicRHI::RHIResizeViewport(FRHIViewport* Viewport, uint32 SizeX, uint32 SizeY, bool bIsFullscreen, EPixelFormat PixelFormat)
{
	checkNoEntry();
}

void FDawnDynamicRHI::RHIEndDrawingViewport(FRHICommandListImmediate& RHICmdList, FRHIViewport* Viewport, FRHIPresentArgs const& PresentArgs)
{
	checkNoEntry();
}

void FDawnDynamicRHI::RHITick(float DeltaTime)
{
	checkNoEntry();
}

bool FDawnDynamicRHI::RHIGetAvailableResolutions(FScreenResolutionArray& Resolutions, bool bIgnoreRefreshRate)
{
	checkNoEntry();
	return false;
}

void FDawnDynamicRHI::RHIGetSupportedResolution(uint32& Width, uint32& Height)
{
	checkNoEntry();
}
