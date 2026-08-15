import sys

# ---- 1) Header: pending-entries members + FlushPendingBindGroups decl ----
h="/mnt/vms/ss-build/UnrealEngine/Engine/Source/Runtime/DawnRHI/Public/DawnCommandContext.h"
s=open(h).read()
anchor="\tTMap<FString, FRHIUniformBuffer*> StaticUBsByName;"
add=(anchor+"\n\n"
 "\t// Bind-group entries accumulated across the SEPARATE per-stage RHISetShaderParameters\n"
 "\t// calls (VS + PS are distinct calls for material/scene draws). The bind group is built\n"
 "\t// at DRAW time from all of them -- per-call creation is always partial. Cleared on\n"
 "\t// RHISetGraphicsPipelineState.\n"
 "\tTMap<uint32, TMap<uint32, WGPUBindGroupEntry>> PendingBindEntries;\n"
 "\tTArray<WGPUBuffer> PendingTransientUBs;\n"
 "\tvoid FlushPendingBindGroups();")
assert s.count(anchor)==1, ("hdr", s.count(anchor))
s=s.replace(anchor,add); open(h,"w").write(s); print("header: pending members added")

# ---- 2) .cpp changes ----
c="/mnt/vms/ss-build/UnrealEngine/Engine/Source/Runtime/DawnRHI/Private/DawnCommandContext.cpp"
s=open(c).read()

# 2a) RHISetGraphicsPipelineState: reset pending on new PSO
old_pso="\twgpuRenderPassEncoderSetPipeline(ActiveRenderPass, PSO->Pipeline);\n\tCurrentPSO = PSO;\n}"
new_pso=("\twgpuRenderPassEncoderSetPipeline(ActiveRenderPass, PSO->Pipeline);\n"
 "\tCurrentPSO = PSO;\n"
 "\t// New shader/PSO: drop the previous draw's accumulated bind-group entries + transient UBs.\n"
 "\tPendingBindEntries.Reset();\n"
 "\tfor (WGPUBuffer UB : PendingTransientUBs) { wgpuBufferRelease(UB); }\n"
 "\tPendingTransientUBs.Reset();\n}")
assert s.count(old_pso)==1, ("pso", s.count(old_pso))
s=s.replace(old_pso,new_pso)

# 2b) RHISetShaderParameters: replace static-inject + BGMISMATCH + create-loop + release
#     (from the "// Fill reflected UniformBuffer bindings" comment to the function's close)
start_marker="\t// Fill reflected UniformBuffer bindings still empty after resources + loose $Globals"
end_marker="\tfor (WGPUBuffer UB : TransientUBs) { wgpuBufferRelease(UB); }\n}"
si=s.find(start_marker); ei=s.find(end_marker)
assert si!=-1 and ei!=-1 and ei>si, ("markers", si, ei)
ei_end=ei+len(end_marker)
merge=("\t// Accumulate this call's entries into the pending set (keyed by binding so a later\n"
 "\t// stage's call extends rather than replaces). The bind group is created at DRAW time in\n"
 "\t// FlushPendingBindGroups() from ALL the accumulated VS+PS calls.\n"
 "\tfor (const TPair<uint32, TArray<WGPUBindGroupEntry>>& Pair : EntriesByGroup)\n"
 "\t{\n"
 "\t\tTMap<uint32, WGPUBindGroupEntry>& Dst = PendingBindEntries.FindOrAdd(Pair.Key);\n"
 "\t\tfor (const WGPUBindGroupEntry& E : Pair.Value) { Dst.Add(E.binding, E); }\n"
 "\t}\n"
 "\tfor (WGPUBuffer UB : TransientUBs) { PendingTransientUBs.Add(UB); }\n"
 "}")
s=s[:si]+merge+s[ei_end:]
print("RHISetShaderParameters: now accumulates into PendingBindEntries")

