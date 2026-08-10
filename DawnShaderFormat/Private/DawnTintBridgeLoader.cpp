// See DawnTintBridgeLoader.h for the full rationale.
#include "DawnTintBridgeLoader.h"

#include "Misc/Paths.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <dlfcn.h>

#ifndef RTLD_DEEPBIND
#define RTLD_DEEPBIND 0x00008
#endif

typedef FDawnTintCookResult (*FCookFn)(const unsigned int*, unsigned int);
typedef void (*FFreeFn)(FDawnTintCookResult*);

FDawnTintBridgeLoader::~FDawnTintBridgeLoader()
{
	// Deliberately do not dlclose() -- matches FDawnShaderConductorLoader's
	// rationale (third-party static state not safe to unload mid-process).
}

bool FDawnTintBridgeLoader::Init(FString& OutError)
{
	if (bInitialized)
	{
		return true;
	}

	const FString SoPath = FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/ThirdParty/DawnTint/Linux/libDawnTintBridge.so"));

	Handle = dlopen(TCHAR_TO_ANSI(*SoPath), RTLD_NOW | RTLD_DEEPBIND);
	if (!Handle)
	{
		OutError = FString::Printf(TEXT("dlopen(%s) failed: %s"), *SoPath, ANSI_TO_TCHAR(dlerror()));
		return false;
	}

	CookFnPtr = dlsym(Handle, "Dawn_LegalizeAndCookSpirvToWgsl");
	FreeFnPtr = dlsym(Handle, "Dawn_FreeTintCookResult");
	if (!CookFnPtr || !FreeFnPtr)
	{
		OutError = FString::Printf(TEXT("dlsym failed: %s"), ANSI_TO_TCHAR(dlerror()));
		return false;
	}

	bInitialized = true;
	return true;
}

FDawnTintCookResult FDawnTintBridgeLoader::LegalizeAndCookSpirvToWgsl(const uint32* SpirvWords, uint32 WordCount)
{
	check(bInitialized);
	static_assert(sizeof(uint32) == sizeof(unsigned int), "word size mismatch");
	return reinterpret_cast<FCookFn>(CookFnPtr)(reinterpret_cast<const unsigned int*>(SpirvWords), WordCount);
}

void FDawnTintBridgeLoader::FreeResult(FDawnTintCookResult* Result)
{
	check(bInitialized);
	reinterpret_cast<FFreeFn>(FreeFnPtr)(Result);
}

FDawnTintBridgeLoader& GetDawnTintBridgeLoader()
{
	static FDawnTintBridgeLoader Loader;
	return Loader;
}
