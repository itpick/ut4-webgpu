// Stage 2 acceptance test: a transformed, textured, depth-tested quad through
// the real DawnRHI (FDynamicRHI/IRHICommandContext), broadening on the
// Stage 1 triangle test (uniform buffer + MVP, sampled texture + sampler,
// depth/stencil, RHISetShaderParameters/bind groups). Still hand-authored
// WGSL — the HLSL->SPIR-V->WGSL cook path is the separate, larger piece of
// this milestone.
#include "CoreMinimal.h"
#include "RequiredProgramMainCPPInclude.h"
#include "RHI.h"
#include "DynamicRHI.h"
#include "RHIContext.h"
#include "RHICommandList.h"
#include "RHITextureInitializer.h"
#include "RHIBufferInitializer.h"
#include "RHITypes.h"
#include "RHIShaderParameters.h"
#include "RHIUniformBufferLayoutInitializer.h"
#include "Modules/ModuleManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"

THIRD_PARTY_INCLUDES_START
#include <ShaderConductor/ShaderConductor.hpp>
THIRD_PARTY_INCLUDES_END

IMPLEMENT_APPLICATION(DawnRHITest, "DawnRHITest");

DEFINE_LOG_CATEGORY_STATIC(LogDawnRHITest, Log, All);

// FRHICommandListBase's real constructor is `protected` — only reachable
// from a derived type. We don't need any of its deferred-recording
// machinery (FDawnCommandContext executes immediately), we only need a
// valid instance to satisfy the `FRHICommandListBase&` parameter on
// RHICreateBufferInitializer/RHICreateTextureInitializer.
class FTestCommandList final : public FRHICommandListBase
{
public:
	FTestCommandList() : FRHICommandListBase(FRHIGPUMask::All(), /*bInImmediate=*/true) {}
};

// Stage 2 binding convention (see FDawnGraphicsPipelineState::BindGroupLayout):
// @group(0) @binding(0) uniform MVP buffer, @binding(1) texture, @binding(2) sampler.
static const TCHAR* GVertexWGSL = TEXT(R"(
struct Uniforms {
  mvp : mat4x4<f32>,
};
@group(0) @binding(0) var<uniform> uniforms : Uniforms;

struct VSOut {
  @builtin(position) pos : vec4<f32>,
  @location(0) uv : vec2<f32>,
};

@vertex
fn vs_main(@location(0) inPos : vec3<f32>, @location(1) inUV : vec2<f32>) -> VSOut {
  var out : VSOut;
  out.pos = uniforms.mvp * vec4<f32>(inPos, 1.0);
  out.uv = inUV;
  return out;
}
)");

static const TCHAR* GPixelWGSL = TEXT(R"(
@group(0) @binding(1) var tex : texture_2d<f32>;
@group(0) @binding(2) var samp : sampler;

@fragment
fn fs_main(@location(0) uv : vec2<f32>) -> @location(0) vec4<f32> {
  return textureSample(tex, samp, uv);
}
)");

static TArray<uint8> StringToUtf8Bytes(const TCHAR* Str)
{
	FTCHARToUTF8 Converter(Str);
	TArray<uint8> Bytes;
	Bytes.Append(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length());
	return Bytes;
}

