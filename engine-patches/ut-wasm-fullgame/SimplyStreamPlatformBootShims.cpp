// Open, clean-room definitions for a small set of free functions that
// SimplyStream's own open Core code (e.g. SimplyStreamPlatformMisc.cpp)
// declares 'extern' and calls during engine bootstrap, but whose
// definitions live only in SimplyStream's closed precompiled objects (which
// this build deliberately does not link). Providing open definitions here
// lets GEngineLoop.PreInit() proceed without those closed artifacts.
#include "CoreTypes.h"

// Whether the platform should run in a reduced-parallelism mode on very
// low-core hosts. Safe open default: false (use normal threading). The
// browser reports real core count via navigator.hardwareConcurrency; a
// future refinement can consult FPlatformMisc::NumberOfCores() here.
// Bare DawnRHITest program (WITH_ENGINE=0) only: the full game Core provides
// IsUsingLowCoreCount() itself -> guard to avoid a duplicate symbol.
#if !WITH_ENGINE
bool IsUsingLowCoreCount()
{
	return false;
}
#endif
