#!/usr/bin/env bash
# Adapt the migrated SimplyStream fork tree to run/build on nixtop (NixOS).
# Tree at /mnt/vms/ss-build/UnrealEngine (moved from framepick /mnt/vms/ss-build).
set -euo pipefail
UE=/mnt/vms/ss-build/UnrealEngine
# 1. NixOS has no /bin/bash -> rewrite UE build-script shebangs to env bash.
find $UE/Engine/Build/BatchFiles -name "*.sh" -exec sed -i "1s|^#!/bin/bash|#!/usr/bin/env bash|" {} +
# 2. Repoint our tooling scripts from framepick /mnt/models to nixtop /mnt/vms.
sed -i "s#/mnt/vms/ss-build#/mnt/vms/ss-build#g" tools/*.sh 2>/dev/null || true
# 3. Build/cook under the nix-ld cook shell, invoking scripts via explicit bash:
#    nix-shell /mnt/vms/ss-build/ue-cook-shell.nix --run "bash Engine/Build/BatchFiles/RunUBT.sh <Target> <Plat> <Cfg> -NoUBA"
# NOTE: framepick-built binaries do NOT reliably run here (env-specific) -> REBUILD.
echo nixtop_setup_done

# Repoint SimplyStream emsdk symlink to migrated stock-emsdk (was framepick /mnt/models)
ln -sfn /mnt/vms/ss-build/stock-emsdk $UE/Engine/Platforms/SimplyStream/emsdk

# 3. Repath ENGINE-TREE source files that hardcode framepick /mnt/models (the sed above only hit tools/).
#    Launch_SimplyStream.Build.cs PreDir (precompiled-object injection) + flatten_includes.py include roots.
UE=/mnt/vms/ss-build/UnrealEngine
sed -i "s#/mnt/models/ss-build#/mnt/vms/ss-build#g" \
  "$UE/Engine/Platforms/SimplyStream/Source/Runtime/Launch/Launch_SimplyStream.Build.cs" 2>/dev/null || true
# 4. Lower the wasm initial-memory so the browser tab can load it (was 3998MB=3.9GB fixed reservation -> tab OOM/Error5).
sed -i "s/^InitialMemoryMB=3998/InitialMemoryMB=256/; s/^MaximumMemoryMB=3998/MaximumMemoryMB=2048/" \
  "$UE/Engine/Platforms/SimplyStream/Config/SimplyStreamEngine.ini" 2>/dev/null || true
# 5. ICU discovery on wasm: game binaries live off-VFS so EngineContentDir() misses the emscripten
#    MEMFS /Engine/Content where the .data preloads ICU. Probe the absolute VFS path explicitly.
#    (Necessary but NOT sufficient: ICU still fails until the .data package is MOUNTED before the
#    pthread game thread runs ICU init -- see HANDOFF for the run-dependency/thread-timing wall.)
python3 - "$UE/Engine/Source/Runtime/Core/Private/Internationalization/ICUInternationalization.cpp" <<PY || true
import sys
f=sys.argv[1]; s=open(f).read()
a="\tconst FString PotentialDataDirectories[] =\n\t{\n"
ins=a+"#if PLATFORM_WASM\n\t\tFString(TEXT(\"/Engine/Content/\")) / DataDirectoryRelativeToContent,\n#endif\n"
if "wndr" not in s.split("PotentialDataDirectories")[1][:400] and a in s:
    open(f,"w").write(s.replace(a,ins,1))
PY
git add tools/nixtop_setup.sh
git -c user.name="Lucas Pick" -c user.email=lpick@tiberius.com -c commit.gpgsign=false commit -q -m "wasm ICU discovery: probe absolute MEMFS /Engine/Content candidate (game EngineContentDir resolves off-VFS). NECESSARY-NOT-SUFFICIENT: ICU still fails because the .data ICU package is not mounted before the pthread game thread runs ICU init (run-dependency/thread-timing wall) -- next step documented" && git push -q origin dawnrhi-stage1 2>&1 | tail -1 && echo PUSHED
# 5. wasm ICU discovery: game binaries live off-VFS so EngineContentDir() misses the emscripten
#    MEMFS /Engine/Content where the .data preloads ICU. Add an absolute-VFS candidate under
#    PLATFORM_WASM in Core/.../ICUInternationalization.cpp (PotentialDataDirectories):
#      FString(TEXT("/Engine/Content/")) / DataDirectoryRelativeToContent
#    NECESSARY-NOT-SUFFICIENT: ICU still fails because the .data ICU package is not mounted before the
#    pthread game thread runs ICU init (emscripten run-dependency + worker FS-timing wall). Real next
#    step: gate the game-thread SIMPLYSTREAM_Init/PreInit on the .data run-dependency clearing.
