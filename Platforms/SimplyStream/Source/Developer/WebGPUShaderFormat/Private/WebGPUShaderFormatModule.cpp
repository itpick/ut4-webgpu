// Clean-room open WebGPUShaderFormat module (itpick/ut4-webgpu).
//
// Fills the empty closed WebGPUShaderFormat stub with our own IShaderFormat,
// reusing DawnShaderFormat's CompileDawnShader (HLSL -> ShaderConductor ->
// SPIRV-Tools -> Tint -> WGSL). SimplyStream's cook LoadModuleChecked's this
// module *by name*, so it must exist under this exact name; the heavy compile
// work lives in DawnShaderFormat.so (RTLD_DEEPBIND isolation intact). No closed
// source referenced or copied (none exists on this tree).
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IShaderFormat.h"
#include "Interfaces/IShaderFormatModule.h"
#include "ShaderCompilerCommon.h"
#include "ShaderCompilerCore.h"
#include "DawnShaderCompiler.h" // CompileDawnShader (exported from DawnShaderFormat)
#include "Misc/Paths.h"
#include "ShaderCore.h" // AddShaderSourceDirectoryMapping

static const FGuid UE_SHADER_WEBGPU_VER = FGuid("8D4F2A17-6C3E-4B9A-A1D8-5E7B0C9F3A26");

class FShaderFormatWebGPU : public UE::ShaderCompilerCommon::FBaseShaderFormat
{
public:
	virtual uint32 GetVersion(FName Format) const override
	{
		return GetTypeHash(UE_SHADER_WEBGPU_VER);
	}
	virtual void GetSupportedFormats(TArray<FName>& OutFormats) const override
	{
		static const FName NAME_SF_WEBGPU_SM5(TEXT("SF_WEBGPU_SM5"));
		static const FName NAME_SF_WEBGPU_ES31(TEXT("SF_WEBGPU_ES31"));
		OutFormats.Add(NAME_SF_WEBGPU_SM5);
		OutFormats.Add(NAME_SF_WEBGPU_ES31);
	}
	virtual const TCHAR* GetPlatformIncludeDirectory() const override
	{
		return TEXT("Dawn");
	}

	// Platform.ush derives FEATURE_LEVEL from a *_PROFILE define that each shader
	// format is responsible for injecting (Vulkan does this via
	// SpirvShaderCompiler::ModifyCompilerInput). Without it every preprocess dies at
	// Platform.ush's "#error FEATURE_LEVEL has not been defined for this platform"
	// (first seen live on FInterpolateGroomGuidesCS during the 2026-08-11 real cook).
	//
	// Per our PlatformCommon.ush prelude contract (Engine/Platforms/SimplyStream/
	// Shaders/Public/PlatformCommon.ush): the WebGPU path sets NO compiler-family
	// define (COMPILER_HLSL pulls /Engine/Public/Platform/D3D/D3DCommon.ush, which
	// FShaderHashCache rejects for our format - verified live in cook11), only
	// COMPILER_HLSLCC + COMPILER_WEBGPU, and OVERRIDE_PLATFORMCOMMON_USH makes
	// Platform.ush include our prelude as the stand-in per-compiler common header
	// (resolved via the /Platform/Dawn mapping registered in StartupModule below).
	virtual void ModifyShaderCompilerInput(FShaderCompilerInput& Input) const override
	{
		static const FName NAME_SF_WEBGPU_ES31_Mod(TEXT("SF_WEBGPU_ES31"));
		Input.Environment.SetDefine(TEXT("COMPILER_HLSLCC"), 1);
		Input.Environment.SetDefine(TEXT("COMPILER_WEBGPU"), 1);
		Input.Environment.SetDefine(TEXT("OVERRIDE_PLATFORMCOMMON_USH"), 1);
		if (Input.ShaderFormat == NAME_SF_WEBGPU_ES31_Mod)
		{
			Input.Environment.SetDefine(TEXT("ES3_1_PROFILE"), 1);
		}
		else
		{
			Input.Environment.SetDefine(TEXT("SM5_PROFILE"), 1);
		}
	}
	virtual void CompilePreprocessedShader(
		const FShaderCompilerInput& Input,
		const FShaderPreprocessOutput& PreprocessOutput,
		FShaderCompilerOutput& Output) const override
	{
		CompileDawnShader(Input, PreprocessOutput, Output);
	}
};

static IShaderFormat* GWebGPUShaderFormatSingleton = nullptr;

class FWebGPUShaderFormatModule : public IShaderFormatModule
{
public:
	virtual void StartupModule() override
	{
		// Both our formats report GetPlatformIncludeDirectory() == "Dawn", so
		// "/Platform/Public/PlatformCommon.ush" (included by Platform.ush when
		// OVERRIDE_PLATFORMCOMMON_USH is set) rewrites to
		// "/Platform/Dawn/Public/PlatformCommon.ush" - but only if a
		// "/Platform/Dawn" source-directory mapping exists
		// (ShaderCore.cpp::ReplaceVirtualFilePathForShaderPlatform). Nothing
		// registers mappings for shader formats automatically, so do it here,
		// pointing at the SimplyStream platform-extension Shaders dir where our
		// prelude lives.
		const FString ShadersDir = FPaths::Combine(FPaths::EngineDir(), TEXT("Platforms"), TEXT("SimplyStream"), TEXT("Shaders"));
		if (FPaths::DirectoryExists(ShadersDir))
		{
			AddShaderSourceDirectoryMapping(TEXT("/Platform/Dawn"), ShadersDir);
		}
	}

	virtual ~FWebGPUShaderFormatModule() override
	{
		delete GWebGPUShaderFormatSingleton;
		GWebGPUShaderFormatSingleton = nullptr;
	}
	virtual IShaderFormat* GetShaderFormat() override
	{
		if (!GWebGPUShaderFormatSingleton)
		{
			GWebGPUShaderFormatSingleton = new FShaderFormatWebGPU();
		}
		return GWebGPUShaderFormatSingleton;
	}
};

IMPLEMENT_MODULE(FWebGPUShaderFormatModule, WebGPUShaderFormat);
