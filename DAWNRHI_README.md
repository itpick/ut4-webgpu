# DawnRHI — the clean-path alternative (separate from RECIPE.md)

`RECIPE.md` (this repo's existing, proven build path) links SimplyStream's
**precompiled** `WebGPURHI` objects into the wasm link — that gets the
engine/game booting and initializing a WebGPU device, but is gated for
*cooking new playable content* by the closed `WebGPUShaderFormat` tool
(source not available; see `README.md`'s "the one gate").

`DawnRHI/`, `DawnRHITest/`, `DawnRHIWasmProbe/` are a **different, from-scratch
approach**: our own `IDynamicRHIModule`/`FDynamicRHI`/`IRHICommandContext`
implementation on top of Dawn (native) and emscripten's built-in
`emdawnwebgpu` port, modeled on Epic's open `VulkanRHI`. It does **not**
link, reference, or reverse-engineer SimplyStream's `WebGPURHI` or
`WebGPUShaderFormat` — precompiled or otherwise. The goal is to remove the
"one gate" entirely by owning both the RHI backend and (in progress) the
shader-cook path (HLSL→SPIR-V via ShaderConductor, SPIR-V→WGSL via Tint,
packaged in our own container format), rather than working around it.

Status:
- `DawnRHI/` + `DawnRHITest/`: native Linux triangle renders through the real
  `FDynamicRHI`/`IRHICommandContext` path (not standalone Dawn calls) —
  verified via offscreen GPU readback to PNG.
- `DawnRHIWasmProbe/`: the same WebGPU pipeline (adapter → device → WGSL
  pipeline → draw → submit) verified working end-to-end in a browser via
  wasm, using emscripten's stock `emdawnwebgpu` port — see
  `DawnRHIWasmProbe/BUILD_RECIPE.md`.
- These files are extracted from a private UE5.8 engine tree (a licensed
  SimplyStream SDK checkout) and only include code we authored; no Epic or
  SimplyStream engine source is included here. To build them, drop this
  `DawnRHI/` module into `Engine/Source/Runtime/`, `DawnRHITest/` and
  `DawnRHIWasmProbe/` into `Engine/Source/Programs/` of a UE5.8 tree with
  Dawn/Tint vendored under `ThirdParty` (see `DawnRHI.Build.cs` for the
  expected layout), and build via UBT (native) or `em++
  --use-port=emdawnwebgpu` (wasm probe).
