import re

# ===== Header: add StaticUBsByName member =====
h="/mnt/vms/ss-build/UnrealEngine/Engine/Source/Runtime/DawnRHI/Public/DawnCommandContext.h"
s=open(h).read()
anchor="\tFDawnGraphicsPipelineState* CurrentPSO = nullptr; // non-owning; set by RHISetGraphicsPipelineState"
add=anchor+"\n\n\t// Static uniform buffers (RHISetStaticUniformBuffers) keyed by layout debug-name.\n\t// Slate's ViewProjection lives in the static \"SlateView\" UB, bound here, NOT via\n\t// RHISetShaderParameters -- so it must be injected into the bind group separately.\n\tTMap<FString, FRHIUniformBuffer*> StaticUBsByName;"
assert s.count(anchor)==1, ("hdr anchor", s.count(anchor))
s=s.replace(anchor, add)
open(h,"w").write(s)
print("header member added")

# ===== Impl: static-UB handlers =====
c="/mnt/vms/ss-build/UnrealEngine/Engine/Source/Runtime/DawnRHI/Private/DawnCommandContext.cpp"
s=open(c).read()

old_sets=('void FDawnCommandContext::RHISetStaticUniformBuffers(const FUniformBufferStaticBindings& InUniformBuffers)\n'
          '{\n'
          '\t// no-op (WebGPU manages barriers automatically; others are non-essential to the UI render path)\n'
          '}')
new_sets=('void FDawnCommandContext::RHISetStaticUniformBuffers(const FUniformBufferStaticBindings& InUniformBuffers)\n'
          '{\n'
          '\tfor (int32 i = 0; i < InUniformBuffers.GetUniformBufferCount(); ++i)\n'
          '\t{\n'
          '\t\tif (FRHIUniformBuffer* UB = InUniformBuffers.GetUniformBuffer(i))\n'
          '\t\t{\n'
          '\t\t\tStaticUBsByName.Add(UB->GetLayout().GetDebugName(), UB);\n'
          '\t\t\t{ static uint32 _su=0; if(_su++<8) UE_LOG(LogTemp, Warning, TEXT("STATICUB set name=%s size=%u"), *UB->GetLayout().GetDebugName(), UB->GetLayout().ConstantBufferSize); }\n'
          '\t\t}\n'
          '\t}\n'
          '}')
assert s.count(old_sets)==1, ("setss", s.count(old_sets))
s=s.replace(old_sets,new_sets)

old_set=('void FDawnCommandContext::RHISetStaticUniformBuffer(FUniformBufferStaticSlot Slot, FRHIUniformBuffer* UniformBuffer)\n'
         '{\n'
         '\t// no-op (WebGPU manages barriers automatically; others are non-essential to the UI render path)\n'
         '}')
new_set=('void FDawnCommandContext::RHISetStaticUniformBuffer(FUniformBufferStaticSlot Slot, FRHIUniformBuffer* UniformBuffer)\n'
         '{\n'
         '\tif (UniformBuffer)\n'
         '\t{\n'
         '\t\tStaticUBsByName.Add(UniformBuffer->GetLayout().GetDebugName(), UniformBuffer);\n'
         '\t}\n'
         '}')
assert s.count(old_set)==1, ("sets", s.count(old_set))
s=s.replace(old_set,new_set)

# ===== Impl: inject static UBs into unfilled UB bindings before bind-group creation =====
inject_anchor='\tcheckf(ActiveRenderPass, TEXT("DawnRHI: RHISetShaderParameters requires an active render pass"));\n\tfor (TPair<uint32, TArray<WGPUBindGroupEntry>>& Pair : EntriesByGroup)'
inject_code=(
'\t// Fill reflected UniformBuffer bindings still empty after resources + loose $Globals\n'
'\t// from the bound static UBs (Slate ViewProjection @binding(0) arrives this way).\n'
'\tif (StaticUBsByName.Num() > 0 && CurrentPSO->VertexShader && CurrentPSO->PixelShader)\n'
'\t{\n'
'\t\tauto InjectStatic = [&](const TArray<FDawnShaderBinding>& Bindings)\n'
'\t\t{\n'
'\t\t\tfor (const FDawnShaderBinding& B : Bindings)\n'
'\t\t\t{\n'
'\t\t\t\tif (B.Kind != EDawnShaderBindingKind::UniformBuffer) { continue; }\n'
'\t\t\t\tTArray<WGPUBindGroupEntry>& GroupEntries = EntriesByGroup.FindOrAdd(B.Group);\n'
'\t\t\t\tbool bFilled = false;\n'
'\t\t\t\tfor (const WGPUBindGroupEntry& E : GroupEntries) { if (E.binding == B.Binding) { bFilled = true; break; } }\n'
'\t\t\t\tif (bFilled) { continue; }\n'
'\t\t\t\tFRHIUniformBuffer* Found = nullptr;\n'
'\t\t\t\tfor (const TPair<FString, FRHIUniformBuffer*>& P : StaticUBsByName)\n'
'\t\t\t\t{\n'
'\t\t\t\t\tif (!B.Name.IsEmpty() && (B.Name == P.Key || B.Name.Contains(P.Key) || P.Key.Contains(B.Name))) { Found = P.Value; break; }\n'
'\t\t\t\t}\n'
'\t\t\t\tif (!Found && StaticUBsByName.Num() == 1) { Found = StaticUBsByName.CreateConstIterator().Value(); }\n'
'\t\t\t\tif (!Found) { continue; }\n'
'\t\t\t\tFDawnUniformBuffer* DUB = static_cast<FDawnUniformBuffer*>(Found);\n'
'\t\t\t\tif (!DUB->Buffer) { continue; }\n'
'\t\t\t\tWGPUBindGroupEntry Entry = {};\n'
'\t\t\t\tEntry.binding = B.Binding;\n'
'\t\t\t\tEntry.buffer = DUB->Buffer;\n'
'\t\t\t\tEntry.size = DUB->GetLayout().ConstantBufferSize;\n'
'\t\t\t\tGroupEntries.Add(Entry);\n'
'\t\t\t\t{ static uint32 _si=0; if(_si++<8) UE_LOG(LogTemp, Warning, TEXT("STATICINJECT grp=%u bind=%u bindname=%s size=%u"), B.Group, B.Binding, *B.Name, DUB->GetLayout().ConstantBufferSize); }\n'
'\t\t\t}\n'
'\t\t};\n'
'\t\tInjectStatic(CurrentPSO->VertexShader->Bindings);\n'
'\t\tInjectStatic(CurrentPSO->PixelShader->Bindings);\n'
'\t}\n\n'
+inject_anchor)
assert s.count(inject_anchor)==1, ("inject", s.count(inject_anchor))
s=s.replace(inject_anchor, inject_code)

open(c,"w").write(s)
print("static-UB handlers + injection applied")
