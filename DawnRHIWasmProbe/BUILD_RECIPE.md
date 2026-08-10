# DawnRHI wasm probe — build recipe (Stage 1, Goal 2)

Standalone (non-UBT) probe validating the WebGPU-in-browser path before
attempting to port the full DawnRHI UE module to wasm. Uses emscripten's
built-in `emdawnwebgpu` port (upstream Emscripten project — MIT/BSD-3,
independent of anything in the SimplyStream fork, including their own
vendored `Source/ThirdParty/emdawn` copy).

```
source /mnt/models/ss-build/stock-emsdk/emsdk_env.sh
em++ -std=c++17 -O1 \
  --use-port=emdawnwebgpu \
  -sASYNCIFY=0 -sEXIT_RUNTIME=0 -sALLOW_MEMORY_GROWTH=1 \
  triangle_wasm.cpp -o triangle_wasm.js
```

Verified (headless Chrome, `--enable-unsafe-webgpu --enable-features=Vulkan
--ignore-gpu-blocklist`, real RTX 5070 via Vulkan — NOT `--use-angle=vulkan`,
which broke Chrome's own compositor surface init in this headless env):
adapter request, device request, WGSL pipeline creation, draw call, and
frame submission all succeed (see browser console log lines). Getting a
headless *screenshot* of the composited canvas hits a
`SharedImageBackingFactory` gap specific to this headless/display-less
Chrome configuration (gpu/command_buffer/service/shared_image/
shared_image_factory.cc:1001) — a Chrome-headless-environment limitation,
not a WebGPU/wasm code issue. Full visual confirmation pending a real
windowed browser (COOP/COEP-served, e.g. Brave).
