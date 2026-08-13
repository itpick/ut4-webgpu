#!/usr/bin/env python3
# The IoStore dispatcher backend (PakFile/Private/FileIoDispatcherBackend.cpp) opens
# the .ucas via Ipf.OpenRead which fails on emscripten MEMFS (same bug as IoStore.cpp).
# That's why backend->Mount hangs after the raw-fopen TOC read. Use FWasmRawFileHandle
# (raw fopen) for the .ucas partition opens here too.
import io, sys
F = "/mnt/vms/ss-build/UnrealEngine/Engine/Source/Runtime/PakFile/Private/FileIoDispatcherBackend.cpp"
s = io.open(F, encoding="utf-8").read()
if "FWasmRawFileHandle" in s:
    print("already patched"); sys.exit(0)

CLS = (
"#if PLATFORM_WASM\n"
"#include <cstdio>\n"
"// emscripten MEMFS: UE platform-file OpenRead fails though raw POSIX fopen works.\n"
"class FWasmRawFileHandle final : public IFileHandle\n"
"{\n"
"\tFILE* Fp = nullptr; int64 FileSize = 0;\n"
"public:\n"
"\texplicit FWasmRawFileHandle(const TCHAR* Path) { Fp = fopen(TCHAR_TO_UTF8(Path), \"rb\"); if (Fp) { fseek(Fp, 0, SEEK_END); FileSize = (int64)ftell(Fp); fseek(Fp, 0, SEEK_SET); } }\n"
"\tvirtual ~FWasmRawFileHandle() { if (Fp) fclose(Fp); }\n"
"\tbool IsValid() const { return Fp != nullptr; }\n"
"\tvirtual int64 Tell() override { return Fp ? (int64)ftell(Fp) : -1; }\n"
"\tvirtual bool Seek(int64 P) override { return Fp && fseek(Fp, (long)P, SEEK_SET) == 0; }\n"
"\tvirtual bool SeekFromEnd(int64 P = 0) override { return Fp && fseek(Fp, (long)P, SEEK_END) == 0; }\n"
"\tvirtual bool Read(uint8* Dst, int64 N) override { return Fp && N >= 0 && fread(Dst, 1, (size_t)N, Fp) == (size_t)N; }\n"
"\tvirtual bool ReadAt(uint8* Dst, int64 N, int64 Off) override { return Fp && N >= 0 && fseek(Fp, (long)Off, SEEK_SET) == 0 && fread(Dst, 1, (size_t)N, Fp) == (size_t)N; }\n"
"\tvirtual bool Write(const uint8*, int64) override { return false; }\n"
"\tvirtual bool Flush(bool = false) override { return true; }\n"
"\tvirtual int64 Size() override { return FileSize; }\n"
"\tvirtual bool Truncate(int64) override { return false; }\n"
"};\n"
"#endif\n\n"
)
log_anchor = "DEFINE_LOG_CATEGORY_STATIC(LogIoDispatcherFileBackend"
if log_anchor in s:
    s = s.replace(log_anchor, CLS + log_anchor, 1)
else:
    # fallback: after the first namespace or the FMappedFileProxy class
    a = "class FMappedFileProxy"
    if a not in s: print("no insertion anchor", file=sys.stderr); sys.exit(1)
    s = s.replace(a, CLS + a, 1)

# patch every "Ipf.OpenRead(*Partition.Filename)" pattern -> wasm raw handle first
old = "\t\tTUniquePtr<IFileHandle> FileHandle(Ipf.OpenRead(*Partition.Filename));\n"
new = (
"\t\tTUniquePtr<IFileHandle> FileHandle;\n"
"#if PLATFORM_WASM\n"
"\t\t{ FWasmRawFileHandle* WasmRaw = new FWasmRawFileHandle(*Partition.Filename); if (WasmRaw->IsValid()) FileHandle.Reset(WasmRaw); else delete WasmRaw; }\n"
"\t\tif (!FileHandle.IsValid())\n"
"#endif\n"
"\t\tFileHandle.Reset(Ipf.OpenRead(*Partition.Filename));\n"
)
cnt = s.count(old)
s = s.replace(old, new)
io.open(F, "w", encoding="utf-8").write(s)
print(f"patched FileIoDispatcherBackend.cpp: FWasmRawFileHandle + {cnt} partition open(s)")
