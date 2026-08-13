#!/usr/bin/env python3
# ROOT FIX: FUnix OpenRead -> GFileRegistry -> PlatformInitialOpenFile ->
# GCaseInsensMapper.OpenCaseInsensitiveRead, whose case-insensitive directory
# scan FAILS on emscripten MEMFS -> every OpenRead returns null (why codex needed
# per-call raw-fopen fallbacks). On wasm, try a direct exact-case open() first
# (our preloaded VFS paths are exact case); fall back to the mapper otherwise.
# Fixes IoStore .utoc/.ucas reads and all other OpenRead-based content loading.
import io, sys
F = "/mnt/vms/ss-build/UnrealEngine/Engine/Source/Runtime/Core/Private/Unix/UnixPlatformFile.cpp"
s = io.open(F, encoding="utf-8").read()
if "wasm: direct exact-case open" in s:
    print("already patched"); sys.exit(0)
anchor = (
"FRegisteredFileHandle* FUnixFileRegistry::PlatformInitialOpenFile(const TCHAR* Filename)\n"
"{\n"
"\tFString MappedToName;\n"
)
add = (
"FRegisteredFileHandle* FUnixFileRegistry::PlatformInitialOpenFile(const TCHAR* Filename)\n"
"{\n"
"#if PLATFORM_WASM\n"
"\t// wasm: direct exact-case open first. emscripten MEMFS is case-sensitive and the\n"
"\t// case-insensitive mapper's directory scan fails on it, so OpenRead otherwise returns\n"
"\t// null for files that plainly exist (our preloaded VFS paths are exact case).\n"
"\t{\n"
"\t\tint32 DirectHandle = open(TCHAR_TO_UTF8(Filename), O_RDONLY | O_CLOEXEC);\n"
"\t\tif (DirectHandle != -1)\n"
"\t\t{\n"
"\t\t\treturn new FFileHandleUnix(DirectHandle, Filename, false);\n"
"\t\t}\n"
"\t}\n"
"#endif\n"
"\tFString MappedToName;\n"
)
if anchor not in s:
    print("anchor missing", file=sys.stderr); sys.exit(1)
s = s.replace(anchor, add, 1)
io.open(F, "w", encoding="utf-8").write(s)
print("patched FUnix PlatformInitialOpenFile: wasm direct-open fast path")
