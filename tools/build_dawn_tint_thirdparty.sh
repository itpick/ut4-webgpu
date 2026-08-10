#!/usr/bin/env bash
# Populates Engine/Source/ThirdParty/DawnTint/{src,lib/Linux} — the exact
# path DawnShaderFormat.Build.cs expects — with the self-built Tint/
# SPIRV-Tools static libraries the DawnShaderFormat IShaderFormat module
# links against.
#
# Deliberately NOT the vendored prebuilt libtint.a/libSPIRV-Tools.a under
# Engine/Platforms/SimplyStream/Source/ThirdParty/Dawn/lib/linux — that
# prebuilt libtint.a is ABI-mismatched and crashes
# tint::spirv::reader::ReadIR with bad_variant_access on every input (see
# HANDOFF.md, "Tint: wall #2"). This script fetches the exact pinned Tint/
# Dawn revision (read from the vendored Dawn/include/dawn/common/
# Version_autogen.h's kDawnVersion) and builds Tint's SPIR-V reader + WGSL
# writer libraries from pristine source instead.
#
# Deliberately NOT committed as binaries to the public itpick/ut4-webgpu
# repo (the fetched source is ~90MB, the built libs another ~75MB) — this
# script is what's committed; run it once against your own private engine
# checkout. Same pattern as the pre-existing vendored Dawn/Tint static libs
# DawnRHI.Build.cs already references, which also aren't pushed publicly.
#
# Usage: UE=/path/to/UnrealEngine ./build_dawn_tint_thirdparty.sh
set -euo pipefail

UE="${UE:?Set UE=/path/to/UnrealEngine}"
DAWN_DIR="$UE/Engine/Platforms/SimplyStream/Source/ThirdParty/Dawn"
VERSION_HEADER="$DAWN_DIR/include/dawn/common/Version_autogen.h"
DEST="$UE/Engine/Source/ThirdParty/DawnTint"
SRC="$DEST/src"
LIBDIR="$DEST/lib/Linux"

DAWN_SHA=$(grep -oE '"[0-9a-f]{40}"' "$VERSION_HEADER" | head -1 | tr -d '"')
echo "[build_dawn_tint_thirdparty] pinned Dawn/Tint commit: $DAWN_SHA"

# fetch_tint_deps.sh (unmodified) hardcodes /tmp/tint-src-fetch/dawn as both
# its own working dir and the third_party/{spirv-headers,spirv-tools,
# abseil-cpp} destination — fetch there, then copy the whole tree into
# place under $SRC (the path DawnShaderFormat.Build.cs expects).
FETCH_DIR=/tmp/tint-src-fetch/dawn
mkdir -p "$FETCH_DIR"
if [ ! -d "$FETCH_DIR/.git" ]; then
	git init -q "$FETCH_DIR"
	git -C "$FETCH_DIR" remote add origin https://dawn.googlesource.com/dawn
fi
git -C "$FETCH_DIR" fetch --depth 1 origin "$DAWN_SHA"
git -C "$FETCH_DIR" checkout -q FETCH_HEAD

# third_party/{spirv-headers,spirv-tools,abseil-cpp} at their pinned SHAs
# (read straight from .gitmodules/git ls-tree gitlinks).
bash "$(dirname "$0")/fetch_tint_deps.sh"

mkdir -p "$SRC" "$LIBDIR"
rsync -a --exclude='.git' "$FETCH_DIR/" "$SRC/"

TC="$UE/Engine/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64/v26_clang-20.1.8-rockylinux8/x86_64-unknown-linux-gnu"
BUILD_DIR=$(mktemp -d)

nix-shell -p cmake ninja "python3.withPackages (ps: [ps.jinja2])" --run "
cmake -GNinja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=$TC/bin/clang -DCMAKE_CXX_COMPILER=$TC/bin/clang++ \
  -DCMAKE_C_FLAGS='--sysroot=$TC' \
  -DCMAKE_CXX_FLAGS='--sysroot=$TC -nostdinc++ -isystem $TC/include/c++/v1' \
  -DCMAKE_EXE_LINKER_FLAGS='--sysroot=$TC' -DCMAKE_SHARED_LINKER_FLAGS='--sysroot=$TC' \
  -DDAWN_ENABLE_VULKAN=OFF -DDAWN_ENABLE_D3D11=OFF -DDAWN_ENABLE_D3D12=OFF -DDAWN_ENABLE_METAL=OFF \
  -DDAWN_ENABLE_NULL=OFF -DDAWN_ENABLE_DESKTOP_GL=OFF -DDAWN_ENABLE_OPENGLES=OFF \
  -DDAWN_USE_X11=OFF -DDAWN_USE_WAYLAND=OFF -DDAWN_USE_GLFW=OFF -DDAWN_USE_WINDOWS_UI=OFF \
  -DDAWN_BUILD_SAMPLES=OFF -DDAWN_BUILD_TESTS=OFF -DDAWN_BUILD_PROTOBUF=OFF -DDAWN_BUILD_NODE_BINDINGS=OFF \
  -DTINT_BUILD_TESTS=OFF -DTINT_BUILD_CMD_TOOLS=OFF -DTINT_BUILD_IR_BINARY=OFF \
  -DTINT_BUILD_SPV_READER=ON -DTINT_BUILD_WGSL_WRITER=ON -DTINT_BUILD_WGSL_READER=ON \
  -DTINT_BUILD_SPV_WRITER=OFF -DTINT_BUILD_GLSL_WRITER=OFF -DTINT_BUILD_HLSL_WRITER=OFF -DTINT_BUILD_MSL_WRITER=OFF \
  -DTINT_BUILD_GLSL_VALIDATOR=OFF \
  -B '$BUILD_DIR' \
  '$SRC'
ninja -C '$BUILD_DIR' -j \$(nproc) libtint_lang_spirv_reader.a libtint_lang_wgsl_writer.a
"

find "$BUILD_DIR" -name '*.a' -exec cp {} "$LIBDIR/" \;
echo "[build_dawn_tint_thirdparty] $(find "$LIBDIR" -name '*.a' | wc -l) static libs -> $LIBDIR"
rm -rf "$BUILD_DIR"

echo "[build_dawn_tint_thirdparty] done. DawnShaderFormat.Build.cs will now find:"
echo "  $SRC (headers)"
echo "  $LIBDIR (*.a)"
