# 0) DawnResources.h: minimal compute shader + compute PSO stub classes (DawnRHI runs no compute).
dr="/mnt/vms/ss-build/UnrealEngine/Engine/Source/Runtime/DawnRHI/Public/DawnResources.h"
s=open(dr).read()
anchor="class FDawnGraphicsPipelineState : public FRHIGraphicsPipelineState"
stub=("// Compute is not supported by DawnRHI (WebGPU compute + the mobile renderer's compute\n"
      "// shaders that don't cross-compile). These stubs exist so compute create/dispatch calls\n"
      "// return valid-but-inert objects instead of crashing; the command context skips them.\n"
      "class FDawnComputeShader : public FRHIComputeShader\n{\npublic:\n\tFDawnComputeShader() = default;\n};\n\n"
      "class FDawnComputePipelineState : public FRHIComputePipelineState\n{\npublic:\n\tFDawnComputePipelineState() = default;\n};\n\n"
      +anchor)
assert s.count(anchor)==1, ("dr-anchor", s.count(anchor))
s=s.replace(anchor,stub)
open(dr,"w").write(s)
print("DawnResources.h: FDawnComputeShader + FDawnComputePipelineState stubs added")

# 1) ShaderResource.cpp: missing REQUIRED shader -> non-fatal on wasm (return null so the pass
#    becomes a no-op) instead of a hard crash. DawnRHI has no compute + the compute shaders
#    don't cross-compile, so this lets the forward mesh render proceed.
sr="/mnt/vms/ss-build/UnrealEngine/Engine/Source/Runtime/RenderCore/Private/ShaderResource.cpp"
s=open(sr).read()
old=('\t\tif (bRequired)\n'
     '\t\t{\n'
     '\t\t\tUE_LOGF(LogShaders, Fatal, "FShaderMapResource_InlineCode::InitRHI is unable to create a shader: frequency=%d, hash=%ls.", static_cast<int32>(Frequency), *ShaderHash.ToString());\n'
     '\t\t}')
new=('\t\tif (bRequired)\n'
     '\t\t{\n'
     '#if PLATFORM_WASM\n'
     '\t\t\t// DawnRHI/WebGPU: no compute support + the mobile renderer\'s compute shaders do not\n'
     '\t\t\t// cross-compile to WGSL. Return null (the pass/dispatch becomes a no-op in DawnRHI)\n'
     '\t\t\t// instead of a hard crash, so the forward mesh render still proceeds.\n'
     '\t\t\tstatic int32 _wndrMissWarn = 0;\n'
     '\t\t\tif (_wndrMissWarn++ < 12) { UE_LOGF(LogShaders, Warning, "FShaderMapResource_InlineCode::InitRHI missing shader (non-fatal on wasm): frequency=%d, hash=%ls.", static_cast<int32>(Frequency), *ShaderHash.ToString()); }\n'
     '#else\n'
     '\t\t\tUE_LOGF(LogShaders, Fatal, "FShaderMapResource_InlineCode::InitRHI is unable to create a shader: frequency=%d, hash=%ls.", static_cast<int32>(Frequency), *ShaderHash.ToString());\n'
     '#endif\n'
     '\t\t}')
assert s.count(old)==1, ("sr", s.count(old))
s=s.replace(old,new)
open(sr,"w").write(s)
print("ShaderResource: missing required shader non-fatal on wasm")

