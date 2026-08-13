#!/usr/bin/env bash
set -uo pipefail
UE=/mnt/vms/ss-build/UnrealEngine
LOG=/mnt/vms/ss-build/build_unrealpak.log
exec >"$LOG" 2>&1
cd "$UE"
echo "=== UnrealPak (Linux host) build started $(date -Is) ==="
nix-shell /mnt/vms/ss-build/ue-cook-shell.nix --run "bash Engine/Build/BatchFiles/RunUBT.sh UnrealPak Linux Development -NoUBA -MaxParallelActions=4"
echo "=== BUILD EXITED rc=$? at $(date -Is) ==="
