// BlueprintLispMCPToolsetTests.cpp - MCP exposure regression coverage

#include "BlueprintLispToolset.h"

#include "Async/Async.h"
#include "Dom/JsonObject.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

#if WITH_DEV_AUTOMATION_TESTS

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLispMCPToolsetSurfaceTest,
	"BlueprintLisp.MCP.ToolsetSurface",
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::CommandletContext |
	EAutomationTestFlags::EngineFilter)

bool FBlueprintLispMCPToolsetSurfaceTest::RunTest(const FString& /*Parameters*/)
{
	TestTrue(
		TEXT("BlueprintLisp Toolset should be registered with Toolset Registry"),
		UToolsetRegistry::IsToolsetClassRegistered(UBlueprintLispToolset::StaticClass()));

	const FString Schema = UToolsetRegistry::GetToolsetJsonSchema(UBlueprintLispToolset::StaticClass());
	TestFalse(TEXT("Toolset schema should not be empty"), Schema.IsEmpty());

	TSharedPtr<FJsonObject> Root;
	const bool bParsed = FJsonSerializer::Deserialize(
		TJsonReaderFactory<>::Create(Schema), Root) && Root.IsValid();
	TestTrue(TEXT("Toolset schema should be valid JSON"), bParsed);
	if (!bParsed)
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* ToolEntries = nullptr;
	const bool bHasTools = Root->TryGetArrayField(TEXT("tools"), ToolEntries) && ToolEntries != nullptr;
	TestTrue(TEXT("Toolset schema should contain a tools array"), bHasTools);
	if (!bHasTools)
	{
		return false;
	}
	TestEqual(TEXT("Toolset schema tool count"), ToolEntries->Num(), 7);

	TSet<FString> ToolNames;
	for (const TSharedPtr<FJsonValue>& ToolEntry : *ToolEntries)
	{
		const TSharedPtr<FJsonObject> ToolObject = ToolEntry.IsValid() ? ToolEntry->AsObject() : nullptr;
		if (ToolObject.IsValid())
		{
			FString Name;
			if (ToolObject->TryGetStringField(TEXT("name"), Name))
			{
				int32 LastDot = INDEX_NONE;
				if (Name.FindLastChar(TEXT('.'), LastDot))
				{
					Name = Name.RightChop(LastDot + 1);
				}
				ToolNames.Add(Name);
				if (Name == TEXT("ReplaceBlueprintGraph") || Name == TEXT("MergeBlueprintGraph"))
				{
					const TSharedPtr<FJsonObject>* InputSchema = nullptr;
					if (TestTrue(TEXT("Edit tool has an input schema"),
						ToolObject->TryGetObjectField(TEXT("inputSchema"), InputSchema) && InputSchema))
					{
						TArray<FString> Required;
						(*InputSchema)->TryGetStringArrayField(TEXT("required"), Required);
						TestTrue(TEXT("Persistence choice is required"), Required.Contains(TEXT("bSavePackage")));
						TestTrue(TEXT("Compilation choice is required"), Required.Contains(TEXT("bCompile")));
					}
				}
			}
		}
	}

	const TSet<FString> ExpectedToolNames = {
		TEXT("ExportBlueprintGraph"),
		TEXT("ListBlueprintGraphs"),
		TEXT("ListBlueprintMemberVariables"),
		TEXT("InspectBlueprintMemberVariable"),
		TEXT("ValidateBlueprintLisp"),
		TEXT("ReplaceBlueprintGraph"),
		TEXT("MergeBlueprintGraph")
	};

	TestEqual(TEXT("MCP tool count"), ToolNames.Num(), ExpectedToolNames.Num());
	for (const FString& ExpectedName : ExpectedToolNames)
	{
		TestTrue(
			*FString::Printf(TEXT("MCP tool %s should be exposed"), *ExpectedName),
			ToolNames.Contains(ExpectedName));
	}

	const TSet<FString> ForbiddenToolNames = {
		TEXT("ExportGraphToFile"),
		TEXT("ExportGraphToDefaultPath"),
		TEXT("ImportGraphFromFile"),
		TEXT("UpdateGraphFromFile"),
		TEXT("UpdateGraphFromText"),
		TEXT("UpdateBlueprintGraph"),
		TEXT("ExportStub")
	};
	for (const FString& ForbiddenName : ForbiddenToolNames)
	{
		TestFalse(
			*FString::Printf(TEXT("File-backed bridge tool %s must not be exposed"), *ForbiddenName),
			ToolNames.Contains(ForbiddenName));
	}
	for (TFieldIterator<UFunction> Function(UBlueprintLispToolset::StaticClass()); Function; ++Function)
	{
		if (!Function->HasMetaData(TEXT("AICallable"))) continue;
		const FString Name = Function->GetName();
		const bool bWrite = Name == TEXT("ReplaceBlueprintGraph") || Name == TEXT("MergeBlueprintGraph");
		TestEqual(*FString::Printf(TEXT("%s access mode"), *Name),
			Function->GetMetaData(TEXT("AIAccessMode")), bWrite ? FString(TEXT("Write")) : FString(TEXT("Read")));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLispMCPInputGuardsTest,
	"BlueprintLisp.MCP.InputGuards",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLispMCPInputGuardsTest::RunTest(const FString& /*Parameters*/)
{
	for (const FString& Path : { TEXT(""), TEXT("/Engine/EngineResources/DefaultTexture"),
		TEXT("C:\\project\\BP.uasset"), TEXT("/Game/Foo/../BP"), TEXT("/Game/Foo\\BP"),
		TEXT("Blueprint'/Game/Foo.BP'"), TEXT("/Game/Foo/BP.BP:Subobject"),
		TEXT("/Game/Foo/BP.BP_C"), TEXT("/Game/Foo/BP.Other"), TEXT("/Game/Foo/BP..BP") })
	{
		const FBlueprintLispPythonResult Read = UBlueprintLispToolset::ListBlueprintGraphs(Path);
		TestFalse(*FString::Printf(TEXT("Invalid read path rejected: %s"), *Path), Read.bSuccess);
		TestTrue(TEXT("Path rejected before loading assets"), Read.Message.Contains(TEXT("path"), ESearchCase::IgnoreCase));
		TestFalse(TEXT("Invalid write path rejected"), UBlueprintLispToolset::ReplaceBlueprintGraph(
			Path, TEXT("EventGraph"), TEXT("(event BeginPlay)"), false, false).bSuccess);
	}
	TestFalse(TEXT("Blank graph is rejected before asset loading"), UBlueprintLispToolset::ExportBlueprintGraph(
		TEXT("/Game/GeneralBlueprintMCP/BP_NotLoaded"), TEXT(" "), false, true).bSuccess);
	TestTrue(TEXT("Valid DSL can be validated without an asset"),
		UBlueprintLispToolset::ValidateBlueprintLisp(TEXT("(event BeginPlay)")).bSuccess);
	TestFalse(TEXT("Malformed DSL is rejected"),
		UBlueprintLispToolset::ValidateBlueprintLisp(TEXT("(event BeginPlay")).bSuccess);

	const FBlueprintLispPythonResult OffThread = Async(EAsyncExecution::ThreadPool, []
	{
		return UBlueprintLispToolset::ListBlueprintGraphs(TEXT("/Game/GeneralBlueprintMCP/BP_NotLoaded"));
	}).Get();
	TestFalse(TEXT("Asset tools reject off-thread calls before UObject access"), OffThread.bSuccess);
	TestTrue(TEXT("Off-thread error explains required context"), OffThread.Message.Contains(TEXT("game thread")));
	if (GEditor)
	{
		UWorld* TestWorld = NewObject<UWorld>(GetTransientPackage());
		TGuardValue<TObjectPtr<UWorld>> PlayWorldGuard(GEditor->PlayWorld, TestWorld);
		const FBlueprintLispPythonResult DuringPIE = UBlueprintLispToolset::MergeBlueprintGraph(
			TEXT("/Game/GeneralBlueprintMCP/BP_NotLoaded"), TEXT("EventGraph"), TEXT("(event BeginPlay)"), false, false);
		TestFalse(TEXT("Mutation is blocked during PIE/SIE"), DuringPIE.bSuccess);
		TestTrue(TEXT("PIE rejection explains required editor state"), DuringPIE.Message.Contains(TEXT("Play In Editor")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBlueprintLispMCPEditWorkflowTest,
	"BlueprintLisp.MCP.EditWorkflow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FBlueprintLispMCPEditWorkflowTest::RunTest(const FString& /*Parameters*/)
{
	// A unique in-memory /Game package exercises the public path resolver without writing a .uasset.
	const FString PackagePath = TEXT("/Game/__GeneralBlueprintMCPTests/") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT("/BP_Fixture");
	UPackage* Package = CreatePackage(*PackagePath);
	Package->SetFlags(RF_Transient);
	UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
		AActor::StaticClass(), Package, TEXT("BP_Fixture"), BPTYPE_Normal,
		UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass(), TEXT("BlueprintLispMCPTests"));
	if (!TestNotNull(TEXT("Fixture Blueprint created"), Blueprint)) return false;
	Blueprint->SetFlags(RF_Transient);
	UEdGraph* Graph = nullptr;
	for (UEdGraph* Candidate : Blueprint->UbergraphPages)
	{
		if (Candidate && Candidate->GetName() == TEXT("EventGraph")) { Graph = Candidate; break; }
	}
	if (!Graph)
	{
		Graph = FBlueprintEditorUtils::CreateNewGraph(Blueprint, TEXT("EventGraph"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
		FBlueprintEditorUtils::AddUbergraphPage(Blueprint, Graph);
	}
	if (!TestNotNull(TEXT("Fixture graph created"), Graph)) return false;
	const FString ObjectPath = Blueprint->GetPathName();
	TestTrue(TEXT("Package paths resolve the loaded Blueprint"), UBlueprintLispToolset::ListBlueprintGraphs(PackagePath).bSuccess);
	const FBlueprintLispPythonResult Replaced = UBlueprintLispToolset::ReplaceBlueprintGraph(
		ObjectPath, TEXT("EventGraph"), TEXT("(event BeginPlay (PrintString \"MCP fixture\"))"), false, false);
	TestTrue(*FString::Printf(TEXT("Replace succeeds: %s"), *Replaced.Message), Replaced.bSuccess);
	TestFalse(TEXT("Explicit save=false does not persist"), Replaced.bSavedPackage);
	const FBlueprintLispPythonResult Before = UBlueprintLispToolset::ExportBlueprintGraph(ObjectPath, TEXT("EventGraph"), false, true);
	TestTrue(TEXT("Edited graph exports"), Before.bSuccess);
	TestTrue(TEXT("Export reflects real imported node"), Before.DSLText.Contains(TEXT("MCP fixture")));
	const FBlueprintLispPythonResult Invalid = UBlueprintLispToolset::ReplaceBlueprintGraph(
		ObjectPath, TEXT("EventGraph"), TEXT("(event BeginPlay"), false, false);
	TestFalse(TEXT("Malformed replace fails"), Invalid.bSuccess);
	const FBlueprintLispPythonResult After = UBlueprintLispToolset::ExportBlueprintGraph(ObjectPath, TEXT("EventGraph"), false, true);
	TestEqual(TEXT("Malformed replace leaves existing graph unchanged"), After.DSLText, Before.DSLText);
	const int32 BeforeAppend = Graph->Nodes.Num();
	const FBlueprintLispPythonResult Appended = UBlueprintLispToolset::MergeBlueprintGraph(
		ObjectPath, TEXT("EventGraph"), TEXT("(event MCP_Appended)"), false, false);
	TestTrue(*FString::Printf(TEXT("Append succeeds: %s"), *Appended.Message), Appended.bSuccess);
	TestTrue(TEXT("Append adds actual graph nodes"), Graph->Nodes.Num() > BeforeAppend);
	TestFalse(TEXT("Append save=false does not persist"), Appended.bSavedPackage);
	TestTrue(TEXT("Members query works on edited asset"), UBlueprintLispToolset::ListBlueprintMemberVariables(ObjectPath).bSuccess);
	Package->SetDirtyFlag(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
