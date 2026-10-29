#include "BlueprintAutoLayoutMCPToolset.h"

#include "BALConstraintCollector.h"
#include "AnimationStateMachineSchema.h"
#include "BlueprintAutoLayoutEngine.h"
#include "Editor.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "MaterialGraph/MaterialGraph.h"
#include "MaterialGraph/MaterialGraphNode.h"
#include "MaterialGraph/MaterialGraphNode_Comment.h"
#include "MaterialGraph/MaterialGraphNode_Root.h"
#include "MaterialGraph/MaterialGraphSchema.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionComment.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
	struct FLayoutContext
	{
		UObject* Asset = nullptr;
		UEdGraph* Graph = nullptr;
		TArray<UEdGraph*> Graphs;
		TStrongObjectPtr<UMaterial> InspectionCopy;
	};

	bool LoadContext(const FString& AssetPath, bool bWrite, FLayoutContext& Context, FBlueprintAutoLayoutMCPResult& Result)
	{
		Result.AssetPath = AssetPath;
		Result.Graphs.Reset();
		if (!IsInGameThread())
		{
			Result.Error = TEXT("Graph tools must run on the game thread.");
			return false;
		}
		if (bWrite && GEditor && GEditor->PlayWorld)
		{
			Result.Error = TEXT("Stop Play In Editor before changing graph layout.");
			return false;
		}
		FString PackageName = AssetPath;
		FString ObjectName;
		if (AssetPath.Split(TEXT("."), &PackageName, &ObjectName))
		{
			if (ObjectName != FPackageName::GetLongPackageAssetName(PackageName))
			{
				Result.Error = TEXT("Use a top-level /Game asset path, without subobjects.");
				return false;
			}
		}
		if (!PackageName.StartsWith(TEXT("/Game/")) || PackageName.Contains(TEXT(".."))
			|| !FPackageName::IsValidLongPackageName(PackageName))
		{
			Result.Error = TEXT("AssetPath must be a valid /Game package or object path.");
			return false;
		}
		const FString ObjectPath = PackageName + TEXT(".") + FPackageName::GetLongPackageAssetName(PackageName);
		Context.Asset = LoadObject<UObject>(nullptr, *ObjectPath);
		if (UBlueprint* Blueprint = Cast<UBlueprint>(Context.Asset))
		{
			Blueprint->GetAllGraphs(Context.Graphs);
			// RigVM and other model-backed graph editors need their own position
			// synchronization. Only K2-derived schemas have the semantics adapted
			// here, along with native animation state-machine topology graphs.
			Context.Graphs.RemoveAll([](const UEdGraph* Graph)
			{
				return !Graph || !Graph->Schema
					|| (!Graph->Schema->IsChildOf(UEdGraphSchema_K2::StaticClass())
						&& !Graph->Schema->IsChildOf(UAnimationStateMachineSchema::StaticClass()));
			});
			if (Context.Graphs.IsEmpty())
			{
				Result.Error = TEXT("This Blueprint contains no supported K2-schema or native animation state-machine graphs. RigVM and other model-backed graph schemas are not supported.");
				return false;
			}
		}
		else if (UMaterial* SourceMaterial = Cast<UMaterial>(Context.Asset))
		{
			UMaterial* Material = SourceMaterial;
			if (!bWrite)
			{
				Context.InspectionCopy.Reset(DuplicateObject<UMaterial>(Material, GetTransientPackage()));
				Material = Context.InspectionCopy.Get();
			}
			if (!Material->MaterialGraph)
			{
				Material->MaterialGraph = CastChecked<UMaterialGraph>(FBlueprintEditorUtils::CreateNewGraph(
					Material, NAME_None, UMaterialGraph::StaticClass(), UMaterialGraphSchema::StaticClass()));
				Material->MaterialGraph->Material = Material;
				Material->MaterialGraph->RebuildGraph();
			}
			Context.Graphs.Add(Material->MaterialGraph);
		}
		else
		{
			Result.Error = TEXT("Asset could not be loaded as a Blueprint, AnimBlueprint or Material.");
			return false;
		}
		for (UEdGraph* Graph : Context.Graphs)
		{
			if (Graph) Result.Graphs.Add(Cast<UMaterial>(Context.Asset) ? TEXT("MaterialGraph") : Graph->GetPathName(Context.Asset));
		}
		return true;
	}

	bool ResolveGraph(FLayoutContext& Context, const FString& Name, FBlueprintAutoLayoutMCPResult& Result)
	{
		Result.GraphName = Name;
		for (UEdGraph* Graph : Context.Graphs)
		{
			if (!Graph) continue;
			const FString Qualified = Cast<UMaterial>(Context.Asset) ? TEXT("MaterialGraph") : Graph->GetPathName(Context.Asset);
			if (Qualified == Name || Graph->GetName() == Name)
			{
				if (Context.Graph)
				{
					Result.Error = TEXT("Ambiguous graph name. Use a qualified name from ListLayoutGraphs.");
					return false;
				}
				Context.Graph = Graph;
			}
		}
		if (!Context.Graph) Result.Error = TEXT("Graph not found. Use ListLayoutGraphs first.");
		return Context.Graph != nullptr;
	}

	FString NodeId(const UEdGraphNode* Node)
	{
		if (const UMaterialGraphNode* MaterialNode = Cast<UMaterialGraphNode>(Node))
		{
			if (MaterialNode->MaterialExpression) return MaterialNode->MaterialExpression->GetName();
		}
		if (const UMaterialGraphNode_Comment* Comment = Cast<UMaterialGraphNode_Comment>(Node))
		{
			if (Comment->MaterialExpressionComment) return Comment->MaterialExpressionComment->GetName();
		}
		if (Cast<UMaterialGraphNode_Root>(Node)) return TEXT("__root__");
		return Node->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
	}

	void Snapshot(UEdGraph* Graph, FBlueprintAutoLayoutMCPResult& Result)
	{
		Result.Nodes.Reset();
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			FBlueprintAutoLayoutMCPNode& Info = Result.Nodes.AddDefaulted_GetRef();
			Info.NodeId = NodeId(Node);
			Info.Title = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();
			Info.X = Node->NodePosX;
			Info.Y = Node->NodePosY;
			Info.Width = Node->NodeWidth;
			Info.Height = Node->NodeHeight;
			Info.bPinned = FBALConstraintCollector::IsPinned(Node);
		}
	}

	void SyncMaterialPositions(UMaterialGraph* Graph)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (UMaterialGraphNode* ExpressionNode = Cast<UMaterialGraphNode>(Node))
			{
				UMaterialExpression* Expression = ExpressionNode->MaterialExpression;
				if (Expression && (Expression->MaterialExpressionEditorX != Node->NodePosX || Expression->MaterialExpressionEditorY != Node->NodePosY))
				{
					Expression->Modify();
					Expression->MaterialExpressionEditorX = Node->NodePosX;
					Expression->MaterialExpressionEditorY = Node->NodePosY;
				}
			}
			else if (UMaterialGraphNode_Comment* CommentNode = Cast<UMaterialGraphNode_Comment>(Node))
			{
				UMaterialExpressionComment* Comment = CommentNode->MaterialExpressionComment;
				if (Comment && (Comment->MaterialExpressionEditorX != Node->NodePosX || Comment->MaterialExpressionEditorY != Node->NodePosY
					|| Comment->SizeX != Node->NodeWidth || Comment->SizeY != Node->NodeHeight))
				{
					Comment->Modify();
					Comment->MaterialExpressionEditorX = Node->NodePosX;
					Comment->MaterialExpressionEditorY = Node->NodePosY;
					Comment->SizeX = Node->NodeWidth;
					Comment->SizeY = Node->NodeHeight;
				}
			}
			else if (Node && Node == Graph->RootNode && Graph->Material
				&& (Graph->Material->EditorX != Node->NodePosX || Graph->Material->EditorY != Node->NodePosY))
			{
				Graph->Material->Modify();
				Graph->Material->EditorX = Node->NodePosX;
				Graph->Material->EditorY = Node->NodePosY;
			}
		}
	}

	FBlueprintAutoLayoutMCPResult RunLayout(const FString& AssetPath, const FString& GraphName,
		const TArray<FString>* NodeIds, bool bSavePackage)
	{
		FBlueprintAutoLayoutMCPResult Result;
		FLayoutContext Context;
		// Resolve and validate on the inspection copy before material graph initialization mutates the asset.
		if (!LoadContext(AssetPath, false, Context, Result) || !ResolveGraph(Context, GraphName, Result)) return Result;
		if (GEditor && GEditor->PlayWorld)
		{
			Result.Error = TEXT("Stop Play In Editor before changing graph layout.");
			return Result;
		}
		if (FBlueprintEditorUtils::IsGraphReadOnly(Context.Graph))
		{
			Result.Error = TEXT("This graph is read-only in the editor.");
			return Result;
		}
		if (NodeIds)
		{
			if (NodeIds->IsEmpty())
			{
				Result.Error = TEXT("NodeIds must contain at least one node ID.");
				return Result;
			}
			for (const FString& Id : *NodeIds)
			{
				if (!Context.Graph->Nodes.ContainsByPredicate([&Id](const UEdGraphNode* Node) { return Node && NodeId(Node) == Id; }))
				{
					Result.Error = FString::Printf(TEXT("Unknown node ID: %s"), *Id);
					return Result;
				}
			}
		}
		const FScopedTransaction Transaction(NSLOCTEXT("BlueprintAutoLayoutMCP", "LayoutGraph", "Auto Layout Graph via MCP"));
		FLayoutContext WriteContext;
		if (!LoadContext(AssetPath, true, WriteContext, Result) || !ResolveGraph(WriteContext, GraphName, Result)) return Result;
		Snapshot(WriteContext.Graph, Result);
		const TArray<FBlueprintAutoLayoutMCPNode> Before = Result.Nodes;
		FBALSettings Settings;
		Settings.bCreateKnotTracks = false;
		Result.bMutationStarted = true;
		WriteContext.Asset->Modify(false);
		if (NodeIds)
		{
			TSet<UEdGraphNode*> Selection;
			for (UEdGraphNode* Node : WriteContext.Graph->Nodes)
			{
				if (Node && NodeIds->Contains(NodeId(Node))) Selection.Add(Node);
			}
			FBlueprintAutoLayoutEngine::LayoutSelection(WriteContext.Graph, Selection, Settings);
		}
		else FBlueprintAutoLayoutEngine::Layout(WriteContext.Graph, Settings);
		if (UMaterialGraph* MaterialGraph = Cast<UMaterialGraph>(WriteContext.Graph))
		{
			// Persist only layout metadata; do not relink or compile material expressions.
			SyncMaterialPositions(MaterialGraph);
		}
		Snapshot(WriteContext.Graph, Result);
		for (const FBlueprintAutoLayoutMCPNode& Node : Result.Nodes)
		{
			const FBlueprintAutoLayoutMCPNode* Old = Before.FindByPredicate([&Node](const FBlueprintAutoLayoutMCPNode& Item) { return Item.NodeId == Node.NodeId; });
			if (!Old || Old->X != Node.X || Old->Y != Node.Y || Old->Width != Node.Width || Old->Height != Node.Height) ++Result.NumChangedNodes;
		}
		if (Result.NumChangedNodes) WriteContext.Asset->MarkPackageDirty();
		if (bSavePackage)
		{
			UPackage* Package = WriteContext.Asset->GetOutermost();
			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			SaveArgs.SaveFlags = SAVE_NoError;
			const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
			Result.bSavedPackage = UPackage::SavePackage(Package, WriteContext.Asset, *Filename, SaveArgs);
			if (!Result.bSavedPackage)
			{
				Result.Error = TEXT("Layout was applied in memory, but saving the package failed.");
				return Result;
			}
		}
		Result.bSuccess = true;
		return Result;
	}
}

