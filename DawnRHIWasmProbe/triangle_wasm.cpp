// Stage 1 Goal 2 probe: a minimal WebGPU triangle running in a real browser
// via wasm + emscripten's built-in "emdawnwebgpu" port (upstream Emscripten
// project, MIT/BSD-3 licensed — not SimplyStream code, not even their
// vendored emdawn copy under Engine/Platforms/SimplyStream). Uses the same
// hand-authored WGSL contract as the native DawnRHI triangle.
#include <webgpu/webgpu.h>
#include <emscripten.h>
#include <cstdio>
#include <cstring>

static WGPUInstance gInstance;
static WGPUDevice gDevice;
static WGPUQueue gQueue;
static WGPUSurface gSurface;
static WGPURenderPipeline gPipeline;

static const char* kVertexWGSL = R"(
struct VSOut {
  @builtin(position) pos : vec4<f32>,
  @location(0) color : vec3<f32>,
};

@vertex
fn vs_main(@builtin(vertex_index) idx : u32) -> VSOut {
  var positions = array<vec2<f32>, 3>(
    vec2<f32>( 0.0,  0.6),
    vec2<f32>(-0.6, -0.6),
    vec2<f32>( 0.6, -0.6)
  );
  var colors = array<vec3<f32>, 3>(
    vec3<f32>(1.0, 0.0, 0.0),
    vec3<f32>(0.0, 1.0, 0.0),
    vec3<f32>(0.0, 0.0, 1.0)
  );
  var out : VSOut;
  out.pos = vec4<f32>(positions[idx], 0.0, 1.0);
  out.color = colors[idx];
  return out;
}
)";

static const char* kFragmentWGSL = R"(
@fragment
fn fs_main(@location(0) color : vec3<f32>) -> @location(0) vec4<f32> {
  return vec4<f32>(color, 1.0);
}
)";

static void LogJS(const char* msg) {
    EM_ASM({ console.log(UTF8ToString($0)); }, msg);
}

static void Render() {
    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(gSurface, &surfaceTexture);
    WGPUTextureView view = wgpuTextureCreateView(surfaceTexture.texture, nullptr);

    WGPUCommandEncoderDescriptor encDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(gDevice, &encDesc);

    WGPURenderPassColorAttachment colorAttach = {};
    colorAttach.view = view;
    colorAttach.loadOp = WGPULoadOp_Clear;
    colorAttach.storeOp = WGPUStoreOp_Store;
    colorAttach.clearValue = {0.05, 0.05, 0.08, 1.0};
    colorAttach.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;

    WGPURenderPassDescriptor rpDesc = {};
    rpDesc.colorAttachmentCount = 1;
    rpDesc.colorAttachments = &colorAttach;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &rpDesc);
    wgpuRenderPassEncoderSetPipeline(pass, gPipeline);
    wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cbDesc = {};
    WGPUCommandBuffer cmdBuf = wgpuCommandEncoderFinish(encoder, &cbDesc);
    wgpuQueueSubmit(gQueue, 1, &cmdBuf);
    wgpuCommandBufferRelease(cmdBuf);
    wgpuCommandEncoderRelease(encoder);
    wgpuTextureViewRelease(view);

    LogJS("DawnRHI wasm probe: frame submitted");
    EM_ASM({ if (window.__dawnrhiTriangleRendered) window.__dawnrhiTriangleRendered(); });
}

static WGPUShaderModule CompileWGSL(const char* src, const char* label) {
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = {src, WGPU_STRLEN};
    WGPUShaderModuleDescriptor desc = {};
    desc.nextInChain = &wgslDesc.chain;
    desc.label = {label, WGPU_STRLEN};
    return wgpuDeviceCreateShaderModule(gDevice, &desc);
}

static void OnDeviceReady(WGPURequestDeviceStatus status, WGPUDevice device, WGPUStringView message, void*, void*) {
    if (status != WGPURequestDeviceStatus_Success) {
        LogJS("RequestDevice failed");
        return;
    }
    gDevice = device;
    gQueue = wgpuDeviceGetQueue(gDevice);
    LogJS("Device ready");

    WGPUEmscriptenSurfaceSourceCanvasHTMLSelector canvasDesc = {};
    canvasDesc.chain.sType = WGPUSType_EmscriptenSurfaceSourceCanvasHTMLSelector;
    canvasDesc.selector = {"#canvas", WGPU_STRLEN};
    WGPUSurfaceDescriptor surfDesc = {};
    surfDesc.nextInChain = &canvasDesc.chain;
    gSurface = wgpuInstanceCreateSurface(gInstance, &surfDesc);

    WGPUSurfaceConfiguration config = {};
    config.device = gDevice;
    config.format = WGPUTextureFormat_BGRA8Unorm;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.width = 256;
    config.height = 256;
    config.presentMode = WGPUPresentMode_Fifo;
    config.alphaMode = WGPUCompositeAlphaMode_Opaque;
    wgpuSurfaceConfigure(gSurface, &config);

    WGPUShaderModule vs = CompileWGSL(kVertexWGSL, "vs");
    WGPUShaderModule fs = CompileWGSL(kFragmentWGSL, "fs");

    WGPUPipelineLayoutDescriptor plDesc = {};
    WGPUPipelineLayout layout = wgpuDeviceCreatePipelineLayout(gDevice, &plDesc);

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = WGPUTextureFormat_BGRA8Unorm;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragState = {};
    fragState.module = fs;
    fragState.entryPoint = {"fs_main", WGPU_STRLEN};
    fragState.targetCount = 1;
    fragState.targets = &colorTarget;

    WGPURenderPipelineDescriptor pipeDesc = {};
    pipeDesc.layout = layout;
    pipeDesc.vertex.module = vs;
    pipeDesc.vertex.entryPoint = {"vs_main", WGPU_STRLEN};
    pipeDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipeDesc.multisample.count = 1;
    pipeDesc.multisample.mask = 0xFFFFFFFF;
    pipeDesc.fragment = &fragState;

    gPipeline = wgpuDeviceCreateRenderPipeline(gDevice, &pipeDesc);
    if (!gPipeline) {
        LogJS("Pipeline creation failed");
        return;
    }
    LogJS("Pipeline ready, rendering...");
    Render();
}

static void OnAdapterReady(WGPURequestAdapterStatus status, WGPUAdapter adapter, WGPUStringView message, void*, void*) {
    if (status != WGPURequestAdapterStatus_Success) {
        LogJS("RequestAdapter failed");
        return;
    }
    LogJS("Adapter ready, requesting device...");
    WGPUDeviceDescriptor deviceDesc = {};
    WGPURequestDeviceCallbackInfo cbInfo = {};
    cbInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    cbInfo.callback = OnDeviceReady;
    wgpuAdapterRequestDevice(adapter, &deviceDesc, cbInfo);
}

int main() {
    LogJS("DawnRHI wasm probe starting");
    WGPUInstanceDescriptor instanceDesc = {};
    gInstance = wgpuCreateInstance(&instanceDesc);
    if (!gInstance) {
        LogJS("wgpuCreateInstance failed");
        return 1;
    }
    WGPURequestAdapterOptions opts = {};
    opts.powerPreference = WGPUPowerPreference_HighPerformance;
    WGPURequestAdapterCallbackInfo cbInfo = {};
    cbInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    cbInfo.callback = OnAdapterReady;
    wgpuInstanceRequestAdapter(gInstance, &opts, cbInfo);
    // Do NOT block here — the browser event loop drives the async
    // Request*/callback chain above after main() returns. Keep the wasm
    // runtime alive so those callbacks can still fire.
    emscripten_exit_with_live_runtime();
    return 0;
}
