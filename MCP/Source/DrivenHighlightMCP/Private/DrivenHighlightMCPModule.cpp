#include "DrivenHighlightMCPToolset.h"
#include "Modules/ModuleManager.h"
#include "ToolsetRegistry/UToolsetRegistry.h"

class FDrivenHighlightMCPModule final : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        UToolsetRegistry::RegisterToolsetClass(UDrivenHighlightInspectToolset::StaticClass());
        UToolsetRegistry::RegisterToolsetClass(UDrivenHighlightEditToolset::StaticClass());
    }

    virtual void ShutdownModule() override
    {
        if (UToolsetRegistry::IsAvailable())
        {
            UToolsetRegistry::UnregisterToolsetClass(UDrivenHighlightEditToolset::StaticClass());
            UToolsetRegistry::UnregisterToolsetClass(UDrivenHighlightInspectToolset::StaticClass());
        }
    }
};
IMPLEMENT_MODULE(FDrivenHighlightMCPModule, DrivenHighlightMCP)
