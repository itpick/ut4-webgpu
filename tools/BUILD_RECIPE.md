# Standalone diagnostic probes (outside UBT)

Two small, self-contained repros used to root-cause the shader-cook-path
walls documented in HANDOFF.md. Neither touches SimplyStream code; both
link only Epic's open ShaderConductor and Google's open Dawn/Tint.

## sc_deepbind_probe.cpp — proves the ShaderConductor SIGSEGV fix

Compiles a trivial HLSL shader to SPIR-V via `ShaderConductor::Compiler::Compile`,
loaded with `dlopen(RTLD_DEEPBIND)` instead of a compile-time link. Takes
`normal` or `deepbind` as argv[1] to compare both dlopen modes (both work;
the crash only reproduces under a *compile-time* `DT_NEEDED` link, which
this probe deliberately never does — see DawnShaderConductorLoader.h for
the full writeup).

```sh
TC=$UE/Engine/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64/v26_clang-20.1.8-rockylinux8/x86_64-unknown-linux-gnu
SCINC=$UE/Engine/Source/ThirdParty/ShaderConductor/ShaderConductor/Include
SCBIN=$UE/Engine/Binaries/ThirdParty/ShaderConductor/Linux/x86_64-unknown-linux-gnu

$TC/bin/clang++ -std=c++17 --sysroot="$TC" -nostdinc++ -isystem "$TC/include/c++/v1" \
  -I"$SCINC" -I"$SCINC/ShaderConductor" \
  -o sc_deepbind_probe sc_deepbind_probe.cpp -ldl -lc++ -lc++abi -lpthread

LD_LIBRARY_PATH=$SCBIN ./sc_deepbind_probe deepbind
```

## tint_probe.cpp — SPIR-V -> WGSL via Tint's IR reader/writer

Reads a `.spv` file, calls `tint::spirv::reader::ReadIR` then
`tint::wgsl::writer::WgslFromIR`, prints the resulting WGSL. Links
directly against the vendored `libtint.a`/`libSPIRV-Tools.a`
(`Engine/Platforms/SimplyStream/Source/ThirdParty/Dawn/lib/linux/`) —
this is Google's open Tint/Dawn, reused in place per project instructions
(same static libs anyone building against Dawn would ship).

**Headers are NOT vendored in that tree** (only the public `webgpu.h`-style
API is) — `tint::spirv::reader::ReadIR`/`tint::wgsl::writer::WgslFromIR`
live in Tint's internal `src/tint/...` layout, which must be fetched
separately. The exact revision is pinned in
`Dawn/include/dawn/common/Version_autogen.h`'s `kDawnVersion`:
**`3c82ef2b508a29f96ac31731d27dffb86f39efd0`**. `fetch_tint_deps.sh` in
this dir fetches the matching submodule revisions (spirv-headers,
spirv-tools, abseil-cpp — read straight from `.gitmodules`/`git ls-tree`
gitlinks, so always exactly right for whatever commit you check out).

```sh
mkdir -p /tmp/tint-src-fetch/dawn && cd /tmp/tint-src-fetch/dawn
git init -q && git remote add origin https://dawn.googlesource.com/dawn
git fetch --depth 1 origin 3c82ef2b508a29f96ac31731d27dffb86f39efd0
git checkout -q FETCH_HEAD
bash fetch_tint_deps.sh   # fetches third_party/{spirv-headers,spirv-tools,abseil-cpp} at their pinned SHAs

TC=$UE/Engine/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64/v26_clang-20.1.8-rockylinux8/x86_64-unknown-linux-gnu
DAWN=$UE/Engine/Platforms/SimplyStream/Source/ThirdParty/Dawn/lib/linux
SRC=/tmp/tint-src-fetch/dawn

$TC/bin/clang++ -std=c++20 --sysroot="$TC" -nostdinc++ -isystem "$TC/include/c++/v1" \
  -I"$SRC" -I"$SRC/third_party/abseil-cpp" -I"$SRC/third_party/spirv-headers/src/include" \
  -I"$SRC/third_party/spirv-tools/src/include" -c tint_probe.cpp -o tint_probe.o

$TC/bin/clang++ --sysroot="$TC" -nostdinc++ -isystem "$TC/include/c++/v1" \
  -o tint_probe tint_probe.o \
  -Wl,--start-group "$DAWN/libtint.a" "$DAWN/libSPIRV-Tools.a" -Wl,--end-group \
  -lc++ -lc++abi -lpthread -ldl

./tint_probe some.spv
```

Compiles and links cleanly against the vendored prebuilt `libtint.a` (all
`ReadIR`/`WgslFromIR`/`ProgramFromIR`/`Generate` symbol names+signatures
match exactly — confirmed via `nm -D`/`nm`), but **crashes at runtime**
against that prebuilt binary: `ReadIR` throws/crashes with
`bad_variant_access` on every input tried, including a minimal
hand-assembled (`spirv-as`), `spirv-val`-clean, textbook-valid SPIR-V
module with zero HLSL/DXC-specific content. **Root-caused and fixed**:
the vendored prebuilt `libtint.a` itself is ABI-mismatched (likely a
different build configuration than the pristine fetched source, despite
matching the exact pinned commit) — self-building `libtint.a` from the
identical fetched source (see the next section) makes `tint_probe` work
perfectly, no crash, clean WGSL output. Full story in HANDOFF.md's "Tint"
section.

## Self-building Tint (works — use this, not the vendored prebuilt libtint.a)

