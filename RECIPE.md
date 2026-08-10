# RECIPE — UT4 → WebGPU wasm with stock open-source Emscripten

Reproducible steps to build a UT4 (UE5.8) WebGPU `.wasm` that boots + initializes WebGPU in a browser, using **stock upstream Emscripten** — no vendor-patched SDK. Verified on Linux (x86-64).

## 0. Prerequisites

- Access to the UE5.8 WebGPU engine fork (gated by Epic's own GitHub access — the standard Epic↔GitHub link; no separate vendor account needed for the *engine source*).
- The UT4 game source (UE5.8 port) as a project pointing at that engine.
- Stock Emscripten (`emscripten-core/emsdk`, `./emsdk install latest && ./emsdk activate latest`). No patched emsdk.
- ~150 GB disk; a machine with real RAM for the link.

## 1. Clone + set up the engine

```sh
git clone -b 5.8 <engine-fork> UnrealEngine && cd UnrealEngine
./Setup.sh                 # GitDependencies — pulls the precompiled WebGPU platform objects + Dawn/Tint host libs
./GenerateProjectFiles.sh
```

`Setup.sh` provisions the precompiled WebGPU RHI/ShaderFormat *objects*, Dawn/Tint host libs, and the CLI — everything except the WGSL shader-format module core (see README) and (historically) the emsdk. Point stock emsdk at `Engine/Platforms/<WebGPUPlatform>/emsdk` (its layout `emsdk/upstream/emscripten/emcc` matches what UBT expects).

## 2. Toolchain patches (the load-bearing fixes)

In the platform toolchain (`*ToolChain.cs`):
- **`-Wno-unused-template` + `-Wno-error`** — absorbs the stock-clang-24 vs vendor-clang-23 warning-as-error delta. This is the *only* engine compile barrier; all ~440 engine modules then compile with 0 errors.
- **`-sDEFAULT_TO_CXX`** on the link — **the key fix.** Stock emcc 6.x drives the link in C mode and links *no* libc++ under `-fno-exceptions + -sMEMORY64`; this flag auto-links (and auto-builds) the correct `libc++-mt-noexcept` wasm64 variant.

In the platform engine ini: set the memory mode to **wasm64** (`Memory64_Emulated` / `-sMEMORY64=2`) so the engine objects match the precompiled platform objects (which are wasm64).

## 3. Build wasm third-party + inject platform objects

```sh
# rebuild the wasm third-party .a libs as wasm64 with stock emcc
Engine/Platforms/<WebGPUPlatform>/Build/BatchFiles/Build_*_Wasm_ThirdParty.sh --memory-mode 2
#   (warm the emscripten zlib port once: `embuilder build zlib`)
#   rc=4 "archive hash mismatch" is a reproducibility checksum, NOT a failure — the libs build fine.
```

Inject the shipped precompiled platform objects (WebGPU RHI, `emdawn`, JS glue, `wgsl_pack`, etc.) into the link via the launch module's `PublicAdditionalLibraries` (glob each module dir's `*.o` — note names like `wgsl_pack.cpp.o`, not `Module.wgsl_pack.cpp.o`). This resolves the WebGPU symbols + the JS-glue globals (`canvas_*`, `runningNode`, `project_name`, `GTotalMemoryAvailable`). The remaining runtime import `__emscripten_atomics_sleep` is provided by the generated `.js`.

## 4. Build the target

Bare engine (proves the toolchain):
```sh
Engine/Build/BatchFiles/RunUBT.sh UnrealGame <WebGPUPlatform> Development
```

**UT4 game** — first add this to the UT4 `.Target.cs`:
```csharp
bUsePCHFiles = true;   // UT4 is UE4-era, not IWYU-clean; needs SharedPCH.Engine (the platform target defaults this off)
```
then:
```sh
Engine/Build/BatchFiles/RunUBT.sh UnrealTournament <WebGPUPlatform> Development
```
Output: `Engine/Binaries/<WebGPUPlatform>/UnrealTournament.{wasm,js,html}` (~178 MB wasm, memory64, no unresolved imports).

## 5. Run it

```sh
python3 host/serve.py 8800 <bundle-dir>
# open the bundle .html (or host/loader-shim.html) at http://127.0.0.1:8800 in a WebGPU browser
```
Expect: `crossOriginIsolated=true`, `SharedArrayBuffer=true`, `navigator.gpu=true`, `requestAdapter/requestDevice OK`, `wasm instantiated OK`, engine C++ logging. Content-less builds exit `status 1` (no map) — that's the shader-format gate, not a failure.

## The remaining gate — a playable map

Cooking a map needs the host `WebGPUShaderFormat` (shaders → WGSL). On UE5.8-Linux the module core + SPIR-V→WGSL emitter are provisioned-only (never committed on any branch); the libs (ShaderConductor/DXC, Tint, SPIRV-Reflect) are present. Everything *else* for the cook works: the UT4 editor builds, runtime libs are handled (nix-ld on NixOS), and the cook runs to shader compilation. To get a playable map: obtain the provisioned shader-format, or author a from-scratch one matching the closed WebGPU RHI's shader/binding contract.
