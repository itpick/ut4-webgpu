p="/mnt/vms/ss-build/UnrealEngine/Engine/Source/Runtime/DawnRHI/Private/DawnDynamicRHI.cpp"
s=open(p).read()

# 1) Feature level SM6 -> ES3_1 (selects the mobile forward renderer instead of deferred).
old_fl="\tGMaxRHIFeatureLevel = ERHIFeatureLevel::SM6;"
new_fl="\tGMaxRHIFeatureLevel = ERHIFeatureLevel::ES3_1; // was SM6 (deferred); ES3_1 -> mobile forward renderer (its shaders cross-compile to WGSL; the deferred/compute set does not)"
assert s.count(old_fl)==1, ("fl", s.count(old_fl))
s=s.replace(old_fl,new_fl)

# 2) Resolve SP_WEBGPU_ES31 as the max shader platform + map the feature level to it.
old_sp=('\t\tconst EShaderPlatform WebGPUSM5 = FGenericDataDrivenShaderPlatformInfo::GetShaderPlatformFromName(TEXT("SP_WEBGPU_SM5"));\n'
        '\t\tUE_LOG(LogDawnRHI, Warning, TEXT("DawnRHI: SP_WEBGPU_SM5 resolved to %d (valid=%d)"),\n'
        '\t\t\t(int32)WebGPUSM5,\n'
        '\t\t\tWebGPUSM5 != SP_NumPlatforms ? (int32)FGenericDataDrivenShaderPlatformInfo::IsValid(WebGPUSM5) : -1);\n'
        '\t\tGMaxRHIShaderPlatform = (WebGPUSM5 != SP_NumPlatforms) ? WebGPUSM5 : (EShaderPlatform)42;')
new_sp=('\t\tconst EShaderPlatform WebGPUES31 = FGenericDataDrivenShaderPlatformInfo::GetShaderPlatformFromName(TEXT("SP_WEBGPU_ES31"));\n'
        '\t\tUE_LOG(LogDawnRHI, Warning, TEXT("DawnRHI: SP_WEBGPU_ES31 resolved to %d (valid=%d)"),\n'
        '\t\t\t(int32)WebGPUES31,\n'
        '\t\t\tWebGPUES31 != SP_NumPlatforms ? (int32)FGenericDataDrivenShaderPlatformInfo::IsValid(WebGPUES31) : -1);\n'
        '\t\tGMaxRHIShaderPlatform = (WebGPUES31 != SP_NumPlatforms) ? WebGPUES31 : (EShaderPlatform)43;\n'
        '\t\t// Map the ES3_1 feature level to our WebGPU ES31 platform so GetFeatureLevelShaderPlatform()/\n'
        '\t\t// the mobile renderer + global shader map all resolve to the cooked SP_WEBGPU_ES31 content.\n'
        '\t\tGShaderPlatformForFeatureLevel[ERHIFeatureLevel::ES3_1] = GMaxRHIShaderPlatform;')
assert s.count(old_sp)==1, ("sp", s.count(old_sp))
s=s.replace(old_sp,new_sp)

open(p,"w").write(s)
print("runtime feature level -> ES3_1 + shader platform -> SP_WEBGPU_ES31 + feature-level mapping")
