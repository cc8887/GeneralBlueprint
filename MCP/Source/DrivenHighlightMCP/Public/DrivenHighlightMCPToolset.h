#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "DrivenHighlightMCPToolset.generated.h"

USTRUCT(BlueprintType)
struct DRIVENHIGHLIGHTMCP_API FDrivenHighlightMCPState
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="DrivenHighlight|MCP")
    FString HighlightId;

    UPROPERTY(BlueprintReadOnly, Category="DrivenHighlight|MCP")
    bool bActive = false;

    /** The inspector currently has a live widget binding. This is not pixel verification. */
    UPROPERTY(BlueprintReadOnly, Category="DrivenHighlight|MCP")
    bool bWidgetBound = false;
};

USTRUCT(BlueprintType)
struct DRIVENHIGHLIGHTMCP_API FDrivenHighlightMCPResult
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category="DrivenHighlight|MCP")
    bool bSuccess = false;

    UPROPERTY(BlueprintReadOnly, Category="DrivenHighlight|MCP")
    FString Error;

    UPROPERTY(BlueprintReadOnly, Category="DrivenHighlight|MCP")
    TArray<FDrivenHighlightMCPState> Highlights;
};

/** Inspect queued editor highlights and their current Slate binding state. */
UCLASS(BlueprintType, Hidden)
class DRIVENHIGHLIGHTMCP_API UDrivenHighlightInspectToolset : public UToolsetDefinition
{
    GENERATED_BODY()
public:
    /** List active requests. A request may have no matching visible Details widget. */
    UFUNCTION(meta=(AICallable, AIAccessMode="Read"), Category="DrivenHighlight|Inspect")
    static FDrivenHighlightMCPResult ListActiveHighlights();

    /** Inspect a returned highlight ID; inactive IDs are a successful query. */
    UFUNCTION(meta=(AICallable, AIAccessMode="Read"), Category="DrivenHighlight|Inspect")
    static FDrivenHighlightMCPResult GetHighlightStatus(const FString& HighlightId);
};

/** Control editor property highlights. These tools change UI state only and do not save assets. */
UCLASS(BlueprintType, Hidden)
class DRIVENHIGHLIGHTMCP_API UDrivenHighlightEditToolset : public UToolsetDefinition
{
    GENERATED_BODY()
public:
    /**
     * Request a Details-row highlight. Open the asset and its Details panel first.
     * Matching uses DetailRowItem.<PropertyDisplayName>; it is not scoped to a specific panel.
     * Success means queued; query GetHighlightStatus after a Slate tick for widget binding.
     * @param AssetPath Project /Game package or object path.
     * @param PropertyDisplayName Exact displayed property label used by the Details row tag.
     * @param DurationSeconds Finite nonnegative duration; zero persists until explicitly hidden.
     */
    UFUNCTION(meta=(AICallable, AIAccessMode="Write"), Category="DrivenHighlight|Edit")
    static FDrivenHighlightMCPResult ShowAssetPropertyHighlight(
        const FString& AssetPath, const FString& PropertyDisplayName, float DurationSeconds);

    /** Hide one request. Repeating this for an inactive ID succeeds. */
    UFUNCTION(meta=(AICallable, AIAccessMode="Write"), Category="DrivenHighlight|Edit")
    static FDrivenHighlightMCPResult HideHighlight(const FString& HighlightId);

    /** Clear all highlights, including those created by DSL imports and layout. */
    UFUNCTION(meta=(AICallable, AIAccessMode="Write"), Category="DrivenHighlight|Edit")
    static FDrivenHighlightMCPResult ClearHighlights();
};
