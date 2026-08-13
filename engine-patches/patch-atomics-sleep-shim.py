#!/usr/bin/env python3
# Post-build JS patch: emscripten 6.0.6 lacks __emscripten_atomics_sleep (the
# engine/SimplyStream objects reference it); the generated glue stubs it as an
# abort(). Replace the stub with a worker-side bounded busy-sleep so pthreads
# proceed. Re-run after every wasm link (UnrealTournament.js is regenerated).
# Usage: patch-atomics-sleep-shim.py /path/to/UnrealTournament.js
import io, sys
F = sys.argv[1]
s = io.open(F, encoding="utf-8").read()
old = 'function ___emscripten_atomics_sleep(...args) {\n  abort("missing function: __emscripten_atomics_sleep");\n}'
new = ('function ___emscripten_atomics_sleep(msecs) {\n'
       '  // WebGPU bring-up shim: emscripten 6.0.6 lacks this symbol. Worker-side\n'
       '  // bounded busy-sleep so the pthread proceeds instead of aborting.\n'
       '  if (typeof msecs !== "number" || !(msecs > 0)) return;\n'
       '  var end = performance.now() + Math.min(msecs, 100);\n'
       '  while (performance.now() < end) {}\n'
       '}')
if "WebGPU bring-up shim" in s:
    print("already patched")
elif old not in s:
    print("ANCHOR NOT FOUND", file=sys.stderr); sys.exit(1)
else:
    io.open(F, "w", encoding="utf-8").write(s.replace(old, new, 1))
    print("patched __emscripten_atomics_sleep shim")
