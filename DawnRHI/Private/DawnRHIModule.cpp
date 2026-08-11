#include "DawnRHIPrivate.h"
#include "DawnDynamicRHI.h"
#include "Modules/ModuleManager.h"

class FDawnRHIModule final : public IDynamicRHIModule
{
public:
	virtual bool IsSupported() override
	{
		// Linux/Vulkan-backed Dawn (native) + SimplyStream/wasm (emdawnwebgpu, browser WebGPU).
		return PLATFORM_LINUX || PLATFORM_WASM;
	}

	virtual FDynamicRHI* CreateRHI(ERHIFeatureLevel::Type RequestedFeatureLevel = ERHIFeatureLevel::Num) override
	{
		return new FDawnDynamicRHI();
	}

	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FDawnRHIModule, DawnRHI)
