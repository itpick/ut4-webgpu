// Acceptance test for the DawnShaderFormat IShaderFormat module (milestone
// 1+2 of the "make the cook path REAL" task): loads DawnShaderFormat via
// FModuleManager exactly like UE's real shader-compiling pipeline does
// (RenderCore's ShaderCore.cpp InvokeCompile() calls
// IShaderFormat::CompilePreprocessedShader() on the module returned by
// IShaderFormatModule::GetShaderFormat() — see that file), then feeds it a
// REAL, already-UE-preprocessed HLSL source file (captured from an actual
// UE shader compile via r.DumpShaderDebugInfo=1 — see HANDOFF.md /
// tools/dump_ut4_shader.sh for how to produce one) and reports whether the
// cook chain (ShaderConductor -> SPIRV-Tools -> Tint) accepts it.
//
// We deliberately do NOT call IShaderFormat::PreprocessShader() ourselves
// here — the input file is UE's *own* preprocessor output, captured
// verbatim, so this test exercises exactly the second half of the real
// contract (CompilePreprocessedShader) against real content, without us
// having to stand up the game/material shader-type machinery that
// originally produced it.
//
// Usage: DawnShaderFormatTest <preprocessed.hlsl> <entryPoint> <vs|ps> <out.wgsl> [virtualSourcePath] [debugGroupName]
#include "CoreMinimal.h"
#include "RequiredProgramMainCPPInclude.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IShaderFormat.h"
#include "Interfaces/IShaderFormatModule.h"
#include "ShaderCompilerCore.h"
#include "ShaderPreprocessTypes.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"

IMPLEMENT_APPLICATION(DawnShaderFormatTest, "DawnShaderFormatTest");

DEFINE_LOG_CATEGORY_STATIC(LogDawnShaderFormatTest, Log, All);

