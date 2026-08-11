// Copyright: DawnRHI project (itpick/ut4-webgpu).
//
// The real cook chain, promoted from the offline tools/hlsl_to_wgsl.cpp
// probe into a proper IShaderFormat backend. See DawnShaderCompiler.h,
// tools/dawn_tint_bridge.h, DawnTintBridgeLoader.h, and HANDOFF.md
// ("Shader-cook path") for the three walls this chain had to break
// through:
//   1. ShaderConductor SIGSEGV -> dlopen(RTLD_DEEPBIND) isolation
//      (FDawnShaderConductorLoader).
//   2. Vendored libtint.a ABI mismatch -> self-build Tint/SPIRV-Tools from
//      the exact pinned source revision.
//   3. Even the CORRECTLY self-built Tint/SPIRV-Tools, wrapped in a
//      plain-C-ABI static archive (dawn_tint_bridge) and linked directly
//      into a UE executable, still crashed ("bad_variant_access") --
//      proven NOT an ABI-layout issue (the identical bridge code on the
//      identical input succeeds as a bare standalone binary). Root cause:
//      static linking gives the whole process exactly one `operator new`/
//      `malloc` -- UE's own Mimalloc-backed override -- which every
//      allocation inside Tint's/SPIRV-Tools' statically-linked code goes
//      through too, corrupting state invisibly. Fixed the same way as
//      wall #1: build dawn_tint_bridge as ITS OWN self-contained .so
//      (static libc++, see tools/build_dawn_tint_thirdparty.sh) and
//      dlopen(RTLD_DEEPBIND) it at runtime (DawnTintBridgeLoader) instead
//      of a compile-time link -- RTLD_DEEPBIND makes it prefer its own
//      embedded allocator machinery over the host's override for calls
//      made from within it.
#include "DawnShaderCompiler.h"
#include "DawnShaderConductorLoader.h"
#include "DawnTintBridgeLoader.h"
#include "DawnShaderFormatDefinitions.h"

#include "ShaderCompilerCore.h"
#include "ShaderPreprocessTypes.h"
#include "ShaderCore.h"

