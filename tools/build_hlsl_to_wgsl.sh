#!/usr/bin/env bash
set -e
TC=/mnt/models/ss-build/UnrealEngine/Engine/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64/v26_clang-20.1.8-rockylinux8/x86_64-unknown-linux-gnu
CLANGXX=$TC/bin/clang++
DAWN=/mnt/models/ss-build/UnrealEngine/Engine/Platforms/SimplyStream/Source/ThirdParty/Dawn
SCINC=/mnt/models/ss-build/UnrealEngine/Engine/Source/ThirdParty/ShaderConductor/ShaderConductor/Include
SRC=/tmp/tint-src-fetch/dawn
BUILD=/tmp/tint-build
cd /tmp

"$CLANGXX" -std=c++20 --sysroot="$TC" -nostdinc++ -isystem "$TC/include/c++/v1" \
  -I"$SCINC" -I"$SCINC/ShaderConductor" \
  -I"$SRC" -I"$SRC/third_party/abseil-cpp" -I"$SRC/third_party/spirv-headers/src/include" \
  -I"$SRC/third_party/spirv-tools/src/include" \
  -c hlsl_to_wgsl.cpp -o hlsl_to_wgsl.o

mapfile -t ALL_LIBS < <(find "$BUILD" -name "*.a")
echo "linking against ${#ALL_LIBS[@]} self-built tint/spirv-tools archives"

"$CLANGXX" --sysroot="$TC" -nostdinc++ -isystem "$TC/include/c++/v1" \
  -o hlsl_to_wgsl hlsl_to_wgsl.o \
  -Wl,--start-group "${ALL_LIBS[@]}" -Wl,--end-group \
  -lc++ -lc++abi -lpthread -ldl
echo BUILD_OK
