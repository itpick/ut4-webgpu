// See DawnShaderConductorLoader.h for the full rationale (libc++ symbol
// interposition between statically-self-contained C++ shared objects).
// Promoted verbatim from DawnRHITest's copy (see that header's history).
#include "DawnShaderConductorLoader.h"

#include "Misc/Paths.h"
#include "HAL/PlatformProcess.h"

#ifndef _GNU_SOURCE
#define _GNU_SOURCE // ensure RTLD_DEEPBIND is visible from <dlfcn.h> below
#endif
#include <dlfcn.h>

#ifndef RTLD_DEEPBIND
#define RTLD_DEEPBIND 0x00008 // glibc extension, always this value on Linux x86_64
#endif

using namespace ShaderConductor;

// ---- Raw mirror of ResultDesc's memory layout ----
// We deliberately never let the compiler generate calls to
// ShaderConductor::Blob's ctor/dtor (those live inside the .so and doing so
// would force a compile-time link, defeating the whole point of loading via
// dlopen). Blob's only data member is a private `BlobImpl* m_impl`
// (see ShaderConductor.hpp) — standard-layout, no virtuals, so mirroring
// field order with a plain pointer reproduces the identical size/alignment/
// padding as the real Blob/ResultDesc types.
namespace
{
	struct FRawBlob { void* Impl; };
	struct FRawReflectionResultDesc { FRawBlob Descs; uint32 DescCount; uint32 InstructionCount; };
	struct FRawResultDesc
	{
		FRawBlob Target;
		bool bIsText;
		FRawBlob ErrorWarningMsg;
		bool bHasError;
		FRawReflectionResultDesc Reflection;
	};

	// Itanium ABI: Compiler::Compile(SourceDesc,Options,TargetDesc) returns
	// ResultDesc BY VALUE; since ResultDesc is non-trivial (Blob has a
	// user-declared dtor), the real ABI is a hidden sret pointer as the
	// FIRST argument, before the explicit ones.
	typedef void (*FCompileFn)(void* /*sret FRawResultDesc*/, const Compiler::SourceDesc&, const Compiler::Options&, const Compiler::TargetDesc&);
	typedef const void* (*FBlobDataFn)(const void* /*this*/);
	typedef uint32 (*FBlobSizeFn)(const void* /*this*/);

	// Exact mangled names, confirmed via `nm -D --defined-only libShaderConductor.so`.
	const char* kMangledCompile = "_ZN15ShaderConductor8Compiler7CompileERKNS0_10SourceDescERKNS0_7OptionsERKNS0_10TargetDescE";
	const char* kMangledBlobData = "_ZNK15ShaderConductor4Blob4DataEv";
	const char* kMangledBlobSize = "_ZNK15ShaderConductor4Blob4SizeEv";
}

FDawnShaderConductorLoader::~FDawnShaderConductorLoader()
{
	// Deliberately do not dlclose(): ShaderConductor/DXC keep process-wide
	// static state (thread pools, DXC COM-style singletons) that are not
	// safe to unload out from under a still-running process.
}

bool FDawnShaderConductorLoader::Init(FString& OutError)
{
	if (bInitialized)
	{
		return true;
	}

	const FString BinDir = FPaths::Combine(FPaths::EngineDir(), TEXT("Binaries/ThirdParty/ShaderConductor/Linux/x86_64-unknown-linux-gnu"));
	const FString DxcPath = FPaths::Combine(BinDir, TEXT("libdxcompiler.so"));
	const FString ScPath = FPaths::Combine(BinDir, TEXT("libShaderConductor.so"));

	// RTLD_DEEPBIND: prefer this object's own symbols over the global scope
	// when resolving references made from *within* it — see header comment.
	// RTLD_GLOBAL on the dxcompiler load so ShaderConductor.so's own
	// DT_NEEDED reference to it can still find it (dlopen's internal
	// dependency resolution consults the global scope by soname).
	DxcHandle = dlopen(TCHAR_TO_ANSI(*DxcPath), RTLD_NOW | RTLD_DEEPBIND | RTLD_GLOBAL);
	if (!DxcHandle)
	{
		OutError = FString::Printf(TEXT("dlopen(%s) failed: %s"), *DxcPath, ANSI_TO_TCHAR(dlerror()));
		return false;
	}

	ScHandle = dlopen(TCHAR_TO_ANSI(*ScPath), RTLD_NOW | RTLD_DEEPBIND);
	if (!ScHandle)
	{
		OutError = FString::Printf(TEXT("dlopen(%s) failed: %s"), *ScPath, ANSI_TO_TCHAR(dlerror()));
		return false;
	}

	CompileFnPtr = dlsym(ScHandle, kMangledCompile);
	BlobDataFnPtr = dlsym(ScHandle, kMangledBlobData);
	BlobSizeFnPtr = dlsym(ScHandle, kMangledBlobSize);
	if (!CompileFnPtr || !BlobDataFnPtr || !BlobSizeFnPtr)
	{
		OutError = FString::Printf(TEXT("dlsym failed: %s"), ANSI_TO_TCHAR(dlerror()));
		return false;
	}

	bInitialized = true;
	return true;
}

