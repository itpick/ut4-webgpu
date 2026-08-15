# ES3_1 mobile forward renderer — path to 3D / gameplay (WIP, 2026-08-15)

Branch goal: get in-game 3D rendering by running the ES3_1 mobile forward renderer
instead of the SM5 deferred renderer, whose compute/TSR/post-process shaders do not
cross-compile to WGSL (Tint).

## Done this session (menu to interactive to ES3_1 3D foundation)
- Menu renders and is interactive (mouse/keyboard input; both browser callbacks were stubs,
  and the queue drain do_events() was never called).
- ES3_1 pivot: RenderMode=WebGPUES31 cook plus GMaxRHIFeatureLevel=ES3_1 runtime. Menu renders
  identically on the mobile platform, 0 GPU errors. Material/mesh shaders cross-compile
  (only deferred/compute/post-process fail, all optional).
- Renderer un-bypassed (bDisableWorldRendering plus FScene::Update). Compute neutralized
  (DawnRHI has no compute, and the mobile compute shaders do not cross-compile) so compute
  passes are no-ops.
- 3D geometry now attempts to render (Player Settings character preview).

## Final blocker: material-shader BIND GROUP construction
DawnRHI's bind-group builder is Slate-specific (one SlateView static UB plus loose Globals
plus one tex/sampler). Material shaders need View/Primitive/Material UBs plus material
textures/samplers. Exact case from the BGMISMATCH diagnostic: expected layout
[1 VS-UB, 102 PS-UB, 103 Texture, 104 Sampler, 105 PS-UB] but the builder produced
[0, 105, 1, 102]. Reflected binding names are empty, so static UBs cannot be matched by name.

Fix path:
- cook side: emit binding names in the reflection so View/Primitive/Material/SlateView UBs
  match their slots by name.
- runtime side: route UE uniform-buffer bindings and material textures/samplers to the
  correct WGSL binding, stop the blanket static-UB injection, and drop resources whose
  Param.Index is not present in the reflected layout.

## Config levers
- RenderMode: Platforms/SimplyStream/Config/SimplyStreamEngine.ini (WebGPUSM5 / WebGPUES31 / WebGPUALL)
- Runtime feature level: DawnDynamicRHI.cpp GMaxRHIFeatureLevel and GMaxRHIShaderPlatform
- Compute-disable CVars: -dpcvars=r.Mobile.SupportGPUScene=0,r.SkinCache.Mode=0,... (harness index.html)

## Apply order (engine-patches)
1. apply-es31-rendermode.py (cook target ES31)  -> then recook + regen-bootstrap (ES31 shader cache)
2. apply-es31-feature-level.py (runtime ES3_1)
3. apply-renderer-unbypass.py (world render + scene update)
4. apply-compute-noop.py (compute passes become no-ops, missing shaders non-fatal)
5. apply-static-ub-bindgroup.py, apply-blend-state.py, apply-a8-font-rgba.py (menu rendering)
6. apply-input-mouse-move-drain.py, apply-keyboard-stub-fix.py (input)
7. diag-bindgroup-mismatch.py (BGMISMATCH diagnostic, for the ongoing material bind-group work)
