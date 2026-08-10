using UnrealBuildTool;

public class DawnCookProbe : ModuleRules
{
	public DawnCookProbe(ReadOnlyTargetRules Target) : base(Target)
	{
		PublicIncludePathModuleNames.Add("Launch");
		// RequiredProgramMainCPPInclude.h text-includes LaunchEngineLoop.cpp directly
		// into THIS module (see that headers own comment: "highly sketchy, but we
		// need some stuff from launchengineloop.cpp"), so its own #includes (e.g.
		// DerivedDataCacheInterface.h, unconditional under #if WITH_ENGINE) resolve
		// against OUR modules include paths, not Launchs own -- mirror the same
		// condition Launch.Build.cs itself uses for a bCompileAgainstEngine +
		// !bBuildWithEditorOnlyData target (see Launch.Build.cs around "if
		// (Target.bBuildWithEditorOnlyData) ... else PrivateIncludePathModuleNames.Add(\"DerivedDataCache\");").
		PrivateIncludePathModuleNames.Add("DerivedDataCache");

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"CoreUObject", // needed by Engine (below); no UObject/UHT bootstrap is ever run, we just need SceneView.cpp's static registration
				"Engine",      // pulls in SceneView.cpp -> IMPLEMENT_STATIC_AND_SHADER_UNIFORM_BUFFER_STRUCT(FViewUniformShaderParameters, "View", ...) so FindUniformBufferStructByName(TEXT("View")) resolves for real (-dumpubdecl mode)
				"ApplicationCore",
				"Projects",
				"RenderCore",
				"RHI",
				"TargetPlatform",
				"ShaderCompilerCommon",
				"TraceLog",
				// Everything below this line exists ONLY because
				// RequiredProgramMainCPPInclude.h text-includes the real
				// LaunchEngineLoop.cpp into this module, and that file's
				// #if WITH_ENGINE / #if !UE_SERVER blocks (real for us: we set
				// bCompileAgainstEngine=true, Target.Type=Program != Server)
				// #include a wide swath of engine-runtime headers with real
				// symbol usage, not just types. This list is a direct mirror
				// of Launch.Build.cs's own PrivateDependencyModuleNames for
				// that exact condition set -- not new functionality we use,
				// just satisfying the same transitive closure Launch.Build.cs
				// itself declares for its own compile of this same .cpp text.
				"InputCore",
				"MoviePlayer",
				"MoviePlayerProxy",
				"Networking",
				"PakFile",
				"SandboxFile",
				"Serialization",
				"Slate",
				"SlateCore",
				"Sockets",
				"Overlay",
				"PreLoadScreen",
				"InstallBundleManager",
				"HeadMountedDisplay",
				"MediaUtils",
				"MRMesh",
			}
		);

		PrivateIncludePathModuleNames.AddRange(
			new string[]
			{
				"Media",
				"SlateNullRenderer",
				"SlateRHIRenderer",
			}
		);

		DynamicallyLoadedModuleNames.AddRange(
			new string[]
			{
				"Media",
				"SlateNullRenderer",
				"SlateRHIRenderer",
				"AudioMixerSDL",
				"AudioMixerPlatformAudioLink",
				"Renderer",
			}
		);

		// DawnShaderFormat is a Developer module loaded dynamically at
		// runtime via FModuleManager (mirrors how ShaderCompileWorker loads
		// Vulkan/Metal/D3D's format modules — see DawnCookProbe.Target.cs).
		// Naming it here just ensures UBT builds it as a dependency/build
		// product of this target.
		DynamicallyLoadedModuleNames.Add("DawnShaderFormat");
	}
}
