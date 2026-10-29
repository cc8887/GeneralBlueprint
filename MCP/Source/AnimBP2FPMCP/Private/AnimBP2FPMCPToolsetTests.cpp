// AnimBP2FPMCPToolsetTests.cpp - MCP exposure regression coverage

#include "AnimBP2FPMCPToolsetImpl.h"

#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "ToolsetRegistry/UToolsetRegistry.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAnimBP2FPMCPToolsetSurfaceTest,
	"AnimBP2FP.MCP.ToolsetSurface",
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::CommandletContext |
	EAutomationTestFlags::EngineFilter)

bool FAnimBP2FPMCPToolsetSurfaceTest::RunTest(const FString& /*Parameters*/)
{
	TestTrue(
		TEXT("AnimBP2FP Toolset should be registered with Toolset Registry"),
		UToolsetRegistry::IsToolsetClassRegistered(UAnimBP2FPToolset::StaticClass()));

	const FString Schema = UToolsetRegistry::GetToolsetJsonSchema(UAnimBP2FPToolset::StaticClass());
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
	TestEqual(TEXT("Toolset schema tool count"), ToolEntries->Num(), 11);

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
			}
		}
	}

	const TSet<FString> ExpectedToolNames = {
		TEXT("ExportAnimBlueprint"),
		TEXT("ExportAnimBlueprintBundle"),
		TEXT("ExportControlRig"),
		TEXT("ExportEventGraph"),
		TEXT("GetAnimBlueprintSyncState"),
		TEXT("ValidateAnimBundle"),
		TEXT("ValidateAnimBlueprintRoundTrip"),
		TEXT("ApplyAnimBundle"),
		TEXT("UpdateAnimBlueprint"),
		TEXT("ReplaceEventGraph"),
		TEXT("MergeEventGraph")
	};

	TestEqual(TEXT("MCP tool count"), ToolNames.Num(), ExpectedToolNames.Num());
	for (const FString& ExpectedName : ExpectedToolNames)
	{
		TestTrue(
			*FString::Printf(TEXT("MCP tool %s should be exposed"), *ExpectedName),
			ToolNames.Contains(ExpectedName));
	}

	const TSet<FString> ForbiddenToolNames = {
		TEXT("list_anim_blueprints"),
		TEXT("validate_anim_blueprint_roundtrip"),
		TEXT("lint_anim_bundle")
	};
	for (const FString& ForbiddenName : ForbiddenToolNames)
	{
		TestFalse(
			*FString::Printf(TEXT("MCP tool %s must not be exposed"), *ForbiddenName),
			ToolNames.Contains(ForbiddenName));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAnimBP2FPMCPInputContractTest,
	"AnimBP2FP.MCP.InputContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::EngineFilter)

bool FAnimBP2FPMCPInputContractTest::RunTest(const FString& /*Parameters*/)
{
	const TArray<FString> InvalidPaths = {
		TEXT("/Engine/Anim"), TEXT("/Game/../Anim"), TEXT("C:\\Anim.uasset"),
		TEXT("/Game/Anim.Anim:Subobject"), TEXT("AnimBlueprint'/Game/Anim.Anim'"), TEXT("/Game/Anim.Other")
	};
	for (const FString& Path : InvalidPaths)
	{
		TestFalse(TEXT("Export rejects invalid project paths before loading"),
			UAnimBP2FPToolset::ExportAnimBlueprint(Path).bSuccess);
		const FAnimBP2FPMCPChangeResult Change = UAnimBP2FPToolset::UpdateAnimBlueprint(Path, TEXT("invalid"), false);
		TestFalse(TEXT("Update rejects invalid project paths"), Change.bSuccess);
		TestFalse(TEXT("Rejected input does not start mutation"), Change.bMutationStarted);
	}
	TArray<FAnimBP2FPMCPBundleSource> Sources;
	FAnimBP2FPMCPBundleSource& Source = Sources.AddDefaulted_GetRef();
	Source.SourceFile = TEXT("invalid.animlang");
	Source.Source = TEXT("(anim-blueprint");
	const FAnimBP2FPMCPBundleResult Invalid = UAnimBP2FPToolset::ValidateAnimBundle(Sources);
	TestFalse(TEXT("Invalid bundle fails read-only validation"), Invalid.bSuccess);
	TestTrue(TEXT("Invalid bundle returns structured source diagnostics"), Invalid.Diagnostics.Num() > 0);
	TestFalse(TEXT("Validation never starts persistence"), Invalid.bMutationStarted || Invalid.bPersisted);
	Sources[0].SourceFile = TEXT("../outside.animlang");
	TestFalse(TEXT("Logical source traversal is rejected"), UAnimBP2FPToolset::ValidateAnimBundle(Sources).bSuccess);
	TestFalse(TEXT("Persistent target traversal is rejected"),
		UAnimBP2FPToolset::ApplyAnimBundle(Sources, TEXT("/Game/../Engine"), EAnimBP2FPMCPBundleMode::Strict, false).bSuccess);
	for (const FName Name : { FName(TEXT("UpdateAnimBlueprint")), FName(TEXT("ReplaceEventGraph")), FName(TEXT("MergeEventGraph")) })
	{
		const UFunction* Function = UAnimBP2FPToolset::StaticClass()->FindFunctionByName(Name);
		if (TestNotNull(TEXT("Mutating function exists"), Function))
			TestFalse(TEXT("Save choice has no implicit default"), Function->HasMetaData(TEXT("CPP_Default_bSavePackage")));
	}
	const UFunction* Apply = UAnimBP2FPToolset::StaticClass()->FindFunctionByName(TEXT("ApplyAnimBundle"));
	if (TestNotNull(TEXT("Bundle apply exists"), Apply))
		TestFalse(TEXT("Persistence choice has no implicit default"), Apply->HasMetaData(TEXT("CPP_Default_bCommitPersistent")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAnimBP2FPMCPRealRigExportTest,
	"AnimBP2FP.MCP.RealRigExport",
	EAutomationTestFlags::EditorContext |
	EAutomationTestFlags::CommandletContext |
	EAutomationTestFlags::EngineFilter)

bool FAnimBP2FPMCPRealRigExportTest::RunTest(const FString& /*Parameters*/)
{
	const FString AnimPath = TEXT("/Game/Blueprints/SandboxCharacter_Mover_ABP");
	if (!LoadObject<UObject>(nullptr, TEXT("/Game/Blueprints/SandboxCharacter_Mover_ABP.SandboxCharacter_Mover_ABP")))
	{
		AddInfo(TEXT("SKIPPED: real GASP AnimBlueprint fixture is not installed"));
		return true;
	}

	const FAnimBP2FPMCPExportBundleResult Bundle =
		UAnimBP2FPToolset::ExportAnimBlueprintBundle(AnimPath, true);
	if (!TestTrue(TEXT("MCP exports the real Anim/Rig bundle"), Bundle.bSuccess)) return false;
	TestEqual(TEXT("MCP bundle contains two independent Rig documents"), Bundle.RigModules.Num(), 2);
	TestTrue(TEXT("Anim document contains hierarchy-control endpoint metadata"),
		Bundle.AnimBlueprint.Text.Contains(TEXT("hand_l_target :target-kind \"control\"")));

	const FString FullBodyPath =
		TEXT("/Game/Characters/UEFN_Mannequin/Rigs/CR_UEFN_Mannequin_FullBodyIK");
	const FAnimBP2FPMCPExportBundleResult Rig =
		UAnimBP2FPToolset::ExportControlRig(FullBodyPath, true);
	if (!TestTrue(TEXT("MCP exports the UE5.8 RuntimeAsset Control Rig"), Rig.bSuccess)) return false;
	if (!TestEqual(TEXT("single Rig export returns one document"), Rig.RigModules.Num(), 1)) return false;
	TestEqual(TEXT("RuntimeAsset is classified as a standalone Rig"),
		Rig.RigModules[0].ModuleKind, FString(TEXT("standalone")));
	TestTrue(TEXT("RuntimeAsset RigLang contains the hand target control"),
		Rig.RigModules[0].Text.Contains(TEXT("(control :id \"Control(hand_l_target)\"")));
	return TestTrue(TEXT("RuntimeAsset RigLang has a canonical content hash"),
		Rig.RigModules[0].ContentHash.StartsWith(TEXT("sha256:")));
}
