#!/usr/bin/env python3
# WebGPU bring-up part 2: standalone forced the physical platform file (bypassing
# IAS + FPakPlatformFile), so AsyncLoading2 has no package store -> no cooked content
# loads. Mount our IoStore containers manually between PreInitPreStartupScreen (which
# inits FIoDispatcher) and PreInitPostStartupScreen (which loads content).
import io, sys
F = "/mnt/vms/ss-build/UnrealEngine/Engine/Platforms/SimplyStream/Source/Runtime/Launch/Private/LaunchSimplyStream.cpp"
s = io.open(F, encoding="utf-8").read()
if "SimplyStream_MountIoStoreContainers" in s:
    print("already patched"); sys.exit(0)

# 1) includes after LaunchEngineLoop.h
inc_anchor = '#include "LaunchEngineLoop.h"\n'
inc_add = (
'#if PLATFORM_WASM\n'
'#include "IO/IoDispatcher.h"\n'
'#include "IO/PackageStore.h"\n'
'#include "Misc/AES.h"\n'
'#include "FileIoDispatcherBackend.h"   // PakFile/Private (link via monolithic wasm)\n'
'#include "FilePackageStore.h"          // PakFile/Private\n'
'#endif\n'
)
if inc_anchor not in s: print("inc anchor missing", file=sys.stderr); sys.exit(1)
s = s.replace(inc_anchor, inc_anchor + inc_add, 1)

# 2) mount function before SIMPLYSTREAM_Init
fn_anchor = 'KEEP_IN_MODULE void SIMPLYSTREAM_Init() {\n'
fn_add = (
'#if PLATFORM_WASM\n'
'static void SimplyStream_MountIoStoreContainers()\n'
'{\n'
'\tif (!FIoDispatcher::IsInitialized())\n'
'\t{\n'
'\t\tfprintf(stderr, "[wndr] IoStore mount: dispatcher NOT initialized\\n");\n'
'\t\treturn;\n'
'\t}\n'
'\tFIoDispatcher& IoD = FIoDispatcher::Get();\n'
'\tstatic TSharedPtr<UE::IoStore::IFileIoDispatcherBackend> Backend = UE::IoStore::MakeFileIoDispatcherBackend();\n'
'\tIoD.Mount(Backend.ToSharedRef());\n'
'\tstatic TSharedRef<FFilePackageStoreBackend> PkgStore = MakeShared<FFilePackageStoreBackend>();\n'
'\tFPackageStore::Get().Mount(PkgStore);\n'
'\tconst TCHAR* Tocs[] = {\n'
'\t\tTEXT("../../../UnrealTournament/Content/Paks/global.utoc"),\n'
'\t\tTEXT("../../../UnrealTournament/Content/Paks/utcontent.utoc"),\n'
'\t};\n'
'\tfor (const TCHAR* Toc : Tocs)\n'
'\t{\n'
'\t\tTIoStatusOr<FIoContainerHeader> St = Backend->Mount(Toc, 0, FGuid(), FAES::FAESKey());\n'
'\t\tif (St.IsOk())\n'
'\t\t{\n'
'\t\t\tFIoContainerHeader* Hdr = new FIoContainerHeader(St.ConsumeValueOrDie());\n'
'\t\t\tPkgStore->Mount(Hdr, 0);\n'
'\t\t\tfprintf(stderr, "[wndr] mounted IoStore container %ls\\n", Toc);\n'
'\t\t}\n'
'\t\telse\n'
'\t\t{\n'
'\t\t\tfprintf(stderr, "[wndr] IoStore mount FAILED %ls\\n", Toc);\n'
'\t\t}\n'
'\t}\n'
'}\n'
'#endif\n\n'
)
if fn_anchor not in s: print("fn anchor missing", file=sys.stderr); sys.exit(1)
s = s.replace(fn_anchor, fn_add + fn_anchor, 1)

# 3) split PreInit with the mount between phases
pre_anchor = '\tGEngineLoop.PreInit(0, NULL, GCmdLine);\n'
pre_add = (
'#if PLATFORM_WASM\n'
'\tif (GEngineLoop.PreInitPreStartupScreen(GCmdLine) == 0)\n'
'\t{\n'
'\t\tSimplyStream_MountIoStoreContainers();\n'
'\t\tGEngineLoop.PreInitPostStartupScreen(GCmdLine);\n'
'\t}\n'
'#else\n'
'\tGEngineLoop.PreInit(0, NULL, GCmdLine);\n'
'#endif\n'
)
if pre_anchor not in s: print("preinit anchor missing", file=sys.stderr); sys.exit(1)
s = s.replace(pre_anchor, pre_add, 1)

io.open(F, "w", encoding="utf-8").write(s)
print("patched LaunchSimplyStream.cpp: IoStore container mount + PreInit split")