bool FDawnShaderConductorLoader::CompileHlslToSpirv(
	const char* HlslSource,
	const char* FileName,
	const char* EntryPoint,
	ShaderConductor::ShaderStage Stage,
	bool bDisableOptimizations,
	bool bHlsl2021,
	TArray<uint32>& OutSpirv,
	FString& OutError)
{
	if (!bInitialized)
	{
		OutError = TEXT("FDawnShaderConductorLoader::Init() was not called (or failed)");
		return false;
	}

	Compiler::SourceDesc Source = {};
	Source.source = HlslSource;
	Source.fileName = FileName;
	Source.entryPoint = EntryPoint;
	Source.stage = Stage;

	Compiler::Options Options = {};
	Options.disableOptimizations = bDisableOptimizations;
	// -HV 2018 (NOT 2021, NOT omitted -- see HANDOFF.md "update 4"):
	// real UE's own default is 2018 (ShaderConductorContext.h:124,
	// `uint32 HlslVersion = 2018;`, unoverridden by VulkanShaderFormat/etc)
	// -- confirmed by reading that header directly, not guessed. HLSL2021
	// mode RESTRICTS `&&`/`||` to scalar-only operands (wanting the new
	// 'and'/'or' keywords for vectors instead), which real UE shader
	// source (Common.ush etc.) does NOT use -- it relies pervasively on
	// vector `&&`/`||`. Critically, DXC's OWN default when no -HV is
	// passed at all is `hlsl::LangStd::vLatest` (confirmed via
	// ShaderConductor's vendored DXC source,
	// clang/include/clang/Basic/LangOptions.h:155) which in this DXC
	// build IS the 2021 behavior -- so simply omitting -HV does NOT
	// recover legacy semantics, it silently keeps the exact same failure.
	// Must explicitly pass "2018" to match real UE. Confirmed by direct
	// repro: with -HV 2021 (or no -HV at all) present, a real,
	// fully-preprocessed NullPixelShader.usf failed with "operands for
	// short-circuiting logical binary operator must be scalar, for
	// non-scalar types use 'and'/'or'" at every real vector &&/|| use
	// site; explicit -HV 2018 made that whole error class disappear.
	// Per-shader HLSL version: shaders carrying CFLAG_HLSL2021 (template-based
	// engine code like LaneVectorization.ush / Nanite traversal / TSR) REQUIRE
	// -HV 2021 ('template' is reserved pre-2021: ~70 real global-shader
	// failures in the first full UT cook); everything else must stay -HV 2018
	// (see the long note above -- 2021 breaks pervasive vector &&/||).
	static const char* ExtraArgs2018[] = { "-fspv-target-env=vulkan1.1", "-HV", "2018" };
	static const char* ExtraArgs2021[] = { "-fspv-target-env=vulkan1.1", "-HV", "2021" };
	Options.numDXCArgs = 3;
	Options.DXCArgs = bHlsl2021 ? ExtraArgs2021 : ExtraArgs2018;

	Compiler::TargetDesc Target = {};
	Target.language = ShadingLanguage::SpirV;

	FRawResultDesc Result;
	FMemory::Memzero(Result); // mimics default-constructed Blobs (Impl=nullptr) + bIsText/bHasError=false

	reinterpret_cast<FCompileFn>(CompileFnPtr)(&Result, Source, Options, Target);

	auto BlobData = reinterpret_cast<FBlobDataFn>(BlobDataFnPtr);
	auto BlobSize = reinterpret_cast<FBlobSizeFn>(BlobSizeFnPtr);

	if (Result.bHasError)
	{
		const void* ErrData = BlobData(&Result.ErrorWarningMsg);
		const uint32 ErrSize = BlobSize(&Result.ErrorWarningMsg);
		FUTF8ToTCHAR Converter(reinterpret_cast<const ANSICHAR*>(ErrData), ErrSize);
		OutError = FString::ConstructFromPtrSize(Converter.Get(), Converter.Length());
		// Deliberately leak the result Blobs (BlobImpl ref-counted heap
		// allocations owned by the .so's own allocator) -- see ~FDawnShaderConductorLoader.
		return false;
	}

	const void* SpirvData = BlobData(&Result.Target);
	const uint32 SpirvSize = BlobSize(&Result.Target);
	OutSpirv.SetNumUninitialized(SpirvSize / 4);
	FMemory::Memcpy(OutSpirv.GetData(), SpirvData, SpirvSize);
	return true;
}

FDawnShaderConductorLoader& GetDawnShaderConductorLoader()
{
	static FDawnShaderConductorLoader Loader;
	return Loader;
}
