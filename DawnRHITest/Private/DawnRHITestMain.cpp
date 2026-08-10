// Stage 1 acceptance test: load the DawnRHI module, drive it through UE's
// real IDynamicRHIModule / FDynamicRHI / IRHICommandContext interfaces
// (not standalone Dawn calls), render a triangle to an offscreen texture,
// and read it back to a PPM.
//
// This program deliberately does NOT link against the DawnRHI module at
// compile time — it loads it by name via FModuleManager, exactly like the
// engine's own RHI selection does for Vulkan/D3D/etc. It also does not use
// FRHICommandListImmediate's deferred/parallel-translate machinery: Stage 1
// drives IRHICommandContext directly since FDawnCommandContext executes
// immediately (see DawnCommandContext.h).
#include "CoreMinimal.h"
#include "RequiredProgramMainCPPInclude.h"
#include "RHI.h"
#include "DynamicRHI.h"
#include "RHIContext.h"
#include "RHICommandList.h"
#include "RHITextureInitializer.h"
#include "RHIBufferInitializer.h"
#include "RHITypes.h"
#include "Modules/ModuleManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"

IMPLEMENT_APPLICATION(DawnRHITest, "DawnRHITest");

DEFINE_LOG_CATEGORY_STATIC(LogDawnRHITest, Log, All);

// FRHICommandListBase's real constructor is `protected` — only reachable
// from a derived type. We don't need any of its deferred-recording
// machinery for Stage 1 (FDawnCommandContext executes immediately), we
// only need a valid instance to satisfy the `FRHICommandListBase&`
// parameter on RHICreateBufferInitializer/RHICreateTextureInitializer.
class FTestCommandList final : public FRHICommandListBase
{
public:
	FTestCommandList() : FRHICommandListBase(FRHIGPUMask::All(), /*bInImmediate=*/true) {}
};

static const TCHAR* GVertexWGSL = TEXT(R"(
struct VSOut {
  @builtin(position) pos : vec4<f32>,
  @location(0) color : vec3<f32>,
};

@vertex
fn vs_main(@location(0) inPos : vec2<f32>, @location(1) inColor : vec3<f32>) -> VSOut {
  var out : VSOut;
  out.pos = vec4<f32>(inPos, 0.0, 1.0);
  out.color = inColor;
  return out;
}
)");

static const TCHAR* GPixelWGSL = TEXT(R"(
@fragment
fn fs_main(@location(0) color : vec3<f32>) -> @location(0) vec4<f32> {
  return vec4<f32>(color, 1.0);
}
)");

static TArray<uint8> StringToUtf8Bytes(const TCHAR* Str)
{
	FTCHARToUTF8 Converter(Str);
	TArray<uint8> Bytes;
	Bytes.Append(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length());
	return Bytes;
}

