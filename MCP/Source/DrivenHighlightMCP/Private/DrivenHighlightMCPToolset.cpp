#include "DrivenHighlightMCPToolset.h"
#include "IDrivenHighlightInspector.h"
#include "IDrivenHighlightService.h"
#include "Misc/PackageName.h"

namespace DrivenHighlightMCP
{
    FDrivenHighlightMCPResult Ready()
    {
        FDrivenHighlightMCPResult Result;
        if (!IsInGameThread())
            Result.Error = TEXT("Highlight tools must run on the editor game thread.");
        else if (!IDrivenHighlightService::IsAvailable() || !IDrivenHighlightInspector::IsAvailable())
            Result.Error = TEXT("DrivenHighlightEditor is not available.");
        else
            Result.bSuccess = true;
        return Result;
    }

    FDrivenHighlightMCPState State(FName Id)
    {
        FDrivenHighlightMCPState Result;
        Result.HighlightId = Id.ToString();
        Result.bActive = IDrivenHighlightInspector::Get().IsActive(Id);
        Result.bWidgetBound = IDrivenHighlightInspector::Get().GetHighlightedWidget(Id).IsValid();
        return Result;
    }

    bool ValidPath(const FString& Path)
    {
        if (!Path.StartsWith(TEXT("/Game/")) || Path.Contains(TEXT("..")) || Path.Contains(TEXT(":")) || Path.Contains(TEXT("\\")))
            return false;
        return Path.Contains(TEXT(".")) ? FPackageName::IsValidObjectPath(Path) : FPackageName::IsValidLongPackageName(Path);
    }
}

FDrivenHighlightMCPResult UDrivenHighlightInspectToolset::ListActiveHighlights()
{
    auto Result = DrivenHighlightMCP::Ready();
    if (!Result.bSuccess) return Result;
    TArray<FName> Ids = IDrivenHighlightInspector::Get().GetAllActive();
    Ids.Sort(FNameLexicalLess());
    for (const FName Id : Ids) Result.Highlights.Add(DrivenHighlightMCP::State(Id));
    return Result;
}

FDrivenHighlightMCPResult UDrivenHighlightInspectToolset::GetHighlightStatus(const FString& HighlightId)
{
    auto Result = DrivenHighlightMCP::Ready();
    if (!Result.bSuccess) return Result;
    if (HighlightId.IsEmpty() || FName(*HighlightId).IsNone())
    {
        Result.bSuccess = false;
        Result.Error = TEXT("HighlightId must be a nonempty ID returned by a highlight tool.");
        return Result;
    }
    Result.Highlights.Add(DrivenHighlightMCP::State(FName(*HighlightId)));
    return Result;
}

FDrivenHighlightMCPResult UDrivenHighlightEditToolset::ShowAssetPropertyHighlight(
    const FString& AssetPath, const FString& PropertyDisplayName, float DurationSeconds)
{
    auto Result = DrivenHighlightMCP::Ready();
    if (!Result.bSuccess) return Result;
    Result.bSuccess = false;
    if (!DrivenHighlightMCP::ValidPath(AssetPath))
        Result.Error = TEXT("AssetPath must be a /Game package or object path without subobjects or traversal.");
    else if (PropertyDisplayName.TrimStartAndEnd().IsEmpty() || FName(*PropertyDisplayName).IsNone())
        Result.Error = TEXT("PropertyDisplayName must be a nonempty Details row display label.");
    else if (!FMath::IsFinite(DurationSeconds) || DurationSeconds < 0)
        Result.Error = TEXT("DurationSeconds must be finite and nonnegative.");
    else
    {
        const FString ObjectPath = AssetPath.Contains(TEXT(".")) ? AssetPath :
            AssetPath + TEXT(".") + FPackageName::GetLongPackageAssetName(AssetPath);
        UObject* Asset = LoadObject<UObject>(nullptr, *ObjectPath);
        if (!Asset)
            Result.Error = TEXT("Could not load the requested project asset.");
        else
        {
            const FName Id = IDrivenHighlightService::Get().ShowAssetProperty(Asset, FName(*PropertyDisplayName), DurationSeconds);
            Result.bSuccess = !Id.IsNone();
            if (Result.bSuccess) Result.Highlights.Add(DrivenHighlightMCP::State(Id));
            else Result.Error = TEXT("The highlight service rejected the request.");
        }
    }
    return Result;
}

FDrivenHighlightMCPResult UDrivenHighlightEditToolset::HideHighlight(const FString& HighlightId)
{
    auto Result = UDrivenHighlightInspectToolset::GetHighlightStatus(HighlightId);
    if (!Result.bSuccess) return Result;
    IDrivenHighlightService::Get().Hide(FName(*HighlightId));
    Result.Highlights[0] = DrivenHighlightMCP::State(FName(*HighlightId));
    return Result;
}

FDrivenHighlightMCPResult UDrivenHighlightEditToolset::ClearHighlights()
{
    auto Result = DrivenHighlightMCP::Ready();
    if (Result.bSuccess) IDrivenHighlightService::Get().HideAll();
    return Result;
}
