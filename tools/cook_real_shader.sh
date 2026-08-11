#!/usr/bin/env bash
# Cook a real, unmodified UE global shader (.usf) all the way through the
# real DawnShaderFormat IShaderFormat -> WGSL, using the naive-flatten +
# manual-generator-splice recipe documented in HANDOFF.md ("update 4").
#
# This is NOT a replacement for UE's real IShaderFormat::PreprocessShader()
# (see HANDOFF.md "Milestone 2" for why that path is separately blocked on
# this box) -- it's the fully real fallback recipe: real C++-reflected
# uniform-buffer declarations (View/DrawRectangleParameters), real
# GenerateInstancedStereoCode() output, real #include flattening, then a
# REAL standalone C-preprocessor pass (UBT's own bundled clang -E) to do
# real macro expansion + #if evaluation (this step is REQUIRED --
# CreateHLSLUniformBufferDeclaration()'s "UniformBuffer Name { ... }" block
# is emitted as unexpanded UB_CB_REMAP_PARAMETER(...) macro CALLS, which
# CleanupUniformBufferCode() can only parse once genuinely macro-expanded
# -- see HANDOFF.md for the full diagnosis), THEN DawnCookProbe's real
# CompilePreprocessedShader() call (which itself calls the real, exported
# CleanupUniformBufferCode() before handing text to DXC).
#
# Usage: cook_real_shader.sh <shader.usf> <EntryPoint> <vs|ps> <out.wgsl>
set -euo pipefail

UE=/mnt/models/ss-build/UnrealEngine
BIN=$UE/Engine/Binaries/Linux/DawnCookProbe-Linux-Shipping
CLANG=$UE/Engine/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64/v26_clang-20.1.8-rockylinux8/x86_64-unknown-linux-gnu/bin/clang
COOKSHELL=/mnt/models/ss-build/ue-cook-shell.nix

SHADER_SRC="$1"
ENTRY="$2"
STAGE="$3"
OUT="$4"

WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

# Every DawnCookProbe invocation below (dumpubdecl/dumpinstancedstereo/the
# final cook) segfaults during Slate/engine SHUTDOWN even on a real success
# -- a known, benign, already-diagnosed teardown crash (see HANDOFF.md
# "Wall D": it happens strictly *after* our own LogDawnCookProbe SUCCESS
# line and output file are already written). set -e would abort this
# script on that nonzero exit code alone, so every invocation is `|| true`
# and we check for the real success marker / output file's existence
# instead of $?.
echo "[1/5] Dumping real auto-generated uniform-buffer declarations (View, DrawRectangleParameters)..."
nix-shell "$COOKSHELL" --run "$BIN -dumpubdecl View $WORK/view.hlsl DawnDumpCommandlet" || true
nix-shell "$COOKSHELL" --run "$BIN -dumpubdecl DrawRectangleParameters $WORK/dr.hlsl DawnDumpCommandlet" || true
if [[ ! -s "$WORK/view.hlsl" || ! -s "$WORK/dr.hlsl" ]]; then
  echo "FAILED: -dumpubdecl did not produce output (real failure, not the benign teardown crash)" >&2
  exit 1
fi
cat "$WORK/view.hlsl" "$WORK/dr.hlsl" > "$WORK/ub_combined.hlsl"

echo "[2/5] Dumping real instanced-stereo companion code (ViewState/GetPrimaryView)..."
nix-shell "$COOKSHELL" --run "$BIN -dumpinstancedstereo $WORK/isr.hlsl DawnDumpCommandlet" || true
if [[ ! -s "$WORK/isr.hlsl" ]]; then
  echo "FAILED: -dumpinstancedstereo did not produce output" >&2
  exit 1
fi

echo "[3/5] Flattening real #includes for $SHADER_SRC..."
python3 "$(dirname "$0")/flatten_includes.py" \
  --generated-ub-file "$WORK/ub_combined.hlsl" \
  --generated-instancedstereo-file "$WORK/isr.hlsl" \
  "$SHADER_SRC" \
  > "$WORK/flat_body.hlsl"

echo "[4/5] Prepending real compile-time constants + running a real C-preprocessor pass..."
{
  echo '#define VULKAN_PROFILE_SM5 1'
  echo '#define UE_LWC_RENDER_TILE_SIZE 2097152.0'
  echo '#define UE_LWC_RENDER_TILE_SIZE_SQRT 1448.1546878700494'
  echo '#define UE_LWC_RENDER_TILE_SIZE_RSQRT 0.0006905339660024878'
  echo '#define UE_LWC_RENDER_TILE_SIZE_RCP 4.76837158203125e-07'
  echo '#define UE_LWC_RENDER_TILE_SIZE_FMOD_PI 0.6736520551441743'
  echo '#define UE_LWC_RENDER_TILE_SIZE_FMOD_2PI 0.6736520551441743'
  # WORKING_COLOR_SPACE_IS_SRGB=1: real UE's default project working color
  # space is sRGB (ShaderCompiler.cpp only emits the RGB_TO_XYZ_MAT-family
  # defines when it's NOT sRGB) -- matches the real default, and avoids
  # ColorSpace.ush's #else branch which needs those extra matrix defines.
  echo '#define WORKING_COLOR_SPACE_IS_SRGB 1'
  cat "$WORK/flat_body.hlsl"
} > "$WORK/flat_final.hlsl"

# NOTE: this clang invocation is EXPECTED to exit non-zero -- real UE's own
# UB_CB_REMAP_PARAMETER macro deliberately does an invalid `UBName##.##StructName`
# token-paste (see HANDOFF.md) that clang/DXC's shared frontend recovers
# from (emits an error diagnostic, but still produces the correct expanded
# text on stdout). Capture stdout regardless of exit code; only real,
# non-recoverable errors should show up outside that class (check
# flat_final_pp.errlog if this script's cook step fails unexpectedly).
"$CLANG" -E -P -undef -ferror-limit=0 -x c "$WORK/flat_final.hlsl" \
  > "$WORK/flat_final_pp.hlsl" 2> "$WORK/flat_final_pp.errlog" || true

echo "[5/5] Cooking through the real DawnShaderFormat IShaderFormat..."
# Exit code 139 (segfault) after a logged SUCCESS line is a known, benign
# teardown crash (see HANDOFF.md "Wall D") -- check the log, not $?.
set +e
nix-shell "$COOKSHELL" --run "$BIN $WORK/flat_final_pp.hlsl $ENTRY $STAGE $OUT $SHADER_SRC $(basename "$SHADER_SRC" .usf)-Real DawnDumpCommandlet" 2>&1 | tee "$WORK/cook.log"
set -e

if grep -q "LogDawnCookProbe: SUCCESS" "$WORK/cook.log"; then
  echo "COOK SUCCEEDED. WGSL written to $OUT"
  exit 0
else
  echo "COOK FAILED. See errors above (or $WORK/cook.log before cleanup)."
  exit 1
fi
