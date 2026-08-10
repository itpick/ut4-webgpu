// Minimal standalone console program that loads the DawnRHI module directly
// and drives it through IDynamicRHI/IRHICommandContext to render a triangle
// offscreen and read it back to a PPM — the Stage 1 acceptance test.
// Deliberately does NOT link Engine/CoreUObject/Slate: this exercises the
// RHI module in isolation rather than via a full engine boot.
using UnrealBuildTool;

[SupportedPlatformGroups("Linux")]
public class DawnRHITestTarget : TargetRules
{
	public DawnRHITestTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Program;
		LinkType = TargetLinkType.Monolithic;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		DefaultBuildSettings = BuildSettingsVersion.Latest;

		LaunchModuleName = "DawnRHITest";

		bCompileAgainstEngine = false;
		bCompileAgainstCoreUObject = false;
		bCompileAgainstApplicationCore = true; // RHI.Build.cs requires this
		bBuildWithEditorOnlyData = false;
		bCompileICU = false;
		bIsBuildingConsoleApplication = true;
		bUseLoggingInShipping = true;
		bBuildDeveloperTools = false;
	}
}
