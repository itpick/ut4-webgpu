# UT wasm boot: ICU wall = game-thread pthread cannot read the emscripten MEMFS

## Chain of evidence (screenshot-verified)
- Boot reaches `GEngineLoop.PreInit()` on the game thread, dies at `ICUInternationalization.cpp:165` "ICU data directory was not discovered".
- The `.data` DOES contain `/Engine/Content/Internationalization/icudt64l` (`FS_createPath` in the `.js`); DawnRHITest (runs on the emscripten main/proxied thread) finds + loads ICU fine.
- Applied `icu-wasm-force-path-diagnostic.py`: on `PLATFORM_WASM`, if the `DirectoryExists()` gate fails, force `ICUDataDirectory` to the known path instead of Fatal.
- RESULT: cleared the gate (past `:165`) -> now dies deeper at `:185` (`u_init`: "Failed to open ICU data file"). So the game thread cannot READ the ICU `.res` files either, not just the dir check.

## Root cause
`LaunchSimplyStream.cpp` `main()` (FS-visible proxied-main thread) does `pthread_create(_main_thread)` (line 243, reusing main's stack via `emscripten_stack_get_base/end` at line 241) -> the game thread runs `main2()` -> `SIMPLYSTREAM_Init()` -> `PreInit()`. That game-thread pthread does NOT see the main-thread-mounted MEMFS `.data` -> ICU (and, critically, ALL future pak/map content loading) fails.

## Ranked fix options (for a focused emscripten-threading pass)
1. Run the FS-dependent engine init on the emscripten main/proxied thread (like DawnRHITest), not a separate game-thread pthread -- but must preserve the frameSync/RAF pump + WebGPU-thread setup.
2. Make the game thread a proper emscripten pthread with FS proxying (don't reuse main stack at line 241; verify `-pthread` FS syscall proxying covers it).
3. Read ICU (+ later content) bytes on the FS-visible thread and hand them to the engine in memory.

## Furthest boot point (screenshot-verified)
WebGPU device OK -> wasm instantiated -> PathPak resolver -> setProjectStuff -> game thread -> PreInit -> ICU init: forced path clears :165, `u_init` fails at :185. Everything up to the game-thread FS barrier works.
