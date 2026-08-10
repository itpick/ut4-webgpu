#!/usr/bin/env python3
# Naive recursive #include flattener for real UE .usf/.ush shader source.
# NOT a replacement for UE's real preprocessor (no #if/#define evaluation,
# no permutation handling) -- purely textual #include substitution, so
# ShaderConductor/DXC's own (standard, non-UE) preprocessor can then run
# its usual #define/#if handling on the result. Used only to get REAL,
# unmodified UT4/engine shader text past the "resolve real UE virtual
# include paths" problem without needing UE's live shader-compiling
# infrastructure.
import sys, re, os

ENGINE_SHADERS = "/mnt/models/ss-build/UnrealEngine/Engine/Shaders/Private"
ENGINE_PUBLIC = "/mnt/models/ss-build/UnrealEngine/Engine/Shaders/Public"

INCLUDE_RE = re.compile(r'^\s*#include\s+"([^"]+)"')

def resolve(path, current_dir):
    if path.startswith("/Engine/Private/"):
        return os.path.join(ENGINE_SHADERS, path[len("/Engine/Private/"):])
    if path.startswith("/Engine/Public/"):
        return os.path.join(ENGINE_PUBLIC, path[len("/Engine/Public/"):])
    if path.startswith("/Engine/Generated/"):
        return None  # generated content, no real file -- skip
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
    root = sys.argv[1]
    seen = set()
    result = flatten(root, seen)
    sys.stdout.write(result)
    sys.stderr.write(f"[flatten] {len(seen)} unique files inlined\n")
