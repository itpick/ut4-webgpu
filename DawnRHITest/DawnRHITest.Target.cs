// Minimal standalone console program that loads the DawnRHI module directly
// and drives it through IDynamicRHI/IRHICommandContext to render a triangle
// offscreen and read it back to a PPM — the Stage 1 acceptance test.
// Deliberately does NOT link Engine/CoreUObject/Slate: this exercises the
// RHI module in isolation rather than via a full engine boot.
//
// Also the wasm acceptance target for the DawnRHI-to-wasm join: when built
// for UnrealTargetPlatform.SimplyStream, this proves the REAL DawnRHI UE
// module (not a hand-rolled standalone harness like DawnRHIWasmProbe) goes
// through UBT + the emscripten toolchain to a real .wasm — closing the
// harness-vs-module gap DawnRHIWasmProbe's real_shader_wasm.cpp left open.
// Never links/references SimplyStream's closed WebGPURHI/WebGPUShaderFormat
// — only stock upstream emscripten's own emdawnwebgpu port.
using UnrealBuildTool;

[SupportedPlatforms("Linux", "SimplyStream")]
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

		if (Target.Platform == UnrealTargetPlatform.SimplyStream)
		{
			// Pull in emscripten's own stock emdawnwebgpu port (MIT/BSD-3,
			// upstream Emscripten project — NOT SimplyStream's vendored
			// Source/ThirdParty/emdawn, and NOT their closed WebGPURHI).
			// This supplies its own webgpu.h + JS glue that matches the
			// port's exact Dawn/Tint revision; DawnRHI.Build.cs deliberately
			// does NOT add the vendored native Dawn/include path for this
			// platform to avoid mixing two different webgpu.h ABIs.
			//
			// -sASYNCIFY=0 / -sEXIT_RUNTIME=0 / -sALLOW_MEMORY_GROWTH=1 match
			// the flags DawnRHIWasmProbe's standalone harness (real_shader_wasm.cpp)
			// already proved working end-to-end in a real browser. Unverified
			// delta vs. that probe: the probe built plain wasm32; this target
			// builds under the platform's default EmscriptenMemoryMode
			// (Memory64_Emulated, -sMEMORY64=2) since that's forced
			// platform-wide by SimplyStreamToolChain — whether the
			// emdawnwebgpu port's prebuilt variant is compatible with
			// MEMORY64=2 is exactly the open question this target exists to
			// answer.
			// --use-port must appear on both the compile line (so the port's
			// headers are on the include path) and the link line (so its
			// generated static lib + JS glue get linked in). The `-s...`
			// settings below are link-only options — emcc's compile-mode
			// -Werror rejects them as "linker setting ignored during
			// compilation" if they land on the compile line too (found by
			// running this build: they did, on the first attempt).
			AdditionalCompilerArguments = (AdditionalCompilerArguments ?? "") + " --use-port=emdawnwebgpu";
			AdditionalLinkerArguments = (AdditionalLinkerArguments ?? "") + " --use-port=emdawnwebgpu -sASYNCIFY=0 -sEXIT_RUNTIME=0 -sALLOW_MEMORY_GROWTH=1";
		}
	}
}
