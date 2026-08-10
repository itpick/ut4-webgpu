// Standalone probe: SPIR-V -> WGSL via Tint's IR-based reader/writer,
// linked directly against the vendored libtint.a (open-source Google
// Tint, part of the Dawn project — NOT SimplyStream code). Headers fetched
// from dawn.googlesource.com at the exact pinned revision recorded in the
// vendored build (dawn::kDawnVersion in Version_autogen.h):
// 3c82ef2b508a29f96ac31731d27dffb86f39efd0
#include <cstdio>
#include <cstdint>
#include <vector>
#include <fstream>
#include <algorithm>

#include "src/tint/lang/spirv/reader/reader.h"
#include "src/tint/lang/wgsl/writer/writer.h"
#include "src/tint/lang/core/ir/module.h"

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: %s <input.spv>\n", argv[0]);
        return 1;
    }

    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint32_t> spirv;
    {
        f.seekg(0, std::ios::end);
        size_t sz = (size_t)f.tellg();
        f.seekg(0);
        spirv.resize(sz / 4);
        f.read(reinterpret_cast<char*>(spirv.data()), sz);
    }
    printf("[tint-probe] read %zu words of SPIR-V from %s\n", spirv.size(), argv[1]);

    try
    {
        tint::spirv::reader::Options ReadOpts = {}; ReadOpts.allowed_features = tint::wgsl::AllowedFeatures::Everything();
        printf("[tint-probe] calling ReadIR...\n"); fflush(stdout);
        auto IrResult = tint::spirv::reader::ReadIR(spirv, ReadOpts);
        printf("[tint-probe] ReadIR returned\n"); fflush(stdout);

        bool bIsSuccess = static_cast<bool>(IrResult == tint::Success);
        printf("[tint-probe] IrResult == Success: %d\n", (int)bIsSuccess); fflush(stdout);

        if (!bIsSuccess)
        {
            printf("[tint-probe] ReadIR FAILED, inspecting Failure()...\n"); fflush(stdout);
            const tint::Failure& Fail = IrResult.Failure();
            printf("[tint-probe] Failure obj addr=%p\n", (const void*)&Fail); fflush(stdout);
            const std::string& Reason = Fail.reason;
            printf("[tint-probe] reason addr=%p size=%zu capacity=%zu\n", (const void*)&Reason, Reason.size(), Reason.capacity()); fflush(stdout);
            if (Reason.size() < 10000 && Reason.data() != nullptr)
            {
                printf("[tint-probe] reason data ptr=%p, first bytes: ", (const void*)Reason.data()); fflush(stdout);
                for (size_t i = 0; i < std::min<size_t>(Reason.size(), 64); ++i)
                {
                    unsigned char c = (unsigned char)Reason.data()[i];
                    printf("%02x ", c);
                }
                printf("\n"); fflush(stdout);
            }
            printf("[tint-probe] ReadIR FAILED: %s\n", Reason.c_str());
            return 2;
        }
        printf("[tint-probe] ReadIR OK\n"); fflush(stdout);

        tint::core::ir::Module& Module = IrResult.Get();
        printf("[tint-probe] Got Module ref\n"); fflush(stdout);

        tint::wgsl::writer::Options WriteOpts = {};
        printf("[tint-probe] calling WgslFromIR...\n"); fflush(stdout);
        auto WgslResult = tint::wgsl::writer::WgslFromIR(Module, WriteOpts);
        printf("[tint-probe] WgslFromIR returned\n"); fflush(stdout);

        bool bWgslSuccess = static_cast<bool>(WgslResult == tint::Success);
        if (!bWgslSuccess)
        {
            printf("[tint-probe] WgslFromIR FAILED: %s\n", WgslResult.Failure().reason.c_str());
            return 3;
        }
        printf("[tint-probe] WgslFromIR OK, %zu bytes of WGSL:\n---\n%s\n---\n",
            WgslResult.Get().wgsl.size(), WgslResult.Get().wgsl.c_str());
        return 0;
    }
    catch (const std::exception& e)
    {
        printf("[tint-probe] EXCEPTION: %s\n", e.what());
        return 99;
    }
}
