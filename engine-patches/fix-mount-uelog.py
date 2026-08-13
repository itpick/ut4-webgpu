#!/usr/bin/env python3
# Switch mount logging from fprintf (%ls broken on wasm 4-byte wchar) to UE_LOG.
import io, sys
F = "/mnt/vms/ss-build/UnrealEngine/Engine/Platforms/SimplyStream/Source/Runtime/Launch/Private/LaunchSimplyStream.cpp"
s = io.open(F, encoding="utf-8").read()
repls = [
 ('fprintf(stderr, "[wndr] IoStore mount: dispatcher NOT initialized\\n");',
  'UE_LOG(LogInit, Error, TEXT("[wndr] IoStore mount: FIoDispatcher NOT initialized"));'),
 ('fprintf(stderr, "[wndr] mounted IoStore container %ls\\n", Toc);',
  'UE_LOG(LogInit, Display, TEXT("[wndr] mounted IoStore container %s"), Toc);'),
 ('fprintf(stderr, "[wndr] IoStore mount FAILED %ls :: %s\\n", Toc, TCHAR_TO_UTF8(*St.Status().ToString()));',
  'UE_LOG(LogInit, Error, TEXT("[wndr] IoStore mount FAILED %s :: %s"), Toc, *St.Status().ToString());'),
]
n = 0
for old, new in repls:
    if old in s:
        s = s.replace(old, new, 1); n += 1
    else:
        print(f"MISS: {old[:50]}", file=sys.stderr)
io.open(F, "w", encoding="utf-8").write(s)
print(f"replaced {n}/3 log calls with UE_LOG")