int32 GuardedMain(int32 ArgC, TCHAR* ArgV[])
{
	// -real mode: genuine end-to-end test against an ACTUAL UE shader
	// source file (e.g. a real Engine/Shaders/Private/*.usf global shader,
	// or a real UT4 project shader), going through the REAL
	// IShaderFormat::PreprocessShader() (FBaseShaderFormat's shared
	// implementation -> UE's own shader preprocessor: #include resolution
	// via the virtual /Engine/... path mapping set up during
	// GEngineLoop.PreInit, environment defines, permutations) followed by
	// CompilePreprocessedShader() -- the full real contract, not just the
	// second half.
	//
	// Usage: DawnShaderFormatTest -real <virtualSourcePath.usf> <entryPoint> <vs|ps> <out.wgsl> [define=value ...]
	const bool bRealMode = ArgC > 1 && FString(ArgV[1]) == TEXT("-real");

	if (bRealMode)
	{
		if (ArgC < 6)
		{
			UE_LOG(LogDawnShaderFormatTest, Error, TEXT("usage: %s -real <virtualSourcePath.usf> <entryPoint> <vs|ps> <out.wgsl> [define=value ...]"), ArgV[0]);
			return 1;
		}
		const FString VirtualSourcePath = ArgV[2];
		const FString EntryPoint = ArgV[3];
		const FString StageStr = ArgV[4];
		const FString OutPath = ArgV[5];

		IShaderFormatModule* FormatModule = &FModuleManager::LoadModuleChecked<IShaderFormatModule>(TEXT("DawnShaderFormat"));
		IShaderFormat* Format = FormatModule->GetShaderFormat();
		checkf(Format, TEXT("DawnShaderFormat module returned a null IShaderFormat"));

		TArray<FName> SupportedFormats;
		Format->GetSupportedFormats(SupportedFormats);
		UE_LOG(LogDawnShaderFormatTest, Log, TEXT("DawnShaderFormat loaded OK. Supported formats:"));
		for (const FName& F : SupportedFormats)
		{
			UE_LOG(LogDawnShaderFormatTest, Log, TEXT("  %s (version %u)"), *F.ToString(), Format->GetVersion(F));
		}

		FShaderCompilerInput Input;
		Input.ShaderFormat = SupportedFormats.Num() > 0 ? SupportedFormats[0] : FName(TEXT("SF_DAWN_WGSL"));
		Input.VirtualSourceFilePath = VirtualSourcePath;
		Input.EntryPointName = EntryPoint;
		Input.DebugGroupName = TEXT("DawnShaderFormatTest-Real");
		Input.Target.Frequency = (StageStr == TEXT("vs")) ? SF_Vertex : SF_Pixel;
		// SP_NumPlatforms (the sentinel/invalid value) segfaults real
		// preprocessing code that indexes per-platform arrays
		// (FDataDrivenShaderPlatformInfo etc.) -- borrow a real, valid
		// platform enum purely so the shared UE preprocessor has a legal
		// index to work with. This does NOT change what shader FORMAT our
		// module claims/produces (Input.ShaderFormat, set above, is still
		// our own SF_DAWN_WGSL) -- see HANDOFF.md for why we don't yet own
		// a real registered EShaderPlatform value of our own.
		Input.Target.Platform = SP_VULKAN_SM5;

		FShaderCompilerEnvironment Environment;
		Environment.SetDefine(TEXT("COMPILER_HLSL"), 1);
		Environment.SetDefine(TEXT("PIXELSHADER"), StageStr == TEXT("ps") ? 1 : 0);
		Environment.SetDefine(TEXT("VERTEXSHADER"), StageStr == TEXT("vs") ? 1 : 0);
		for (int32 ArgIdx = 6; ArgIdx < ArgC; ++ArgIdx)
		{
			FString Arg = ArgV[ArgIdx];
			FString DefName, DefValue;
			if (Arg.Split(TEXT("="), &DefName, &DefValue))
			{
				Environment.SetDefine(*DefName, *DefValue);
				UE_LOG(LogDawnShaderFormatTest, Log, TEXT("  extra define: %s=%s"), *DefName, *DefValue);
			}
		}

		FShaderPreprocessOutput PreprocessOutput;
		const double PreprocessStart = FPlatformTime::Seconds();
		const bool bPreprocessOk = Format->PreprocessShader(Input, Environment, PreprocessOutput);
		const double PreprocessMs = (FPlatformTime::Seconds() - PreprocessStart) * 1000.0;

		if (!bPreprocessOk)
		{
			UE_LOG(LogDawnShaderFormatTest, Error, TEXT("PREPROCESS FAILED (%.1f ms). %d error(s):"), PreprocessMs, PreprocessOutput.GetErrors().Num());
			for (const FShaderCompilerError& Err : PreprocessOutput.GetErrors())
			{
				UE_LOG(LogDawnShaderFormatTest, Error, TEXT("  %s"), *Err.GetErrorString());
			}
			return 3;
		}
		UE_LOG(LogDawnShaderFormatTest, Log, TEXT("Preprocess OK (%.1f ms). %d preprocessed chars."), PreprocessMs, PreprocessOutput.GetSourceViewWide().Len());

		FShaderCompilerOutput Output;
		const double StartTime = FPlatformTime::Seconds();
		Format->CompilePreprocessedShader(Input, PreprocessOutput, Output);
		const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;

		if (!Output.bSucceeded)
		{
			UE_LOG(LogDawnShaderFormatTest, Error, TEXT("COMPILE FAILED (%.1f ms). %d error(s):"), ElapsedMs, Output.Errors.Num());
			for (const FShaderCompilerError& Err : Output.Errors)
			{
				UE_LOG(LogDawnShaderFormatTest, Error, TEXT("  %s"), *Err.GetErrorString());
			}
			return 2;
		}

		FShaderCodeReader Reader(Output.ShaderCode.GetReadView());
		TConstArrayView<uint8> Code = Reader.GetOffsetShaderCode(0);
		FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Code.GetData()), Code.Num());
		FString Wgsl = FString::ConstructFromPtrSize(Converter.Get(), Converter.Length());
		FFileHelper::SaveStringToFile(Wgsl, *OutPath);

		UE_LOG(LogDawnShaderFormatTest, Log, TEXT("SUCCESS (%.1f ms). %d bytes WGSL written to %s"), ElapsedMs, Code.Num(), *OutPath);
		return 0;
	}

	if (ArgC < 5)
	{
		UE_LOG(LogDawnShaderFormatTest, Error, TEXT("usage: %s <preprocessed.hlsl> <entryPoint> <vs|ps> <out.wgsl> [virtualSourcePath] [debugGroupName]"), ArgV[0]);
		return 1;
	}

	const FString InPath = ArgV[1];
	const FString EntryPoint = ArgV[2];
	const FString StageStr = ArgV[3];
	const FString OutPath = ArgV[4];
	const FString VirtualSourcePath = ArgC > 5 ? FString(ArgV[5]) : FString(TEXT("/Test/Unknown.usf"));
	const FString DebugGroupName = ArgC > 6 ? FString(ArgV[6]) : FString(TEXT("DawnShaderFormatTest"));

	FString PreprocessedSource;
	if (!FFileHelper::LoadFileToString(PreprocessedSource, *InPath))
	{
		UE_LOG(LogDawnShaderFormatTest, Error, TEXT("Failed to load %s"), *InPath);
		return 1;
	}
	UE_LOG(LogDawnShaderFormatTest, Log, TEXT("Loaded %d chars of preprocessed HLSL from %s"), PreprocessedSource.Len(), *InPath);

	// ---- Load DawnShaderFormat exactly the way UE's real shader-compiling
	// infra discovers/loads any IShaderFormat module. ----
	IShaderFormatModule* FormatModule = &FModuleManager::LoadModuleChecked<IShaderFormatModule>(TEXT("DawnShaderFormat"));
	IShaderFormat* Format = FormatModule->GetShaderFormat();
	checkf(Format, TEXT("DawnShaderFormat module returned a null IShaderFormat"));

	TArray<FName> SupportedFormats;
	Format->GetSupportedFormats(SupportedFormats);
	UE_LOG(LogDawnShaderFormatTest, Log, TEXT("DawnShaderFormat loaded OK. Supported formats:"));
	for (const FName& F : SupportedFormats)
	{
		UE_LOG(LogDawnShaderFormatTest, Log, TEXT("  %s (version %u)"), *F.ToString(), Format->GetVersion(F));
	}

	// ---- Build a real FShaderCompilerInput/FShaderPreprocessOutput pair. ----
	FShaderCompilerInput Input;
	Input.ShaderFormat = SupportedFormats.Num() > 0 ? SupportedFormats[0] : FName(TEXT("SF_DAWN_WGSL"));
	Input.VirtualSourceFilePath = VirtualSourcePath;
	Input.EntryPointName = EntryPoint;
	Input.DebugGroupName = DebugGroupName;
	Input.Target.Frequency = (StageStr == TEXT("vs")) ? SF_Vertex : SF_Pixel;
	Input.Target.Platform = SP_NumPlatforms; // no real ShaderPlatform enum value owned by us yet — see HANDOFF.md

	FShaderPreprocessOutput PreprocessOutput;
	{
		FTCHARToUTF8 Utf8Source(*PreprocessedSource);
		FAnsiStringView SourceView(reinterpret_cast<const ANSICHAR*>(Utf8Source.Get()), Utf8Source.Length());
		PreprocessOutput.EditSource() = FShaderSource(SourceView);
	}

	FShaderCompilerOutput Output;

	const double StartTime = FPlatformTime::Seconds();
	// The exact real dispatch: RenderCore/Private/ShaderCore.cpp's
	// InvokeCompile() calls Compiler->CompilePreprocessedShader(CompileInput,
	// Job.PreprocessOutput, Job.Output) — this is that same call.
	Format->CompilePreprocessedShader(Input, PreprocessOutput, Output);
	const double ElapsedMs = (FPlatformTime::Seconds() - StartTime) * 1000.0;

	if (!Output.bSucceeded)
	{
		UE_LOG(LogDawnShaderFormatTest, Error, TEXT("FAILED (%.1f ms). %d error(s):"), ElapsedMs, Output.Errors.Num());
		for (const FShaderCompilerError& Err : Output.Errors)
		{
			UE_LOG(LogDawnShaderFormatTest, Error, TEXT("  %s"), *Err.GetErrorString());
		}
		return 2;
	}

	// FShaderCode always appends a trailing optional-data-size marker on
	// finalize (see FShaderCode::FinalizeShaderCode/GetReadView) — even
	// when, like us, nothing ever called AddOptionalData. Strip it via
	// FShaderCodeReader (the same class every real consumer of
	// FShaderCompilerOutput::ShaderCode uses) rather than reading
	// GetReadView() raw, or the last 4 bytes of the saved .wgsl would be
	// binary garbage.
	FShaderCodeReader Reader(Output.ShaderCode.GetReadView());
	TConstArrayView<uint8> Code = Reader.GetOffsetShaderCode(0);
	FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(Code.GetData()), Code.Num());
	FString Wgsl = FString::ConstructFromPtrSize(Converter.Get(), Converter.Length());
	FFileHelper::SaveStringToFile(Wgsl, *OutPath);

	UE_LOG(LogDawnShaderFormatTest, Log, TEXT("SUCCESS (%.1f ms). %d bytes WGSL written to %s"), ElapsedMs, Code.Num(), *OutPath);
	return 0;
}

INT32_MAIN_INT32_ARGC_TCHAR_ARGV()
{
	GEngineLoop.PreInit(ArgC, ArgV, TEXT("-nocrashreports -nosplash"));
	int32 Result = GuardedMain(ArgC, ArgV);
	FEngineLoop::AppExit();
	return Result;
}
