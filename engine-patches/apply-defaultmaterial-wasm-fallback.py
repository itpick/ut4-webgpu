#!/usr/bin/env python3
# WebGPU bring-up: on wasm, [/Script/Engine.Engine] DefaultMaterialName resolves
# EMPTY from GConfig (config-merge/timing: the section isn't in the merged
# GEngineIni when InitDefaultMaterials runs, though RendererSettings CVars are).
# And the resolver uses FindObject (must be pre-loaded). Fall back to LOADing the
# config path, else the known engine default WorldGridMaterial, so GDefaultMaterials
# populates instead of asserting null. Scoped to PLATFORM_WASM.
import io, sys
F = "/mnt/vms/ss-build/UnrealEngine/Engine/Source/Runtime/Engine/Private/Materials/Material.cpp"
s = io.open(F, encoding="utf-8").read()
if "WebGPU bring-up: default-material wasm fallback" in s:
    print("already patched"); sys.exit(0)
anchor = (
"\t\t\t\tFString ResolvedPath = ResolveIniObjectsReference(GDefaultMaterialNames[Domain]);\n"
"\t\t\t\tGDefaultMaterials[Domain] = FindObject<UMaterial>(nullptr, *ResolvedPath);\n"
)
add = (
"#if PLATFORM_WASM\n"
"\t\t\t\t// WebGPU bring-up: default-material wasm fallback. Config resolves empty and\n"
"\t\t\t\t// FindObject needs the material pre-loaded; LOAD the config path, else the\n"
"\t\t\t\t// engine default WorldGridMaterial, so GDefaultMaterials never asserts null.\n"
"\t\t\t\tif (!GDefaultMaterials[Domain])\n"
"\t\t\t\t{\n"
"\t\t\t\t\tif (!ResolvedPath.IsEmpty())\n"
"\t\t\t\t\t{\n"
"\t\t\t\t\t\tGDefaultMaterials[Domain] = LoadObject<UMaterial>(nullptr, *ResolvedPath);\n"
"\t\t\t\t\t}\n"
"\t\t\t\t\tif (!GDefaultMaterials[Domain])\n"
"\t\t\t\t\t{\n"
"\t\t\t\t\t\tGDefaultMaterials[Domain] = LoadObject<UMaterial>(nullptr, TEXT(\"/Engine/EngineMaterials/WorldGridMaterial.WorldGridMaterial\"));\n"
"\t\t\t\t\t}\n"
"\t\t\t\t}\n"
"#endif\n"
)
if anchor not in s:
    print("ANCHOR NOT FOUND", file=sys.stderr); sys.exit(1)
s = s.replace(anchor, anchor + add, 1)
io.open(F, "w", encoding="utf-8").write(s)
print("patched Material.cpp default-material wasm fallback")
