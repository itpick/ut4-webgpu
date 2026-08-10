// Offline cook tool: HLSL -> SPIR-V (Epic's open ShaderConductor, dlopen
// isolated) -> legalized/stripped SPIR-V (Google's open SPIRV-Tools
// Optimizer, self-built) -> WGSL (Google's open Tint IR reader/writer,
// self-built). Not SimplyStream code anywhere in this chain.
//
// Usage: hlsl_to_wgsl <in.hlsl> <entryPoint> <vs|ps> <out.wgsl>
#define _GNU_SOURCE
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <dlfcn.h>

#include "ShaderConductor.hpp"
#include "spirv-tools/optimizer.hpp"
#include "src/tint/lang/spirv/reader/reader.h"
#include "src/tint/lang/wgsl/writer/writer.h"
#include "src/tint/lang/core/ir/module.h"

#ifndef RTLD_DEEPBIND
#define RTLD_DEEPBIND 0x00008
#endif

// ---- ShaderConductor dlopen wrapper (see DawnShaderConductorLoader.h for full rationale) ----
namespace sc_loader
{
    struct RawBlob { void* m_impl; };
    struct RawReflectionResultDesc { RawBlob descs; uint32_t descCount; uint32_t instructionCount; };
    struct RawResultDesc { RawBlob target; bool isText; RawBlob errorWarningMsg; bool hasError; RawReflectionResultDesc reflection; };
    typedef void (*CompileFnSret)(void*, const ShaderConductor::Compiler::SourceDesc&, const ShaderConductor::Compiler::Options&, const ShaderConductor::Compiler::TargetDesc&);
    typedef const void* (*BlobDataFn)(const void*);
    typedef uint32_t (*BlobSizeFn)(const void*);

    bool CompileHlslToSpirv(const char* hlsl, const char* entry, ShaderConductor::ShaderStage stage, std::vector<uint32_t>& outSpirv, std::string& outError)
    {
        void* hDxc = dlopen("libdxcompiler.so", RTLD_NOW | RTLD_DEEPBIND | RTLD_GLOBAL);
        void* hSc = dlopen("libShaderConductor.so", RTLD_NOW | RTLD_DEEPBIND);
        if (!hDxc || !hSc) { outError = std::string("dlopen failed: ") + dlerror(); return false; }

        auto Compile = reinterpret_cast<CompileFnSret>(dlsym(hSc, "_ZN15ShaderConductor8Compiler7CompileERKNS0_10SourceDescERKNS0_7OptionsERKNS0_10TargetDescE"));
        auto BlobData = reinterpret_cast<BlobDataFn>(dlsym(hSc, "_ZNK15ShaderConductor4Blob4DataEv"));
        auto BlobSize = reinterpret_cast<BlobSizeFn>(dlsym(hSc, "_ZNK15ShaderConductor4Blob4SizeEv"));
        if (!Compile || !BlobData || !BlobSize) { outError = std::string("dlsym failed: ") + dlerror(); return false; }

        ShaderConductor::Compiler::SourceDesc Source = {};
        Source.source = hlsl;
        Source.fileName = "cooked.hlsl";
        Source.entryPoint = entry;
        Source.stage = stage;

        ShaderConductor::Compiler::Options Options = {};
        Options.disableOptimizations = true; // sidesteps SPIRV-Tools opt inside ShaderConductor's own isolated .so (unrelated to our own Optimizer pass below)
        static const char* extraArgs[] = { "-fspv-target-env=vulkan1.1" };
        Options.numDXCArgs = 1;
        Options.DXCArgs = extraArgs;

        ShaderConductor::Compiler::TargetDesc Target = {};
        Target.language = ShaderConductor::ShadingLanguage::SpirV;

        RawResultDesc Result; memset(&Result, 0, sizeof(Result));
        Compile(&Result, Source, Options, Target);
        if (Result.hasError)
        {
            const void* d = BlobData(&Result.errorWarningMsg);
            uint32_t n = BlobSize(&Result.errorWarningMsg);
            outError.assign(reinterpret_cast<const char*>(d), n);
            return false;
        }
        uint32_t sz = BlobSize(&Result.target);
        const void* data = BlobData(&Result.target);
        outSpirv.resize(sz / 4);
        memcpy(outSpirv.data(), data, sz);
        return true;
    }
}

