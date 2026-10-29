#if WITH_DEV_AUTOMATION_TESTS

#include "BlueprintAutoLayoutMCPToolset.h"
#include "BALConstraintCollector.h"
#include "AnimationStateMachineGraph.h"
#include "AnimationStateMachineSchema.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "MaterialGraph/MaterialGraphSchema.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "UObject/Package.h"

#define BAL_MCP_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBALMCPRegistrationSchema, "BlueprintAutoLayout.MCP.RegistrationAndSchema", BAL_MCP_FLAGS)
bool FBALMCPRegistrationSchema::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Inspect registered during module startup"), UToolsetRegistry::IsToolsetClassRegistered(UBlueprintAutoLayoutInspectToolset::StaticClass()));
	TestTrue(TEXT("Edit registered during module startup"), UToolsetRegistry::IsToolsetClassRegistered(UBlueprintAutoLayoutEditToolset::StaticClass()));
	const FString Inspect = UToolsetRegistry::GetToolsetJsonSchema(UBlueprintAutoLayoutInspectToolset::StaticClass());
	const FString Edit = UToolsetRegistry::GetToolsetJsonSchema(UBlueprintAutoLayoutEditToolset::StaticClass());
	TestTrue(TEXT("Inspection tool exported"), Inspect.Contains(TEXT("InspectGraphLayout")));
	TestTrue(TEXT("Selection tool exported"), Edit.Contains(TEXT("LayoutNodes")));
	TestTrue(TEXT("Explicit save argument exported"), Edit.Contains(TEXT("bSavePackage")));
	TestTrue(TEXT("Read metadata"), UBlueprintAutoLayoutInspectToolset::StaticClass()->FindFunctionByName(TEXT("ListLayoutGraphs"))->GetMetaData(TEXT("AIAccessMode")) == TEXT("Read"));
	TestTrue(TEXT("Write metadata"), UBlueprintAutoLayoutEditToolset::StaticClass()->FindFunctionByName(TEXT("LayoutGraph"))->GetMetaData(TEXT("AIAccessMode")) == TEXT("Write"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBALMCPBlueprintOperations, "BlueprintAutoLayout.MCP.BlueprintOperations", BAL_MCP_FLAGS)
bool FBALMCPBlueprintOperations::RunTest(const FString& Parameters)
{
	const FString PackageName = TEXT("/Game/__GeneralBlueprintMCPTests/BAL_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UPackage* Package = CreatePackage(*PackageName);
	UBlueprint* Blueprint = NewObject<UBlueprint>(Package, *FPackageName::GetLongPackageAssetName(PackageName), RF_Transient);
	UEdGraph* Graph = NewObject<UEdGraph>(Blueprint, TEXT("EventGraph"), RF_Transient);
	Graph->Schema = UEdGraphSchema_K2::StaticClass();
	Blueprint->UbergraphPages.Add(Graph);
	TArray<UEdGraphNode*> Nodes;
	for (int32 Index = 0; Index < 3; ++Index)
	{
		UEdGraphNode* Node = NewObject<UEdGraphNode>(Graph, NAME_None, RF_Transient);
		Node->CreateNewGuid();
		Node->CreatePin(EGPD_Input, UEdGraphSchema_K2::PC_Exec, TEXT("In"));
		Node->CreatePin(EGPD_Output, UEdGraphSchema_K2::PC_Exec, TEXT("Out"));
		Graph->Nodes.Add(Node);
		Nodes.Add(Node);
	}
	Nodes[0]->Pins[1]->MakeLinkTo(Nodes[1]->Pins[0]);
	Nodes[1]->Pins[1]->MakeLinkTo(Nodes[2]->Pins[0]);
	Package->SetDirtyFlag(false);
	const FString Path = Blueprint->GetPathName();
	const FBlueprintAutoLayoutMCPResult Inspect = UBlueprintAutoLayoutInspectToolset::InspectGraphLayout(Path, TEXT("EventGraph"));
	TestTrue(TEXT("Blueprint inspection succeeds"), Inspect.bSuccess);
	TestEqual(TEXT("All nodes returned"), Inspect.Nodes.Num(), 3);
	TestFalse(TEXT("Read leaves package clean"), Package->IsDirty());
	const FBlueprintAutoLayoutMCPResult Invalid = UBlueprintAutoLayoutEditToolset::LayoutNodes(Path, TEXT("EventGraph"), { TEXT("missing-id") }, false);
	TestFalse(TEXT("Unknown selection rejected"), Invalid.bSuccess);
	TestFalse(TEXT("Unknown selection causes no mutation"), Invalid.bMutationStarted);
	TestFalse(TEXT("Unknown selection leaves package clean"), Package->IsDirty());
	FBALConstraintCollector::SetPinned(Nodes[0], true);
	const FIntPoint Locked(Nodes[0]->NodePosX, Nodes[0]->NodePosY);
	const FBlueprintAutoLayoutMCPResult Layout = UBlueprintAutoLayoutEditToolset::LayoutGraph(Path, TEXT("EventGraph"), false);
	TestTrue(TEXT("Layout succeeds"), Layout.bSuccess);
	TestTrue(TEXT("Overlapping nodes moved"), Layout.NumChangedNodes > 0);
	TestEqual(TEXT("Pinned node position preserved"), FIntPoint(Nodes[0]->NodePosX, Nodes[0]->NodePosY), Locked);
	TestFalse(TEXT("Save false does not persist"), Layout.bSavedPackage);
	TestEqual(TEXT("No reroute nodes created"), Graph->Nodes.Num(), 3);
	TestEqual(TEXT("Connection preserved"), Nodes[0]->Pins[1]->LinkedTo[0], Nodes[1]->Pins[0]);
	Nodes[1]->NodePosX = 0;
	Nodes[1]->NodePosY = 0;
	const FIntPoint Unselected(Nodes[2]->NodePosX, Nodes[2]->NodePosY);
	const FBlueprintAutoLayoutMCPResult Selection = UBlueprintAutoLayoutEditToolset::LayoutNodes(Path, TEXT("EventGraph"), { Nodes[1]->NodeGuid.ToString(EGuidFormats::DigitsWithHyphens) }, false);
	TestTrue(TEXT("Selection layout succeeds"), Selection.bSuccess);
	TestEqual(TEXT("Unselected position preserved"), FIntPoint(Nodes[2]->NodePosX, Nodes[2]->NodePosY), Unselected);
	TestFalse(TEXT("Engine asset path rejected"), UBlueprintAutoLayoutInspectToolset::ListLayoutGraphs(TEXT("/Engine/EngineMaterials/DefaultMaterial")).bSuccess);
	TestFalse(TEXT("Traversal rejected"), UBlueprintAutoLayoutInspectToolset::ListLayoutGraphs(TEXT("/Game/../Test")).bSuccess);
	TestFalse(TEXT("Filesystem path rejected"), UBlueprintAutoLayoutInspectToolset::ListLayoutGraphs(TEXT("C:/Test.uasset")).bSuccess);
	// A Blueprint can carry graph types whose node positions must be edited
	// through a separate model/controller. Exercise the schema filter with a
	// real non-K2 schema, without introducing a RigVM plugin dependency.
	UEdGraph* UnsupportedGraph = NewObject<UEdGraph>(Blueprint, TEXT("ForeignGraph"), RF_Transient);
	UnsupportedGraph->Schema = UMaterialGraphSchema::StaticClass();
	Blueprint->UbergraphPages.Add(UnsupportedGraph);
	const FBlueprintAutoLayoutMCPResult MixedGraphs = UBlueprintAutoLayoutInspectToolset::ListLayoutGraphs(Path);
	TestTrue(TEXT("K2 graph retained beside unsupported graph"), MixedGraphs.bSuccess);
	TestEqual(TEXT("Unsupported schema omitted from catalog"), MixedGraphs.Graphs.Num(), 1);
	const FBlueprintAutoLayoutMCPResult UnsupportedLayout = UBlueprintAutoLayoutEditToolset::LayoutGraph(Path, TEXT("ForeignGraph"), false);
	TestFalse(TEXT("Unsupported schema cannot be laid out"), UnsupportedLayout.bSuccess);
	TestFalse(TEXT("Unsupported schema rejected before mutation"), UnsupportedLayout.bMutationStarted);
	UAnimationStateMachineGraph* StateMachineGraph = NewObject<UAnimationStateMachineGraph>(Blueprint, TEXT("StateMachine"), RF_Transient);
	StateMachineGraph->Schema = UAnimationStateMachineSchema::StaticClass();
	Blueprint->UbergraphPages.Add(StateMachineGraph);
	const FBlueprintAutoLayoutMCPResult AnimationGraphs = UBlueprintAutoLayoutInspectToolset::ListLayoutGraphs(Path);
	TestTrue(TEXT("Native animation state-machine schema is supported"), AnimationGraphs.bSuccess);
	TestEqual(TEXT("Native state-machine graph retained beside K2 graph"), AnimationGraphs.Graphs.Num(), 2);
	Blueprint->UbergraphPages.Remove(StateMachineGraph);
	Blueprint->UbergraphPages.Remove(Graph);
	const FBlueprintAutoLayoutMCPResult NoSupportedGraphs = UBlueprintAutoLayoutInspectToolset::ListLayoutGraphs(Path);
	TestFalse(TEXT("Blueprint without supported schema fails explicitly"), NoSupportedGraphs.bSuccess);
	TestTrue(TEXT("Unsupported-only Blueprint explains schema boundary"), NoSupportedGraphs.Error.Contains(TEXT("K2-schema")));
	Package->SetDirtyFlag(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBALMCPMaterialOperations, "BlueprintAutoLayout.MCP.MaterialOperations", BAL_MCP_FLAGS)
bool FBALMCPMaterialOperations::RunTest(const FString& Parameters)
{
	const FString PackageName = TEXT("/Game/__GeneralBlueprintMCPTests/Material_") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
	UPackage* Package = CreatePackage(*PackageName);
	UMaterial* Material = NewObject<UMaterial>(Package, *FPackageName::GetLongPackageAssetName(PackageName), RF_Transient);
	for (int32 Index = 0; Index < 2; ++Index)
	{
		UMaterialExpressionConstant* Expression = NewObject<UMaterialExpressionConstant>(Material, NAME_None, RF_Transient);
		Expression->Material = Material;
		Material->GetExpressionCollection().AddExpression(Expression);
	}
	Package->SetDirtyFlag(false);
	const FString Path = Material->GetPathName();
	const FBlueprintAutoLayoutMCPResult Before = UBlueprintAutoLayoutInspectToolset::InspectGraphLayout(Path, TEXT("MaterialGraph"));
	TestTrue(TEXT("Unopened material can be inspected"), Before.bSuccess);
	TestEqual(TEXT("Expressions and root listed"), Before.Nodes.Num(), 3);
	TestNull(TEXT("Read does not initialize source graph"), Material->MaterialGraph.Get());
	TestFalse(TEXT("Read leaves source clean"), Package->IsDirty());
	const FBlueprintAutoLayoutMCPResult Layout = UBlueprintAutoLayoutEditToolset::LayoutGraph(Path, TEXT("MaterialGraph"), false);
	TestTrue(TEXT("Material layout succeeds"), Layout.bSuccess);
	TestTrue(TEXT("Material graph initialized"), Material->MaterialGraph != nullptr);
	TestTrue(TEXT("Overlapping material nodes moved"), Layout.NumChangedNodes > 0);
	TestFalse(TEXT("Material stays unsaved"), Layout.bSavedPackage);
	const FBlueprintAutoLayoutMCPResult After = UBlueprintAutoLayoutInspectToolset::InspectGraphLayout(Path, TEXT("MaterialGraph"));
	TestTrue(TEXT("Material reinspection succeeds"), After.bSuccess);
	for (const FBlueprintAutoLayoutMCPNode& Node : Layout.Nodes)
	{
		const FBlueprintAutoLayoutMCPNode* Refreshed = After.Nodes.FindByPredicate([&Node](const FBlueprintAutoLayoutMCPNode& Candidate) { return Candidate.NodeId == Node.NodeId; });
		TestNotNull(TEXT("Material node ID remains stable"), Refreshed);
		if (Refreshed) TestEqual(TEXT("Authored position survives graph reconstruction"), FIntPoint(Refreshed->X, Refreshed->Y), FIntPoint(Node.X, Node.Y));
	}
	Package->SetDirtyFlag(false);
	return true;
}

#endif
