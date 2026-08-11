// Copyright 1998-2016 Epic Games, Inc. All Rights Reserved.

using UnrealBuildTool;
using System.Collections.Generic;
using System.IO;

public class UnrealTournamentTarget : TargetRules
{
	public UnrealTournamentTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V2;
		IncludeOrderVersion = EngineIncludeOrderVersion.Oldest;
		// UE 5.8 removed C++17: CppStandardVersion.Minimum = Cpp20
		// (5.6 had Minimum = Cpp17). Set the standard explicitly rather than
		// bumping DefaultBuildSettings, which would also escalate the V4-V7
		// warning levels to errors.
		CppStandard = CppStandardVersion.Cpp20;
		// UT4 changes engine-level settings (logging/checks in Shipping), so it
		// cannot share build products with UnrealGame under an Installed Build.
		BuildEnvironment = TargetBuildEnvironment.Unique;

		ExtraModuleNames.AddRange(new string[] {
			"UnrealTournament",
			"UnrealTournamentFullScreenMovie"
		});

		bUseLoggingInShipping = true;
		bUseChecksInShipping = true;

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// UT4 is old (UE4.15-era) code with many MSVC warnings (C4458 shadow,
			// C4800 implicit-bool, C4834 nodiscard, C4996 deprecations) that
			// clang/Linux/Mac don't flag. Disable them outright (/wd) - more
			// reliable than /WX-, which UBT's per-warning /we flags override.
			bOverrideBuildEnvironment = true;
			AdditionalCompilerArguments = "/wd4458 /wd4459 /wd4456 /wd4457 /wd4800 /wd4834 /wd4996 /wd4996 /WX-";
			ShadowVariableWarningLevel = WarningLevel.Off;
		}

		if (Target.Platform == UnrealTargetPlatform.Mac)
		{
			bOverrideBuildEnvironment = true;
			AdditionalCompilerArguments = "-Wno-error -Wno-bitwise-instead-of-logical -Wno-unused-but-set-variable -Wno-ordered-compare-function-pointers -Wno-deprecated-builtins";

			// CEF3 (Chromium Embedded Framework) has no ARM64 Mac binaries
			if (Target.Architectures.Contains(UnrealArch.Arm64))
			{
				bCompileCEF3 = false;
			}
		}
	
		if (Target.Platform == UnrealTargetPlatform.SimplyStream)
		{
			// Full UT4 game target for wasm/WebGPU via our open DawnRHI stack.
			// Mirror DawnRHITest emdawnwebgpu compile wiring (stock upstream port,
			// NOT SimplyStream closed WebGPURHI); link-side flags (PROXY_TO_PTHREAD,
			// use-port, ASYNCIFY, ICU preload) are injected by SimplyStreamToolChain
			// (target link args are dropped there). bUsePCHFiles: UT4 is UE4.15-era,
			// not IWYU-clean -> needs SharedPCH.Engine (RECIPE.md). CEF3 has no wasm.
			bOverrideBuildEnvironment = true;
			bUsePCHFiles = true;
			bCompileCEF3 = false;
			AdditionalCompilerArguments = "-Wno-error -Wno-unused-template --use-port=emdawnwebgpu";
		}

}
}
