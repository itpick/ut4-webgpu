# Change 1: PF_A8 -> RGBA8Unorm (WebGPU has no A8; coverage expanded to all channels at upload)
h="/mnt/vms/ss-build/UnrealEngine/Engine/Source/Runtime/DawnRHI/Public/DawnResources.h"
s=open(h).read()
old_a8="\tcase PF_A8:            return WGPUTextureFormat_R8Unorm;   // 1B"
new_a8="\tcase PF_A8:            return WGPUTextureFormat_RGBA8Unorm; // no WebGPU A8; 1B coverage expanded to RGBA (a=cov) at upload so the grayscale-font shader's .w read works"
assert s.count(old_a8)==1, ("a8fmt", s.count(old_a8))
s=s.replace(old_a8,new_a8)
open(h,"w").write(s)
print("A8->RGBA8 format mapping applied")

# Change 2: RHIUnlockTexture — expand A8 (1B) staging to RGBA8 before upload
c="/mnt/vms/ss-build/UnrealEngine/Engine/Source/Runtime/DawnRHI/Private/DawnDynamicRHI.cpp"
s=open(c).read()
old_up='\t\t\twgpuQueueWriteTexture(Queue, &Dst, Tex->LockStaging, (size_t)Tex->LockSize, &Layout, &WriteExtent);\n\t\t}'
new_up=('\t\t\tif (Tex->GetDesc().Format == PF_A8)\n'
        '\t\t\t{\n'
        '\t\t\t\t// A8 has no WebGPU equivalent -> the texture is RGBA8Unorm. Expand the\n'
        '\t\t\t\t// 1-byte coverage into RGBA (all channels = coverage) so the Slate\n'
        '\t\t\t\t// grayscale-font shader (samples .w) reads the glyph coverage, not 1.0.\n'
        '\t\t\t\tconst uint32 W = Tex->LockW, H = Tex->LockH;\n'
        '\t\t\t\tTArray<uint8> Rgba; Rgba.SetNumUninitialized((int32)(W * H * 4));\n'
        '\t\t\t\tconst uint8* Sp = (const uint8*)Tex->LockStaging;\n'
        '\t\t\t\tfor (uint32 y = 0; y < H; ++y)\n'
        '\t\t\t\t{\n'
        '\t\t\t\t\tconst uint8* SR = Sp + (uint64)y * Tex->LockStride;\n'
        '\t\t\t\t\tuint8* DR = Rgba.GetData() + (uint64)y * W * 4;\n'
        '\t\t\t\t\tfor (uint32 x = 0; x < W; ++x) { const uint8 Cov = SR[x]; DR[x*4+0]=Cov; DR[x*4+1]=Cov; DR[x*4+2]=Cov; DR[x*4+3]=Cov; }\n'
        '\t\t\t\t}\n'
        '\t\t\t\tWGPUTexelCopyBufferLayout L2 = {}; L2.bytesPerRow = W * 4; L2.rowsPerImage = H;\n'
        '\t\t\t\twgpuQueueWriteTexture(Queue, &Dst, Rgba.GetData(), (size_t)Rgba.Num(), &L2, &WriteExtent);\n'
        '\t\t\t}\n'
        '\t\t\telse\n'
        '\t\t\t{\n'
        '\t\t\t\twgpuQueueWriteTexture(Queue, &Dst, Tex->LockStaging, (size_t)Tex->LockSize, &Layout, &WriteExtent);\n'
        '\t\t\t}\n'
        '\t\t}')
assert s.count(old_up)==1, ("unlockupload", s.count(old_up))
s=s.replace(old_up,new_up)
open(c,"w").write(s)
print("RHIUnlockTexture A8 expansion applied")
