#!/usr/bin/env python3
# TOC OpenRead fails because PreInit likely re-wrapped the platform file with IAS
# PathPak (fails without a platform server). Force the physical (FUnix/POSIX) file
# for the container reads, and log the platform file type to confirm.
import io, sys
F = "/mnt/vms/ss-build/UnrealEngine/Engine/Platforms/SimplyStream/Source/Runtime/Launch/Private/LaunchSimplyStream.cpp"
s = io.open(F, encoding="utf-8").read()
if "IoStore mount: forcing physical" in s:
    print("already patched"); sys.exit(0)
anchor = (
'\tif (!FIoDispatcher::IsInitialized())\n'
'\t{\n'
'\t\tUE_LOG(LogInit, Error, TEXT("[wndr] IoStore mount: FIoDispatcher NOT initialized"));\n'
'\t\treturn;\n'
'\t}\n'
)
add = (
'\tUE_LOG(LogInit, Display, TEXT("[wndr] IoStore mount: platform file BEFORE force = %s"), FPlatformFileManager::Get().GetPlatformFile().GetName());\n'
'\t// PreInit may have re-wrapped the platform file with IAS PathPak (OpenRead fails\n'
'\t// with no platform server). Force the physical POSIX file so container reads hit MEMFS.\n'
'\tUE_LOG(LogInit, Display, TEXT("[wndr] IoStore mount: forcing physical platform file"));\n'
'\tFPlatformFileManager::Get().SetPlatformFile(IPlatformFile::GetPlatformPhysical());\n'
)
if anchor not in s:
    print("anchor missing", file=sys.stderr); sys.exit(1)
s = s.replace(anchor, anchor + add, 1)
io.open(F, "w", encoding="utf-8").write(s)
print("patched: force physical + log platform file")
