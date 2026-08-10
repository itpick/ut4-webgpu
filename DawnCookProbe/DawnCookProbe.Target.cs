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
public class DawnCookProbeTarget : TargetRules
{
	public DawnCookProbeTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Program;
		LinkType = TargetLinkType.Monolithic;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		DefaultBuildSettings = BuildSettingsVersion.Latest;

		LaunchModuleName = "DawnCookProbe";

		// bCompileAgainstEngine/CoreUObject: turned ON (session "update 3")
		// purely to statically link Engine/Source/Runtime/Engine/Private/
		// SceneView.cpp into this monolithic Program, so its
		// IMPLEMENT_STATIC_AND_SHADER_UNIFORM_BUFFER_STRUCT(FViewUniformShaderParameters,
		// "View", ...) global runs at process startup (ordinary C++ static
		// init — no FModuleManager/engine-init ceremony needed) and
		// registers "View" into RenderCore's FShaderParametersMetadata
		// name->struct map. That map is exactly what UE's real shader
		// compiler consults to auto-generate the `cbuffer View { ... }`
		// declaration text every real shader gets — see -dumpubdecl mode
		// in Main.cpp. bBuildDeveloperTools stays FALSE below, so
		// UEBuildSimplyStream.cs's ModifyModuleRulesForOtherPlatform hook
		// does NOT add "WebGPUShaderFormat" to our TargetPlatform
		// dependency (that injection is gated on
		// `bBuildShaderFormats || Target.bBuildDeveloperTools`) — this
		// target still never touches/loads WebGPUShaderFormat, same as
		// before.
		bCompileAgainstEngine = true;
		bCompileAgainstCoreUObject = true;
		// UE::ShaderParameters::CreateUniformBufferShaderDeclaration (RenderCore/
		// Public/ShaderParameters.h) -- the real function -dumpubdecl mode needs --
		// is itself gated `#if WITH_EDITOR`. UEBuildTarget.cs computes WITH_EDITOR=1
		// for a Program target iff bCompileAgainstEditor is set (Type==Program is
		// one of the two allowed types for that combo) -- this is the documented,
		// non-hacky way to get WITH_EDITOR=1 in a Program without becoming a full
		// TargetType.Editor target.
		bCompileAgainstEditor = true;
		bCompileAgainstApplicationCore = true; // RHI-adjacent modules pull this in transitively
		// bBuildWithEditorOnlyData was previously true here for shader
		// debug-dump metadata; with Engine now linked in (see above) that
		// combination (WITH_EDITORONLY_DATA=1, WITH_EDITOR=0 — Programs
		// aren't TargetType.Editor) breaks compilation across unrelated
		// Engine headers (SkeletalMesh.h/StaticMesh.h/AnimBlueprint.h
		// declare WITH_EDITORONLY_DATA-gated overrides of WITH_EDITOR-gated
		// base virtuals like UObject::PostEditChangeProperty — a
		// combination Engine's headers assume never happens, since real
		// targets are always WITH_EDITOR==WITH_EDITORONLY_DATA). Turned off;
		// we don't use shader debug-dump in this test.
		// Re-enabled (session update 4): now that bCompileAgainstEditor=true gives
		// us a real WITH_EDITOR=1, the WITH_EDITORONLY_DATA=1/WITH_EDITOR=0 mismatch
		// that broke SkeletalMesh.h/StaticMesh.h/AnimBlueprint.h (see removed
		// comment below) no longer applies -- WITH_EDITOR now equals
		// WITH_EDITORONLY_DATA again (both 1), the combination Engine headers
		// actually assume. Leaving this false while WITH_EDITOR=1 broke a
		// DIFFERENT, equally-real set of headers instead (Core/Serialization/
		// MemoryImage.cpp: undeclared identifier GetDebugString).
		bBuildWithEditorOnlyData = true;
		bCompileICU = false;
		bIsBuildingConsoleApplication = true;
		bUseLoggingInShipping = true;
		// bBuildDeveloperTools now ON: Launch.Build.cs unconditionally adds
		// "AutomationController"/"AutomationTest"/"ProfileVisualizer" to
		// PrivateIncludePathModuleNames whenever bCompileAgainstEngine=true
		// (LaunchEngineLoop.cpp's #include "IAutomationControllerModule.h"
		// under #if !UE_BUILD_SHIPPING) — those are Developer-category
		// modules UBT won't resolve at all unless bBuildDeveloperTools=true.
		// This is the "normal" combination (every real Engine-linking
		// target ships with DeveloperTools on); our earlier assumption that
		// Engine-linking + DeveloperTools-off was a safe combination was
		// wrong (confirmed by this and other transitively-broken Developer
		// modules). Getting AutomationController et al. to compile needs it.
		//
		// bBuildRequiresCookedData now explicitly forced true (normally
		// false for TargetType.Program) as the SURGICAL replacement for
		// bBuildDeveloperTools=false's previous job of keeping
		// WebGPUShaderFormat out of our dependency graph:
		// UEBuildSimplyStream.cs's ModifyModuleRulesForOtherPlatform only
		// adds "WebGPUShaderFormat" to the TargetPlatform module (and
		// SimplyStreamTargetPlatform* to Engine) inside a block gated on
		// `!Target.bBuildRequiresCookedData` — forcing it true skips that
		// whole block regardless of bBuildDeveloperTools, so
		// WebGPUShaderFormat still never becomes part of THIS target's own
		// module manifest (confirmed by reading UEBuildSimplyStream.cs
		// directly — not guessed). We never touch/link/load
		// WebGPUShaderFormat itself; this only prevents its *unrelated*
		// auto-injection into a module (TargetPlatform) we happen to also
		// depend on for other reasons.
		bBuildDeveloperTools = true;
		bBuildRequiresCookedData = true;
		// bBuildTargetDeveloperTools defaults to bBuildDeveloperTools (true), and
		// once bCompileAgainstEditor pulled in UnrealEd, UnrealEd.Build.cs its own
		// generic `if (Target.bBuildTargetDeveloperTools)` block enumerates and
		// links EVERY registered platforms TargetPlatform module -- including
		// SimplyStreamTargetPlatform (Engine/Platforms/SimplyStream/Source/
		// Developer/SimplyStreamTargetPlatform/, which is broken to compile
		// standalone here: SimplyStreamBulkDataCookedIndex.cpp cant find
		// Engine/Texture.h). This is a SEPARATE mechanism from
		// UEBuildSimplyStream.cs ModifyModuleRulesForOtherPlatform (which we
		// already routed around via bBuildRequiresCookedData) -- we dont cook
		// or need ANY per-platform target-platform module, so turn this whole
		// category off explicitly rather than debug SimplyStreamTargetPlatforms
		// own include-path bug (not our code, not on the forbidden-modules list,
		// but out of scope for a shader-declaration-dumping tool).
		bBuildTargetDeveloperTools = false;
	}
}
