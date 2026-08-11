// Copyright: DawnRHI project (itpick/ut4-webgpu).
//
// Real UE IShaderFormat module: HLSL -> our own WGSL text container, via
// Epic's open ShaderConductor + Google's open SPIRV-Tools + Google's open
// Tint (see DawnShaderCompiler.cpp). Modeled directly on
// Engine/Source/Developer/VulkanShaderFormat/Private/VulkanShaderFormat.cpp.
//
// Named "DawnShaderFormat" so FTargetPlatformManagerModule's generic
// "*ShaderFormat*" wildcard module scan (IShaderFormat.h's
// SHADERFORMAT_MODULE_WILDCARD, consumed in
// TargetPlatformManagerModule.cpp) discovers and loads this module the
// same way it discovers VulkanShaderFormat/MetalShaderFormat/etc. — no
// bespoke engine-side wiring needed for basic discovery. Actually being
// *requested* by a real ITargetPlatform::GetAllTargetedShaderFormats() is
// separate future work (a Dawn/WebGPU target platform definition) — see
// HANDOFF.md; today this format is driven directly through the real
// IShaderFormat/IShaderFormatModule interfaces (see DawnShaderFormatTest).
#include "Modules/ModuleInterface.h"
#include "Modules/ModuleManager.h"
#include "Interfaces/IShaderFormat.h"
#include "Interfaces/IShaderFormatModule.h"
#include "ShaderCompilerCommon.h"
#include "ShaderCompilerCore.h"

#include "DawnShaderCompiler.h"
#include "DawnShaderFormatDefinitions.h"

// Bumped whenever the cook chain's behaviour changes in a way that should
// invalidate cached DDC shader jobs for this format (mirrors Vulkan's
// GetVersion() pattern combining a format GUID with a third-party version
// hash — we don't yet track a ShaderConductor/Tint version hash the way
// Vulkan's FShaderConductorModuleWrapper does, so this is a plain literal
// bumped by hand; tracked as future work alongside real DDC integration).
static const FGuid UE_SHADER_DAWN_WGSL_VER = FGuid("A1D9E6B2-8C3F-4E7A-9B1D-2F6C4A8E0D31");

class FShaderFormatDawn : public UE::ShaderCompilerCommon::FBaseShaderFormat
{
public:
	virtual uint32 GetVersion(FName Format) const override
	{
		return GetTypeHash(UE_SHADER_DAWN_WGSL_VER);
	}

	virtual void GetSupportedFormats(TArray<FName>& OutFormats) const override
	{
		// Our own native format name.
		OutFormats.Add(GetDawnWgslShaderFormatName());
		// Also claim the SimplyStream WebGPU shader-platform format names
		// (SP_WEBGPU_SM5/ES31 -> SF_WEBGPU_SM5/ES31 in that platform's
		// DataDrivenPlatformInfo.ini) so a REAL cook for the SimplyStream
		// target selects THIS open module. The closed WebGPUShaderFormat is
		// a non-functional source stub on this tree, so there is no conflict.
		// The compile path keys on shader frequency, not format name, so the
		// HLSL->SPIR-V->WGSL pipeline is identical regardless of requester.
		static const FName NAME_SF_WEBGPU_SM5(TEXT("SF_WEBGPU_SM5"));
		static const FName NAME_SF_WEBGPU_ES31(TEXT("SF_WEBGPU_ES31"));
		OutFormats.Add(NAME_SF_WEBGPU_SM5);
		OutFormats.Add(NAME_SF_WEBGPU_ES31);
	}

	virtual const TCHAR* GetPlatformIncludeDirectory() const override
	{
		return TEXT("Dawn");
	}

	virtual void CompilePreprocessedShader(
		const FShaderCompilerInput& Input,
		const FShaderPreprocessOutput& PreprocessOutput,
		FShaderCompilerOutput& Output) const override
	{
		CompileDawnShader(Input, PreprocessOutput, Output);
	}
};

static IShaderFormat* GDawnShaderFormatSingleton = nullptr;

class FDawnShaderFormatModule : public IShaderFormatModule
{
public:
	virtual ~FDawnShaderFormatModule() override
	{
		delete GDawnShaderFormatSingleton;
		GDawnShaderFormatSingleton = nullptr;
	}

	virtual IShaderFormat* GetShaderFormat() override
	{
		if (!GDawnShaderFormatSingleton)
		{
			GDawnShaderFormatSingleton = new FShaderFormatDawn();
		}
		return GDawnShaderFormatSingleton;
	}
};

IMPLEMENT_MODULE(FDawnShaderFormatModule, DawnShaderFormat);
