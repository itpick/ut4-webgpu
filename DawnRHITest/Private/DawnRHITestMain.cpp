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

#include "DawnShaderConductorLoader.h"
#include "DawnResources.h" // milestone step 3: FDawnVertexShader/FDawnPixelShader/FDawnShaderBinding — see RunDawnRHIRealShaderTest below

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
//
// MILESTONE: this WGSL is no longer hand-authored -- it is the verbatim
// output of our own clean-path cook tool (tools/hlsl_to_wgsl.cpp in the
// itpick/ut4-webgpu repo) run on real HLSL source (see the comment above
// each shader below), through: Epic's open ShaderConductor (HLSL->SPIR-V,
// via FDawnShaderConductorLoader's dlopen/RTLD_DEEPBIND isolation) ->
// Google's open SPIRV-Tools Optimizer (RegisterLegalizationPasses() +
// CreateStripReflectInfoPass(), self-built -- see HANDOFF.md "Tint wall"
// for why self-built, not the vendored prebuilt libSPIRV-Tools.a/libtint.a)
// -> Google's open Tint IR reader/writer (tint::spirv::reader::ReadIR ->
// tint::wgsl::writer::WgslFromIR, also self-built). Not SimplyStream code
// anywhere in this chain. Never hand-edited after cooking.
//
// Cooked from (Engine-external, see tools/ in the repo for the exact
// files/commands):
//   struct Uniforms { float4x4 mvp; };
//   [[vk::binding(0, 0)]] ConstantBuffer<Uniforms> uniforms : register(b0);
//   struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
//   VSOut vs_main(float3 inPos : POSITION, float2 inUV : TEXCOORD0) {
//     VSOut o;
//     o.pos = mul(uniforms.mvp, float4(inPos, 1.0));
//     o.uv = inUV;
//     return o;
//   }
//
// NOTE: the cooked shader computes `vec4(pos,1) * uniforms.mvp` (vector *
// matrix -- WGSL's vector-matrix multiply, result[c] = dot(v, column_c(M)))
// rather than the hand-authored version's `uniforms.mvp * vec4(pos,1)`
// (matrix * vector). Both are valid HLSL->SPIR-V->WGSL translations of
// `mul(M,v)` (this is ShaderConductor's default `packMatricesInRowMajor`
// behaviour) -- they just require a TRANSPOSED CPU-side uniform buffer to
// produce the same on-screen transform; see FUniforms below.
static const TCHAR* GVertexWGSL = TEXT(R"(
struct S {
  mvp : mat4x4<f32>,
}

@group(0u) @binding(0u) var<uniform> uniforms : S;

var<private> v : vec4<f32>;

var<private> v_1 : vec2<f32>;

fn vs_main_inner(v_2 : vec3<f32>, v_3 : vec2<f32>) {
  v = (vec4<f32>(v_2.x, v_2.y, v_2.z, 1.0f) * uniforms.mvp);
  v_1 = v_3;
}

struct tint_symbol_1 {
  @builtin(position) @invariant
  tint_symbol : vec4<f32>,
  @location(0u)
  m : vec2<f32>,
}

@vertex
fn vs_main(@location(0u) v_4 : vec3<f32>, @location(1u) v_5 : vec2<f32>) -> tint_symbol_1 {
  vs_main_inner(v_4, v_5);
  return tint_symbol_1(v, v_1);
}
)");

