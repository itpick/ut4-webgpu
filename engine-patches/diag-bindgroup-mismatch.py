p="/mnt/vms/ss-build/UnrealEngine/Engine/Source/Runtime/DawnRHI/Private/DawnCommandContext.cpp"
s=open(p).read()
anchor='\tcheckf(ActiveRenderPass, TEXT("DawnRHI: RHISetShaderParameters requires an active render pass"));\n\tfor (TPair<uint32, TArray<WGPUBindGroupEntry>>& Pair : EntriesByGroup)'
diag=('\t// BGMISMATCH diag: compare built entries against the reflected layout; log only\n'
      '\t// when they disagree (the complex game/loading shaders that fail bind-group validation).\n'
      '\t{\n'
      '\t\tTMap<uint32, TSet<uint32>> Expected;\n'
      '\t\tif (CurrentPSO->VertexShader) for (const FDawnShaderBinding& B : CurrentPSO->VertexShader->Bindings) Expected.FindOrAdd(B.Group).Add(B.Binding);\n'
      '\t\tif (CurrentPSO->PixelShader)  for (const FDawnShaderBinding& B : CurrentPSO->PixelShader->Bindings)  Expected.FindOrAdd(B.Group).Add(B.Binding);\n'
      '\t\tfor (const TPair<uint32, TArray<WGPUBindGroupEntry>>& Pair : EntriesByGroup)\n'
      '\t\t{\n'
      '\t\t\tconst TSet<uint32>& Exp = Expected.FindOrAdd(Pair.Key);\n'
      '\t\t\tbool bMismatch = false;\n'
      '\t\t\tfor (const WGPUBindGroupEntry& E : Pair.Value) if (!Exp.Contains(E.binding)) bMismatch = true;\n'
      '\t\t\tfor (uint32 b : Exp) { bool f=false; for (const WGPUBindGroupEntry& E : Pair.Value) if (E.binding==b) f=true; if(!f) bMismatch=true; }\n'
      '\t\t\tif (bMismatch) { static uint32 _bm=0; if (_bm++<24) {\n'
      '\t\t\t\tFString es, xs; for (const WGPUBindGroupEntry& E : Pair.Value) es += FString::Printf(TEXT("%u "), E.binding);\n'
      '\t\t\t\tTArray<uint32> ExpArr = Exp.Array(); ExpArr.Sort(); for (uint32 b : ExpArr) xs += FString::Printf(TEXT("%u "), b);\n'
      '\t\t\t\tUE_LOG(LogDawnRHI, Warning, TEXT("BGMISMATCH grp=%u freq=%d built=[%s] expected=[%s] res=%d pdata=%d params=%d staticUB=%d"), Pair.Key, (int)Shader->GetFrequency(), *es, *xs, InResourceParameters.Num(), InParametersData.Num(), InParameters.Num(), StaticUBsByName.Num());\n'
      '\t\t\t\tfor (const FRHIShaderParameter& P : InParameters) UE_LOG(LogDawnRHI, Warning, TEXT("   BGMloose bufIdx=%u base=%u off=%u sz=%u"), (uint32)P.BufferIndex, (uint32)P.BaseIndex, (uint32)P.ByteOffset, (uint32)P.ByteSize);\n'
      '\t\t\t\tfor (const FRHIShaderParameterResource& R : InResourceParameters) UE_LOG(LogDawnRHI, Warning, TEXT("   BGMres idx=%u type=%d"), R.Index, (int)R.Type);\n'
      '\t\t\t\tif (CurrentPSO->VertexShader) for (const FDawnShaderBinding& B : CurrentPSO->VertexShader->Bindings) UE_LOG(LogDawnRHI, Warning, TEXT("   BGMvs grp=%u bind=%u kind=%d name=%s"), B.Group, B.Binding, (int)B.Kind, *B.Name);\n'
      '\t\t\t\tif (CurrentPSO->PixelShader)  for (const FDawnShaderBinding& B : CurrentPSO->PixelShader->Bindings)  UE_LOG(LogDawnRHI, Warning, TEXT("   BGMps grp=%u bind=%u kind=%d name=%s"), B.Group, B.Binding, (int)B.Kind, *B.Name);\n'
      '\t\t\t} }\n'
      '\t\t}\n'
      '\t}\n'
      +anchor)
assert s.count(anchor)==1, ("anchor", s.count(anchor))
s=s.replace(anchor,diag)
open(p,"w").write(s)
print("BGMISMATCH diagnostic added")
