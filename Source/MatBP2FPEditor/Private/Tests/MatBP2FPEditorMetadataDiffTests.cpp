// Copyright (c) 2026 cc8887. All Rights Reserved.

#include "CoreMinimal.h"
#include "MatBP2FPVersionCompat.h"
#include "MatBPExporter.h"
#include "MatBPImporter.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Misc/AutomationTest.h"
#include "Runtime/Launch/Resources/Version.h"

#if WITH_DEV_AUTOMATION_TESTS

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
#define MBP_METADATA_TEST_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
#else
#define MBP_METADATA_TEST_FLAGS (EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
#endif

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FMatBPOmittedEditorMetadataDiffTest,
	"MatBP2FP.Compatibility.Diff.UnserializedEditorMetadata_IsIgnored",
	MBP_METADATA_TEST_FLAGS)

bool FMatBPOmittedEditorMetadataDiffTest::RunTest(const FString& Parameters)
{
	const FString Source = TEXT(
		"(material \"M_EditorMetadata\"\n"
		"  (expressions\n"
		"    (constant $const1 :value 0.5))\n"
		"  (outputs :roughness (connect $const1 0)))");

	UMaterial* Material = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
	Material->AddToRoot();
	const FMatBPImporter::FImportResult Initial = FMatBPImporter::UpdateMaterial(Material, Source);
	bool bPassed = TestTrue(TEXT("Metadata fixture imports"), Initial.bSuccess);

	UMaterialExpression* Target = nullptr;
	for (UMaterialExpression* Expression : MatBP2FPCompat::GetMaterialExpressions(Material))
	{
		if (Expression)
		{
			Target = Expression;
			break;
		}
	}
	bPassed &= TestNotNull(TEXT("Metadata target expression exists"), Target);
	if (Target)
	{
		Target->MaterialExpressionEditorX = 320;
		Target->MaterialExpressionEditorY = -128;
		Target->Desc = TEXT("Keep this editor-only description");

		const FMatBPImporter::FUpdateResult OmittedUpdate =
			FMatBPImporter::UpdateMaterialDetailed(Material, Source);
		bPassed &= TestTrue(TEXT("DSL update with omitted metadata succeeds"), OmittedUpdate.bSuccess);
		bPassed &= TestTrue(TEXT("DSL update with omitted metadata stays incremental"),
			OmittedUpdate.bUsedIncrementalPatch);
		bPassed &= TestEqual(TEXT("Omitted metadata creates no semantic or cosmetic changes"),
			OmittedUpdate.NumChanges, 0);
		bPassed &= TestEqual(TEXT("Omitted metadata preserves editor X"),
			Target->MaterialExpressionEditorX, 320);
		bPassed &= TestEqual(TEXT("Omitted metadata preserves editor Y"),
			Target->MaterialExpressionEditorY, -128);
		bPassed &= TestEqual(TEXT("Omitted metadata preserves the comment"),
			Target->Desc, FString(TEXT("Keep this editor-only description")));

		TSharedPtr<FMaterialGraphAST> ExplicitAST = FMatBPExporter::ExportToAST(Material);
		bPassed &= TestTrue(TEXT("Material exports for explicit AST update"), ExplicitAST.IsValid());
		if (ExplicitAST.IsValid() && ExplicitAST->Expressions.Num() == 1)
		{
			ExplicitAST->Expressions[0]->EditorPosition = FVector2D::ZeroVector;
			ExplicitAST->Expressions[0]->Comment.Empty();
			const FMatBPImporter::FUpdateResult ExplicitUpdate =
				FMatBPImporter::UpdateMaterialDetailedFromAST(Material, ExplicitAST);
			bPassed &= TestTrue(TEXT("Explicit AST metadata update succeeds"), ExplicitUpdate.bSuccess);
			bPassed &= TestTrue(TEXT("Explicit AST metadata update stays incremental"),
				ExplicitUpdate.bUsedIncrementalPatch);
			bPassed &= TestEqual(TEXT("Explicit position and comment produce two cosmetic changes"),
				ExplicitUpdate.NumChanges, 2);
			bPassed &= TestEqual(TEXT("Explicit metadata has no semantic property changes"),
				ExplicitUpdate.NumPropertyChanges, 0);
			bPassed &= TestEqual(TEXT("Both explicit metadata changes are applied"),
				ExplicitUpdate.NumApplied, 2);
			bPassed &= TestEqual(TEXT("Explicit AST clears editor X"),
				Target->MaterialExpressionEditorX, 0);
			bPassed &= TestEqual(TEXT("Explicit AST clears editor Y"),
				Target->MaterialExpressionEditorY, 0);
			bPassed &= TestTrue(TEXT("Explicit AST clears the comment"), Target->Desc.IsEmpty());

			TSharedPtr<FMaterialGraphAST> CombinedAST = FMatBPExporter::ExportToAST(Material);
			bPassed &= TestTrue(TEXT("Material exports for combined metadata update"), CombinedAST.IsValid());
			if (CombinedAST.IsValid() && CombinedAST->Expressions.Num() == 1)
			{
				CombinedAST->Expressions[0]->EditorPosition = FVector2D(1.0f, 0.0f);
				CombinedAST->Expressions[0]->Properties.Add(TEXT("value"), TEXT("0.75"));
				const FMatBPImporter::FUpdateResult CombinedUpdate =
					FMatBPImporter::UpdateMaterialDetailedFromAST(Material, CombinedAST);
				bPassed &= TestTrue(TEXT("Combined AST metadata and property update succeeds"),
					CombinedUpdate.bSuccess);
				bPassed &= TestTrue(TEXT("Combined AST update stays incremental"),
					CombinedUpdate.bUsedIncrementalPatch);
				bPassed &= TestEqual(TEXT("One-pixel position and value both produce changes"),
					CombinedUpdate.NumChanges, 2);
				bPassed &= TestEqual(TEXT("Combined AST update has one semantic property change"),
					CombinedUpdate.NumPropertyChanges, 1);
				bPassed &= TestEqual(TEXT("Combined AST changes are both applied"),
					CombinedUpdate.NumApplied, 2);
				bPassed &= TestEqual(TEXT("Explicit one-pixel X survives incremental AutoLayout"),
					Target->MaterialExpressionEditorX, 1);
				bPassed &= TestEqual(TEXT("Explicit one-pixel Y survives incremental AutoLayout"),
					Target->MaterialExpressionEditorY, 0);
				if (UMaterialExpressionConstant* Constant = Cast<UMaterialExpressionConstant>(Target))
				{
					bPassed &= TestTrue(TEXT("Combined AST value is applied"),
						FMath::IsNearlyEqual(Constant->R, 0.75f));
				}
				else
				{
					AddError(TEXT("Metadata target is not a constant expression"));
					bPassed = false;
				}

				TSharedPtr<FMaterialGraphAST> StructuralAST = FMatBPExporter::ExportToAST(Material);
				bPassed &= TestTrue(TEXT("Material exports for structural metadata update"),
					StructuralAST.IsValid());
				if (StructuralAST.IsValid() && StructuralAST->Expressions.Num() == 1)
				{
					StructuralAST->Expressions[0]->EditorPosition = FVector2D(123.0f, 45.0f);
					StructuralAST->Expressions[0]->Comment = TEXT("Existing exact position");
					TSharedPtr<FMatExpressionAST> Added = MakeShared<FMatExpressionAST>();
					Added->ExprType = TEXT("constant");
					Added->Id = TEXT("$added");
					Added->Properties.Add(TEXT("value"), TEXT("0.25"));
					Added->EditorPosition = FVector2D(777.0f, -333.0f);
					Added->Comment = TEXT("Added exact position");
					StructuralAST->Expressions.Add(Added);

					const FMatBPImporter::FUpdateResult StructuralUpdate =
						FMatBPImporter::UpdateMaterialDetailedFromAST(Material, StructuralAST);
					bPassed &= TestTrue(TEXT("Structural direct AST update succeeds"),
						StructuralUpdate.bSuccess);
					bPassed &= TestFalse(TEXT("Structural direct AST update uses full rebuild"),
						StructuralUpdate.bUsedIncrementalPatch);
					bPassed &= TestEqual(TEXT("Structural direct AST update adds one expression"),
						StructuralUpdate.NumStructuralChanges, 1);

					UMaterialExpressionConstant* ExistingConstant = nullptr;
					UMaterialExpressionConstant* AddedConstant = nullptr;
					for (UMaterialExpression* Expression : MatBP2FPCompat::GetMaterialExpressions(Material))
					{
						if (UMaterialExpressionConstant* Constant = Cast<UMaterialExpressionConstant>(Expression))
						{
							if (FMath::IsNearlyEqual(Constant->R, 0.75f))
							{
								ExistingConstant = Constant;
							}
							else if (FMath::IsNearlyEqual(Constant->R, 0.25f))
							{
								AddedConstant = Constant;
							}
						}
					}
					bPassed &= TestNotNull(TEXT("Rebuilt existing constant exists"), ExistingConstant);
					bPassed &= TestNotNull(TEXT("Rebuilt added constant exists"), AddedConstant);
					if (ExistingConstant)
					{
						bPassed &= TestEqual(TEXT("Existing direct AST X survives structural rebuild"),
							ExistingConstant->MaterialExpressionEditorX, 123);
						bPassed &= TestEqual(TEXT("Existing direct AST Y survives structural rebuild"),
							ExistingConstant->MaterialExpressionEditorY, 45);
						bPassed &= TestEqual(TEXT("Existing direct AST comment survives structural rebuild"),
							ExistingConstant->Desc, FString(TEXT("Existing exact position")));
					}
					if (AddedConstant)
					{
						bPassed &= TestEqual(TEXT("Added direct AST X survives structural rebuild"),
							AddedConstant->MaterialExpressionEditorX, 777);
						bPassed &= TestEqual(TEXT("Added direct AST Y survives structural rebuild"),
							AddedConstant->MaterialExpressionEditorY, -333);
						bPassed &= TestEqual(TEXT("Added direct AST comment survives structural rebuild"),
							AddedConstant->Desc, FString(TEXT("Added exact position")));
					}
				}
			}
		}
		else if (ExplicitAST.IsValid())
		{
			AddError(TEXT("Expected exactly one expression in metadata fixture"));
			bPassed = false;
		}
	}

	Material->RemoveFromRoot();
	return bPassed;
}

#endif // WITH_DEV_AUTOMATION_TESTS
