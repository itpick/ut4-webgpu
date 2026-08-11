# DawnRHI wasm probe — build recipe (Stage 1, Goal 2 + Goal 3)

Standalone (non-UBT) probe validating the WebGPU-in-browser path before
attempting to port the full DawnRHI UE module to wasm. Uses emscripten's
built-in `emdawnwebgpu` port (upstream Emscripten project — MIT/BSD-3,
independent of anything in the SimplyStream fork, including their own
vendored `Source/ThirdParty/emdawn` copy).

## Goal 2: hand-authored WGSL triangle (`triangle_wasm.cpp`)

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

## Goal 3: REAL cooked UE shader pair, offscreen readback (`real_shader_wasm.cpp`)

Renders the exact same real, unmodified UE shader pair DawnRHITest renders
natively (`Engine/Shaders/Private/ScreenPass.usf`'s `ScreenPassVS`/
`CopyRectPS`, cooked via `tools/cook_real_shader.sh` through the real
`DawnShaderFormat` — see main `HANDOFF.md`) through emdawnwebgpu, **in a
real browser**, driven by the same reflection sidecar
(`.wgsl.bindings.txt`) parsing DawnRHITestMain.cpp uses natively (real
`@binding` numbers 106/109/110, not a hardcoded `{0,1,2}` assumption).

Since Goal 2 already established that headless Chrome in this environment
cannot screenshot a composited canvas (`SharedImageBackingFactory` gap),
Goal 3 does **not** use a canvas/swapchain at all — it renders to an
offscreen `RenderAttachment` texture, `copyTextureToBuffer`s it into a
`MapRead` buffer, `mapAsync`s it, and logs actual pixel values (center,
corner, and a 16x16 grid sample across the whole image) to the console —
the same verification method used to validate
`docs/proof-real-shader-milestone2.png` natively. This sidesteps the
canvas-compositor gap entirely while still proving the full real
resource/pipeline/bind-group/draw/readback path works in-browser.

### Regenerate the real cooked shader assets

```sh
cd /home/lucas/workspace/ut4-webgpu-push
bash tools/cook_real_shader.sh \
  /mnt/models/ss-build/UnrealEngine/Engine/Shaders/Private/ScreenPass.usf \
  ScreenPassVS vs /path/to/real/ScreenPassVS.wgsl
bash tools/cook_real_shader.sh \
  /mnt/models/ss-build/UnrealEngine/Engine/Shaders/Private/ScreenPass.usf \
  CopyRectPS ps /path/to/real/CopyRectPS.wgsl
# each writes a `.wgsl` + a `.wgsl.bindings.txt` reflection sidecar
```

Not committed here (regenerable, and this dir intentionally holds only
our own new source, not generated cook output — same policy as the rest
of the branch). `docs/real-shader-wasm-browser-console.log` in this repo
is a real captured run's full console output, kept as evidence.

### Build

```sh
source /mnt/models/ss-build/stock-emsdk/emsdk_env.sh
em++ -std=c++17 -O1 \
  --use-port=emdawnwebgpu \
  -sASYNCIFY=0 -sEXIT_RUNTIME=0 -sALLOW_MEMORY_GROWTH=1 \
  --preload-file real@/real \
  real_shader_wasm.cpp -o real_shader_wasm.js
```

`--preload-file real@/real` bakes the four real cooked-shader-asset files
(two `.wgsl`, two `.wgsl.bindings.txt`, from a local `real/` directory
next to this script) into the wasm's MEMFS data blob at `/real/...`, so
they can be loaded with plain synchronous `fopen`/`fread` — no async
fetch/ASYNCIFY needed for asset loading, matching this probe's
`-sASYNCIFY=0`.

### Serve + verify in a real browser (headless Chrome via raw CDP)

```sh
python3 host/serve.py 8799 /path/to/dawnrhi-wasm/dir   # COOP/COEP server
node tools/cdp_capture.mjs <chrome-binary> 'http://127.0.0.1:8799/real_shader.html' 8000
```

`tools/cdp_capture.mjs` is a dependency-free (Node 22+ built-in
`WebSocket`/`fetch` only, no puppeteer/playwright) CDP driver: launches
headless Chrome with `--remote-debugging-port`, opens a target via the
`/json/new` HTTP endpoint, connects its devtools websocket, enables
`Runtime`/`Log`/`Page`, navigates, and dumps every
`Runtime.consoleAPICalled`/`Log.entryAdded`/`Runtime.exceptionThrown`
message — this is how the actual pixel readback (not a screenshot) gets
verified: the wasm code itself logs the real mapped-buffer pixel bytes to
`console.log`, and CDP captures that text.

**Two environment gotchas hit and fixed getting this far (this session,
Goal 3):**