# 2c) Insert FlushPendingBindGroups() before RHIDrawPrimitive
draw_anchor="void FDawnCommandContext::RHIDrawPrimitive(uint32 BaseVertexIndex, uint32 NumPrimitives, uint32 NumInstances)"
flush=(
"void FDawnCommandContext::FlushPendingBindGroups()\n"
"{\n"
"\tif (!CurrentPSO || !ActiveRenderPass || CurrentPSO->BindGroupLayouts.Num() == 0) { return; }\n"
"\t// Build the per-group reflected layout (binding + kind + name) from VS+PS.\n"
"\tTMap<uint32, TArray<FDawnShaderBinding>> LayoutByGroup;\n"
"\tauto AddLayout = [&](const TArray<FDawnShaderBinding>& Bs)\n"
"\t{\n"
"\t\tfor (const FDawnShaderBinding& B : Bs)\n"
"\t\t{\n"
"\t\t\tTArray<FDawnShaderBinding>& L = LayoutByGroup.FindOrAdd(B.Group);\n"
"\t\t\tbool bDup=false; for (const FDawnShaderBinding& X : L) { if (X.Binding==B.Binding) { bDup=true; break; } }\n"
"\t\t\tif (!bDup) { L.Add(B); }\n"
"\t\t}\n"
"\t};\n"
"\tif (CurrentPSO->VertexShader) { AddLayout(CurrentPSO->VertexShader->Bindings); }\n"
"\tif (CurrentPSO->PixelShader)  { AddLayout(CurrentPSO->PixelShader->Bindings); }\n"
"\tfor (const TPair<uint32, TArray<FDawnShaderBinding>>& LG : LayoutByGroup)\n"
"\t{\n"
"\t\tconst uint32 Group = LG.Key;\n"
"\t\tif (Group >= (uint32)CurrentPSO->BindGroupLayouts.Num()) { continue; }\n"
"\t\tconst TMap<uint32, WGPUBindGroupEntry>* Pending = PendingBindEntries.Find(Group);\n"
"\t\tTArray<WGPUBindGroupEntry> Entries;\n"
"\t\tfor (const FDawnShaderBinding& B : LG.Value)\n"
"\t\t{\n"
"\t\t\tif (Pending) { if (const WGPUBindGroupEntry* E = Pending->Find(B.Binding)) { Entries.Add(*E); continue; } }\n"
"\t\t\t// Static UB (View/SlateView/etc) matched by name for still-empty uniform buffers.\n"
"\t\t\tif (B.Kind == EDawnShaderBindingKind::UniformBuffer && StaticUBsByName.Num() > 0)\n"
"\t\t\t{\n"
"\t\t\t\tFRHIUniformBuffer* Found = nullptr;\n"
"\t\t\t\tfor (const TPair<FString, FRHIUniformBuffer*>& P : StaticUBsByName)\n"
"\t\t\t\t\tif (!B.Name.IsEmpty() && (B.Name == P.Key || B.Name.Contains(P.Key) || P.Key.Contains(B.Name))) { Found = P.Value; break; }\n"
"\t\t\t\tif (Found) { FDawnUniformBuffer* DUB = static_cast<FDawnUniformBuffer*>(Found); if (DUB->Buffer) { WGPUBindGroupEntry E={}; E.binding=B.Binding; E.buffer=DUB->Buffer; E.size=DUB->GetLayout().ConstantBufferSize; Entries.Add(E); continue; } }\n"
"\t\t\t}\n"
"\t\t}\n"
"\t\tif (Entries.Num() != LG.Value.Num()) { static uint32 _bi=0; if(_bi++<24) UE_LOG(LogDawnRHI, Warning, TEXT(\"BGINCOMPLETE grp=%u have=%d need=%d\"), Group, Entries.Num(), LG.Value.Num()); continue; }\n"
"\t\tWGPUBindGroupDescriptor BgDesc = {};\n"
"\t\tBgDesc.layout = CurrentPSO->BindGroupLayouts[Group];\n"
"\t\tBgDesc.entryCount = Entries.Num();\n"
"\t\tBgDesc.entries = Entries.GetData();\n"
"\t\tWGPUBindGroup BindGroup = wgpuDeviceCreateBindGroup(Owner->GetDevice(), &BgDesc);\n"
"\t\tif (BindGroup) { wgpuRenderPassEncoderSetBindGroup(ActiveRenderPass, Group, BindGroup, 0, nullptr); wgpuBindGroupRelease(BindGroup); }\n"
"\t}\n"
"}\n\n"
+draw_anchor)
assert s.count(draw_anchor)==1, ("draw_anchor", s.count(draw_anchor))
s=s.replace(draw_anchor, flush)

# 2d) call FlushPendingBindGroups() at the start of both draw fns
for sig in [
  "void FDawnCommandContext::RHIDrawPrimitive(uint32 BaseVertexIndex, uint32 NumPrimitives, uint32 NumInstances)\n{\n\tcheckf(ActiveRenderPass, TEXT(\"DawnRHI Stage 1: RHIDrawPrimitive requires an active render pass\"));",
  "void FDawnCommandContext::RHIDrawIndexedPrimitive(FRHIBuffer* IndexBufferRHI, int32 BaseVertexIndex, uint32 FirstInstance, uint32 NumVertices, uint32 StartIndex, uint32 NumPrimitives, uint32 NumInstances)\n{",
]:
    assert s.count(sig)==1, ("drawsig", sig[:60], s.count(sig))
    s=s.replace(sig, sig+"\n\tFlushPendingBindGroups();")
print("draw functions call FlushPendingBindGroups()")
open(c,"w").write(s)
print("DONE: accumulate-at-draw restructure applied")
