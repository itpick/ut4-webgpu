f = "/mnt/vms/ss-build/UnrealEngine/Engine/Source/Runtime/Core/Private/Internationalization/ICUInternationalization.cpp"
s = open(f).read()
marker = "wndr: on wasm the ICU tree IS"
if marker in s:
    print("already patched")
    raise SystemExit(0)
anchor = "\tif (ICUDataDirectory.IsEmpty())\n\t{\n\t\tauto GetPrioritizedDataDirectoriesString"
assert anchor in s, "anchor not found"
ins = (
    "#if PLATFORM_WASM\n"
    "\tif (ICUDataDirectory.IsEmpty())\n"
    "\t{\n"
    "\t\t// wndr: on wasm the ICU tree IS in the emscripten .data at /Engine/Content/Internationalization,\n"
    "\t\t// but FPaths::DirectoryExists() on the game-thread pthread cannot see the main-thread MEMFS mount.\n"
    "\t\t// ICU loads via our OpenDataFile callback (u_setDataFileFunctions), not raw FS, so force the known\n"
    "\t\t// path instead of Fatal-aborting; if the real reads also fail, u_init's checkf reports it precisely.\n"
    "\t\tconst FString WasmICUDir = TEXT(\"/Engine/Content/Internationalization/\");\n"
    "\t\tu_setDataDirectory(StringCast<char>(*WasmICUDir).Get());\n"
    "\t\tICUDataDirectory = WasmICUDir / TEXT(\"icudt64l\") / TEXT(\"\");\n"
    "\t\tFPaths::NormalizeFilename(ICUDataDirectory);\n"
    "\t\tUE_LOGF(LogICUInternationalization, Warning, \"wndr: ICU dir not found via DirectoryExists on wasm; forcing %ls\", *ICUDataDirectory);\n"
    "\t}\n"
    "#endif\n"
)
s = s.replace(anchor, ins + anchor, 1)
open(f, "w").write(s)
print("patched OK")
