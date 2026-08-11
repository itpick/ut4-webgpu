// SimplyStream (emscripten/wasm) physical file layer.
// Open, clean-room: reuses Epic's own POSIX FUnixPlatformFile (Emscripten
// exposes a POSIX filesystem: open/read/write/stat/opendir), rather than
// SimplyStream's closed precompiled implementation.
#pragma once

#include "Unix/UnixPlatformFile.h"

// The SimplyStream physical file is Epic's open POSIX file implementation.
typedef FUnixPlatformFile FSimplyStreamPlatformFile;
