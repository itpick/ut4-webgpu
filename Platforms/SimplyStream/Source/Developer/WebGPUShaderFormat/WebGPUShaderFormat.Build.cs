// Clean-room open WebGPUShaderFormat (itpick/ut4-webgpu).
//
// The closed WebGPUShaderFormat is an EMPTY source stub on this tree (only a
// .Build.cs, no Private/, no source) wired against closed WebGPURHI / wgsl_pack
// / WndrZSTD / hlslcc. SimplyStream's cook hard-loads a module *named*
// WebGPUShaderFormat (UEBuildSimplyStream.cs LoadModuleChecked), and UE binds
// modules by name, so DawnShaderFormat cannot satisfy it. This fills the empty
// stub with OUR implementation, reusing DawnShaderFormat's CompileDawnShader.
// NO closed dependencies. No closed source referenced or copied (none exists).
using System.IO;
using UnrealBuildTool;

[SupportedPlatformGroups("Linux")]
public class WebGPUShaderFormat : ModuleRules
{
	public WebGPUShaderFormat(ReadOnlyTargetRules Target) : base(Target)
	{
		BinariesSubFolder = "SimplyStream";
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
				"DawnShaderFormat", // reuse our open HLSL->WGSL compiler (CompileDawnShader)
			}
		);
		// CompileDawnShader is declared in DawnShaderFormat's Private header and
		// exported via DAWNSHADERFORMAT_API; add that Private dir to our includes.
		PrivateIncludePaths.Add(Path.Combine(EngineDirectory, "Source", "Developer", "DawnShaderFormat", "Private"));

		if (!Target.IsInPlatformGroup(UnrealPlatformGroup.Linux))
		{
			PrecompileForTargets = PrecompileTargetsType.None;
		}
	}
}
