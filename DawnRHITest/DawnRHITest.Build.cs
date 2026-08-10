using System.IO;
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
		//
		// Deliberately HEADER-ONLY here — do NOT link libShaderConductor.so
		// at compile time (no AddEngineThirdPartyPrivateStaticDependencies,
		// no PublicAdditionalLibraries). Doing so makes it an ELF DT_NEEDED
		// dependency loaded into the same global symbol scope as this
		// (large, libc++-template-heavy) executable at process startup,
		// which reproducibly SIGSEGVs inside SPIRV-Tools' internal passes
		// -- root-caused to libc++ symbol interposition between
		// libShaderConductor.so's own statically-linked libc++ copy and
		// this executable's own statically-linked libc++ copy (both are
		// fully self-contained .so/binaries with ~900-1200 duplicate
		// default-visibility "std::__1::..." symbols and zero external
		// libc++.so/libstdc++.so dependency -- confirmed via readelf).
		// DawnShaderConductorLoader.h/.cpp instead dlopen()s the .so at
		// runtime with RTLD_DEEPBIND, which isolates its internal symbol
		// resolution from this executable's global scope and eliminates
		// the crash (verified via a minimal standalone repro AND via this
		// program's own -testshaderconductor smoke test).
		string SCIncludeDir = Path.Combine(Target.UEThirdPartySourceDirectory, "ShaderConductor", "ShaderConductor", "Include");
		PublicSystemIncludePaths.Add(SCIncludeDir);
	}
}
