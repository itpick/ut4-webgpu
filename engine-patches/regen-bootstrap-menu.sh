#!/usr/bin/env bash
# Bootstrap v2: shader cache + registry + DDPI + the MENU content closure
# (Engine cooked Content for Slate/fonts, UT UI/Fonts/Maps/SlateLargeImages).
# Excludes the Character/Weapons/Environments bulk (3.6G) to stay tab-loadable.
set -euo pipefail
UE=/mnt/vms/ss-build/UnrealEngine
COOK="$UE/UnrealTournament/Saved/Cooked/SimplyStream"
UT="$COOK/UnrealTournament/Content/RestrictedAssets"
OUT=/mnt/vms/ss-build/ut-local-webgpu
PACKAGER=/mnt/vms/ss-build/stock-emsdk/upstream/emscripten/tools/file_packager.py

ARGS=(
  "$OUT/ContentBootstrap.data"
  --preload "$COOK/Engine/GlobalShaderCache-SP_WEBGPU_ES31.bin@Engine/GlobalShaderCache-None.bin"
  --preload "$COOK/UnrealTournament/AssetRegistry.bin@UnrealTournament/AssetRegistry.bin"
  --preload "$COOK/UnrealTournament/Metadata@UnrealTournament/Metadata"
  --preload "$UE/Engine/Platforms/SimplyStream/Config/DataDrivenPlatformInfo.ini@Engine/Platforms/SimplyStream/Config/DataDrivenPlatformInfo.ini"
  --preload "$UE/Engine/Platforms/SimplyStream/Config/DataDrivenPlatformInfo.ini@Engine/Config/SimplyStream/DataDrivenPlatformInfo.ini"
  --preload "$COOK/Engine/Content@Engine/Content"
  --preload "$UE/Engine/Config@Engine/Config"
  --preload "$UE/UnrealTournament/Config@UnrealTournament/Config"
  --preload "$UT/UI@UnrealTournament/Content/RestrictedAssets/UI"
  --preload "$UT/Fonts@UnrealTournament/Content/RestrictedAssets/Fonts"
  --preload "$UT/SlateLargeImages@UnrealTournament/Content/RestrictedAssets/SlateLargeImages"
  --preload "$UT/Maps@UnrealTournament/Content/RestrictedAssets/Maps"
  --js-output="$OUT/ContentBootstrap.js"
)
python3 "$PACKAGER" "${ARGS[@]}"
echo "=== bootstrap size ==="; ls -la "$OUT/ContentBootstrap.data"
