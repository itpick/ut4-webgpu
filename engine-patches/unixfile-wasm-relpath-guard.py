p="/mnt/vms/ss-build/UnrealEngine/Engine/Source/Runtime/Core/Private/Unix/UnixPlatformFile.cpp"
s=open(p).read()
import re
n=0
# Relax: "X.IsEmpty() || X[0] != TEXT(chr47)" -> keep only IsEmpty on wasm.
for var in ["Filename","PossiblyWrongFilename"]:
    old="%s.IsEmpty() || %s[0] != TEXT(%s/%s)" % (var,var,chr(39),chr(39))
    # C wants TEXT(\x27/\x27); build it precisely:
    old="%s.IsEmpty() || %s[0] != TEXT(\x27/\x27)" % (var,var)
    new="%s.IsEmpty() /* wndr wasm: allow relative paths, emscripten resolves vs cwd */ || (!PLATFORM_WASM && %s[0] != TEXT(\x27/\x27))" % (var,var)
    if old in s:
        s=s.replace(old,new); n+=1; print("patched guard for",var)
    else:
        print("NOT FOUND for",var)
open(p,"w").write(s)
print("total patched:",n)
