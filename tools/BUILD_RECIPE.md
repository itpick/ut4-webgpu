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

Compiles and links cleanly (all `ReadIR`/`WgslFromIR`/`ProgramFromIR`/
`Generate` symbol names+signatures match the vendored `libtint.a` exactly
— confirmed via `nm -D`/`nm`). **Currently fails at runtime**: `ReadIR`
throws/crashes with `bad_variant_access` on every input tried, including
a minimal hand-assembled (`spirv-as`), `spirv-val`-clean, textbook-valid
SPIR-V module with zero HLSL/DXC-specific content — see HANDOFF.md's
"Tint wall" section for the full ruled-out list and next-step
recommendation (build `libtint.a` ourselves from this same fetched source
rather than depending on SimplyStream's vendored prebuilt one, to test
whether it's a headers-vs-prebuilt-binary ABI mismatch).
