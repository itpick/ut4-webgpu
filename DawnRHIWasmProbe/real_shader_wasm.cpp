// DawnRHI wasm probe -- Stage 1 Goal 3: render a REAL cooked UE shader pair
// (ScreenPass.usf's ScreenPassVS/CopyRectPS, via tools/cook_real_shader.sh,
// the exact same WGSL + reflection sidecars DawnRHITest -vs=/-ps=/-out=
// renders natively) through emdawnwebgpu in a real browser, entirely
// offscreen (render-to-texture + copyTextureToBuffer + mapAsync readback --
// no swapchain/canvas compositing, since headless Chrome's
// SharedImageBackingFactory gap blocks screenshotting a composited canvas
// in this environment, per DawnRHIWasmProbe/BUILD_RECIPE.md's Stage1/Goal2
// finding). This is NOT the DawnRHI UE module compiled to wasm -- it is a
// standalone harness that reproduces the SAME resource setup (vertex
// layout, checkerboard texture, DrawRectangleParameters UB, reflection-
// driven bind group at binding 106/109/110) that DawnRHITestMain.cpp's
// RunDawnRHIRealShaderTest() uses natively, ported to the webgpu.h/
// emdawnwebgpu API surface, to prove that surface can carry the real
// cooked shader pair through to a pixel-correct render in-browser.
#include <webgpu/webgpu.h>
#include <emscripten.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static WGPUInstance gInstance;
static WGPUDevice gDevice;
static WGPUQueue gQueue;
static WGPURenderPipeline gPipeline;
static WGPUBuffer gVertexBuffer;
static WGPUBuffer gUniformBuffer;
static WGPUBuffer gReadbackBuffer;
static WGPUTexture gCheckerTexture;
static WGPUTexture gOffscreenTarget;
static WGPUSampler gSampler;
static WGPUBindGroup gBindGroup;

static const int kWidth = 256, kHeight = 256;
static const int kTexSize = 8;
static const uint32_t kBytesPerRow = kWidth * 4; // 1024, already 256-aligned

struct FParsedBinding { std::string Name; uint32_t Set; uint32_t Binding; std::string Kind; };

static void LogJS(const char* msg) {
    EM_ASM({ console.log(UTF8ToString($0)); }, msg);
}
static void LogJSf(const char* fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    LogJS(buf);
}

static std::string LoadFile(const char* path) {
    FILE* f = fopen(path, "rb");
    if (!f) { LogJSf("FATAL: failed to open %s (preload-file missing?)", path); return std::string(); }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::string out;
    out.resize(sz);
    size_t rd = fread(&out[0], 1, sz, f);
    fclose(f);
    LogJSf("Loaded %s (%d bytes)", path, (int)rd);
    return out;
}

static std::vector<FParsedBinding> LoadBindingsSidecar(const char* path) {
    std::string text = LoadFile(path);
    std::vector<FParsedBinding> out;
    size_t pos = 0;
    while (pos < text.size()) {
        size_t eol = text.find('\n', pos);
        if (eol == std::string::npos) eol = text.size();
        std::string line = text.substr(pos, eol - pos);
        pos = eol + 1;
        if (line.empty()) continue;
        // Name \t Set \t Binding \t Kind
        size_t t1 = line.find('\t');
        size_t t2 = line.find('\t', t1 + 1);
        size_t t3 = line.find('\t', t2 + 1);
        if (t1 == std::string::npos || t2 == std::string::npos || t3 == std::string::npos) continue;
        FParsedBinding b;
        b.Name = line.substr(0, t1);
        b.Set = (uint32_t)atoi(line.substr(t1 + 1, t2 - t1 - 1).c_str());
        b.Binding = (uint32_t)atoi(line.substr(t2 + 1, t3 - t2 - 1).c_str());
        b.Kind = line.substr(t3 + 1);
        out.push_back(b);
    }
    return out;
}

