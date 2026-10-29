#pragma once

#include "CoreMinimal.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "BlueprintAutoLayoutMCPToolset.generated.h"

/** Authored graph coordinates; these values do not measure rendered widget bounds. */
USTRUCT(BlueprintType)
struct BLUEPRINTAUTOLAYOUTMCP_API FBlueprintAutoLayoutMCPNode
{
	GENERATED_BODY()
	/** Blueprint node GUID, or Material expression object name (__root__ for the output). */
	UPROPERTY(BlueprintReadOnly, Category="BlueprintAutoLayout|MCP") FString NodeId;
	UPROPERTY(BlueprintReadOnly, Category="BlueprintAutoLayout|MCP") FString Title;
	UPROPERTY(BlueprintReadOnly, Category="BlueprintAutoLayout|MCP") int32 X = 0;
	UPROPERTY(BlueprintReadOnly, Category="BlueprintAutoLayout|MCP") int32 Y = 0;
	UPROPERTY(BlueprintReadOnly, Category="BlueprintAutoLayout|MCP") int32 Width = 0;
	UPROPERTY(BlueprintReadOnly, Category="BlueprintAutoLayout|MCP") int32 Height = 0;
	UPROPERTY(BlueprintReadOnly, Category="BlueprintAutoLayout|MCP") bool bPinned = false;
};

USTRUCT(BlueprintType)
struct BLUEPRINTAUTOLAYOUTMCP_API FBlueprintAutoLayoutMCPResult
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="BlueprintAutoLayout|MCP") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category="BlueprintAutoLayout|MCP") FString Error;
	UPROPERTY(BlueprintReadOnly, Category="BlueprintAutoLayout|MCP") FString AssetPath;
	UPROPERTY(BlueprintReadOnly, Category="BlueprintAutoLayout|MCP") FString GraphName;
	/** Qualified names accepted by InspectGraphLayout, LayoutGraph and LayoutNodes. */
	UPROPERTY(BlueprintReadOnly, Category="BlueprintAutoLayout|MCP") TArray<FString> Graphs;
	UPROPERTY(BlueprintReadOnly, Category="BlueprintAutoLayout|MCP") TArray<FBlueprintAutoLayoutMCPNode> Nodes;
	UPROPERTY(BlueprintReadOnly, Category="BlueprintAutoLayout|MCP") int32 NumChangedNodes = 0;
	UPROPERTY(BlueprintReadOnly, Category="BlueprintAutoLayout|MCP") bool bMutationStarted = false;
	UPROPERTY(BlueprintReadOnly, Category="BlueprintAutoLayout|MCP") bool bSavedPackage = false;
};

UCLASS(BlueprintType, Hidden)
class BLUEPRINTAUTOLAYOUTMCP_API UBlueprintAutoLayoutInspectToolset : public UToolsetDefinition
{
	GENERATED_BODY()
public:
	/** List K2-schema and native animation state-machine graphs in a /Game Blueprint or AnimBlueprint, or the main Material graph as MaterialGraph. RigVM and other model-backed schemas are excluded. */
	UFUNCTION(meta=(AICallable, AIAccessMode="Read"), Category="BlueprintAutoLayout|Inspect")
	static FBlueprintAutoLayoutMCPResult ListLayoutGraphs(const FString& AssetPath);
	/** Inspect authored node positions and stable node IDs. Material inspection uses a transient copy and never dirties the asset. */
	UFUNCTION(meta=(AICallable, AIAccessMode="Read"), Category="BlueprintAutoLayout|Inspect")
	static FBlueprintAutoLayoutMCPResult InspectGraphLayout(const FString& AssetPath, const FString& GraphName);
};

UCLASS(BlueprintType, Hidden)
class BLUEPRINTAUTOLAYOUTMCP_API UBlueprintAutoLayoutEditToolset : public UToolsetDefinition
{
	GENERATED_BODY()
public:
	/** Arrange all nodes using the existing layout solver. Preserves pinned nodes; reroute creation is disabled. Save is explicit. */
	UFUNCTION(meta=(AICallable, AIAccessMode="Write"), Category="BlueprintAutoLayout|Edit")
	static FBlueprintAutoLayoutMCPResult LayoutGraph(const FString& AssetPath, const FString& GraphName, bool bSavePackage);
	/** Arrange only the IDs returned by InspectGraphLayout, holding all other nodes fixed. Rejects empty or unknown IDs before editing. Save is explicit. */
	UFUNCTION(meta=(AICallable, AIAccessMode="Write"), Category="BlueprintAutoLayout|Edit")
	static FBlueprintAutoLayoutMCPResult LayoutNodes(const FString& AssetPath, const FString& GraphName, const TArray<FString>& NodeIds, bool bSavePackage);
};
