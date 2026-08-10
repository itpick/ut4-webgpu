// Standalone acceptance test for the DawnShaderFormat IShaderFormat module:
// loads it exactly the way UE's real shader-compiling pipeline does
// (FModuleManager -> IShaderFormatModule::GetShaderFormat() ->
// IShaderFormat::CompilePreprocessedShader(), the same entry point
// RenderCore's ShaderCore.cpp InvokeCompile() calls, whether run in-process
// or via ShaderCompileWorker), fed a REAL UE-preprocessed shader source
// (see Main.cpp for how it's obtained). Modeled on
// Engine/Source/Programs/ShaderCompileWorker's own target settings (that
// program also runs bCompileAgainstCoreUObject=false,
// bBuildDeveloperTools=false and still dynamically loads IShaderFormat
// Developer modules at runtime — the module *type* doesn't need to be in
// this target's own compiled scope, only DynamicallyLoadedModuleNames
// needs to name it so UBT builds it as a build product).
using UnrealBuildTool;

[SupportedPlatformGroups("Linux")]
public class DawnShaderFormatTestTarget : TargetRules
{
	public DawnShaderFormatTestTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Program;
		LinkType = TargetLinkType.Monolithic;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		DefaultBuildSettings = BuildSettingsVersion.Latest;

		LaunchModuleName = "DawnShaderFormatTest";

		bCompileAgainstEngine = false;
		bCompileAgainstCoreUObject = false;
		bCompileAgainstApplicationCore = true; // RHI-adjacent modules pull this in transitively
		bBuildWithEditorOnlyData = true;       // shader debug-dump metadata / DumpDebugInfoPath handling wants this
		bCompileICU = false;
		bIsBuildingConsoleApplication = true;
		bUseLoggingInShipping = true;
		bBuildDeveloperTools = false; // DawnShaderFormat is loaded dynamically at runtime, not statically linked in
	}
}