int RunDawnRHITest()
{
	IDynamicRHIModule* RHIModule = &FModuleManager::LoadModuleChecked<IDynamicRHIModule>(TEXT("DawnRHI"));
	checkf(RHIModule->IsSupported(), TEXT("DawnRHI: not supported on this platform"));

	GDynamicRHI = RHIModule->CreateRHI();
	checkf(GDynamicRHI, TEXT("DawnRHI: CreateRHI returned null"));
	GDynamicRHI->Init();

	UE_LOG(LogDawnRHITest, Log, TEXT("DawnRHI initialised: %s"), GDynamicRHI->GetName());

	FTestCommandList CmdList;

	// --- Vertex buffer: pos(vec2) + color(vec3), interleaved, 3 verts ---
	struct FVertex { float X, Y, R, G, B; };
	FVertex Verts[3] = {
		{  0.0f,  0.6f, 1.0f, 0.0f, 0.0f },
		{ -0.6f, -0.6f, 0.0f, 1.0f, 0.0f },
		{  0.6f, -0.6f, 0.0f, 0.0f, 1.0f },
	};
	const uint32 VertexStride = sizeof(FVertex);
	const uint32 VertexBufferSize = sizeof(Verts);

	FRHIBufferCreateDesc BufDesc = FRHIBufferCreateDesc::Create(TEXT("TriangleVB"), VertexBufferSize, VertexStride, EBufferUsageFlags::VertexBuffer);
	FRHIBufferInitializer BufInit = GDynamicRHI->RHICreateBufferInitializer(CmdList, BufDesc);
	BufInit.WriteData(Verts, VertexBufferSize);
	FBufferRHIRef VertexBuffer = BufInit.Finalize();

	// --- Vertex declaration matching FVertex ---
	FVertexDeclarationElementList Elements;
	Elements.Add(FVertexElement(0, offsetof(FVertex, X), VET_Float2, 0, VertexStride));
	Elements.Add(FVertexElement(0, offsetof(FVertex, R), VET_Float3, 1, VertexStride));
	FVertexDeclarationRHIRef VertexDecl = GDynamicRHI->RHICreateVertexDeclaration(Elements);

	// --- Shaders: hand-authored WGSL (Stage 1 — Stage 3 swaps the producer for HLSL->SPIR-V->WGSL) ---
	TArray<uint8> VsCode = StringToUtf8Bytes(GVertexWGSL);
	TArray<uint8> PsCode = StringToUtf8Bytes(GPixelWGSL);
	FVertexShaderRHIRef VertexShader = GDynamicRHI->RHICreateVertexShader(FRHICreateShaderDesc(VsCode));
	FPixelShaderRHIRef PixelShader = GDynamicRHI->RHICreatePixelShader(FRHICreateShaderDesc(PsCode));

	// --- Graphics pipeline state ---
	FGraphicsPipelineStateInitializer PsoInit;
	PsoInit.BoundShaderState.VertexDeclarationRHI = VertexDecl;
	PsoInit.BoundShaderState.VertexShaderRHI = VertexShader;
	PsoInit.BoundShaderState.PixelShaderRHI = PixelShader;
	PsoInit.PrimitiveType = PT_TriangleList;
	PsoInit.RenderTargetsEnabled = 1;
	PsoInit.RenderTargetFormats[0] = UE_PIXELFORMAT_TO_UINT8(PF_R8G8B8A8);
	FGraphicsPipelineStateRHIRef PSO = GDynamicRHI->RHICreateGraphicsPipelineState(PsoInit);

	// --- Offscreen render target ---
	const int32 Width = 256, Height = 256;
	FRHITextureCreateDesc TexDesc = FRHITextureCreateDesc::Create2D(TEXT("Offscreen"), Width, Height, PF_R8G8B8A8);
	TexDesc.AddFlags(ETextureCreateFlags::RenderTargetable);
	FRHITextureInitializer TexInit = GDynamicRHI->RHICreateTextureInitializer(CmdList, TexDesc);
	FTextureRHIRef ColorTarget = TexInit.Finalize();

	// --- Draw ---
	IRHICommandContext* Context = GDynamicRHI->RHIGetDefaultContext();
	FRHIRenderPassInfo RPInfo(ColorTarget, ERenderTargetActions::Clear_Store);
	Context->RHIBeginRenderPass(RPInfo, TEXT("DawnRHITest.Triangle"));
	Context->RHISetGraphicsPipelineState(PSO, 0, true);
	Context->RHISetStreamSource(0, VertexBuffer, 0);
	Context->RHISetViewport(0, 0, 0, (float)Width, (float)Height, 1);
	Context->RHIDrawPrimitive(0, 1, 1);
	Context->RHIEndRenderPass();

	GDynamicRHI->RHISubmitCommandLists({});
	GDynamicRHI->RHIBlockUntilGPUIdle();

	// --- Readback ---
	TArray<FColor> Pixels;
	GDynamicRHI->RHIReadSurfaceData(ColorTarget, FIntRect(0, 0, Width, Height), Pixels, FReadSurfaceDataFlags());
	checkf(Pixels.Num() == Width * Height, TEXT("DawnRHI: readback size mismatch"));

	// --- Write PPM ---
	FString OutPath = TEXT("/tmp/dawnrhi_via_ue_rhi.ppm");
	TArray<uint8> Ppm;
	FString Header = FString::Printf(TEXT("P6\n%d %d\n255\n"), Width, Height);
	FTCHARToUTF8 HeaderUtf8(*Header);
	Ppm.Append(reinterpret_cast<const uint8*>(HeaderUtf8.Get()), HeaderUtf8.Length());
	for (int32 i = 0; i < Pixels.Num(); ++i)
	{
		Ppm.Add(Pixels[i].R);
		Ppm.Add(Pixels[i].G);
		Ppm.Add(Pixels[i].B);
	}
	FFileHelper::SaveArrayToFile(Ppm, *OutPath);
	UE_LOG(LogDawnRHITest, Log, TEXT("Wrote %s"), *OutPath);

	const FColor Center = Pixels[(Height / 2 + 20) * Width + Width / 2];
	UE_LOG(LogDawnRHITest, Log, TEXT("Center-ish pixel = (%d,%d,%d,%d)"), Center.R, Center.G, Center.B, Center.A);

	GDynamicRHI->Shutdown();
	UE_LOG(LogDawnRHITest, Log, TEXT("SUCCESS"));
	return 0;
}

INT32_MAIN_INT32_ARGC_TCHAR_ARGV()
{
	GEngineLoop.PreInit(ArgC, ArgV, TEXT("-nocrashreports"));
	int32 Result = RunDawnRHITest();
	FEngineLoop::AppExit();
	return Result;
}