static bool LegalizeAndStrip(std::vector<uint32_t>& spirv, std::string& outLog)
{
    spvtools::Optimizer opt(SPV_ENV_VULKAN_1_1);
    opt.SetMessageConsumer([&outLog](spv_message_level_t, const char*, const spv_position_t&, const char* m) {
        outLog += m; outLog += "\n";
    });
    opt.RegisterLegalizationPasses();
    opt.RegisterPass(spvtools::CreateStripReflectInfoPass());
    std::vector<uint32_t> result;
    if (!opt.Run(spirv.data(), spirv.size(), &result))
    {
        return false;
    }
    spirv = std::move(result);
    return true;
}

static bool SpirvToWgsl(const std::vector<uint32_t>& spirv, std::string& outWgsl, std::string& outError)
{
    tint::spirv::reader::Options ReadOpts = {};
    auto IrResult = tint::spirv::reader::ReadIR(spirv, ReadOpts);
    if (IrResult != tint::Success)
    {
        outError = "ReadIR: " + IrResult.Failure().reason;
        return false;
    }
    tint::wgsl::writer::Options WriteOpts = {};
    auto WgslResult = tint::wgsl::writer::WgslFromIR(IrResult.Get(), WriteOpts);
    if (WgslResult != tint::Success)
    {
        outError = "WgslFromIR: " + WgslResult.Failure().reason;
        return false;
    }
    outWgsl = WgslResult.Get().wgsl;
    return true;
}

int main(int argc, char** argv)
{
    if (argc < 5)
    {
        fprintf(stderr, "usage: %s <in.hlsl> <entryPoint> <vs|ps> <out.wgsl>\n", argv[0]);
        return 1;
    }
    std::ifstream f(argv[1]);
    std::stringstream ss;
    ss << f.rdbuf();
    std::string hlsl = ss.str();
    const char* entry = argv[2];
    std::string stageStr = argv[3];
    const char* outPath = argv[4];

    ShaderConductor::ShaderStage stage = (stageStr == "vs") ? ShaderConductor::ShaderStage::VertexShader : ShaderConductor::ShaderStage::PixelShader;

    std::vector<uint32_t> spirv;
    std::string err;
    if (!sc_loader::CompileHlslToSpirv(hlsl.c_str(), entry, stage, spirv, err))
    {
        fprintf(stderr, "[cook] ShaderConductor FAILED: %s\n", err.c_str());
        return 2;
    }
    fprintf(stderr, "[cook] HLSL -> SPIR-V OK: %zu words\n", spirv.size());

    std::string optLog;
    if (!LegalizeAndStrip(spirv, optLog))
    {
        fprintf(stderr, "[cook] spirv-opt legalize+strip FAILED: %s\n", optLog.c_str());
        return 3;
    }
    fprintf(stderr, "[cook] legalize+strip-reflect OK: %zu words%s%s\n", spirv.size(), optLog.empty() ? "" : " log: ", optLog.c_str());

    std::string wgsl;
    if (!SpirvToWgsl(spirv, wgsl, err))
    {
        fprintf(stderr, "[cook] SPIR-V -> WGSL FAILED: %s\n", err.c_str());
        return 4;
    }
    fprintf(stderr, "[cook] SPIR-V -> WGSL OK: %zu bytes\n", wgsl.size());

    std::ofstream out(outPath);
    out << wgsl;
    out.close();
    fprintf(stderr, "[cook] wrote %s\n", outPath);
    printf("%s\n", wgsl.c_str());
    return 0;
}