// Stage 2 shader-cook-path probe #1: does Epic's own ShaderConductor
// (HLSL -> SPIR-V, standard open ThirdParty, prebuilt Linux .so) work when
// built/linked through UBT's own toolchain?
//
// STATUS: reproducibly crashes (SIGSEGV, heap corruption) inside
// SPIRV-Tools' internal passes — InlineExhaustivePass when optimizations
// are enabled, spirvToolsTrimCapabilities (a *mandatory* legalization pass,
// unaffected by Options::disableOptimizations) otherwise. Confirmed NOT
// caused by: (a) ad-hoc/wrong toolchain — rebuilt through UBT's own
// v26_clang-20.1.8-rockylinux8 toolchain, the exact one embedded in the
// vendored .so's own debug symbols; (b) shader content — a maximally
// trivial passthrough shader (no cbuffer, no struct) crashes identically;
// (c) UE's Mimalloc allocator override — crashes identically with
// `-ansimalloc`. The HLSL Clang-based frontend + SPIR-V emission genuinely
// run first (real progress), so this isn't a link/load failure.
//
// Best remaining hypothesis: Epic normally hosts this exact fragile
// DXC/SPIRV-Tools stack only inside the dedicated, specially-configured
// ShaderCompileWorker process (isolated per-shader subprocess, specific
// build flags) — never called in-process from an arbitrary minimal
// program the way this probe does. Untested next step: replicate that
// process-isolation model (or ShaderCompileWorker's exact Target.cs
// flags) rather than calling ShaderConductor in-process here. Disabled
// by default (guarded behind -testshaderconductor) so it doesn't block
// the working DawnRHI scene render below.
static void ShaderConductorSmokeTest()
{
	using namespace ShaderConductor;
	static const char* kHLSL = R"(
float4 vs_main(float3 inPos : POSITION) : SV_Position {
  return float4(inPos, 1.0);
}
)";
	Compiler::SourceDesc Source = {};
	Source.source = kHLSL;
	Source.fileName = "probe.hlsl";
	Source.entryPoint = "vs_main";
	Source.stage = ShaderStage::VertexShader;

	Compiler::Options Options = {};
	Options.disableOptimizations = true; // see DAWNRHI_README / commit log: SPIRV-Tools optimizer crashes in this environment
	Compiler::TargetDesc Target = {};
	Target.language = ShadingLanguage::SpirV;

	Compiler::ResultDesc Result = Compiler::Compile(Source, Options, Target);
	if (Result.hasError)
	{
		const char* Msg = reinterpret_cast<const char*>(Result.errorWarningMsg.Data());
		UE_LOG(LogDawnRHITest, Error, TEXT("ShaderConductor error: %s"), ANSI_TO_TCHAR(Msg));
		return;
	}
	UE_LOG(LogDawnRHITest, Log, TEXT("ShaderConductor SUCCESS: HLSL -> SPIR-V, %u bytes"), (uint32)Result.target.Size());
	const uint32* Words = reinterpret_cast<const uint32*>(Result.target.Data());
	UE_LOG(LogDawnRHITest, Log, TEXT("SPIR-V magic = 0x%08x (expect 0x07230203)"), Words[0]);

	TArray<uint8> SpirvBytes;
	SpirvBytes.Append(reinterpret_cast<const uint8*>(Result.target.Data()), Result.target.Size());
	FFileHelper::SaveArrayToFile(SpirvBytes, TEXT("/tmp/dawnrhi_sc_probe_vs.spv"));
	UE_LOG(LogDawnRHITest, Log, TEXT("Wrote /tmp/dawnrhi_sc_probe_vs.spv"));
}