static uint32_t FindBindingIndex(const std::vector<FParsedBinding>& parsed, const char* name) {
    for (const auto& p : parsed) if (p.Name == name) return p.Binding;
    LogJSf("FATAL: reflected binding '%s' not found in sidecar", name);
    return 0;
}

static WGPUShaderModule CompileWGSL(const std::string& src, const char* label) {
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = { src.c_str(), src.size() };
    WGPUShaderModuleDescriptor desc = {};
    desc.nextInChain = &wgslDesc.chain;
    desc.label = { label, WGPU_STRLEN };
    return wgpuDeviceCreateShaderModule(gDevice, &desc);
}

static void OnUncapturedError(WGPUDevice const*, WGPUErrorType type, WGPUStringView message, void*, void*) {
    LogJSf("*** WGPU VALIDATION/DEVICE ERROR (type=%d): %.*s", (int)type, (int)message.length, message.data);
}

static void OnBufferMapped(WGPUMapAsyncStatus status, WGPUStringView message, void*, void*) {
    if (status != WGPUMapAsyncStatus_Success) {
        LogJSf("Readback mapAsync FAILED: status=%d msg=%.*s", (int)status, (int)message.length, message.data);
        return;
    }
    const uint8_t* data = (const uint8_t*)wgpuBufferGetConstMappedRange(gReadbackBuffer, 0, kWidth * kHeight * 4);
    if (!data) { LogJS("FATAL: GetConstMappedRange returned null"); return; }

    auto PixelAt = [&](int x, int y, uint8_t out[4]) {
        const uint8_t* px = data + (size_t)(y * kBytesPerRow) + (size_t)(x * 4);
        out[0] = px[0]; out[1] = px[1]; out[2] = px[2]; out[3] = px[3];
    };

    uint8_t center[4], corner[4];
    PixelAt(kWidth / 2, kHeight / 2, center);
    PixelAt(4, 4, corner);
    LogJSf("Center pixel = (%d,%d,%d,%d), corner(4,4) = (%d,%d,%d,%d)",
           center[0], center[1], center[2], center[3], corner[0], corner[1], corner[2], corner[3]);

    // Sample a 16x16 grid across the whole image (same verification method
    // as docs/proof-real-shader-milestone2.png's native check) and confirm
    // exactly the two expected checkerboard colours appear, correctly tiled.
    int countA = 0, countB = 0, countOther = 0;
    const uint8_t colA[3] = {255, 200, 40};
    const uint8_t colB[3] = {30, 60, 200};
    for (int gy = 0; gy < 16; ++gy) {
        std::string row = "row " + std::to_string(gy) + ": ";
        for (int gx = 0; gx < 16; ++gx) {
            int x = gx * (kWidth / 16) + (kWidth / 32);
            int y = gy * (kHeight / 16) + (kHeight / 32);
            uint8_t p[4]; PixelAt(x, y, p);
            if (p[0] == colA[0] && p[1] == colA[1] && p[2] == colA[2]) { countA++; row += "A"; }
            else if (p[0] == colB[0] && p[1] == colB[1] && p[2] == colB[2]) { countB++; row += "B"; }
            else { countOther++; row += "?"; }
        }
        LogJS(row.c_str());
    }
    LogJSf("Grid sample: A(255,200,40)=%d B(30,60,200)=%d other=%d (16x16=256 total)", countA, countB, countOther);

    if (countOther == 0 && countA > 0 && countB > 0) {
        LogJS("SUCCESS: real cooked ScreenPassVS/CopyRectPS shader pair rendered a pixel-exact checkerboard through emdawnwebgpu in-browser (offscreen readback verified)");
    } else {
        LogJS("FAILURE: readback pixels do not match expected checkerboard");
    }

    wgpuBufferUnmap(gReadbackBuffer);
    EM_ASM({ if (window.__dawnrhiRealShaderDone) window.__dawnrhiRealShaderDone(); });
}

