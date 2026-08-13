#!/usr/bin/env bash
# Rebuild libDawnTintBridge.so on nixtop. Reconstructed from HANDOFF.md recipe +
# tools/build_hlsl_to_wgsl.sh flags. Tint/SPIRV-Tools/absl are prebuilt (.a in
# DawnTint/lib/Linux) so this is a single-.cpp compile + link (fast, no OOM).
set -euo pipefail
UE=/mnt/vms/ss-build/UnrealEngine
D=$UE/Engine/Source/ThirdParty/DawnTint
TC=$UE/Engine/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64/v26_clang-20.1.8-rockylinux8/x86_64-unknown-linux-gnu
CXX=$TC/bin/clang++
REPO=/mnt/vms/ss-build/ut4-webgpu-push
SRC=$REPO/tools/dawn_tint_bridge.cpp
OUT=$UE/Engine/Binaries/ThirdParty/DawnTint/Linux/libDawnTintBridge.so
WORK=/tmp/bridge-build; mkdir -p "$WORK"

echo "[1/3] keep header in sync (both ABI sides)"
cp "$REPO/tools/dawn_tint_bridge.h" "$D/include/dawn_tint_bridge.h"

echo "[2/3] compile dawn_tint_bridge.cpp"
"$CXX" -std=c++20 --sysroot="$TC" -nostdinc++ -isystem "$TC/include/c++/v1" \
  -fPIC -fvisibility=hidden -O2 \
  -I"$REPO/tools" \
  -I"$D/src" \
  -I"$D/src/third_party/abseil-cpp" \
  -I"$D/src/third_party/spirv-headers/src/include" \
  -I"$D/src/third_party/spirv-tools/src/include" \
  -c "$SRC" -o "$WORK/dawn_tint_bridge.o"

echo "[3/3] link libDawnTintBridge.so against prebuilt tint/spirv/absl archives"
mapfile -t LIBS < <(find "$D/lib/Linux" -name "*.a" ! -name "libDawnTintBridge.a")
echo "  linking ${#LIBS[@]} archives"
cp "$OUT" "$OUT.bak.$(date +%s)" 2>/dev/null || true
"$CXX" --sysroot="$TC" -nostdinc++ -isystem "$TC/include/c++/v1" \
  -shared -fPIC -fvisibility=hidden \
  -Wl,--exclude-libs,ALL -Wl,-Bsymbolic \
  -o "$OUT" "$WORK/dawn_tint_bridge.o" \
  -Wl,--start-group "${LIBS[@]}" -Wl,--end-group \
  -static-libstdc++ -lc++ -lc++abi -lpthread -ldl
echo "=== verify exported ABI symbols ==="
nm -D --defined-only "$OUT" | grep -E "Dawn_LegalizeAndCookSpirvToWgsl|Dawn_FreeTintCookResult" || { echo "MISSING ABI SYMBOLS"; exit 1; }
ls -la "$OUT"
echo BRIDGE_BUILD_OK
