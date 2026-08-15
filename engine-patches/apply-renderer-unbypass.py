# 1) GameViewportClient.cpp: stop forcing bDisableWorldRendering=true on wasm (ES3_1 materials cook now)
gv="/mnt/vms/ss-build/UnrealEngine/Engine/Source/Runtime/Engine/Private/GameViewportClient.cpp"
s=open(gv).read()
old=('#if PLATFORM_WASM\n'
     '\t// The WEBGPU cook currently emits no material shader maps (not even for the\n'
     '\t// default WorldGridMaterial), so the deferred 3D scene render dereferences an\n'
     '\t// incomplete material and crashes before the Slate UI can draw. Skip the world\n'
     '\t// render on wasm: the canvas is cleared and the Slate menu still draws on top,\n'
     '\t// giving a visible front-end until the material shader cook is fixed.\n'
     '\tbDisableWorldRendering = true;\n'
     '#endif')
new=('#if PLATFORM_WASM\n'
     '\t// ES3_1 mobile forward renderer: material shaders now cross-compile + cook, so\n'
     '\t// enable world rendering. (Was forced off because the SM5 deferred cook had no\n'
     '\t// material shader maps.) Re-enable via a CVar escape hatch for safety.\n'
     '\tstatic const auto* CVarWndrWorld = IConsoleManager::Get().FindConsoleVariable(TEXT("wndr.DisableWorldRendering"));\n'
     '\tif (CVarWndrWorld && CVarWndrWorld->GetInt() != 0) { bDisableWorldRendering = true; }\n'
     '#endif')
assert s.count(old)==1, ("gv", s.count(old))
s=s.replace(old,new)
open(gv,"w").write(s)
print("GameViewportClient: world rendering ENABLED on wasm (CVar wndr.DisableWorldRendering to re-disable)")

# 2) RendererScene.cpp: allow FScene::Update to run (mobile renderer needs the scene populated)
rs="/mnt/vms/ss-build/UnrealEngine/Engine/Source/Runtime/Renderer/Private/RendererScene.cpp"
s=open(rs).read()
old2=('#if PLATFORM_WASM\n'
      '\t// The WEBGPU cook has no material/compute shader maps, so the GPU-scene update\n'
      '\t// (GPU scene scatter upload, light grid injection, lightmap cluster buffers)\n'
      '\t// crashes. The 3D scene is not rendered on wasm, so this whole update is dead\n'
      '\t// work -- skip it so the frame completes and the Slate front-end presents.\n'
      '\t// Remove once material + compute shaders cook for SF_WEBGPU_SM5.\n'
      '\treturn;\n'
      '#endif')
new2=('#if PLATFORM_WASM\n'
      '\t// ES3_1: material/compute shaders cook now; let the scene update run so the\n'
      '\t// mobile forward renderer has primitives. CVar escape hatch kept for safety.\n'
      '\tstatic const auto* CVarWndrSceneUpd = IConsoleManager::Get().FindConsoleVariable(TEXT("wndr.SkipSceneUpdate"));\n'
      '\tif (CVarWndrSceneUpd && CVarWndrSceneUpd->GetInt() != 0) { return; }\n'
      '#endif')
assert s.count(old2)==1, ("rs", s.count(old2))
s=s.replace(old2,new2)
open(rs,"w").write(s)
print("RendererScene: FScene::Update ENABLED on wasm (CVar wndr.SkipSceneUpdate to re-skip)")
