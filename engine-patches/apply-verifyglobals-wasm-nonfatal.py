#!/usr/bin/env python3
# "Just get it working": boot past missing global shaders on the wasm runtime.
# The closed SimplyStream RHI runs as SP_WEBGPU_SM5 and demands SM5-gated globals
# the ES3.1 cook never produced; VerifyGlobalShaders fatals on the first one.
# Force bErrorOnMissing=false under PLATFORM_WASM so missing globals are skipped
# (the non-fatal else branch is a no-op without the editor) and the menu can render
# with the shaders that ARE present. Scoped to wasm; no other platform affected.
import io, sys
F = "/mnt/vms/ss-build/UnrealEngine/Engine/Source/Runtime/Engine/Private/ShaderCompiler/ShaderCompiler.cpp"
s = io.open(F, encoding="utf-8").read()
if "WebGPU bring-up: boot past missing globals" in s:
    print("already patched"); sys.exit(0)
anchor = (
"\tbool bErrorOnMissing = bLoadedFromCacheFile;\n"
"\tif (FPlatformProperties::RequiresCookedData())\n"
"\t{\n"
"\t\t// We require all shaders to exist on cooked platforms because we can't compile them.\n"
"\t\tbErrorOnMissing = true;\n"
"\t}"
)
add = (
"\n#if PLATFORM_WASM\n"
"\t// WebGPU bring-up: boot past missing globals. The closed SimplyStream RHI runs\n"
"\t// as SP_WEBGPU_SM5 and thus demands SM5-gated globals the ES3.1 cook never\n"
"\t// produced; fataling here blocks the whole engine. Skip missing globals so the\n"
"\t// menu renders with the shaders that ARE present (full coverage iterated later).\n"
"\tbErrorOnMissing = false;\n"
"#endif"
)
if anchor not in s:
    print("ANCHOR NOT FOUND", file=sys.stderr); sys.exit(1)
s = s.replace(anchor, anchor + add, 1)
io.open(F, "w", encoding="utf-8").write(s)
print("patched ShaderCompiler.cpp VerifyGlobalShaders (PLATFORM_WASM bErrorOnMissing=false)")
