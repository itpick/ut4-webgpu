# Set GRHIMaxDispatchThreadGroupsPerDimension in DawnRHI init. DawnRHI runs no compute, but
# UE still computes dispatch configs on the CPU (e.g. GPU-scene scatter in
# UpdateAllPrimitiveSceneInfos). An unset (0) max-dispatch global asserts (div-by-0) ->
# wasm memory-access-out-of-bounds crash. Set a valid ceiling; the dispatch is a no-op anyway.
p="/mnt/vms/ss-build/UnrealEngine/Engine/Source/Runtime/DawnRHI/Private/DawnDynamicRHI.cpp"
s=open(p).read()
anchor="\tGMaxRHIFeatureLevel = ERHIFeatureLevel::ES3_1;"
if "GRHIMaxDispatchThreadGroupsPerDimension" in s:
    print("already set"); raise SystemExit
assert s.count(anchor)==1
s=s.replace(anchor, anchor+"\n\tGRHIMaxDispatchThreadGroupsPerDimension = FIntVector(65535, 65535, 65535);", 1)
open(p,"w").write(s); print("set GRHIMaxDispatchThreadGroupsPerDimension")
