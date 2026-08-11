#!/usr/bin/env bash
# Adapt the migrated SimplyStream fork tree to run/build on nixtop (NixOS).
# Tree at /mnt/vms/ss-build/UnrealEngine (moved from framepick /mnt/models/ss-build).
set -euo pipefail
UE=/mnt/vms/ss-build/UnrealEngine
# 1. NixOS has no /bin/bash -> rewrite UE build-script shebangs to env bash.
find $UE/Engine/Build/BatchFiles -name "*.sh" -exec sed -i "1s|^#!/bin/bash|#!/usr/bin/env bash|" {} +
# 2. Repoint our tooling scripts from framepick /mnt/models to nixtop /mnt/vms.
sed -i "s#/mnt/models/ss-build#/mnt/vms/ss-build#g" tools/*.sh 2>/dev/null || true
# 3. Build/cook under the nix-ld cook shell, invoking scripts via explicit bash:
#    nix-shell /mnt/vms/ss-build/ue-cook-shell.nix --run "bash Engine/Build/BatchFiles/RunUBT.sh <Target> <Plat> <Cfg> -NoUBA"
# NOTE: framepick-built binaries do NOT reliably run here (env-specific) -> REBUILD.
echo nixtop_setup_done
