#include "DawnRHIPrivate.h"
#include "DawnDynamicRHI.h"
#include "Modules/ModuleManager.h"

class FDawnRHIModule final : public IDynamicRHIModule
{
public:
	virtual bool IsSupported() override
	{
		// Stage 1: Linux/Vulkan-backed Dawn only.
		return PLATFORM_LINUX;
	}

	virtual FDynamicRHI* CreateRHI(ERHIFeatureLevel::Type RequestedFeatureLevel = ERHIFeatureLevel::Num) override
	{
		return new FDawnDynamicRHI();
	}

	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};

IMPLEMENT_MODULE(FDawnRHIModule, DawnRHI)
