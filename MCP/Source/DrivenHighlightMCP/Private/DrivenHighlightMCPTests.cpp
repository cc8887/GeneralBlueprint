#include "DrivenHighlightMCPToolset.h"
#include "Curves/CurveFloat.h"
#include "IDrivenHighlightService.h"
#include "Misc/AutomationTest.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "UObject/Package.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrivenHighlightMCPSchema,
    "DrivenHighlight.MCP.ToolsetSchema", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDrivenHighlightMCPSchema::RunTest(const FString&)
{
    TestTrue(TEXT("inspect registered"), UToolsetRegistry::IsToolsetClassRegistered(UDrivenHighlightInspectToolset::StaticClass()));
    TestTrue(TEXT("edit registered"), UToolsetRegistry::IsToolsetClassRegistered(UDrivenHighlightEditToolset::StaticClass()));
    const FString Read = UToolsetRegistry::GetToolsetJsonSchema(UDrivenHighlightInspectToolset::StaticClass());
    const FString Write = UToolsetRegistry::GetToolsetJsonSchema(UDrivenHighlightEditToolset::StaticClass());
    TestTrue(TEXT("binding state schema"), Read.Contains(TEXT("bWidgetBound")));
    TestTrue(TEXT("property highlight schema"), Write.Contains(TEXT("ShowAssetPropertyHighlight")));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDrivenHighlightMCPLifecycle,
    "DrivenHighlight.MCP.RequestLifecycle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDrivenHighlightMCPLifecycle::RunTest(const FString&)
{
    TestFalse(TEXT("reject engine path"), UDrivenHighlightEditToolset::ShowAssetPropertyHighlight(TEXT("/Engine/Bad"), TEXT("Color"), 0).bSuccess);
    TestFalse(TEXT("reject empty id"), UDrivenHighlightEditToolset::HideHighlight(TEXT("")).bSuccess);
    const FString PackageName = TEXT("/Game/MCPHighlightTest_") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/HighlightAsset");
    UPackage* Package = CreatePackage(*PackageName);
    UObject* Asset = NewObject<UCurveFloat>(Package, TEXT("HighlightAsset"), RF_Public | RF_Standalone | RF_Transient);
    const auto Shown = UDrivenHighlightEditToolset::ShowAssetPropertyHighlight(Asset->GetPathName(), TEXT("TestProperty"), 0);
    TestTrue(TEXT("request queued"), Shown.bSuccess);
    if (Shown.Highlights.Num() != 1) return false;
    const FString Id = Shown.Highlights[0].HighlightId;
    TestTrue(TEXT("active after show"), Shown.Highlights[0].bActive);
    TestFalse(TEXT("queue does not prove widget binding"), Shown.Highlights[0].bWidgetBound);
    const auto Status = UDrivenHighlightInspectToolset::GetHighlightStatus(Id);
    TestTrue(TEXT("status queried"), Status.bSuccess);
    const auto Hidden = UDrivenHighlightEditToolset::HideHighlight(Id);
    TestTrue(TEXT("hide succeeds"), Hidden.bSuccess);
    if (Hidden.Highlights.Num() == 1) TestFalse(TEXT("inactive after hide"), Hidden.Highlights[0].bActive);
    TestTrue(TEXT("hide idempotent"), UDrivenHighlightEditToolset::HideHighlight(Id).bSuccess);
    const auto PackageShown = UDrivenHighlightEditToolset::ShowAssetPropertyHighlight(PackageName, TEXT("TestProperty"), 0);
    TestTrue(TEXT("bare package path resolves asset"), PackageShown.bSuccess);
    if (PackageShown.Highlights.Num() == 1)
        UDrivenHighlightEditToolset::HideHighlight(PackageShown.Highlights[0].HighlightId);
    Asset->ClearFlags(RF_Standalone);
    Package->SetDirtyFlag(false);
    return true;
}
#endif
