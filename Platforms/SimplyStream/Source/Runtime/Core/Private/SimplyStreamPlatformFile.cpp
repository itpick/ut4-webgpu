// SimplyStream (emscripten/wasm) physical file layer — open, clean-room.
// Emscripten provides a POSIX filesystem, so we reuse Epic's own open
// FUnixPlatformFile implementation wholesale (compiled here via source
// include, since UBT only compiles Private/Unix/*.cpp for the Unix platform
// group and SimplyStream is in the Mobile group). This intentionally does
// NOT use SimplyStream's closed precompiled platform-file objects.

// POSIX system headers the Unix impl relies on (normally supplied by the
// Unix platform's compiler pre-setup; pulled in explicitly here because this
// TU compiles under the SimplyStream/Mobile platform group).
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <utime.h>

// glibc-internal typedef used by one cast in the Unix impl; musl/Emscripten
// only provides the standard time_t, so alias it for this translation unit.
#ifndef __time_t
#define __time_t time_t
#endif

#include "Unix/UnixPlatformFile.h"

// Bring in Epic's open POSIX FUnixPlatformFile method definitions.
#include "Unix/UnixPlatformFile.cpp"

// Globals normally defined in Unix/UnixPlatformMemory.cpp (not compiled for the
// SimplyStream platform, which has its own PlatformMemory) but referenced by the
// reused open FUnixPlatformFile impl. Provide open definitions here.
int32 CORE_API GMaxNumberFileMappingCache = 100;
bool GAllowExclusiveLockOnWrite = true;

IPlatformFile& IPlatformFile::GetPlatformPhysical()
{
	static FUnixPlatformFile Singleton;
	return Singleton;
}