int RunDawnRHITest()
{
	if (FParse::Param(FCommandLine::Get(), TEXT("testshaderconductor")))
	{
		ShaderConductorSmokeTest();
	}

	IDynamicRHIModule* RHIModule = &FModuleManager::LoadModuleChecked<IDynamicRHIModule>(TEXT("DawnRHI"));
	checkf(RHIModule->IsSupported(), TEXT("DawnRHI: not supported on this platform"));

	GDynamicRHI = RHIModule->CreateRHI();
	checkf(GDynamicRHI, TEXT("DawnRHI: CreateRHI returned null"));
	GDynamicRHI->Init();

	UE_LOG(LogDawnRHITest, Log, TEXT("DawnRHI initialised: %s"), GDynamicRHI->GetName());

	FTestCommandList CmdList;

	// --- Quad mesh: pos(vec3) + uv(vec2), 6 verts (2 triangles, no index buffer yet) ---
	struct FVertex { float X, Y, Z, U, V; };
	FVertex Verts[6] = {
		{ -0.5f, -0.5f, 0.0f, 0.0f, 1.0f },
		{  0.5f, -0.5f, 0.0f, 1.0f, 1.0f },
		{  0.5f,  0.5f, 0.0f, 1.0f, 0.0f },
		{ -0.5f, -0.5f, 0.0f, 0.0f, 1.0f },
		{  0.5f,  0.5f, 0.0f, 1.0f, 0.0f },
		{ -0.5f,  0.5f, 0.0f, 0.0f, 0.0f },
	};
	const uint32 VertexStride = sizeof(FVertex);
	const uint32 VertexBufferSize = sizeof(Verts);

	FRHIBufferCreateDesc BufDesc = FRHIBufferCreateDesc::Create(TEXT("QuadVB"), VertexBufferSize, VertexStride, EBufferUsageFlags::VertexBuffer);
	FRHIBufferInitializer BufInit = GDynamicRHI->RHICreateBufferInitializer(CmdList, BufDesc);
	BufInit.WriteData(Verts, VertexBufferSize);
	FBufferRHIRef VertexBuffer = BufInit.Finalize();

	FVertexDeclarationElementList Elements;
	Elements.Add(FVertexElement(0, offsetof(FVertex, X), VET_Float3, 0, VertexStride));
	Elements.Add(FVertexElement(0, offsetof(FVertex, U), VET_Float2, 1, VertexStride));
	FVertexDeclarationRHIRef VertexDecl = GDynamicRHI->RHICreateVertexDeclaration(Elements);

	// --- Shaders: hand-authored WGSL (the HLSL->SPIR-V->WGSL cook path is separate work) ---
	TArray<uint8> VsCode = StringToUtf8Bytes(GVertexWGSL);
	TArray<uint8> PsCode = StringToUtf8Bytes(GPixelWGSL);
	FVertexShaderRHIRef VertexShader = GDynamicRHI->RHICreateVertexShader(FRHICreateShaderDesc(VsCode));
	FPixelShaderRHIRef PixelShader = GDynamicRHI->RHICreatePixelShader(FRHICreateShaderDesc(PsCode));

	// --- Sampler + checkerboard texture ---
	FSamplerStateInitializerRHI SamplerInit(SF_Point, AM_Wrap, AM_Wrap, AM_Wrap);
	FSamplerStateRHIRef Sampler = GDynamicRHI->RHICreateSamplerState(SamplerInit);

	const int32 TexSize = 8;
	FRHITextureCreateDesc TexDesc2D = FRHITextureCreateDesc::Create2D(TEXT("Checkerboard"), TexSize, TexSize, PF_R8G8B8A8);
	TexDesc2D.AddFlags(ETextureCreateFlags::ShaderResource);
	FRHITextureInitializer TexInit2D = GDynamicRHI->RHICreateTextureInitializer(CmdList, TexDesc2D);
	{
		// NOTE: deliberately NOT using TArray<FColor> + raw memcpy here.
		// FColor's in-memory byte layout on little-endian is B,G,R,A (see
		// Math/Color.h) — a memcpy of FColor structs into an RGBA8Unorm
		// texture silently swaps the R and B channels. The texture format
		// we declared (PF_R8G8B8A8 -> WGPUTextureFormat_RGBA8Unorm) means
		// "byte 0 is R", so we write explicit R,G,B,A bytes to match.
		FRHITextureSubresourceInitializer Sub = TexInit2D.GetTexture2DSubresource(0);
		TArray<uint8> Checker;
		Checker.SetNumUninitialized(TexSize * TexSize * 4);
		for (int32 y = 0; y < TexSize; ++y)
		{
			for (int32 x = 0; x < TexSize; ++x)
			{
				bool bEven = ((x / 2) + (y / 2)) % 2 == 0;
				uint8* Px = &Checker[(y * TexSize + x) * 4];
				if (bEven) { Px[0] = 255; Px[1] = 200; Px[2] = 40;  Px[3] = 255; }
				else       { Px[0] = 30;  Px[1] = 60;  Px[2] = 200; Px[3] = 255; }
			}
		}
		Sub.WriteData(Checker.GetData(), Checker.Num());
	}
	FTextureRHIRef CheckerTexture = TexInit2D.Finalize();

	// --- Uniform buffer: MVP = uniform scale 0.7 + translate (0.2, -0.1) ---
	// Column-major, matching WGSL's mat4x4<f32> layout, so `mvp * vec4(pos,1)`
	// applies scale to x/y/z and adds the translation from column 3.
	struct FUniforms { float Mvp[16]; };
	FUniforms Uniforms = {};
	Uniforms.Mvp[0] = 0.7f;  Uniforms.Mvp[1] = 0.0f;  Uniforms.Mvp[2]  = 0.0f; Uniforms.Mvp[3]  = 0.0f;
	Uniforms.Mvp[4] = 0.0f;  Uniforms.Mvp[5] = 0.7f;  Uniforms.Mvp[6]  = 0.0f; Uniforms.Mvp[7]  = 0.0f;
	Uniforms.Mvp[8] = 0.0f;  Uniforms.Mvp[9] = 0.0f;  Uniforms.Mvp[10] = 0.7f; Uniforms.Mvp[11] = 0.0f;
	Uniforms.Mvp[12] = 0.2f; Uniforms.Mvp[13] = -0.1f; Uniforms.Mvp[14] = 0.0f; Uniforms.Mvp[15] = 1.0f;

	FRHIUniformBufferLayoutInitializer LayoutInit(TEXT("DawnRHITestUniforms"), sizeof(FUniforms));
	TRefCountPtr<FRHIUniformBufferLayout> UBLayout = new FRHIUniformBufferLayout(LayoutInit);
	FUniformBufferRHIRef UniformBuffer = GDynamicRHI->RHICreateUniformBuffer(&Uniforms, UBLayout, UniformBuffer_MultiFrame, EUniformBufferValidation::None);

	// --- Graphics pipeline state (with depth/stencil) ---
	FDepthStencilStateInitializerRHI DepthInit(/*bEnableDepthWrite=*/true, CF_LessEqual);
	FDepthStencilStateRHIRef DepthState = GDynamicRHI->RHICreateDepthStencilState(DepthInit);

	FGraphicsPipelineStateInitializer PsoInit;
	PsoInit.BoundShaderState.VertexDeclarationRHI = VertexDecl;
	PsoInit.BoundShaderState.VertexShaderRHI = VertexShader;
	PsoInit.BoundShaderState.PixelShaderRHI = PixelShader;
	PsoInit.PrimitiveType = PT_TriangleList;
	PsoInit.RenderTargetsEnabled = 1;
	PsoInit.RenderTargetFormats[0] = UE_PIXELFORMAT_TO_UINT8(PF_R8G8B8A8);
	PsoInit.DepthStencilTargetFormat = PF_DepthStencil;
	PsoInit.DepthStencilState = DepthState;
	FGraphicsPipelineStateRHIRef PSO = GDynamicRHI->RHICreateGraphicsPipelineState(PsoInit);

	// --- Offscreen colour + depth render targets ---
	const int32 Width = 256, Height = 256;
	FRHITextureCreateDesc ColorDesc = FRHITextureCreateDesc::Create2D(TEXT("Offscreen"), Width, Height, PF_R8G8B8A8);
	ColorDesc.AddFlags(ETextureCreateFlags::RenderTargetable);
	FRHITextureInitializer ColorInit = GDynamicRHI->RHICreateTextureInitializer(CmdList, ColorDesc);
	FTextureRHIRef ColorTarget = ColorInit.Finalize();

	FRHITextureCreateDesc DepthDesc = FRHITextureCreateDesc::Create2D(TEXT("OffscreenDepth"), Width, Height, PF_DepthStencil);
	DepthDesc.AddFlags(ETextureCreateFlags::DepthStencilTargetable);
	FRHITextureInitializer DepthInit2 = GDynamicRHI->RHICreateTextureInitializer(CmdList, DepthDesc);
	FTextureRHIRef DepthTarget = DepthInit2.Finalize();

	// --- Draw ---
	IRHICommandContext* Context = GDynamicRHI->RHIGetDefaultContext();
	FRHIRenderPassInfo RPInfo(ColorTarget, ERenderTargetActions::Clear_Store);
	RPInfo.DepthStencilRenderTarget.DepthStencilTarget = DepthTarget;
	RPInfo.DepthStencilRenderTarget.Action = EDepthStencilTargetActions::ClearDepthStencil_StoreDepthStencil;

	Context->RHIBeginRenderPass(RPInfo, TEXT("DawnRHITest.Quad"));
	Context->RHISetGraphicsPipelineState(PSO, 0, true);
	Context->RHISetStreamSource(0, VertexBuffer, 0);
	Context->RHISetViewport(0, 0, 0, (float)Width, (float)Height, 1);

	FRHIShaderParameterResource ResourceParams[3] = {
		FRHIShaderParameterResource(UniformBuffer.GetReference(), 0),
		FRHIShaderParameterResource(CheckerTexture.GetReference(), 1),
		FRHIShaderParameterResource(Sampler.GetReference(), 2),
	};
	Context->RHISetShaderParameters(PixelShader.GetReference(), TConstArrayView<uint8>(), TConstArrayView<FRHIShaderParameter>(), TConstArrayView<FRHIShaderParameterResource>(ResourceParams, 3), TConstArrayView<FRHIShaderParameterResource>());

	Context->RHIDrawPrimitive(0, 2, 1);
	Context->RHIEndRenderPass();

	GDynamicRHI->RHISubmitCommandLists({});
	GDynamicRHI->RHIBlockUntilGPUIdle();

	// --- Readback ---
	TArray<FColor> Pixels;
	GDynamicRHI->RHIReadSurfaceData(ColorTarget, FIntRect(0, 0, Width, Height), Pixels, FReadSurfaceDataFlags());
	checkf(Pixels.Num() == Width * Height, TEXT("DawnRHI: readback size mismatch"));

	// --- Write PPM ---
	FString OutPath = TEXT("/tmp/dawnrhi_scene2.ppm");
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

	const FColor Center = Pixels[(Height / 2) * Width + Width / 2];
	UE_LOG(LogDawnRHITest, Log, TEXT("Center pixel = (%d,%d,%d,%d)"), Center.R, Center.G, Center.B, Center.A);

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
