// DawnRHITest wasm platform stubs — SimplyStream (wasm) target only.
//
// Core's SimplyStream platform-extension code (SimplyStreamPlatformMemory.cpp
// / SimplyStreamPlatformProcess.cpp — ordinary open Core code shipped as part
// of the platform, NOT the closed WebGPURHI/WebGPUShaderFormat modules) each
// declare one global `extern` and expect it defined elsewhere:
//   extern uint64 GTotalMemoryAvailable;   // SimplyStreamPlatformMemory.cpp
//   extern std::string project_name;       // SimplyStreamPlatformProcess.cpp
// Normally these come from SimplyStream's precompiled JS-glue platform
// objects (see RECIPE.md's existing, separate build path, which links those
// in via PublicAdditionalLibraries). DawnRHI's clean-path port deliberately
// never links SimplyStream's precompiled objects, so building any program
// for this platform needs its own trivial definitions of these two globals
// -- found as the only two undefined symbols at the first real wasm link
// attempt of this target. Nothing here references WebGPURHI/WebGPUShaderFormat
// in any way.
#include "CoreMinimal.h"

// __EMSCRIPTEN__ (not a hypothesized PLATFORM_SIMPLYSTREAM macro) because
// it's the exact guard SimplyStreamPlatformMemory.cpp itself already uses
// for platform-conditional code in this codebase -- confirmed reliable by
// direct inspection rather than assumed by UBT per-platform-macro convention.
#if defined(__EMSCRIPTEN__)
#include <string>

// FSimplyStreamPlatformMemory::GetConstants() only uses this to size
// FPlatformMemoryConstants::TotalPhysical/TotalPhysicalGB and for a
// friendly "Memory total: ...GB" log line -- not load-bearing for RHI
// init. Matches this target's --initial-memory=4192206848 link setting
// (4GB) rather than a guess.
uint64 GTotalMemoryAvailable = 4192206848ull;

// FSimplyStreamPlatformProcess::ExecutableName() only falls back to this
// when FApp::HasProjectName() is false; DawnRHITest always has a project
// name (LaunchModuleName), so this is linked but never actually read.
std::string project_name = "DawnRHITest";

// SimplyStreamToolChain.cs unconditionally passes `-Dstrncpy=strncpy2` to
// every compile for this platform (SetUpEnvironment's Arguments.Add
// list — a generic toolchain-wide C-runtime rename, nothing to do with
// WebGPURHI/WebGPUShaderFormat). Every call to strncpy() anywhere in the
// engine, including inside libc's own <cstring> declaration once that
// header is preprocessed under this -D, is therefore textually rewritten
// to call a symbol literally named `strncpy2`. SimplyStream's own
// precompiled/vendor toolchain artifacts apparently ship a matching
// custom-built libc that exports strncpy under that renamed symbol (the
// "lib-5.0.7-up-mt" third-party lib path h5conf logs); stock emsdk's
// unmodified musl sysroot only ever exports the real `strncpy`, so
// without this shim every renamed call site is left dangling at link/
// runtime -- confirmed live in headless Chrome: static-initializer setup
// (__wasm_call_ctors, before main() even runs) aborts with
// "missing function: strncpy2".
//
// Fix: locally #undef the macro so we can both declare and call the
// *real* libc symbol under its true name, then define strncpy2() as a
// trivial forwarder. (Naively writing `return strncpy(...)` without the
// #undef would itself get rewritten to `strncpy2(...)`, i.e. infinite
// self-recursion -- the macro applies to every raw `strncpy` token in
// this translation unit too, not just other modules'.)
#pragma push_macro("strncpy")
#undef strncpy
extern "C" char* strncpy(char* Dest, const char* Src, size_t Count);
extern "C" char* strncpy2(char* Dest, const char* Src, size_t Count)
{
	return strncpy(Dest, Src, Count);
}
#pragma pop_macro("strncpy")
#endif
