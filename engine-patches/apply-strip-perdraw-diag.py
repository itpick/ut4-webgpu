import re
def strip(path, lines):
    s=open(path).read()
    for ln in lines:
        assert s.count(ln)==1, (path, "count", s.count(ln), ln[:60])
        s=s.replace(ln,"")
    open(path,"w").write(s)

cc="/mnt/vms/ss-build/UnrealEngine/Engine/Source/Runtime/DawnRHI/Private/DawnCommandContext.cpp"
strip(cc, [
 '\tUE_LOG(LogDawnRHI, Warning, TEXT("FLOWDIAG %s"), TEXT("SetStreamSource"));\n',
 '\tUE_LOG(LogDawnRHI, Warning, TEXT("FLOWDIAG %s"), TEXT("SetPSO"));\n',
 '\tUE_LOG(LogDawnRHI, Warning, TEXT("FLOWDIAG %s"), TEXT("DrawPrimitive"));\n',
 '\tUE_LOG(LogDawnRHI, Warning, TEXT("SPDIAG n=%d layouts=%d"), InResourceParameters.Num(), CurrentPSO->BindGroupLayouts.Num());\n',
 '\t\tUE_LOG(LogDawnRHI, Warning, TEXT("SPDIAG   idx=%u type=%d"), Param.Index, (int)Param.Type);\n',
 '\tUE_LOG(LogDawnRHI, Warning, TEXT("FLOWDIAG %s"), TEXT("DrawIndexed"));\n',
])

dd="/mnt/vms/ss-build/UnrealEngine/Engine/Source/Runtime/DawnRHI/Private/DawnDynamicRHI.cpp"
strip(dd, [
 '\tUE_LOG(LogDawnRHI, Warning, TEXT("LOCKDIAG fmt=%d mip=%u W=%u H=%u stride=%u size=%llu blkX=%d blkB=%d"), (int)Desc.Format, Arguments.MipIndex, MipW, MipH, Stride, (unsigned long long)Size, FmtInfo.BlockSizeX, FmtInfo.BlockBytes);\n',
 '\tUE_LOG(LogDawnRHI, Warning, TEXT("UNLOCKDIAG W=%u H=%u stride=%u size=%llu rows=%u blkX=%u"), Tex->LockW, Tex->LockH, Tex->LockStride, (unsigned long long)Tex->LockSize, Tex->LockRows, Tex->LockBlockX);\n',
 '\tUE_LOG(LogDawnRHI, Warning, TEXT("UNLOCKDONE"));\n',
 '\tUE_LOG(LogDawnRHI, Warning, TEXT("UPD2DIAG fmt=%d W=%u H=%u srcX=%u srcY=%u dstX=%u dstY=%u pitch=%u blkB=%d"), (int)Desc.Format, UpdateRegion.Width, UpdateRegion.Height, UpdateRegion.SrcX, UpdateRegion.SrcY, UpdateRegion.DestX, UpdateRegion.DestY, SourcePitch, Fmt.BlockBytes);\n',
])
print("stripped per-draw diagnostics (kept BGMISMATCH + throttled diags)")
