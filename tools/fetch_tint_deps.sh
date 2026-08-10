#!/usr/bin/env bash
set -e
cd /tmp/tint-src-fetch/dawn

fetchsub() {
  path="$1"; url="$2"; sha="$3"
  mkdir -p "$path"
  ( cd "$path" && \
    git init -q && \
    (git remote add origin "$url" 2>/dev/null || true) && \
    timeout 180 git fetch --depth 1 origin "$sha" 2>&1 | tail -5 && \
    git checkout -q FETCH_HEAD )
}

fetchsub third_party/spirv-headers/src https://chromium.googlesource.com/external/github.com/KhronosGroup/SPIRV-Headers 3397e1e4fe0a9964e1837c2934b81835093494b8
fetchsub third_party/spirv-tools/src https://chromium.googlesource.com/external/github.com/KhronosGroup/SPIRV-Tools 392b4893c4955125c1873c33a97f2a8ee8363bd3
fetchsub third_party/abseil-cpp https://chromium.googlesource.com/chromium/src/third_party/abseil-cpp cae4b6a3990e1431caa09c7b2ed1c76d0dfeab17
echo DONE
du -sh third_party/spirv-headers/src third_party/spirv-tools/src third_party/abseil-cpp