FBlueprintAutoLayoutMCPResult UBlueprintAutoLayoutInspectToolset::ListLayoutGraphs(const FString& AssetPath)
{
	FBlueprintAutoLayoutMCPResult Result;
	FLayoutContext Context;
	Result.bSuccess = LoadContext(AssetPath, false, Context, Result);
	return Result;
}

FBlueprintAutoLayoutMCPResult UBlueprintAutoLayoutInspectToolset::InspectGraphLayout(const FString& AssetPath, const FString& GraphName)
{
	FBlueprintAutoLayoutMCPResult Result;
	FLayoutContext Context;
	if (LoadContext(AssetPath, false, Context, Result) && ResolveGraph(Context, GraphName, Result))
	{
		Snapshot(Context.Graph, Result);
		Result.bSuccess = true;
	}
	return Result;
}

FBlueprintAutoLayoutMCPResult UBlueprintAutoLayoutEditToolset::LayoutGraph(const FString& AssetPath, const FString& GraphName, bool bSavePackage)
{
	return RunLayout(AssetPath, GraphName, nullptr, bSavePackage);
}

FBlueprintAutoLayoutMCPResult UBlueprintAutoLayoutEditToolset::LayoutNodes(const FString& AssetPath, const FString& GraphName, const TArray<FString>& NodeIds, bool bSavePackage)
{
	return RunLayout(AssetPath, GraphName, &NodeIds, bSavePackage);
}
