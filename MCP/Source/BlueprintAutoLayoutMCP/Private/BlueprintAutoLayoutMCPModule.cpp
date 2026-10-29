#include "Modules/ModuleManager.h"
#include "BlueprintAutoLayoutMCPToolset.h"
#include "ToolsetRegistry/UToolsetRegistry.h"

class FBlueprintAutoLayoutMCPModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		RegisterToolsets();
		PostInitHandle = FCoreDelegates::GetOnPostEngineInit().AddRaw(this, &FBlueprintAutoLayoutMCPModule::RegisterToolsets);
	}
	virtual void ShutdownModule() override
	{
		FCoreDelegates::GetOnPostEngineInit().Remove(PostInitHandle);
		if (bRegistered)
		{
			UToolsetRegistry::UnregisterToolsetClass(UBlueprintAutoLayoutEditToolset::StaticClass());
			UToolsetRegistry::UnregisterToolsetClass(UBlueprintAutoLayoutInspectToolset::StaticClass());
		}
	}
private:
	void RegisterToolsets()
	{
		if (bRegistered || !UToolsetRegistry::IsAvailable()) return;
		UToolsetRegistry::RegisterToolsetClass(UBlueprintAutoLayoutInspectToolset::StaticClass());
		UToolsetRegistry::RegisterToolsetClass(UBlueprintAutoLayoutEditToolset::StaticClass());
		bRegistered = true;
	}
	bool bRegistered = false;
	FDelegateHandle PostInitHandle;
};

IMPLEMENT_MODULE(FBlueprintAutoLayoutMCPModule, BlueprintAutoLayoutMCP)
