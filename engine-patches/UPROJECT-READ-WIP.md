# WIP: get the .uproject to load in the UT wasm PreInit

Engine-tree edits (reproducible via the .py scripts here; engine tree has no .git):
- SimplyStreamToolChain.cs: added `--preload-file .../UnrealTournament.uproject@UnrealTournament/UnrealTournament.uproject`
  (verified mounted at /UnrealTournament/UnrealTournament.uproject in the .data manifest).
- UnixPlatformFile.cpp (PLATFORM_WASM):
  - relaxed the `Filename[0] != /` guards in OpenCaseInsensitiveRead + MapCaseInsensitiveFile (allow relative).
  - NormalizeFilename: on wasm, ConvertRelativePathToFull(base="/Engine/Binaries/SimplyStream", Result)
    so ../../../UnrealTournament/... -> /UnrealTournament/... (matches the mount).

STATUS: .uproject still "Failed to open descriptor file" in PreInit. Diagnosing whether the read
reaches FUnixPlatformFile::OpenRead at all (prior diag used LowLevelOutputDebugStringf which is
NOT captured on the game-thread worker; re-diagnosing with UE_LOG).
