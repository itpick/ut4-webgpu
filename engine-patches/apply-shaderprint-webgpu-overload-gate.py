#!/usr/bin/env python3
# Fix #5: the WebGPU cooker does SetDefine("Buffer","StructuredBuffer") /
# ("RWBuffer","RWStructuredBuffer") (tint has no texel-buffer type). That
# collapses ShaderPrintCommon.ush's Buffer<uint>/RWBuffer<uint> overloads of
# ReadSymbol/ClearCounters onto their StructuredBuffer<uint> twins -> DXC
# "redefinition". Guard the texel-buffer overloads out under COMPILER_WEBGPU;
# every caller's Buffer<uint> is remapped to StructuredBuffer<uint> too, so the
# surviving overload matches all calls. Recook-only (shader source).
import sys, io

F = "/mnt/vms/ss-build/UnrealEngine/Engine/Shaders/Private/ShaderPrintCommon.ush"
lines = io.open(F, encoding="utf-8").read().split("\n")

SIGS = [
    "void ClearCounters(RWBuffer<uint> InRWBuffer)",
    "FShaderPrintItem ReadSymbol(uint Offset, Buffer<uint> InBuffer)",
]

if any("COMPILER_WEBGPU" in l for l in lines):
    print("already patched (COMPILER_WEBGPU guard present)")
    sys.exit(0)

out = []
i = 0
patched = 0
while i < len(lines):
    line = lines[i]
    if line.strip() in SIGS:
        # emit guard-open before the signature
        out.append("#if !COMPILER_WEBGPU // tint has no texel Buffer<T>; the StructuredBuffer<T> overload above serves all remapped callers")
        # copy signature + body until brace balance returns to 0
        depth = 0
        started = False
        while i < len(lines):
            out.append(lines[i])
            depth += lines[i].count("{") - lines[i].count("}")
            if "{" in lines[i]:
                started = True
            if started and depth == 0:
                break
            i += 1
        out.append("#endif // !COMPILER_WEBGPU")
        patched += 1
        i += 1
        continue
    out.append(line)
    i += 1

if patched != len(SIGS):
    print(f"ERROR: expected to guard {len(SIGS)} functions, guarded {patched}", file=sys.stderr)
    sys.exit(1)

io.open(F, "w", encoding="utf-8").write("\n".join(out))
print(f"patched {patched} overloads")
import subprocess
print(subprocess.run(["grep","-nE","COMPILER_WEBGPU|ClearCounters\\(RWBuffer|ReadSymbol\\(uint Offset, Buffer", F], capture_output=True, text=True).stdout)
