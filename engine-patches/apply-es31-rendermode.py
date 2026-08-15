# ES3_1 mobile cook: flip the SimplyStream platform RenderMode so the cook targets
# SF_WEBGPU_ES31 (mobile forward) instead of SF_WEBGPU_SM5 (deferred). The deferred
# shader set (compute/TSR/post-process) does not cross-compile to WGSL; the mobile
# forward mesh/material shaders do. Pair with engine-patches/apply-es31-feature-level.py
# (runtime GMaxRHIFeatureLevel=ES3_1) and the ES31 global-shader-cache in regen-bootstrap.
import sys
p = "/mnt/vms/ss-build/UnrealEngine/Engine/Platforms/SimplyStream/Config/SimplyStreamEngine.ini"
s = open(p).read()
if "RenderMode=WebGPUES31" in s:
    print("already ES31"); sys.exit(0)
assert s.count("RenderMode=WebGPUSM5") == 1
open(p, "w").write(s.replace("RenderMode=WebGPUSM5", "RenderMode=WebGPUES31"))
print("RenderMode -> WebGPUES31")
