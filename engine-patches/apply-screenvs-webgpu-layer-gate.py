#!/usr/bin/env python3
# Fix #2 (FScreenPassVS): WGSL/Tint has no vertex-shader layer/viewport-index
# output, and DXC emits SPV_EXT_shader_viewport_index_layer for
# SV_RenderTargetArrayIndex -> Tint ReadIR rejects the extension. Demote the
# layer output to a plain varying under COMPILER_WEBGPU so the layered screen-VS
# permutation (TScreenVSForGS<true>) compiles. Layered screen passes are unused
# on the WebGPU menu path. Recook-only (shader source; no C++ rebuild).
import sys, io

F = "/mnt/vms/ss-build/UnrealEngine/Engine/Shaders/Private/ScreenVertexShader.usf"
src = io.open(F, encoding="utf-8").read()

old = """	/** Controls which of the cube map faces to rasterize the primitive to, only the value from the first vertex is used. */
	uint RTIndex : SV_RenderTargetArrayIndex;"""

new = """	/** Controls which of the cube map faces to rasterize the primitive to, only the value from the first vertex is used. */
#if COMPILER_WEBGPU
	// WGSL/tint has no VS render-target-array-index output (SV_RenderTargetArrayIndex
	// -> SPV_EXT_shader_viewport_index_layer, which tint's SPIR-V reader rejects).
	// Demote to a plain varying so this permutation compiles; layered screen passes
	// are unused on the WebGPU path.
	uint RTIndex : TEXCOORD2;
#else
	uint RTIndex : SV_RenderTargetArrayIndex;
#endif"""

if new.split("\n")[3].strip() in src and "COMPILER_WEBGPU" in src:
    print("already patched")
    sys.exit(0)
if old not in src:
    print("ANCHOR NOT FOUND -- aborting", file=sys.stderr)
    sys.exit(1)

src = src.replace(old, new, 1)
io.open(F, "w", encoding="utf-8").write(src)
print("patched ScreenVertexShader.usf")
# show the result
import subprocess
print(subprocess.run(["sed", "-n", "18,40p", F], capture_output=True, text=True).stdout)