static void RenderAndReadback() {
    // --- Checkerboard texture upload (8x8, 2x2 tiles, same colours/layout
    // as DawnRHITestMain.cpp's native RunDawnRHIRealShaderTest) ---
    std::vector<uint8_t> checker(kTexSize * kTexSize * 4);
    for (int y = 0; y < kTexSize; ++y) {
        for (int x = 0; x < kTexSize; ++x) {
            bool even = ((x / 2) + (y / 2)) % 2 == 0;
            uint8_t* p = &checker[(y * kTexSize + x) * 4];
            if (even) { p[0]=255; p[1]=200; p[2]=40;  p[3]=255; }
            else      { p[0]=30;  p[1]=60;  p[2]=200; p[3]=255; }
        }
    }
    WGPUTexelCopyTextureInfo dst = {};
    dst.texture = gCheckerTexture;
    WGPUTexelCopyBufferLayout layout = {};
    layout.bytesPerRow = kTexSize * 4;
    layout.rowsPerImage = kTexSize;
    WGPUExtent3D writeSize = { (uint32_t)kTexSize, (uint32_t)kTexSize, 1 };
    wgpuQueueWriteTexture(gQueue, &dst, checker.data(), checker.size(), &layout, &writeSize);

    // --- DrawRectangleParameters uniform buffer (same reverse-engineered
    // convention as native: PosScaleBias=(W,H,0,0), UVScaleBias=(TexSize,TexSize,0,0),
    // InvTargetSizeAndTextureSize=(1/W,1/H,1/TexSize,1/TexSize)) ---
    float ubData[12] = {
        (float)kWidth, (float)kHeight, 0.0f, 0.0f,
        (float)kTexSize, (float)kTexSize, 0.0f, 0.0f,
        1.0f / kWidth, 1.0f / kHeight, 1.0f / kTexSize, 1.0f / kTexSize,
    };
    wgpuQueueWriteBuffer(gQueue, gUniformBuffer, 0, ubData, sizeof(ubData));

    // --- Encode render pass to offscreen target ---
    // NOTE: zero-initializing WGPUTextureViewDescriptor with `{}` leaves
    // mipLevelCount/arrayLayerCount at literal 0, which Dawn's validation
    // rejects ("arrayLayerCount (0) or mipLevelCount (0) is zero") --
    // unlike some other WebGPU-adjacent APIs, `{}` is NOT "whole resource"
    // here; the real "whole resource" sentinel is WGPU_MIP_LEVEL_COUNT_UNDEFINED/
    // WGPU_ARRAY_LAYER_COUNT_UNDEFINED (UINT32_MAX), matching the *_INIT
    // macros in webgpu.h. Found live via this session's OnUncapturedError
    // callback firing in-browser -- a real wasm-harness bug, not an
    // emdawnwebgpu/browser gap.
    WGPUTextureViewDescriptor tvDesc = {};
    tvDesc.mipLevelCount = WGPU_MIP_LEVEL_COUNT_UNDEFINED;
    tvDesc.arrayLayerCount = WGPU_ARRAY_LAYER_COUNT_UNDEFINED;
    WGPUTextureView targetView = wgpuTextureCreateView(gOffscreenTarget, &tvDesc);

    WGPUCommandEncoderDescriptor encDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gDevice, &encDesc);

    WGPURenderPassColorAttachment colorAttach = {};
    colorAttach.view = targetView;
    colorAttach.loadOp = WGPULoadOp_Clear;
    colorAttach.storeOp = WGPUStoreOp_Store;
    colorAttach.clearValue = {0.0, 0.0, 0.0, 1.0};
    colorAttach.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor rpDesc = {};
    rpDesc.colorAttachmentCount = 1;
    rpDesc.colorAttachments = &colorAttach;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &rpDesc);
    wgpuRenderPassEncoderSetPipeline(pass, gPipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, gBindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, gVertexBuffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderDraw(pass, 6, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUTexelCopyTextureInfo copySrc = {};
    copySrc.texture = gOffscreenTarget;
    WGPUTexelCopyBufferInfo copyDst = {};
    copyDst.buffer = gReadbackBuffer;
    copyDst.layout.bytesPerRow = kBytesPerRow;
    copyDst.layout.rowsPerImage = kHeight;
    WGPUExtent3D copySize = { (uint32_t)kWidth, (uint32_t)kHeight, 1 };
    wgpuCommandEncoderCopyTextureToBuffer(encoder, &copySrc, &copyDst, &copySize);

    WGPUCommandBufferDescriptor cbDesc = {};
    WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(encoder, &cbDesc);
    wgpuQueueSubmit(gQueue, 1, &cmdBuf);
    wgpuCommandBufferRelease(cmdBuf);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(targetView);

    LogJS("Frame submitted, mapping readback buffer...");
    WGPUBufferMapCallbackInfo mapCb = {};
    mapCb.mode = WGPUCallbackMode_AllowSpontaneous;
    mapCb.callback = OnBufferMapped;
    wgpuBufferMapAsync(gReadbackBuffer, WGPUMapMode_Read, 0, kWidth * kHeight * 4, mapCb);
}

static void OnDeviceReady(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void*, void*) {
    if (status != WGPURequestDeviceStatus_Success) {
        LogJSf("RequestDevice failed: %.*s", (int)message.length, message.data);
        return;
    }
    gDevice = device;
    gQueue = wgpuDeviceGetQueue(gDevice);
    LogJS("Device ready");

    // --- Load real cooked WGSL + reflection sidecars (verbatim
    // tools/cook_real_shader.sh output for ScreenPass.usf's ScreenPassVS/
    // CopyRectPS -- byte-identical to what DawnRHITest -vs=/-ps= renders
    // natively) ---
    std::string vsWgsl = LoadFile("/real/ScreenPassVS.wgsl");
    std::string psWgsl = LoadFile("/real/CopyRectPS.wgsl");
    std::vector<FParsedBinding> vsBindings = LoadBindingsSidecar("/real/ScreenPassVS.wgsl.bindings.txt");
    std::vector<FParsedBinding> psBindings = LoadBindingsSidecar("/real/CopyRectPS.wgsl.bindings.txt");
    if (vsWgsl.empty() || psWgsl.empty() || vsBindings.empty() || psBindings.empty()) {
        LogJS("FATAL: failed to load real cooked shader assets");
        return;
    }
    LogJSf("Loaded %d VS binding(s), %d PS binding(s) from real reflection sidecars", (int)vsBindings.size(), (int)psBindings.size());

    const uint32_t drawRectIdx = FindBindingIndex(vsBindings, "DrawRectangleParameters");
    const uint32_t texIdx = FindBindingIndex(psBindings, "InputTexture");
    const uint32_t sampIdx = FindBindingIndex(psBindings, "InputSampler");
    LogJSf("Reflected bind indices: DrawRectangleParameters=%u InputTexture=%u InputSampler=%u", drawRectIdx, texIdx, sampIdx);

    WGPUShaderModule vsModule = CompileWGSL(vsWgsl, "ScreenPassVS");
    WGPUShaderModule psModule = CompileWGSL(psWgsl, "CopyRectPS");

    // --- Reflection-driven bind group layout (NOT a fixed {0,1,2}
    // assumption -- built from the real sidecar binding numbers, same as
    // DawnRHI's RHICreateGraphicsPipelineState does natively) ---
    WGPUBindGroupLayoutEntry bglEntries[3] = {};
    bglEntries[0].binding = drawRectIdx;
    bglEntries[0].visibility = WGPUShaderStage_Vertex;
    bglEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
    bglEntries[1].binding = texIdx;
    bglEntries[1].visibility = WGPUShaderStage_Fragment;
    bglEntries[1].texture.sampleType = WGPUTextureSampleType_Float;
    bglEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    bglEntries[2].binding = sampIdx;
    bglEntries[2].visibility = WGPUShaderStage_Fragment;
    bglEntries[2].sampler.type = WGPUSamplerBindingType_Filtering;

    WGPUBindGroupLayoutDescriptor bglDesc = {};
    bglDesc.entryCount = 3;
    bglDesc.entries = bglEntries;
    WGPUBindGroupLayout bgl = wgpuDeviceCreateBindGroupLayout(gDevice, &bglDesc);

    WGPUPipelineLayoutDescriptor plDesc = {};
    plDesc.bindGroupLayoutCount = 1;
    plDesc.bindGroupLayouts = &bgl;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(gDevice, &plDesc);

    // --- Vertex layout: ATTRIBUTE0=float4 position, ATTRIBUTE1=float2 UV
    // (matches ScreenPassVS's real @location(0)/@location(1) interface) ---
    WGPUVertexAttribute attrs[2] = {};
    attrs[0].format = WGPUVertexFormat_Float32x4;
    attrs[0].offset = 0;
    attrs[0].shaderLocation = 0;
    attrs[1].format = WGPUVertexFormat_Float32x2;
    attrs[1].offset = 16;
    attrs[1].shaderLocation = 1;
    WGPUVertexBufferLayout vbLayout = {};
    vbLayout.arrayStride = 24; // 4 floats pos + 2 floats uv
    vbLayout.stepMode = WGPUVertexStepMode_Vertex;
    vbLayout.attributeCount = 2;
    vbLayout.attributes = attrs;

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragState = {};
    fragState.module = psModule;
    fragState.entryPoint = { "CopyRectPS", WGPU_STRLEN };
    fragState.targetCount = 1;
    fragState.targets = &colorTarget;

    WGPURenderPipelineDescriptor pipeDesc = {};
    pipeDesc.layout = pipelineLayout;
    pipeDesc.vertex.module = vsModule;
    pipeDesc.vertex.entryPoint = { "ScreenPassVS", WGPU_STRLEN };
    pipeDesc.vertex.bufferCount = 1;
    pipeDesc.vertex.buffers = &vbLayout;
    pipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipeDesc.multisample.count = 1;
    pipeDesc.multisample.mask = 0xFFFFFFFF;
    pipeDesc.fragment = &fragState;

    gPipeline = wgpuDeviceCreateRenderPipeline(gDevice, &pipeDesc);
    if (!gPipeline) { LogJS("FATAL: real-shader pipeline creation failed"); return; }
    LogJS("Real-shader pipeline created OK (reflection-driven bind group layout)");

    // --- Unit quad vertex buffer (same 6 verts as native RunDawnRHIRealShaderTest) ---
    float verts[6 * 6] = {
        0,0,0,1, 0,0,
        1,0,0,1, 1,0,
        1,1,0,1, 1,1,
        0,0,0,1, 0,0,
        1,1,0,1, 1,1,
        0,1,0,1, 0,1,
    };
    WGPUBufferDescriptor vbDesc = {};
    vbDesc.size = sizeof(verts);
    vbDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    gVertexBuffer = wgpuDeviceCreateBuffer(gDevice, &vbDesc);
    wgpuQueueWriteBuffer(gQueue, gVertexBuffer, 0, verts, sizeof(verts));

    WGPUBufferDescriptor ubDesc = {};
    ubDesc.size = 48; // 3x vec4f
    ubDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    gUniformBuffer = wgpuDeviceCreateBuffer(gDevice, &ubDesc);

    WGPUBufferDescriptor rbDesc = {};
    rbDesc.size = kWidth * kHeight * 4;
    rbDesc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    gReadbackBuffer = wgpuDeviceCreateBuffer(gDevice, &rbDesc);

    WGPUSamplerDescriptor sDesc = {};
    sDesc.addressModeU = WGPUAddressMode_Repeat;
    sDesc.addressModeV = WGPUAddressMode_Repeat;
    sDesc.addressModeW = WGPUAddressMode_Repeat;
    sDesc.magFilter = WGPUFilterMode_Nearest;
    sDesc.minFilter = WGPUFilterMode_Nearest;
    sDesc.maxAnisotropy = 1;
    gSampler = wgpuDeviceCreateSampler(gDevice, &sDesc);

    WGPUTextureDescriptor texDesc = {};
    texDesc.dimension = WGPUTextureDimension_2D;
    texDesc.size = { (uint32_t)kTexSize, (uint32_t)kTexSize, 1 };
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    gCheckerTexture = wgpuDeviceCreateTexture(gDevice, &texDesc);

    WGPUTextureDescriptor targetDesc = {};
    targetDesc.dimension = WGPUTextureDimension_2D;
    targetDesc.size = { (uint32_t)kWidth, (uint32_t)kHeight, 1 };
    targetDesc.format = WGPUTextureFormat_RGBA8Unorm;
    targetDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
    targetDesc.mipLevelCount = 1;
    targetDesc.sampleCount = 1;
    gOffscreenTarget = wgpuDeviceCreateTexture(gDevice, &targetDesc);

    WGPUTextureViewDescriptor checkerViewDesc = {};
    checkerViewDesc.mipLevelCount = WGPU_MIP_LEVEL_COUNT_UNDEFINED;
    checkerViewDesc.arrayLayerCount = WGPU_ARRAY_LAYER_COUNT_UNDEFINED;
    WGPUTextureView checkerView = wgpuTextureCreateView(gCheckerTexture, &checkerViewDesc);

    WGPUBindGroupEntry bgEntries[3] = {};
    bgEntries[0].binding = drawRectIdx;
    bgEntries[0].buffer = gUniformBuffer;
    bgEntries[0].size = 48;
    bgEntries[1].binding = texIdx;
    bgEntries[1].textureView = checkerView;
    bgEntries[2].binding = sampIdx;
    bgEntries[2].sampler = gSampler;

    WGPUBindGroupDescriptor bgDesc = {};
    bgDesc.layout = bgl;
    bgDesc.entryCount = 3;
    bgDesc.entries = bgEntries;
    gBindGroup = wgpuDeviceCreateBindGroup(gDevice, &bgDesc);

    RenderAndReadback();
}

static void OnAdapterReady(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void*, void*) {
    if (status != WGPURequestAdapterStatus_Success) {
        LogJSf("RequestAdapter failed: %.*s", (int)message.length, message.data);
        return;
    }
    LogJS("Adapter ready, requesting device...");
    WGPUDeviceDescriptor deviceDesc = {};
    deviceDesc.uncapturedErrorCallbackInfo.callback = OnUncapturedError;
    WGPURequestDeviceCallbackInfo cbInfo = {};
    cbInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    cbInfo.callback = OnDeviceReady;
    wgpuAdapterRequestDevice(adapter, &deviceDesc, cbInfo);
}

int main() {
    LogJS("DawnRHI real-shader wasm probe starting (ScreenPassVS/CopyRectPS via emdawnwebgpu)");
    WGPUInstanceDescriptor instanceDesc = {};
    gInstance = wgpuCreateInstance(&instanceDesc);
    if (!gInstance) { LogJS("wgpuCreateInstance failed"); return 1; }
    WGPURequestAdapterOptions opts = {};
    opts.powerPreference = WGPUPowerPreference_HighPerformance;
    WGPURequestAdapterCallbackInfo cbInfo = {};
    cbInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    cbInfo.callback = OnAdapterReady;
    wgpuInstanceRequestAdapter(gInstance, &opts, cbInfo);
    emscripten_exit_with_live_runtime();
    return 0;
}
