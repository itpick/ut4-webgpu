using UnrealBuildTool;

public class DawnRHITest : ModuleRules
{
	public DawnRHITest(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePathModuleNames.Add("Launch");

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"ApplicationCore", // required by RequiredProgramMainCPPInclude.h / LaunchEngineLoop.cpp
				"RHI",
				"RHICore",
				"Projects", // FModuleManager module loading
			}
		);

		// Force the DawnRHI module to build and be loadable even though
		// nothing statically links against its symbols (we load it
		// dynamically by name at runtime, the same way engine RHI selection
		// does for Vulkan/D3D/etc.).
		DynamicallyLoadedModuleNames.Add("DawnRHI");

		// Stage 2 shader-cook-path probe: Epic's own standard (open,
		// non-SimplyStream) ThirdParty HLSL->SPIR-V compiler, already
		// vendored under Engine/Source/ThirdParty/ShaderConductor with a
		// prebuilt Linux .so.
		AddEngineThirdPartyPrivateStaticDependencies(Target, "ShaderConductor");
	}
}
