p="/mnt/vms/ss-build/UnrealEngine/Engine/Source/Runtime/Core/Private/Unix/UnixPlatformFile.cpp"
s=open(p).read()
old="""	FPaths::NormalizeFilename(Result);
	return FPaths::ConvertRelativePathToFull(Result);
}"""
new="""	FPaths::NormalizeFilename(Result);
#if PLATFORM_WASM
	// wasm/MEMFS: the install root is \"/\" and binaries live at /Engine/Binaries/SimplyStream.
	// Resolve UE\x27s ../../../-style relative paths against that base so they map onto the mounted
	// /Engine and /UnrealTournament trees (emscripten cwd is not the install root).
	return FPaths::ConvertRelativePathToFull(TEXT(\"/Engine/Binaries/SimplyStream\"), Result);
#else
	return FPaths::ConvertRelativePathToFull(Result);
#endif
}"""
assert old in s, "NormalizeFilename body not found"
s=s.replace(old,new,1)
open(p,"w").write(s)
print("patched NormalizeFilename for wasm absolutization")
