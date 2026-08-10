// Standalone probe (outside UBT/UE) testing whether the ShaderConductor
// SPIRV-Tools SIGSEGV is caused by libc++ symbol interposition between
// the caller's own statically-linked libc++ copy and
// libShaderConductor.so's own statically-linked libc++ copy (confirmed
// via readelf: neither links an external libstdc++.so/libc++.so; each
// exports ~900-1200 default-visibility "std::__1::..." symbols of its
// own -- a classic setup for symbol interposition corruption when both
// images are loaded into the same global scope).
//
// Fix under test: dlopen() libShaderConductor.so (and its libdxcompiler.so
// dependency) ourselves with RTLD_DEEPBIND, so code *inside* those .so's
// (e.g. SPIRV-Tools' IRContext destructor calling
// std::__1::unordered_map::~unordered_map()) resolves its own symbol
// references against ITS OWN copy of libc++ first, instead of being
// interposed by the global/executable scope's copy. This requires the
// .so to NOT also be a compile-time (DT_NEEDED) dependency of this
// binary -- if it were, ld.so would already have loaded it in normal
// global-scope mode before main() runs, and a later dlopen() of the same
// path is a no-op that just bumps the refcount. So every single symbol
// we need from the .so (including ShaderConductor::Blob's ctor/methods)
// is resolved via dlsym below, never via the linker.
#define _GNU_SOURCE
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <dlfcn.h>
#include <functional>
#include <unistd.h>

#include "ShaderConductor.hpp"

using namespace ShaderConductor;

// ---- Raw mirror of ResultDesc's layout, so we never need the compiler
// to emit calls to Blob's ctor/dtor (which live in the .so and would
// force a compile-time link, defeating the whole point of this probe).
// Blob's only data member is `BlobImpl* m_impl` (private, no virtuals) --
// standard-layout, sizeof/alignof == sizeof(void*). Mirroring field order
// with a plain pointer reproduces the same layout/padding.
struct RawBlob { void* m_impl; };
struct RawReflectionResultDesc { RawBlob descs; uint32_t descCount; uint32_t instructionCount; };
struct RawResultDesc
{
    RawBlob target;
    bool isText;
    RawBlob errorWarningMsg;
    bool hasError;
    RawReflectionResultDesc reflection;
};

typedef void (*CompileFnSret)(void* /*sret ResultDesc*/, const Compiler::SourceDesc&, const Compiler::Options&, const Compiler::TargetDesc&);
typedef void* (*BlobCtorFn)(void* /*this*/);
typedef const void* (*BlobDataFn)(const void* /*this*/);
typedef uint32_t (*BlobSizeFn)(const void* /*this*/);

static const char* kMangledCompile = "_ZN15ShaderConductor8Compiler7CompileERKNS0_10SourceDescERKNS0_7OptionsERKNS0_10TargetDescE";
static const char* kMangledBlobCtor = "_ZN15ShaderConductor4BlobC2Ev";
static const char* kMangledBlobData = "_ZNK15ShaderConductor4Blob4DataEv";
static const char* kMangledBlobSize = "_ZNK15ShaderConductor4Blob4SizeEv";

static const char* kHLSL =
    "float4 vs_main(float3 inPos : POSITION) : SV_Position {\n"
    "  return float4(inPos, 1.0);\n"
    "}\n";

int main(int argc, char** argv)
{
    bool bDeepBind = (argc > 1 && strcmp(argv[1], "deepbind") == 0);
    printf("[probe] mode=%s (pid=%d)\n", bDeepBind ? "RTLD_DEEPBIND" : "normal(RTLD_GLOBAL)", (int)getpid());
    fflush(stdout);

    int flags = RTLD_NOW | (bDeepBind ? RTLD_DEEPBIND : RTLD_GLOBAL);

    // dxcompiler must be loadable (ShaderConductor.so depends on it); load it
    // explicitly first with the SAME flag so it's consistently bound too.
    void* hDxc = dlopen("libdxcompiler.so", flags | RTLD_GLOBAL);
    if (!hDxc) { printf("[probe] dlopen dxcompiler failed: %s\n", dlerror()); return 1; }
    printf("[probe] dlopen libdxcompiler.so OK (%p)\n", hDxc);

    void* hSc = dlopen("libShaderConductor.so", flags);
    if (!hSc) { printf("[probe] dlopen ShaderConductor failed: %s\n", dlerror()); return 1; }
    printf("[probe] dlopen libShaderConductor.so OK (%p)\n", hSc);

    CompileFnSret Compile = reinterpret_cast<CompileFnSret>(dlsym(hSc, kMangledCompile));
    BlobDataFn BlobData = reinterpret_cast<BlobDataFn>(dlsym(hSc, kMangledBlobData));
    BlobSizeFn BlobSize = reinterpret_cast<BlobSizeFn>(dlsym(hSc, kMangledBlobSize));
    if (!Compile || !BlobData || !BlobSize) { printf("[probe] dlsym failed: %s\n", dlerror()); return 1; }
    printf("[probe] dlsym OK: Compile=%p Data=%p Size=%p\n", (void*)Compile, (void*)BlobData, (void*)BlobSize);

    Compiler::SourceDesc Source = {};
    Source.source = kHLSL;
    Source.fileName = "probe.hlsl";
    Source.entryPoint = "vs_main";
    Source.stage = ShaderStage::VertexShader;

    Compiler::Options Options = {};
    Options.disableOptimizations = true;

    Compiler::TargetDesc Target = {};
    Target.language = ShadingLanguage::SpirV;

    RawResultDesc Result;
    memset(&Result, 0, sizeof(Result)); // mimics default-constructed Blobs (m_impl=nullptr) + isText/hasError=false

    printf("[probe] calling Compile (sizeof(RawResultDesc)=%zu)...\n", sizeof(RawResultDesc));
    fflush(stdout);
    Compile(&Result, Source, Options, Target);
    printf("[probe] Compile RETURNED (no crash!)\n");
    fflush(stdout);

    if (Result.hasError)
    {
        const void* ErrData = BlobData(&Result.errorWarningMsg);
        uint32_t ErrSize = BlobSize(&Result.errorWarningMsg);
        printf("[probe] ShaderConductor error: %.*s\n", (int)ErrSize, (const char*)ErrData);
        return 2;
    }

    const void* SpirvData = BlobData(&Result.target);
    uint32_t SpirvSize = BlobSize(&Result.target);
    printf("[probe] SUCCESS: %u bytes SPIR-V, magic=0x%08x\n", SpirvSize, SpirvSize >= 4 ? *(const uint32_t*)SpirvData : 0);
    return 0;
}