# 2) DawnCommandContext.cpp: compute set/dispatch -> safe no-ops (were checkNoEntry).
cc="/mnt/vms/ss-build/UnrealEngine/Engine/Source/Runtime/DawnRHI/Private/DawnCommandContext.cpp"
s=open(cc).read()
for sig, body in [
  ("void FDawnCommandContext::RHISetComputePipelineState(FRHIComputePipelineState* ComputePipelineState)",
   "\t// no-op: DawnRHI runs no compute (mobile compute shaders don't cross-compile to WGSL); the pass is skipped."),
  ("void FDawnCommandContext::RHIDispatchComputeShader(uint32 ThreadGroupCountX, uint32 ThreadGroupCountY, uint32 ThreadGroupCountZ)",
   "\t// no-op: DawnRHI runs no compute; the dispatch is skipped (its output buffers keep their prior/cleared contents)."),
  ("void FDawnCommandContext::RHIDispatchIndirectComputeShader(FRHIBuffer* ArgumentBuffer, uint32 ArgumentOffset)",
   "\t// no-op: DawnRHI runs no compute; indirect dispatch skipped."),
]:
    old_f=sig+"\n{\n\tcheckNoEntry();\n}"
    new_f=sig+"\n{\n"+body+"\n}"
    assert s.count(old_f)==1, ("cc", sig, s.count(old_f))
    s=s.replace(old_f,new_f)
# compute RHISetShaderParameters (was checkNoEntry) -> no-op
old_sp="void FDawnCommandContext::RHISetShaderParameters(FRHIComputeShader* ComputeShader, TConstArrayView<uint8> InParametersData, TConstArrayView<FRHIShaderParameter> InParameters, TConstArrayView<FRHIShaderParameterResource> InResourceParameters, TConstArrayView<FRHIShaderParameterResource> InBindlessParameters)\n{\n\tcheckNoEntry();\n}"
new_sp="void FDawnCommandContext::RHISetShaderParameters(FRHIComputeShader* ComputeShader, TConstArrayView<uint8> InParametersData, TConstArrayView<FRHIShaderParameter> InParameters, TConstArrayView<FRHIShaderParameterResource> InResourceParameters, TConstArrayView<FRHIShaderParameterResource> InBindlessParameters)\n{\n\t// no-op: DawnRHI runs no compute; compute shader parameters are ignored.\n}"
assert s.count(old_sp)==1, ("cc-sp", s.count(old_sp))
s=s.replace(old_sp,new_sp)
open(cc,"w").write(s)
print("DawnCommandContext: compute set/dispatch/params -> no-ops")

# 3) DawnDynamicRHI.cpp: RHICreateComputeShader + RHICreateComputePipelineState -> return dummies (no crash).
dd="/mnt/vms/ss-build/UnrealEngine/Engine/Source/Runtime/DawnRHI/Private/DawnDynamicRHI.cpp"
s=open(dd).read()
old_cs="FComputeShaderRHIRef FDawnDynamicRHI::RHICreateComputeShader(const FRHICreateShaderDesc& CreateShaderDesc)\n{\n\tcheckNoEntry();\n\treturn {};\n}"
new_cs=("FComputeShaderRHIRef FDawnDynamicRHI::RHICreateComputeShader(const FRHICreateShaderDesc& CreateShaderDesc)\n{\n"
        "\t// DawnRHI runs no compute; hand back an empty compute shader object so callers don't crash.\n"
        "\t// (Compute passes are no-ops in the command context.)\n"
        "\treturn new FDawnComputeShader();\n}")
assert s.count(old_cs)==1, ("dd-cs", s.count(old_cs))
s=s.replace(old_cs,new_cs)
old_cp="FComputePipelineStateRHIRef FDawnDynamicRHI::RHICreateComputePipelineState(const FComputePipelineStateInitializer& Initializer)\n{\n\tcheckNoEntry();\n\treturn {};\n}"
new_cp=("FComputePipelineStateRHIRef FDawnDynamicRHI::RHICreateComputePipelineState(const FComputePipelineStateInitializer& Initializer)\n{\n"
        "\t// DawnRHI runs no compute; dummy PSO (dispatch is a no-op).\n"
        "\treturn new FDawnComputePipelineState();\n}")
assert s.count(old_cp)==1, ("dd-cp", s.count(old_cp))
s=s.replace(old_cp,new_cp)
open(dd,"w").write(s)
print("DawnDynamicRHI: compute shader + pipeline -> dummy objects")
