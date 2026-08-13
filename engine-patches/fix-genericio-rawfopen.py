#!/usr/bin/env python3
# The platform IoDispatcher opens the .ucas via Ipf.FileSize + Ipf.OpenReadNoBuffering,
# which use the case-insensitive file mapper that HANGS on emscripten MEMFS. This is
# where backend->Mount deadlocks. Use raw fopen for both on wasm.
import io, sys
F = "/mnt/vms/ss-build/UnrealEngine/Engine/Source/Runtime/Core/Internal/IO/GenericPlatformIoDispatcher.cpp"
s = io.open(F, encoding="utf-8").read()
if "FWasmRawIoFileHandle" in s:
    print("already patched"); sys.exit(0)

CLS = (
"#if PLATFORM_WASM\n"
"#include <cstdio>\n"
"// emscripten MEMFS: UE platform-file FileSize/OpenReadNoBuffering hang in the\n"
"// case-insensitive mapper though raw POSIX fopen works.\n"
"class FWasmRawIoFileHandle final : public IFileHandle\n"
"{\n"
"\tFILE* Fp = nullptr; int64 Sz = 0;\n"
"public:\n"
"\texplicit FWasmRawIoFileHandle(const TCHAR* P) { Fp = fopen(TCHAR_TO_UTF8(P), \"rb\"); if (Fp) { fseek(Fp, 0, SEEK_END); Sz = (int64)ftell(Fp); fseek(Fp, 0, SEEK_SET); } }\n"
"\tvirtual ~FWasmRawIoFileHandle() { if (Fp) fclose(Fp); }\n"
"\tbool IsValid() const { return Fp != nullptr; }\n"
"\tvirtual int64 Tell() override { return Fp ? (int64)ftell(Fp) : -1; }\n"
"\tvirtual bool Seek(int64 P) override { return Fp && fseek(Fp, (long)P, SEEK_SET) == 0; }\n"
"\tvirtual bool SeekFromEnd(int64 P = 0) override { return Fp && fseek(Fp, (long)P, SEEK_END) == 0; }\n"
"\tvirtual bool Read(uint8* D, int64 N) override { return Fp && N >= 0 && fread(D, 1, (size_t)N, Fp) == (size_t)N; }\n"
"\tvirtual bool ReadAt(uint8* D, int64 N, int64 O) override { return Fp && N >= 0 && fseek(Fp, (long)O, SEEK_SET) == 0 && fread(D, 1, (size_t)N, Fp) == (size_t)N; }\n"
"\tvirtual bool Write(const uint8*, int64) override { return false; }\n"
"\tvirtual bool Flush(bool = false) override { return true; }\n"
"\tvirtual int64 Size() override { return Sz; }\n"
"\tvirtual bool Truncate(int64) override { return false; }\n"
"\tstatic int64 RawSize(const TCHAR* P) { FILE* f = fopen(TCHAR_TO_UTF8(P), \"rb\"); if (!f) return -1; fseek(f, 0, SEEK_END); int64 n = (int64)ftell(f); fclose(f); return n; }\n"
"};\n"
"#endif\n\n"
)
anchor = "TIoStatusOr<FIoFileHandle> FGenericPlatformIoDispatcher::OpenFile("
if anchor not in s:
    print("open anchor missing", file=sys.stderr); sys.exit(1)
s = s.replace(anchor, CLS + anchor, 1)

# FileSize
fs_old = "\tFileHandle->FileSize				= Ipf.FileSize(Filename);\n"
fs_new = (
"#if PLATFORM_WASM\n"
"\tFileHandle->FileSize				= FWasmRawIoFileHandle::RawSize(Filename);\n"
"#else\n"
"\tFileHandle->FileSize				= Ipf.FileSize(Filename);\n"
"#endif\n"
)
if fs_old not in s:
    print("filesize anchor missing", file=sys.stderr); sys.exit(1)
s = s.replace(fs_old, fs_new, 1)

# OpenReadNoBuffering
op_old = "\tFileHandle->Handle.Reset(Ipf.OpenReadNoBuffering(Filename));\n"
op_new = (
"#if PLATFORM_WASM\n"
"\t{ FWasmRawIoFileHandle* R = new FWasmRawIoFileHandle(Filename); if (R->IsValid()) FileHandle->Handle.Reset(R); else { delete R; FileHandle->Handle.Reset(Ipf.OpenReadNoBuffering(Filename)); } }\n"
"#else\n"
"\tFileHandle->Handle.Reset(Ipf.OpenReadNoBuffering(Filename));\n"
"#endif\n"
)
if op_old not in s:
    print("openreadnobuf anchor missing", file=sys.stderr); sys.exit(1)
s = s.replace(op_old, op_new, 1)

io.open(F, "w", encoding="utf-8").write(s)
print("patched GenericPlatformIoDispatcher.cpp: raw fopen FileSize + OpenReadNoBuffering")