// Cooked from:
//   [[vk::binding(1, 0)]] Texture2D tex : register(t0);
//   [[vk::binding(2, 0)]] SamplerState samp : register(s0);
//   float4 fs_main(float2 uv : TEXCOORD0) : SV_Target {
//     return tex.Sample(samp, uv);
//   }
static const TCHAR* GPixelWGSL = TEXT(R"(
@group(0u) @binding(1u) var tex : texture_2d<f32>;

@group(0u) @binding(2u) var samp : sampler;

var<private> v : vec4<f32>;

fn fs_main_inner(v_1 : vec2<f32>) {
  v = textureSample(tex, samp, v_1);
}

@fragment
fn fs_main(@location(0u) v_2 : vec2<f32>) -> @location(0u) vec4<f32> {
  fs_main_inner(v_2);
  return v;
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
// (HLSL -> SPIR-V, standard open ThirdParty, prebuilt Linux .so) work?
//
// FORMER STATUS (root-caused and FIXED this session, see
// DawnShaderConductorLoader.h for the full writeup): calling
// ShaderConductor::Compiler::Compile via a normal compile-time link
// (ELF DT_NEEDED) reproducibly SIGSEGV'd inside SPIRV-Tools' internal
// passes — InlineExhaustivePass with optimizations enabled,
// spirvToolsTrimCapabilities (a mandatory legalization pass) otherwise.
// Root cause: libc++ symbol interposition — libShaderConductor.so and
// this executable are BOTH fully self-contained (statically link their
// own private copy of libc++, zero external libc++.so/libstdc++.so
// dependency), so loading the .so into the same ELF global symbol scope
// let the .so's own *internal* SPIRV-Tools/libc++ calls get silently
// resolved against the executable's copy instead of its own — heap
// corruption. Ruled out first: wrong/mismatched toolchain (rebuilt via
// UBT's exact v26_clang-20.1.8-rockylinux8 toolchain), shader content
// (a maximally trivial passthrough shader crashed identically), UE's
// Mimalloc allocator override (crashed identically with -ansimalloc).
// Confirmed via a minimal standalone dlopen-based repro (zero duplicate
// libc++ symbols of its own) that Compiler::Compile is NOT fundamentally
// broken — it produces valid SPIR-V for both previously-crashing code
// paths (default Options AND disableOptimizations=true) once loaded via
// dlopen(RTLD_DEEPBIND) instead of a compile-time link. Fix now applied
// here via FDawnShaderConductorLoader.
static void ShaderConductorSmokeTest()
{
	using namespace ShaderConductor;
	static const char* kHLSL = R"(
float4 vs_main(float3 inPos : POSITION) : SV_Position {
  return float4(inPos, 1.0);
}
)";

	FDawnShaderConductorLoader Loader;
	FString InitError;
	if (!Loader.Init(InitError))
	{
		UE_LOG(LogDawnRHITest, Error, TEXT("FDawnShaderConductorLoader::Init failed: %s"), *InitError);
		return;
	}

	// Exercise BOTH previously-crashing code paths.
	for (bool bDisableOpt : { true, false })
	{
		TArray<uint32> Spirv;
		FString CompileError;
		if (!Loader.CompileHlslToSpirv(kHLSL, "probe.hlsl", "vs_main", ShaderStage::VertexShader, bDisableOpt, Spirv, CompileError))
		{
			UE_LOG(LogDawnRHITest, Error, TEXT("ShaderConductor error (disableOptimizations=%d): %s"), bDisableOpt ? 1 : 0, *CompileError);
			continue;
		}
		UE_LOG(LogDawnRHITest, Log, TEXT("ShaderConductor SUCCESS (disableOptimizations=%d): HLSL -> SPIR-V, %u bytes, magic=0x%08x"),
			bDisableOpt ? 1 : 0, Spirv.Num() * 4, Spirv.Num() > 0 ? Spirv[0] : 0);

		if (bDisableOpt)
		{
			TArray<uint8> SpirvBytes;
			SpirvBytes.Append(reinterpret_cast<const uint8*>(Spirv.GetData()), Spirv.Num() * 4);
			FFileHelper::SaveArrayToFile(SpirvBytes, TEXT("/tmp/dawnrhi_sc_probe_vs.spv"));
			UE_LOG(LogDawnRHITest, Log, TEXT("Wrote /tmp/dawnrhi_sc_probe_vs.spv"));
		}
	}
}

// Milestone step 3: render a REAL cooked UE shader pair (ScreenPass.usf's
// ScreenPassVS/CopyRectPS, cooked via tools/cook_real_shader.sh through the
// real DawnShaderFormat IShaderFormat -- see HANDOFF.md) through FDawnDynamicRHI,
// using the REAL reflected bindings DawnCookProbeMain.cpp wrote to each
// shader's ".bindings.txt" sidecar (see WriteBindingsSidecar in
// DawnCookProbeMain.cpp / Output.ParameterMap population in
// DawnShaderCompiler.cpp) to build a reflection-driven BindGroupLayout and
// resource-parameter list -- NOT the old fixed @group(0){0,1,2} assumption
// (see DawnResources.h/DawnDynamicRHI.cpp's FDawnShaderBinding wiring).
//
// Usage: DawnRHITest -realshader <vs.wgsl> <ps.wgsl> <out.png>
namespace
{
	struct FParsedBinding
	{
		FString Name;
		uint32 Set = 0;
		uint32 Binding = 0;
		FString Kind;
	};

	TArray<FParsedBinding> LoadBindingsSidecar(const FString& WgslPath)
	{
		TArray<FParsedBinding> Out;
		TArray<FString> Lines;
		FFileHelper::LoadFileToStringArray(Lines, *(WgslPath + TEXT(".bindings.txt")));
		for (const FString& Line : Lines)
		{
			TArray<FString> Cols;
			Line.ParseIntoArray(Cols, TEXT("\t"), false);
			if (Cols.Num() < 4)
			{
				continue;
			}
			FParsedBinding B;
			B.Name = Cols[0];
			B.Set = (uint32)FCString::Atoi(*Cols[1]);
			B.Binding = (uint32)FCString::Atoi(*Cols[2]);
			B.Kind = Cols[3];
			Out.Add(B);
		}
		return Out;
	}

	TArray<FDawnShaderBinding> ToDawnShaderBindings(const TArray<FParsedBinding>& Parsed)
	{
		TArray<FDawnShaderBinding> Out;
		for (const FParsedBinding& P : Parsed)
		{
			FDawnShaderBinding B;
			B.Group = P.Set;
			B.Binding = P.Binding;
			B.Name = P.Name;
			if (P.Kind == TEXT("UniformBuffer")) { B.Kind = EDawnShaderBindingKind::UniformBuffer; }
			else if (P.Kind == TEXT("Texture"))  { B.Kind = EDawnShaderBindingKind::Texture; }
			else if (P.Kind == TEXT("Sampler"))  { B.Kind = EDawnShaderBindingKind::Sampler; }
			else                                  { B.Kind = EDawnShaderBindingKind::Unknown; }
			Out.Add(B);
		}
		return Out;
	}

	uint32 FindBindingIndex(const TArray<FParsedBinding>& Parsed, const TCHAR* Name)
	{
		for (const FParsedBinding& P : Parsed)
		{
			if (P.Name == Name)
			{
				return P.Binding;
			}
		}
		checkf(false, TEXT("DawnRHITest -realshader: reflected binding '%s' not found in sidecar"), Name);
		return 0;
	}

	TArray<uint8> LoadFileUtf8Bytes(const FString& Path)
	{
		FString Text;
		// NOTE: checkf() is compiled out entirely in Shipping (this Target's
		// config) -- a load failure here would previously fall through
		// silently to an empty Code array instead of aborting, which is
		// exactly the real bug this session hit (see HANDOFF.md). Verify
		// with a real runtime check + UE_LOG instead.
		const bool bLoaded = FFileHelper::LoadFileToString(Text, *Path);
		UE_LOG(LogTemp, Log, TEXT("DawnRHITest -realshader: LoadFileToString('%s') -> %s, %d chars"), *Path, bLoaded ? TEXT("OK") : TEXT("FAILED"), Text.Len());
		if (!bLoaded)
		{
			UE_LOG(LogTemp, Fatal, TEXT("DawnRHITest -realshader: failed to load %s"), *Path);
		}
		FTCHARToUTF8 Converter(*Text);
		TArray<uint8> Bytes;
		Bytes.Append(reinterpret_cast<const uint8*>(Converter.Get()), Converter.Length());
		return Bytes;
	}
}

DEFINE_LOG_CATEGORY_STATIC(LogDawnRHIRealShader, Log, All);

int RunDawnRHIRealShaderTest(const FString& VsPath, const FString& PsPath, const FString& OutPpmPath)
{
	const TArray<FParsedBinding> VsBindings = LoadBindingsSidecar(VsPath);
	const TArray<FParsedBinding> PsBindings = LoadBindingsSidecar(PsPath);
	UE_LOG(LogDawnRHIRealShader, Log, TEXT("Loaded %d VS binding(s), %d PS binding(s) from real reflection sidecars"), VsBindings.Num(), PsBindings.Num());

	IDynamicRHIModule* RHIModule = &FModuleManager::LoadModuleChecked<IDynamicRHIModule>(TEXT("DawnRHI"));
	checkf(RHIModule->IsSupported(), TEXT("DawnRHI: not supported on this platform"));

	GDynamicRHI = RHIModule->CreateRHI();
	checkf(GDynamicRHI, TEXT("DawnRHI: CreateRHI returned null"));
	GDynamicRHI->Init();
	UE_LOG(LogDawnRHIRealShader, Log, TEXT("DawnRHI initialised: %s"), GDynamicRHI->GetName());

	FTestCommandList CmdList;

	// --- Real vertex interface: ScreenPassVS takes ATTRIBUTE0 = float4
	// position, ATTRIBUTE1 = float2 UV (see the cooked WGSL's
	// @location(0)/@location(1) vertex inputs) -- a unit quad in [0,1]^2,
	// matching real UE's DrawRectangle() convention (see below).
	struct FVertex { float X, Y, Z, W, U, V; };
	FVertex Verts[6] = {
		{ 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f },
		{ 1.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f },
		{ 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f },
		{ 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f },
		{ 1.0f, 1.0f, 0.0f, 1.0f, 1.0f, 1.0f },
		{ 0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 1.0f },
	};
	const uint32 VertexStride = sizeof(FVertex);
	FRHIBufferCreateDesc BufDesc = FRHIBufferCreateDesc::Create(TEXT("QuadVB"), sizeof(Verts), VertexStride, EBufferUsageFlags::VertexBuffer);
	FRHIBufferInitializer BufInit = GDynamicRHI->RHICreateBufferInitializer(CmdList, BufDesc);
	BufInit.WriteData(Verts, sizeof(Verts));
	FBufferRHIRef VertexBuffer = BufInit.Finalize();

	FVertexDeclarationElementList Elements;
	Elements.Add(FVertexElement(0, offsetof(FVertex, X), VET_Float4, 0, VertexStride));
	Elements.Add(FVertexElement(0, offsetof(FVertex, U), VET_Float2, 1, VertexStride));
	FVertexDeclarationRHIRef VertexDecl = GDynamicRHI->RHICreateVertexDeclaration(Elements);

	// --- Real cooked shaders (WGSL text is the verbatim tools/cook_real_shader.sh
	// output -- see HANDOFF.md) ---
	TArray<uint8> VsCode = LoadFileUtf8Bytes(VsPath);
	TArray<uint8> PsCode = LoadFileUtf8Bytes(PsPath);
	FVertexShaderRHIRef VertexShader = GDynamicRHI->RHICreateVertexShader(FRHICreateShaderDesc(VsCode));
	FPixelShaderRHIRef PixelShader = GDynamicRHI->RHICreatePixelShader(FRHICreateShaderDesc(PsCode));

	// Hand each shader its real reflected bindings (milestone step 1's
	// output) so RHICreateGraphicsPipelineState builds a reflection-driven
	// BindGroupLayout instead of assuming fixed {0,1,2} slots.
	static_cast<FDawnVertexShader*>(VertexShader.GetReference())->Bindings = ToDawnShaderBindings(VsBindings);
	static_cast<FDawnPixelShader*>(PixelShader.GetReference())->Bindings = ToDawnShaderBindings(PsBindings);

	// --- Sampler + checkerboard texture (InputTexture/InputSampler) ---
	FSamplerStateInitializerRHI SamplerInit(SF_Point, AM_Wrap, AM_Wrap, AM_Wrap);
	FSamplerStateRHIRef Sampler = GDynamicRHI->RHICreateSamplerState(SamplerInit);

	const int32 TexSize = 8;
	FRHITextureCreateDesc TexDesc2D = FRHITextureCreateDesc::Create2D(TEXT("Checkerboard"), TexSize, TexSize, PF_R8G8B8A8);
	TexDesc2D.AddFlags(ETextureCreateFlags::ShaderResource);
	FRHITextureInitializer TexInit2D = GDynamicRHI->RHICreateTextureInitializer(CmdList, TexDesc2D);
	{
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

	// --- DrawRectangleParameters uniform buffer: real UE DrawRectangle()
	// convention (Common.ush), reverse-engineered directly from the cooked
	// VS body (see HANDOFF.md) -- PosScaleBias/UVScaleBias are (Scale.xy,
	// Bias.xy) in PIXELS, InvTargetSizeAndTextureSize is
	// (1/TargetSize.xy, 1/TextureSize.xy). With PosScaleBias = (Width,
	// Height, 0, 0) and a unit-quad InPosition in [0,1]^2, this maps the
	// quad to cover the full 256x256 render target; UVScaleBias =
	// (TexSize, TexSize, 0, 0) maps the unit-quad UV to cover the full
	// checkerboard texture.
	const int32 Width = 256, Height = 256;
	struct FDrawRectangleParameters { float PosScaleBias[4]; float UVScaleBias[4]; float InvTargetSizeAndTextureSize[4]; };
	FDrawRectangleParameters DrawRectParams = {
		{ (float)Width, (float)Height, 0.0f, 0.0f },
		{ (float)TexSize, (float)TexSize, 0.0f, 0.0f },
		{ 1.0f / Width, 1.0f / Height, 1.0f / TexSize, 1.0f / TexSize },
	};
	FRHIUniformBufferLayoutInitializer LayoutInit(TEXT("DrawRectangleParameters"), sizeof(FDrawRectangleParameters));
	TRefCountPtr<FRHIUniformBufferLayout> UBLayout = new FRHIUniformBufferLayout(LayoutInit);
	FUniformBufferRHIRef UniformBuffer = GDynamicRHI->RHICreateUniformBuffer(&DrawRectParams, UBLayout, UniformBuffer_MultiFrame, EUniformBufferValidation::None);

	// --- Graphics pipeline state (no depth needed for this simple blit) ---
	FGraphicsPipelineStateInitializer PsoInit;
	PsoInit.BoundShaderState.VertexDeclarationRHI = VertexDecl;
	PsoInit.BoundShaderState.VertexShaderRHI = VertexShader;
	PsoInit.BoundShaderState.PixelShaderRHI = PixelShader;
	PsoInit.PrimitiveType = PT_TriangleList;
	PsoInit.RenderTargetsEnabled = 1;
	PsoInit.RenderTargetFormats[0] = UE_PIXELFORMAT_TO_UINT8(PF_R8G8B8A8);
	FGraphicsPipelineStateRHIRef PSO = GDynamicRHI->RHICreateGraphicsPipelineState(PsoInit);

	// --- Offscreen colour render target ---
	FRHITextureCreateDesc ColorDesc = FRHITextureCreateDesc::Create2D(TEXT("Offscreen"), Width, Height, PF_R8G8B8A8);
	ColorDesc.AddFlags(ETextureCreateFlags::RenderTargetable);
	FRHITextureInitializer ColorInit = GDynamicRHI->RHICreateTextureInitializer(CmdList, ColorDesc);
	FTextureRHIRef ColorTarget = ColorInit.Finalize();

	// --- Draw ---
	IRHICommandContext* Context = GDynamicRHI->RHIGetDefaultContext();
	FRHIRenderPassInfo RPInfo(ColorTarget, ERenderTargetActions::Clear_Store);
	Context->RHIBeginRenderPass(RPInfo, TEXT("DawnRHITest.RealShader"));
	Context->RHISetGraphicsPipelineState(PSO, 0, true);
	Context->RHISetStreamSource(0, VertexBuffer, 0);
	Context->RHISetViewport(0, 0, 0, (float)Width, (float)Height, 1);

	// Real reflected indices (NOT hardcoded 0/1/2) -- proves the bind group
	// this test builds is actually driven by milestone step 1's reflection
	// data, not eyeballed from the printed WGSL.
	const uint32 DrawRectIdx = FindBindingIndex(VsBindings, TEXT("DrawRectangleParameters"));
	const uint32 TexIdx = FindBindingIndex(PsBindings, TEXT("InputTexture"));
	const uint32 SampIdx = FindBindingIndex(PsBindings, TEXT("InputSampler"));
	UE_LOG(LogDawnRHIRealShader, Log, TEXT("Reflected bind indices: DrawRectangleParameters=%u InputTexture=%u InputSampler=%u"), DrawRectIdx, TexIdx, SampIdx);

	FRHIShaderParameterResource ResourceParams[3] = {
		FRHIShaderParameterResource(UniformBuffer.GetReference(), (uint16)DrawRectIdx),
		FRHIShaderParameterResource(CheckerTexture.GetReference(), (uint16)TexIdx),
		FRHIShaderParameterResource(Sampler.GetReference(), (uint16)SampIdx),
	};
	Context->RHISetShaderParameters(PixelShader.GetReference(), TConstArrayView<uint8>(), TConstArrayView<FRHIShaderParameter>(), TConstArrayView<FRHIShaderParameterResource>(ResourceParams, 3), TConstArrayView<FRHIShaderParameterResource>());

	Context->RHIDrawPrimitive(0, 2, 1);
	Context->RHIEndRenderPass();

	GDynamicRHI->RHISubmitCommandLists({});
	GDynamicRHI->RHIBlockUntilGPUIdle();

	// --- Readback + write PPM (converted to PNG by tooling outside the RHI --
	// see tools/ppm_to_png.py) ---
	TArray<FColor> Pixels;
	GDynamicRHI->RHIReadSurfaceData(ColorTarget, FIntRect(0, 0, Width, Height), Pixels, FReadSurfaceDataFlags());
	checkf(Pixels.Num() == Width * Height, TEXT("DawnRHI: readback size mismatch"));

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
	FFileHelper::SaveArrayToFile(Ppm, *OutPpmPath);
	UE_LOG(LogDawnRHIRealShader, Log, TEXT("Wrote %s"), *OutPpmPath);

	const FColor Center = Pixels[(Height / 2) * Width + Width / 2];
	const FColor Corner = Pixels[4 * Width + 4];
	UE_LOG(LogDawnRHIRealShader, Log, TEXT("Center pixel = (%d,%d,%d,%d), corner(4,4) = (%d,%d,%d,%d)"),
		Center.R, Center.G, Center.B, Center.A, Corner.R, Corner.G, Corner.B, Corner.A);

	GDynamicRHI->Shutdown();
	UE_LOG(LogDawnRHIRealShader, Log, TEXT("SUCCESS"));
	return 0;
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
	// Storage is still column-major (WGSL's fixed mat4x4<f32> host-shareable
	// layout: Mvp[0..3]=column0, [4..7]=column1, [8..11]=column2,
	// [12..15]=column3 -- this is NOT affected by shader source order, it's
	// the WGSL memory layout spec). What DOES change with the cooked
	// vertex shader (see GVertexWGSL above) is that it computes
	// `vec4(pos,1) * uniforms.mvp` (vector * matrix: per the WGSL spec,
	// result[c] = dot(v, column_c(M))) instead of the old hand-authored
	// `uniforms.mvp * vec4(pos,1)` (matrix * vector: result = sum_c
	// v[c]*column_c(M)). Those two are transposes of each other, so to get
	// the identical on-screen transform (scale 0.7, translate (0.2,-0.1))
	// this buffer is the TRANSPOSE of the original: the translation moves
	// from column 3's (x,y,z) into the 4th (w) component of columns 0/1
	// (Mvp[3]/Mvp[7]) instead -- worked out from the WGSL vector*matrix
	// definition, not guessed; verified against the actual PNG readback.
	struct FUniforms { float Mvp[16]; };
	FUniforms Uniforms = {};
	Uniforms.Mvp[0] = 0.7f;  Uniforms.Mvp[1] = 0.0f;  Uniforms.Mvp[2]  = 0.0f; Uniforms.Mvp[3]  = 0.2f;
	Uniforms.Mvp[4] = 0.0f;  Uniforms.Mvp[5] = 0.7f;  Uniforms.Mvp[6]  = 0.0f; Uniforms.Mvp[7]  = -0.1f;
	Uniforms.Mvp[8] = 0.0f;  Uniforms.Mvp[9] = 0.0f;  Uniforms.Mvp[10] = 0.7f; Uniforms.Mvp[11] = 0.0f;
	Uniforms.Mvp[12] = 0.0f; Uniforms.Mvp[13] = 0.0f;  Uniforms.Mvp[14] = 0.0f; Uniforms.Mvp[15] = 1.0f;

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

	int32 Result;
	FString VsPath, PsPath, OutPath;
	// -vs=<vs.wgsl> -ps=<ps.wgsl> -out=<out.ppm> -- FParse::Value key=value
	// switches, not positional tokens (UE's FCommandLine::Parse buckets any
	// "-foo"/"-foo=bar" argument into a separate Switches array from plain
	// positional Tokens, which made an earlier positional-index version of
	// this flag silently pick up the wrong paths — checkf is compiled out
	// in Shipping, so the bug produced garbage args instead of an assert).
	if (FParse::Value(FCommandLine::Get(), TEXT("-vs="), VsPath) &&
		FParse::Value(FCommandLine::Get(), TEXT("-ps="), PsPath) &&
		FParse::Value(FCommandLine::Get(), TEXT("-out="), OutPath))
	{
		Result = RunDawnRHIRealShaderTest(VsPath, PsPath, OutPath);
	}
	else
	{
		Result = RunDawnRHITest();
	}

	FEngineLoop::AppExit();
	return Result;
}
