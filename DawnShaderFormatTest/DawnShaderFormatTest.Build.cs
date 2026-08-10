using UnrealBuildTool;

public class DawnShaderFormatTest : ModuleRules
{
	public DawnShaderFormatTest(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePathModuleNames.Add("Launch");

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"ApplicationCore",
				"Projects",
				"RenderCore",
				"RHI",
				"TargetPlatform",
				"ShaderCompilerCommon",
				"TraceLog",
			}
		);

		// DawnShaderFormat is a Developer module loaded dynamically at
		// runtime via FModuleManager (mirrors how ShaderCompileWorker loads
		// Vulkan/Metal/D3D's format modules — see DawnShaderFormatTest.Target.cs).
		// Naming it here just ensures UBT builds it as a dependency/build
		// product of this target.
		DynamicallyLoadedModuleNames.Add("DawnShaderFormat");
	}
}
