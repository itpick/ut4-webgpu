#!/usr/bin/env python3
# Naive recursive #include flattener for real UE .usf/.ush shader source.
# NOT a replacement for UE's real preprocessor (no #if/#define evaluation,
# no permutation handling) -- purely textual #include substitution, so
# ShaderConductor/DXC's own (standard, non-UE) preprocessor can then run
# its usual #define/#if handling on the result. Used only to get REAL,
# unmodified UT4/engine shader text past the "resolve real UE virtual
# include paths" problem without needing UE's live shader-compiling
# infrastructure.
#
# --generated-ub-file PATH (session "update 3", see HANDOFF.md "Milestone
# 2"): every real UE shader implicitly expects /Engine/Generated/
# GeneratedUniformBuffers.ush to already contain real `cbuffer View { ... }`
# / `cbuffer Primitive { ... }` declarations, auto-injected by UE's actual
# shader compiler from FShaderParametersMetadata reflection (see
# UE::ShaderParameters::AddUniformBufferIncludesToEnvironment,
# RenderCore/Private/ShaderParameters.cpp) -- this flattener has no access
# to that C++ reflection itself, so instead of skipping this virtual
# #include like all other /Engine/Generated/ paths, point it at a real file
# containing the REAL declaration text pre-dumped via
# `DawnShaderFormatTest -dumpubdecl <StructName> <outfile>` (which calls
# the exact same UE reflection API). Concatenate multiple structs' dumps
# into one file before passing it here if more than one UB is referenced.
import sys, re, os

ENGINE_SHADERS = "/mnt/models/ss-build/UnrealEngine/Engine/Shaders/Private"
ENGINE_PUBLIC = "/mnt/models/ss-build/UnrealEngine/Engine/Shaders/Public"

INCLUDE_RE = re.compile(r'^\s*#include\s+"([^"]+)"')

GENERATED_UB_FILE = None  # set via --generated-ub-file, see module docstring above

def resolve(path, current_dir):
    if path.startswith("/Engine/Private/"):
        return os.path.join(ENGINE_SHADERS, path[len("/Engine/Private/"):])
    if path.startswith("/Engine/Public/"):
        return os.path.join(ENGINE_PUBLIC, path[len("/Engine/Public/"):])
    if path == "/Engine/Generated/GeneratedUniformBuffers.ush" and GENERATED_UB_FILE:
        return GENERATED_UB_FILE
    if path.startswith("/Engine/Generated/"):
        return None  # other generated content, no real file -- skip
    # relative to current dir
    return os.path.join(current_dir, path)

def flatten(path, seen, depth=0):
    if depth > 40:
        return f"// [flatten] max depth exceeded for {path}\n"
    real = path if os.path.isabs(path) and os.path.exists(path) else None
    if real is None:
        return f"// [flatten] could not resolve: {path}\n"
    if real in seen:
        return f"// [flatten] already included: {path}\n"
    seen.add(real)
    out = [f"// ==== begin {path} ====\n"]
    current_dir = os.path.dirname(real)
    try:
        with open(real, "r", encoding="utf-8-sig", errors="replace") as f:
            for line in f:
                m = INCLUDE_RE.match(line)
                if m:
                    inc = m.group(1)
                    resolved = resolve(inc, current_dir)
                    if resolved is None:
                        out.append(f"// [flatten] skipped generated include: {inc}\n")
                        continue
                    out.append(flatten(resolved, seen, depth + 1))
                else:
                    out.append(line)
    except FileNotFoundError:
        out.append(f"// [flatten] FILE NOT FOUND: {real}\n")
    out.append(f"// ==== end {path} ====\n")
    return "".join(out)

if __name__ == "__main__":
    args = sys.argv[1:]
    if "--generated-ub-file" in args:
        i = args.index("--generated-ub-file")
        GENERATED_UB_FILE = args[i + 1]
        del args[i:i + 2]
    root = args[0]
    seen = set()
    result = flatten(root, seen)
    sys.stdout.write(result)
    sys.stderr.write(f"[flatten] {len(seen)} unique files inlined\n")
