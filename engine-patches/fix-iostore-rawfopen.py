#!/usr/bin/env python3
# GUARANTEED IoStore MEMFS read fix: UE's platform-file OpenRead/FileExists fail on
# emscripten MEMFS (NormalizeFilename + case-insensitive mapper) though raw POSIX fopen
# works (proven: toc-diag rawfopen=1, FileExists=0). Add a minimal IFileHandle over
# fopen and use it for both the .utoc (FIoStoreTocResourceStorage) and .ucas
# (FContainerFileAccess) reads on wasm.
import io, sys
F = "/mnt/vms/ss-build/UnrealEngine/Engine/Source/Runtime/Core/Private/IO/IoStore.cpp"
s = io.open(F, encoding="utf-8").read()
if "FWasmRawFileHandle" in s:
    print("already patched"); sys.exit(0)

# 1) class definition before the log category
CLS = (
"#if PLATFORM_WASM\n"
"#include <cstdio>\n"
"// emscripten MEMFS: UE platform-file OpenRead/FileExists fail (NormalizeFilename +\n"
"// case-insensitive mapper) though raw POSIX fopen works. Minimal IFileHandle over fopen.\n"
"class FWasmRawFileHandle final : public IFileHandle\n"
"{\n"
"\tFILE* Fp = nullptr;\n"
"\tint64 FileSize = 0;\n"
"public:\n"
"\texplicit FWasmRawFileHandle(const TCHAR* Path)\n"
"\t{\n"
"\t\tFp = fopen(TCHAR_TO_UTF8(Path), \"rb\");\n"
"\t\tif (Fp) { fseek(Fp, 0, SEEK_END); FileSize = (int64)ftell(Fp); fseek(Fp, 0, SEEK_SET); }\n"
"\t}\n"
"\tvirtual ~FWasmRawFileHandle() { if (Fp) fclose(Fp); }\n"
"\tbool IsValid() const { return Fp != nullptr; }\n"
"\tvirtual int64 Tell() override { return Fp ? (int64)ftell(Fp) : -1; }\n"
"\tvirtual bool Seek(int64 NewPosition) override { return Fp && fseek(Fp, (long)NewPosition, SEEK_SET) == 0; }\n"
"\tvirtual bool SeekFromEnd(int64 NewPositionRelativeToEnd = 0) override { return Fp && fseek(Fp, (long)NewPositionRelativeToEnd, SEEK_END) == 0; }\n"
"\tvirtual bool Read(uint8* Destination, int64 BytesToRead) override { return Fp && BytesToRead >= 0 && fread(Destination, 1, (size_t)BytesToRead, Fp) == (size_t)BytesToRead; }\n"
"\tvirtual bool Write(const uint8*, int64) override { return false; }\n"
"\tvirtual int64 Size() override { return FileSize; }\n"
"\tvirtual bool Flush(bool = false) override { return true; }\n"
"\tvirtual bool Truncate(int64) override { return false; }\n"
"};\n"
"#endif\n\n"
)
log_anchor = "DEFINE_LOG_CATEGORY(LogIoStore);"
if log_anchor not in s:
    print("log anchor missing", file=sys.stderr); sys.exit(1)
s = s.replace(log_anchor, CLS + log_anchor, 1)

# 2) TOC constructor
toc_old = (
"\tif (Data.IsType<FEmptyVariantState>())\n"
"\t{\n"
"\t\tFFileOpenResult Result = Ipf.OpenRead(TocFilePath, IPlatformFile::EOpenReadFlags::None);\n"
"\t\tif (Result.HasValue())\n"
"\t\t{\n"
"\t\t\tFReadBlocks Blocks;\n"
"\t\t\tBlocks.File = Result.StealValue();\n"
"\t\t\tData.Emplace<FReadBlocks>(MoveTemp(Blocks));\n"
"\t\t}\n"
"\t}\n"
)
toc_new = (
"\tif (Data.IsType<FEmptyVariantState>())\n"
"\t{\n"
"#if PLATFORM_WASM\n"
"\t\t{\n"
"\t\t\tTUniquePtr<FWasmRawFileHandle> WasmRaw = MakeUnique<FWasmRawFileHandle>(TocFilePath);\n"
"\t\t\tif (WasmRaw->IsValid())\n"
"\t\t\t{\n"
"\t\t\t\tFReadBlocks Blocks;\n"
"\t\t\t\tBlocks.File = MoveTemp(WasmRaw);\n"
"\t\t\t\tData.Emplace<FReadBlocks>(MoveTemp(Blocks));\n"
"\t\t\t}\n"
"\t\t}\n"
"\t\tif (Data.IsType<FEmptyVariantState>())\n"
"#endif\n"
"\t\t{\n"
"\t\t\tFFileOpenResult Result = Ipf.OpenRead(TocFilePath, IPlatformFile::EOpenReadFlags::None);\n"
"\t\t\tif (Result.HasValue())\n"
"\t\t\t{\n"
"\t\t\t\tFReadBlocks Blocks;\n"
"\t\t\t\tBlocks.File = Result.StealValue();\n"
"\t\t\t\tData.Emplace<FReadBlocks>(MoveTemp(Blocks));\n"
"\t\t\t}\n"
"\t\t}\n"
"\t}\n"
)
if toc_old not in s:
    print("TOC anchor missing", file=sys.stderr); sys.exit(1)
s = s.replace(toc_old, toc_new, 1)

# 3) .ucas container file access
ucas_old = (
"\t\t\t\tHandle[i] = Ipf.OpenRead(ContainerFileName);\n"
"\t\t\t\tif (Handle[i] == nullptr)\n"
)
ucas_new = (
"#if PLATFORM_WASM\n"
"\t\t\t\t{\n"
"\t\t\t\t\tFWasmRawFileHandle* WasmRaw = new FWasmRawFileHandle(ContainerFileName);\n"
"\t\t\t\t\tif (WasmRaw->IsValid()) { Handle[i] = WasmRaw; }\n"
"\t\t\t\t\telse { delete WasmRaw; Handle[i] = Ipf.OpenRead(ContainerFileName); }\n"
"\t\t\t\t}\n"
"#else\n"
"\t\t\t\tHandle[i] = Ipf.OpenRead(ContainerFileName);\n"
"#endif\n"
"\t\t\t\tif (Handle[i] == nullptr)\n"
)
if ucas_old not in s:
    print("UCAS anchor missing", file=sys.stderr); sys.exit(1)
s = s.replace(ucas_old, ucas_new, 1)

io.open(F, "w", encoding="utf-8").write(s)
print("patched IoStore.cpp: FWasmRawFileHandle for .utoc + .ucas reads")