void CompileDawnShader(
	const FShaderCompilerInput& Input,
	const FShaderPreprocessOutput& PreprocessOutput,
	FShaderCompilerOutput& Output)
{
	FDawnShaderConductorLoader& ScLoader = GetDawnShaderConductorLoader();
	FString ScLoaderError;
	if (!ScLoader.IsInitialized() && !ScLoader.Init(ScLoaderError))
	{
		Output.bSucceeded = false;
		Output.Errors.Add(FShaderCompilerError(*FString::Printf(TEXT("DawnShaderFormat: ShaderConductor loader init failed: %s"), *ScLoaderError)));
		return;
	}

	FDawnTintBridgeLoader& TintLoader = GetDawnTintBridgeLoader();
	FString TintLoaderError;
	if (!TintLoader.IsInitialized() && !TintLoader.Init(TintLoaderError))
	{
		Output.bSucceeded = false;
		Output.Errors.Add(FShaderCompilerError(*FString::Printf(TEXT("DawnShaderFormat: Tint bridge loader init failed: %s"), *TintLoaderError)));
		return;
	}

	ShaderConductor::ShaderStage Stage;
	switch (Input.Target.Frequency)
	{
	case SF_Vertex:  Stage = ShaderConductor::ShaderStage::VertexShader; break;
	case SF_Pixel:   Stage = ShaderConductor::ShaderStage::PixelShader;  break;
	default:
		Output.bSucceeded = false;
		Output.Errors.Add(FShaderCompilerError(*FString::Printf(
			TEXT("DawnShaderFormat: shader frequency %d not yet supported by the cook chain (only SF_Vertex/SF_Pixel today — compute/geometry/raytracing are tracked future work, see HANDOFF.md)"),
			(int32)Input.Target.Frequency)));
		return;
	}

	// Preprocessed source is what FBaseShaderFormat::PreprocessShader
	// produced from Input.VirtualSourceFilePath + Input.Environment — real
	// UE #include/permutation-define expansion, identical to what every
	// other IShaderFormat backend (Vulkan/Metal/D3D) receives here.
	const FString PreprocessedSourceStr(PreprocessOutput.GetSourceViewWide());
	const FTCHARToUTF8 SourceUtf8(*PreprocessedSourceStr);
	const FTCHARToUTF8 EntryUtf8(*Input.EntryPointName);
	const FTCHARToUTF8 NameUtf8(*Input.GetSourceFilename());

	TArray<uint32> Spirv;
	FString ScError;
	if (!ScLoader.CompileHlslToSpirv(SourceUtf8.Get(), NameUtf8.Get(), EntryUtf8.Get(), Stage, /*bDisableOptimizations=*/true, Spirv, ScError))
	{
		Output.bSucceeded = false;
		Output.Errors.Add(FShaderCompilerError(*FString::Printf(TEXT("DawnShaderFormat: ShaderConductor HLSL->SPIR-V failed: %s"), *ScError)));
		return;
	}

	// Everything downstream of raw SPIR-V (legalize+strip-reflect,
	// SPIR-V->WGSL via Tint, binding reflection) happens inside the
	// dlopen(RTLD_DEEPBIND)-isolated bridge .so — see file header comment.
	FDawnTintCookResult BridgeResult = TintLoader.LegalizeAndCookSpirvToWgsl(Spirv.GetData(), static_cast<uint32>(Spirv.Num()));

	auto Utf8BufToFString = [](const char* Buf, uint32 Len) -> FString
	{
		if (!Buf || Len == 0) { return FString(); }
		FUTF8ToTCHAR Converter(Buf, Len);
		return FString::ConstructFromPtrSize(Converter.Get(), Converter.Length());
	};

	if (!BridgeResult.Success)
	{
		const FString Diagnostic = BridgeResult.Diagnostic
			? Utf8BufToFString(BridgeResult.Diagnostic, BridgeResult.DiagnosticLen)
			: FString(TEXT("(no diagnostic)"));
		Output.bSucceeded = false;
		Output.Errors.Add(FShaderCompilerError(*FString::Printf(TEXT("DawnShaderFormat: %s"), *Diagnostic)));
		TintLoader.FreeResult(&BridgeResult);
		return;
	}

	// Our own container: plain UTF-8 WGSL text, exactly what
	// FDawnDynamicRHI::RHICreateVertexShader/RHICreatePixelShader already
	// hand to wgpuDeviceCreateShaderModule (see DawnRHI/Private/
	// DawnDynamicRHI.cpp) — no SimplyStream header/bytecode contract
	// anywhere in this format.
	TArray<uint8>& Code = Output.ShaderCode.GetWriteAccess();
	Code.Append(reinterpret_cast<const uint8*>(BridgeResult.Wgsl), BridgeResult.WgslLen);

	const FString ReflectionSummary = Utf8BufToFString(BridgeResult.Diagnostic, BridgeResult.DiagnosticLen);

	// Milestone step 1: populate FShaderCompilerOutput::ParameterMap with the
	// REAL reflected bindings the Tint bridge just read off the actual
	// SPIR-V module (see tools/dawn_tint_bridge.cpp's ReflectBindings) —
	// same real, exported RenderCore API every other IShaderFormat backend
	// (Vulkan's SpirVShaderCompiler.inl, D3D, Metal) uses to report its real
	// resource bindings, populated with real {set,binding} numbers instead
	// of this module's old hardcoded @group(0){0,1,2} assumption. BufferIndex
	// carries the real WGSL @group number, BaseIndex the real @binding
	// number — this is exactly what DawnRHI needs to build a reflection-driven
	// bind group layout instead of trusting a fixed 3-slot table (see
	// DawnResources.h/DawnDynamicRHI.cpp's FDawnVertexShader/FDawnPixelShader
	// ::Bindings, populated from this same reflected data by DawnCookProbeMain.cpp
	// / any future real shader-map consumer).
	for (uint32 i = 0; i < BridgeResult.NumBindings; ++i)
	{
		const FDawnReflectedBinding& B = BridgeResult.Bindings[i];
		if (B.Name[0] == '\0')
		{
			continue; // unnamed/unreflected — nothing a ParameterMap lookup could ever ask for by name
		}
		EShaderParameterType ParamType;
		switch (static_cast<EDawnReflectedBindingKind>(B.Kind))
		{
		case DawnBindingKind_UniformBuffer: ParamType = EShaderParameterType::UniformBuffer; break;
		case DawnBindingKind_Texture:       ParamType = EShaderParameterType::SRV; break;
		case DawnBindingKind_Sampler:       ParamType = EShaderParameterType::Sampler; break;
		default: continue; // unclassified resource kind — do not fabricate a type
		}
		Output.ParameterMap.AddParameterAllocation(ANSI_TO_TCHAR(B.Name), (uint16)B.Set, (uint16)B.Binding, 1, ParamType);
	}

	Output.Target = Input.Target;
	Output.bSucceeded = true;
	Output.GenerateOutputHash();

	UE_LOG(LogShaders, Display, TEXT("DawnShaderFormat: cooked %s|%s -> %u bytes WGSL, %u reflected binding(s). %s"),
		*Input.VirtualSourceFilePath, *Input.EntryPointName, BridgeResult.WgslLen, BridgeResult.NumBindings, *ReflectionSummary);

	TintLoader.FreeResult(&BridgeResult);
}