```sh
mkdir -p /tmp/tint-build && cd /tmp/tint-build
TC=$UE/Engine/Extras/ThirdPartyNotUE/SDKs/HostLinux/Linux_x64/v26_clang-20.1.8-rockylinux8/x86_64-unknown-linux-gnu
SRC=/tmp/tint-src-fetch/dawn   # from the fetch recipe above

nix-shell -p cmake ninja "python3.withPackages (ps: [ps.jinja2])" --run "
cmake -GNinja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=$TC/bin/clang -DCMAKE_CXX_COMPILER=$TC/bin/clang++ \
  -DCMAKE_C_FLAGS='--sysroot=$TC' \
  -DCMAKE_CXX_FLAGS='--sysroot=$TC -nostdinc++ -isystem $TC/include/c++/v1' \
  -DCMAKE_EXE_LINKER_FLAGS='--sysroot=$TC' -DCMAKE_SHARED_LINKER_FLAGS='--sysroot=$TC' \
  -DDAWN_ENABLE_VULKAN=OFF -DDAWN_ENABLE_D3D11=OFF -DDAWN_ENABLE_D3D12=OFF -DDAWN_ENABLE_METAL=OFF \
  -DDAWN_ENABLE_NULL=OFF -DDAWN_ENABLE_DESKTOP_GL=OFF -DDAWN_ENABLE_OPENGLES=OFF \
  -DDAWN_USE_X11=OFF -DDAWN_USE_WAYLAND=OFF -DDAWN_USE_GLFW=OFF -DDAWN_USE_WINDOWS_UI=OFF \
  -DDAWN_BUILD_SAMPLES=OFF -DDAWN_BUILD_TESTS=OFF -DDAWN_BUILD_PROTOBUF=OFF -DDAWN_BUILD_NODE_BINDINGS=OFF \
  -DTINT_BUILD_TESTS=OFF -DTINT_BUILD_CMD_TOOLS=OFF -DTINT_BUILD_IR_BINARY=OFF \
  -DTINT_BUILD_SPV_READER=ON -DTINT_BUILD_WGSL_WRITER=ON -DTINT_BUILD_WGSL_READER=ON \
  -DTINT_BUILD_SPV_WRITER=OFF -DTINT_BUILD_GLSL_WRITER=OFF -DTINT_BUILD_HLSL_WRITER=OFF -DTINT_BUILD_MSL_WRITER=OFF \
  -DTINT_BUILD_GLSL_VALIDATOR=OFF \
  \"$SRC\"
ninja -j 12 libtint_lang_spirv_reader.a libtint_lang_wgsl_writer.a
"
```

Builds ~130 fine-grained static libs under `/tmp/tint-build` (Tint's CMake
splits every `lang/...` subdir into its own `.a` — there is no single
combined `libtint.a` target; the vendored one is presumably `ar`-merged
from all of these by whatever built it). Link against all of them with
`-Wl,--start-group $(find /tmp/tint-build -name '*.a') -Wl,--end-group`
(see `link_tint_probe.sh`-style pattern in `build_hlsl_to_wgsl.sh`).
Configure takes ~5s, the two targeted libs + their ~90 transitive deps
(mostly Abseil) build in a few minutes on 12 cores.

## hlsl_to_wgsl.cpp — the full offline cook tool (the milestone)

`usage: hlsl_to_wgsl <in.hlsl> <entryPoint> <vs|ps> <out.wgsl>`

Chains everything above into one tool: HLSL -> `ShaderConductor::Compiler::Compile`
(dlopen/RTLD_DEEPBIND, `-fspv-target-env=vulkan1.1`) -> SPIR-V ->
`spvtools::Optimizer` (`RegisterLegalizationPasses()` +
`CreateStripReflectInfoPass()`, self-built SPIRV-Tools — strips the
`SPV_GOOGLE_hlsl_functionality1` extension ShaderConductor's UE fork
unconditionally emits via a hardcoded `-fspv-reflect`, which Tint's
reader otherwise cleanly rejects with "extension ... is not supported")
-> `tint::spirv::reader::ReadIR` -> `tint::wgsl::writer::WgslFromIR`
(self-built Tint) -> WGSL text file. Build with `build_hlsl_to_wgsl.sh`
(needs the self-built Tint libs above + `$LD_LIBRARY_PATH` pointing at
`Engine/Binaries/ThirdParty/ShaderConductor/Linux/x86_64-unknown-linux-gnu`
for the ShaderConductor `.so`s it dlopens at runtime).

`shaders/scene_vs.hlsl`/`scene_ps.hlsl` are the real HLSL sources for
DawnRHITest's Stage-2 scene shaders (matching `GVertexWGSL`/`GPixelWGSL`'s
old hand-authored semantics — MVP-transformed textured quad), with
explicit `[[vk::binding(N, 0)]]` attributes to land on DawnRHI's fixed
`@group(0){0=uniform,1=texture,2=sampler}` convention.
`shaders/scene_vs.wgsl`/`scene_ps.wgsl` are this tool's actual cooked
output for them — the exact text now embedded (verbatim, never
hand-edited) as `GVertexWGSL`/`GPixelWGSL` in `DawnRHITestMain.cpp`,
replacing the old hand-authored WGSL. Verified rendering through the real
`FDawnDynamicRHI` produces an identical `Center pixel = (30,60,200,255)`
readback to the original hand-authored-WGSL baseline (see HANDOFF.md).
