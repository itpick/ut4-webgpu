// DawnShaderFormat — a real UE IShaderFormat Developer module: HLSL (as
// preprocessed by UE's own shared shader preprocessor) -> SPIR-V (Epic's
// open ShaderConductor, dlopen/RTLD_DEEPBIND isolated per DawnRHITest's
// fix) -> legalized/reflect-stripped SPIR-V (Google's open SPIRV-Tools
// Optimizer, self-built) -> WGSL (Google's open Tint IR reader/writer,
// self-built, dlopen/RTLD_DEEPBIND isolated too — see
// DawnTintBridgeLoader.h) -> our own plain-UTF8-WGSL-text container (what
// FDawnDynamicRHI::RHICreateVertexShader/RHICreatePixelShader already
// consume).
//
// Modeled on Engine/Source/Developer/VulkanShaderFormat. Named
// "DawnShaderFormat" so it matches IShaderFormat.h's
// SHADERFORMAT_MODULE_WILDCARD ("*ShaderFormat*") and is auto-discovered
// by FTargetPlatformManagerModule the same way Vulkan/Metal/D3D's format
// modules are — no bespoke wiring needed for the module to be found and
// loaded by UE's own shader-format discovery.
//
// NOT SimplyStream code: no reference to WebGPURHI/WebGPUShaderFormat
// anywhere in this module.
using System.IO;
using UnrealBuildTool;

[SupportedPlatformGroups("Linux")]
public class DawnShaderFormat : ModuleRules
{
	public DawnShaderFormat(ReadOnlyTargetRules Target) : base(Target)
	{
		IWYUSupport = IWYUSupport.None;

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject",
				"RenderCore",
				"RHI",
				"ShaderCompilerCommon",
				"ShaderPreprocessor",
				"TargetPlatform",
				"Projects",
			}
		);

		// Epic's own open ThirdParty HLSL->SPIR-V compiler (Engine/Source/
		// ThirdParty/ShaderConductor). Header-only here, exactly like
		// DawnRHITest — see DawnShaderConductorLoader.h for why this must
		// never be a compile-time link (libc++ symbol-interposition SIGSEGV,
		// root-caused and fixed in the DawnRHITest milestone; the fix is
		// dlopen(RTLD_DEEPBIND) at runtime instead).
		string SCIncludeDir = Path.Combine(Target.UEThirdPartySourceDirectory, "ShaderConductor", "ShaderConductor", "Include");
		PublicSystemIncludePaths.Add(SCIncludeDir);

		if (Target.IsInPlatformGroup(UnrealPlatformGroup.Linux))
		{
			// dawn_tint_bridge.h: the plain-C-ABI header shared with
			// libDawnTintBridge.so (see tools/dawn_tint_bridge.h/.cpp,
			// tools/build_dawn_tint_thirdparty.sh). This module only ever
			// includes this POD-only header — never Tint's/SPIRV-Tools' own
			// C++ headers, and never links their .a's directly. See
			// DawnTintBridgeLoader.h for the full "wall #3" writeup: even a
			// *correctly* self-built Tint, wrapped in a plain-C static
			// archive and linked directly into a UE binary, corrupted state
			// ("bad_variant_access") because static linking gives the whole
			// process exactly one operator-new/malloc (UE's Mimalloc-backed
			// override) that every Tint/SPIRV-Tools allocation then goes
			// through too. Fixed the same way wall #1 (ShaderConductor) was
			// fixed: libDawnTintBridge.so is its own fully self-contained
			// shared object (static libc++, zero external libc++.so/
			// libstdc++ dependency) loaded via dlopen(RTLD_DEEPBIND) at
			// runtime (DawnTintBridgeLoader.cpp), never a compile-time link.
			string DawnTintDir = Path.Combine(EngineDirectory, "Source", "ThirdParty", "DawnTint");
			string DawnTintIncludeDir = Path.Combine(DawnTintDir, "include");
			string DawnTintSoPath = Path.Combine(EngineDirectory, "Binaries", "ThirdParty", "DawnTint", "Linux", "libDawnTintBridge.so");

			if (!Directory.Exists(DawnTintIncludeDir) || !File.Exists(DawnTintSoPath))
			{
				throw new BuildException(
					"DawnShaderFormat: expected dawn_tint_bridge.h at " + DawnTintIncludeDir +
					" and libDawnTintBridge.so at " + DawnTintSoPath +
					" (run tools/build_dawn_tint_thirdparty.sh from itpick/ut4-webgpu to produce them).");
			}

			PublicSystemIncludePaths.Add(DawnTintIncludeDir);

			// Ship the .so as a runtime dependency (staged next to the
			// binary on package/cook) rather than a link-time dependency —
			// mirrors how ShaderConductor's own .so is header-only here and
			// loaded via dlopen (DawnShaderConductorLoader).
			RuntimeDependencies.Add(DawnTintSoPath);

			PublicSystemLibraries.Add("dl");
			PublicSystemLibraries.Add("pthread");
		}
		else
		{
			PrecompileForTargets = PrecompileTargetsType.None;
		}
	}
}
