# Cook breakthrough (2026-08-12/13) — content wall cleared

The full UT4 ES3.1 WebGPU cook now COMPLETES bounded, producing the cooked
content the in-browser engine was missing at boot (GlobalShaderCache +
AssetRegistry + UT-Entry menu map).

## Root cause of prior OOM (rc=137)
The Tint crash-guard caught ~630K TINT_UNIMPLEMENTED ICEs and kept cooking,
but let Tint print a ~10-line reflected-binding banner per ICE -> 6.3M-line /
321MB cook log -> OOM on the 30GB box.

## Fix (decisive)
dawnshaderformat-tint-log-cap.patch — truncate each failed-shader Tint
diagnostic to its first actionable line in DawnShaderCompiler.cpp. Cook log
dropped from 6.3M lines to ~16K. (Belt-and-suspenders: cap shader worker
concurrency; the winning cook did not need it once the log was capped.)

## Working recipe
run_webgpu_es31_cook.sh — ES3.1-only target, -clean -unversioned
-DisablePlugins=GPULightmass -AllowPartialShaderMaps
-ini:Engine:[ConsoleVariables]:r.AreShaderErrorsFatal=0. Exits rc=1 (benign
partial-shader-maps), NOT rc=137.

## Cooked output (nixtop, uncommitted binaries)
UnrealTournament/Saved/Cooked/SimplyStream/ (4.0G):
- Engine/GlobalShaderCache-SP_WEBGPU_ES31.bin (9.5MB)
- UnrealTournament/AssetRegistry.bin
- Content/RestrictedAssets/Maps/UT-Entry.umap + .uexp + _BuiltData
Staged: Saved/StagedBuilds/SimplyStream/ (1.5G, 20016 UFS files).

## Remaining last mile = package into wasm .data
Preload the MINIMAL set via --preload-file in SimplyStreamToolChain.cs:
- GlobalShaderCache-None.bin  <-- rename/symlink from -SP_WEBGPU_ES31.bin
  (runtime LegacyShaderPlatformToShaderFormat probes "-None.bin")
- UT-Entry + deps, AssetRegistry.bin
Then rebuild bundle -> pull to Mac -> localhost self-test.
Do NOT rebuild the editor to "fix" the cache name — that rebuild OOM-kills at
~15GB; rename at package time instead.

Dead end: SimplyStream's simplystream-cli deploy requires a Client ID
(closed SaaS). Not our path.
