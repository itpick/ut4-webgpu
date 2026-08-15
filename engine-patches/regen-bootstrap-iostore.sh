#!/usr/bin/env bash
# Bootstrap v3 (IoStore): shader cache + registry + DDPI + config + Metadata +
# the IoStore CONTAINERS (global + utcontent). The containers carry the cooked
# content; no loose Content dirs needed. Mounted at runtime by
# SimplyStream_MountIoStoreContainers (LaunchSimplyStream).
set -euo pipefail
UE=/mnt/vms/ss-build/UnrealEngine
COOK="$UE/UnrealTournament/Saved/Cooked/SimplyStream"
META="$COOK/UnrealTournament/Metadata"
IOS=/mnt/vms/ss-build/ut-iostore
OUT=/mnt/vms/ss-build/ut-local-webgpu
PACKAGER=/mnt/vms/ss-build/stock-emsdk/upstream/emscripten/tools/file_packager.py

ARGS=(
  "$OUT/ContentBootstrap.data"
  --preload "$COOK/Engine/GlobalShaderCache-SP_WEBGPU_ES31.bin@Engine/GlobalShaderCache-None.bin"
  --preload "$COOK/UnrealTournament/AssetRegistry.bin@UnrealTournament/AssetRegistry.bin"
  --preload "$META/scriptobjects.bin@UnrealTournament/Metadata/scriptobjects.bin"
  --preload "$UE/Engine/Platforms/SimplyStream/Config/DataDrivenPlatformInfo.ini@Engine/Platforms/SimplyStream/Config/DataDrivenPlatformInfo.ini"
  --preload "$UE/Engine/Platforms/SimplyStream/Config/DataDrivenPlatformInfo.ini@Engine/Config/SimplyStream/DataDrivenPlatformInfo.ini"
  --preload "$UE/Engine/Platforms/SimplyStream/Config/SimplyStreamEngine.ini@Engine/Platforms/SimplyStream/Config/SimplyStreamEngine.ini"
  --preload "$UE/Engine/Config@Engine/Config"
  --preload "$UE/Engine/Content/Slate@Engine/Content/Slate"
  --preload "$UE/Engine/Content/SlateDebug@Engine/Content/SlateDebug"
  --preload "$UE/UnrealTournament/Config@UnrealTournament/Config"
  --preload "$IOS/global.utoc@UnrealTournament/Content/Paks/global.utoc"
  --preload "$IOS/global.ucas@UnrealTournament/Content/Paks/global.ucas"
  --preload "$IOS/utcontent.utoc@UnrealTournament/Content/Paks/utcontent.utoc"
  --preload "$IOS/utcontent.ucas@UnrealTournament/Content/Paks/utcontent.ucas"
  --js-output="$OUT/ContentBootstrap.js"
)
python3 "$PACKAGER" "${ARGS[@]}"
echo "=== bootstrap size ==="; ls -la "$OUT/ContentBootstrap.data"
