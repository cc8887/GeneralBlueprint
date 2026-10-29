// UE 5.8 native MCP Toolset registration and schema tests.

#if defined(MATBP2FP_WITH_MCP) && MATBP2FP_WITH_MCP && WITH_DEV_AUTOMATION_TESTS

#include "CoreMinimal.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "MatBP2FPMCPToolset.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialFunction.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "ToolsetRegistry/UToolsetRegistry.h"
#include "UObject/Package.h"

#define MBP_MCP_TEST_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMatBP2FPMCPToolsetSchema,
	"MatBP2FP.MCP.ToolsetSchema", MBP_MCP_TEST_FLAGS)

bool FMatBP2FPMCPToolsetSchema::RunTest(const FString& Parameters)
{
	const FString InspectSchema = UToolsetRegistry::GetToolsetJsonSchema(
		UMatBP2FPInspectToolset::StaticClass());
	const FString EditSchema = UToolsetRegistry::GetToolsetJsonSchema(
		UMatBP2FPEditToolset::StaticClass());

	TestFalse(TEXT("Inspect schema is not empty"), InspectSchema.IsEmpty());
	TestFalse(TEXT("Edit schema is not empty"), EditSchema.IsEmpty());
	TestTrue(TEXT("Inspect schema contains export tool"),
		InspectSchema.Contains(TEXT("ExportMaterialToText")));
	TestTrue(TEXT("Inspect schema contains project path tool"),
		InspectSchema.Contains(TEXT("MaterialPathToDSLPath")));
	TestTrue(TEXT("Inspect schema contains material function export"),
		InspectSchema.Contains(TEXT("ExportMaterialFunctionToText")));
	TestTrue(TEXT("Inspect schema contains in-memory lint"), InspectSchema.Contains(TEXT("ValidateMatLang")));
	TestTrue(TEXT("Edit schema contains import tool"),
		EditSchema.Contains(TEXT("ImportMaterialFromText")));
	TestTrue(TEXT("Edit schema contains explicit save argument"),
		EditSchema.Contains(TEXT("bSavePackage")));
	TestFalse(TEXT("MCP surface does not expose file system import"),
		EditSchema.Contains(TEXT("ImportMaterialFromFile")));
	TSharedPtr<FJsonObject> Root;
	if (TestTrue(TEXT("Edit schema is valid JSON"), FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(EditSchema), Root)))
	{
		for (const TSharedPtr<FJsonValue>& Entry : Root->GetArrayField(TEXT("tools")))
		{
			const TSharedPtr<FJsonObject> Tool = Entry->AsObject();
			const TSharedPtr<FJsonObject> Input = Tool->GetObjectField(TEXT("inputSchema"));
			TArray<FString> Required;
			Input->TryGetStringArrayField(TEXT("required"), Required);
			TestTrue(TEXT("Every material edit requires an explicit save choice"), Required.Contains(TEXT("bSavePackage")));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMatBP2FPMCPToolsetRegistration,
	"MatBP2FP.MCP.ToolsetRegistration", MBP_MCP_TEST_FLAGS)

bool FMatBP2FPMCPToolsetRegistration::RunTest(const FString& Parameters)
{
	const bool bInspectWasRegistered = UToolsetRegistry::IsToolsetClassRegistered(
		UMatBP2FPInspectToolset::StaticClass());
	const bool bEditWasRegistered = UToolsetRegistry::IsToolsetClassRegistered(
		UMatBP2FPEditToolset::StaticClass());

	if (!bInspectWasRegistered)
	{
		UToolsetRegistry::RegisterToolsetClass(UMatBP2FPInspectToolset::StaticClass());
	}
	if (!bEditWasRegistered)
	{
		UToolsetRegistry::RegisterToolsetClass(UMatBP2FPEditToolset::StaticClass());
	}

	TestTrue(TEXT("Inspect toolset is registered"), UToolsetRegistry::IsToolsetClassRegistered(
		UMatBP2FPInspectToolset::StaticClass()));
	TestTrue(TEXT("Edit toolset is registered"), UToolsetRegistry::IsToolsetClassRegistered(
		UMatBP2FPEditToolset::StaticClass()));

	if (!bEditWasRegistered)
	{
		UToolsetRegistry::UnregisterToolsetClass(UMatBP2FPEditToolset::StaticClass());
	}
	if (!bInspectWasRegistered)
	{
		UToolsetRegistry::UnregisterToolsetClass(UMatBP2FPInspectToolset::StaticClass());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMatBP2FPMCPInputContract,
	"MatBP2FP.MCP.InputContract", MBP_MCP_TEST_FLAGS)

bool FMatBP2FPMCPInputContract::RunTest(const FString& Parameters)
{
	const FMatBP2FPMCPLintResult Valid = UMatBP2FPInspectToolset::ValidateMatLang(
		TEXT("(material \"M_Valid\" (expressions (constant $value :value 0.5)) (outputs :roughness (connect $value)))"));
	TestTrue(TEXT("Valid MatLang passes syntax and graph validation"), Valid.bSuccess);
	TestFalse(TEXT("Validated text includes its canonical form"), Valid.CanonicalText.IsEmpty());
	const FMatBP2FPMCPLintResult Invalid = UMatBP2FPInspectToolset::ValidateMatLang(
		TEXT("(material \"M_Invalid\" (outputs :roughness (connect $missing)))"));
	TestFalse(TEXT("Dangling expression reference fails graph validation"), Invalid.bSuccess);
	TestTrue(TEXT("Graph validation returns source diagnostics"), Invalid.Diagnostics.Num() > 0);
	if (Invalid.Diagnostics.Num() > 0)
	{
		TestEqual(TEXT("Unknown connection reports a stable rule"), Invalid.Diagnostics[0].RuleId, FString(TEXT("ML1002")));
		TestTrue(TEXT("Source diagnostic has a location"), Invalid.Diagnostics[0].Line > 0 && Invalid.Diagnostics[0].Column > 0);
	}
	for (const FString& Path : {FString(TEXT("/Engine/M")), FString(TEXT("/Game/../M")), FString(TEXT("C:\\M.uasset")), FString(TEXT("/Game/M.M:Subobject")), FString(TEXT("/Game/M.Other"))})
	{
		TestFalse(TEXT("Export rejects paths outside project assets"), UMatBP2FPInspectToolset::ExportMaterialToText(Path).bSuccess);
		TestFalse(TEXT("Update rejects paths outside project assets"), UMatBP2FPEditToolset::UpdateMaterialFromText(Path, TEXT("invalid"), false).bSuccess);
	}
	TestFalse(TEXT("Import rejects traversal in a DSL asset name before creating a package"),
		UMatBP2FPEditToolset::ImportMaterialFromText(TEXT("(material \"../Escape\")"), TEXT("/Game/MCP"), false).bSuccess);
	TestFalse(TEXT("Material Function document is rejected by material import"),
		UMatBP2FPEditToolset::ImportMaterialFromText(TEXT("(material-function \"MF_Test\")"), TEXT("/Game/MCP"), false).bSuccess);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMatBP2FPMCPOperations,
	"MatBP2FP.MCP.InMemoryAssetOperations", MBP_MCP_TEST_FLAGS)

bool FMatBP2FPMCPOperations::RunTest(const FString& Parameters)
{
	const FString Suffix = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	const FString Name = TEXT("M_MCP_") + Suffix;
	const FString Folder = TEXT("/Game/GeneralBlueprintMCPTests");
	const FString PackagePath = Folder / Name;
	const FString Source = FString::Printf(TEXT("(material \"%s\" (expressions (constant $value :value 0.5)) (outputs :roughness (connect $value)))"), *Name);
	const FMatBP2FPPythonResult Imported = UMatBP2FPEditToolset::ImportMaterialFromText(Source, Folder, false);
	TestTrue(TEXT("Native MCP imports a material into an unsaved project package"), Imported.bSuccess);
	TestFalse(TEXT("Unsaved import reports no persistence"), Imported.bSavedPackage);
	TestFalse(TEXT("Unsaved import writes no package to disk"), FPackageName::DoesPackageExist(PackagePath));
	if (Imported.bSuccess)
	{
		UMaterial* Material = FindObject<UMaterial>(nullptr, *(PackagePath + TEXT(".") + Name));
		if (TestNotNull(TEXT("Imported material is a real loaded asset"), Material))
		{
			const FExpressionInput* Roughness = Material->GetExpressionInputForProperty(MP_Roughness);
			const UMaterialExpressionConstant* Constant = Roughness ? Cast<UMaterialExpressionConstant>(Roughness->Expression) : nullptr;
			if (TestNotNull(TEXT("Imported roughness is connected to a constant"), Constant))
				TestEqual(TEXT("DSL value reaches the material expression"), Constant->R, 0.5f);
		}
		const FMatBP2FPPythonResult Exported = UMatBP2FPInspectToolset::ExportMaterialToText(PackagePath);
		TestTrue(TEXT("Native MCP exports the imported material"), Exported.bSuccess && Exported.DSLText.Contains(TEXT("constant")));
		TestFalse(TEXT("New import refuses to overwrite an existing material"), UMatBP2FPEditToolset::ImportMaterialFromText(Source, Folder, false).bSuccess);
	}
	UPackage* FunctionPackage = CreatePackage(*(Folder / (TEXT("MF_MCP_") + Suffix)));
	UMaterialFunction* Function = NewObject<UMaterialFunction>(FunctionPackage, *FString(TEXT("MF_MCP_") + Suffix), RF_Public | RF_Standalone);
	const FMatBP2FPPythonResult ExportedFunction = UMatBP2FPInspectToolset::ExportMaterialFunctionToText(Function->GetPathName());
	TestTrue(TEXT("Native MCP exports a real Material Function object"), ExportedFunction.bSuccess && ExportedFunction.DSLText.Contains(TEXT("material-function")));
	if (UObject* Material = FindObject<UObject>(nullptr, *(PackagePath + TEXT(".") + Name)))
	{
		FAssetRegistryModule::AssetDeleted(Material);
		Material->ClearFlags(RF_Public | RF_Standalone);
		Material->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
	}
	if (UPackage* Package = FindPackage(nullptr, *PackagePath)) Package->SetDirtyFlag(false);
	Function->ClearFlags(RF_Public | RF_Standalone);
	Function->Rename(nullptr, GetTransientPackage(), REN_DontCreateRedirectors | REN_NonTransactional);
	FunctionPackage->SetDirtyFlag(false);
	return true;
}

#endif // MATBP2FP_WITH_MCP && WITH_DEV_AUTOMATION_TESTS