1. **Chrome needs real shared libs, not just being present as a binary**:
   the Playwright-cached Chrome
   (`~/.cache/ms-playwright/chromium-1228/chrome-linux64/chrome`) fails
   with `error while loading shared libraries: libcairo.so.2` when run
   bare on this NixOS-style box (no FHS `/usr/lib`). Fixed by extracting
   an `LD_LIBRARY_PATH` from
   `nix-shell -p cairo pango gtk3 nss nspr alsa-lib atk at-spi2-atk cups
   libdrm expat libxkbcommon libxcomposite libxdamage libxfixes
   libxrandr mesa libgbm glib gdk-pixbuf dbus libx11 libxext libxrender
   libxcb --run 'echo $NIX_LDFLAGS'` (parse the `-L<path>` tokens into a
   colon-joined `LD_LIBRARY_PATH`) and exporting it before launching
   Chrome. A real environment constraint, not a code issue.
2. **`--use-angle=vulkan` not needed for offscreen-only rendering**: Goal
   2's recipe used it to get the real GPU (RTX 5070) via Vulkan for a
   *canvas*-presenting path; Goal 3 doesn't need ANGLE/canvas at all
   (`--enable-features=Vulkan` alone is enough for Dawn's own Vulkan
   backend to pick up the real GPU for the offscreen render+readback), so
   this run's launch flags **omit** `--use-angle=vulkan` — avoids
   reintroducing Goal 2's canvas-compositor gap for a path that doesn't
   need a canvas at all.

### Real bug found + fixed this session (Goal 3)

**Zero-initializing `WGPUTextureViewDescriptor` with `{}` is NOT "whole
resource"** in this webgpu.h/Dawn build — `mipLevelCount`/
`arrayLayerCount` land on literal `0`, which Dawn's validation rejects
outright (`"The texture view's arrayLayerCount (0) or mipLevelCount (0)
is zero"`), cascading into `[Invalid TextureView]`/`[Invalid
CommandBuffer]` errors on every downstream call that consumes the view
(`CreateBindGroup`, `BeginRenderPass`, `Queue.Submit`) — first render
attempt's readback correctly came back all-zero
(`Center pixel = (0,0,0,0)`), and the real
`OnUncapturedError`/`WGPUUncapturedErrorCallbackInfo` device callback
(wired up specifically to catch exactly this class of thing, mirroring
native DawnRHI's "zero Dawn validation errors" bar) caught it precisely,
with the real Dawn diagnostic text, in-browser. The real "whole resource"
sentinel is `WGPU_MIP_LEVEL_COUNT_UNDEFINED`/
`WGPU_ARRAY_LAYER_COUNT_UNDEFINED` (`UINT32_MAX`), matching what the
`WGPU_TEXTURE_VIEW_DESCRIPTOR_INIT` macro in `webgpu.h` actually sets —
fixed by setting those two fields explicitly on both
`wgpuTextureCreateView` call sites (checkerboard source view, offscreen
target view) in `real_shader_wasm.cpp`. After the fix: zero WGPU
validation/error-callback lines in the whole captured console, and a
correct readback.

### Result: real render, zero WGPU errors, pixel-exact vs. the native proof

```
Loaded 1 VS binding(s), 2 PS binding(s) from real reflection sidecars
Reflected bind indices: DrawRectangleParameters=106 InputTexture=109 InputSampler=110
Real-shader pipeline created OK (reflection-driven bind group layout)
Frame submitted, mapping readback buffer...
Center pixel = (255,200,40,255), corner(4,4) = (255,200,40,255)
Grid sample: A(255,200,40)=128 B(30,60,200)=128 other=0 (16x16=256 total)
SUCCESS: real cooked ScreenPassVS/CopyRectPS shader pair rendered a pixel-exact
checkerboard through emdawnwebgpu in-browser (offscreen readback verified)
```

`Center pixel`/`corner(4,4)` are byte-identical to the native
`DawnRHITest -vs=/-ps=/-out=` run documented in the main `HANDOFF.md`
(`Center pixel = (255,200,40,255), corner(4,4) = (255,200,40,255)`) — the
same real shader pair, the same reflection-driven bind group logic,
produces the same pixels on native Dawn and on emdawnwebgpu-in-a-real-
browser. Full captured console output (including the pre-fix failure, for
the record) is in `docs/real-shader-wasm-browser-console.log`.

### What this does and does NOT prove

Proves: the DawnRHI resource/pipeline/bind-group/command-encoding *logic*
(reflection-driven bind group layout from real sidecar data, real cooked
WGSL compiling and executing, real texture upload, real uniform buffer,
real vertex buffer, real draw, real GPU-side render, real readback) works
identically against emdawnwebgpu's webgpu.h surface as it does against
native Dawn's — this is exactly the "webgpu.h is the same API surface"
premise the whole wasm-port plan rests on, now demonstrated with real
shader content, not just the Goal 2 hand-authored triangle.

Does NOT yet prove: canvas/swapchain presentation in-browser (still
blocked on the Goal 2 `SharedImageBackingFactory` headless-Chrome gap —
untested in a real windowed browser); the actual `FDawnDynamicRHI`/
`FDawnCommandContext` UE module compiled to wasm (this probe is a
standalone harness reimplementing the same resource setup against raw
webgpu.h, not the UE RHI module itself — porting the real module to wasm
still needs UBT/engine-side wasm target support, untouched this session);
async device/adapter acquisition patterns beyond the simple callback
chain used here (no `emscripten_request_animation_frame` main-loop
yielding was needed since this probe does one offscreen frame and exits,
not a live render loop).
