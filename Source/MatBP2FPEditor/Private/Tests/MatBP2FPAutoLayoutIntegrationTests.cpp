// Copyright (c) 2026 cc8887. All Rights Reserved.

#include "CoreMinimal.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "EdGraph/EdGraphPin.h"
#include "MaterialGraph/MaterialGraph.h"
#include "MaterialGraph/MaterialGraphNode.h"
#include "MaterialGraph/MaterialGraphNode_Root.h"
#include "MatBP2FPVersionCompat.h"
#include "MatBPImporter.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Runtime/Launch/Resources/Version.h"

#if WITH_DEV_AUTOMATION_TESTS

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
#define MBP_AUTOLAYOUT_TEST_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
#else
#define MBP_AUTOLAYOUT_TEST_FLAGS (EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
#endif

#define MBP_AUTOLAYOUT_TEST(Name) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMatBPAutoLayout##Name, "MatBP2FP.Integration.AutoLayout." #Name, MBP_AUTOLAYOUT_TEST_FLAGS)

namespace MatBP2FPAutoLayoutTests
{
	static bool EnsureAutoLayoutModule(FAutomationTestBase& Test, bool& bOutShouldSkip)
	{
		bOutShouldSkip = false;
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("GeneralBlueprint"));
		if (!Plugin.IsValid() || !Plugin->IsEnabled())
		{
			Test.AddInfo(TEXT("GeneralBlueprint is not enabled; integrated Auto Layout test skipped"));
			bOutShouldSkip = true;
			return true;
		}

		IModuleInterface* Module = FModuleManager::Get().LoadModulePtr<IModuleInterface>(TEXT("BlueprintAutoLayout"));
		return Test.TestNotNull(TEXT("BlueprintAutoLayout module loads"), Module);
	}

	static bool LoadPluginFile(const FString& RelativePath, FString& OutSource, FString& OutError)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("GeneralBlueprint"));
		if (!Plugin.IsValid())
		{
			OutError = TEXT("GeneralBlueprint plugin was not found");
			return false;
		}

		const FString FilePath = FPaths::Combine(Plugin->GetBaseDir(), RelativePath);
		if (!FFileHelper::LoadFileToString(OutSource, *FilePath))
		{
			OutError = FString::Printf(TEXT("Failed to load test graph: %s"), *FilePath);
			return false;
		}
		return true;
	}

	static UMaterial* CreateTransientMaterial()
	{
		UMaterial* Material = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
		Material->AddToRoot();
		return Material;
	}

	static void ReleaseTransientMaterial(UMaterial* Material)
	{
		if (Material)
		{
			Material->RemoveFromRoot();
		}
	}

	static TArray<UMaterialExpression*> GetLayoutExpressions(UMaterial* Material)
	{
		TArray<UMaterialExpression*> Expressions;
		for (UMaterialExpression* Expression : MatBP2FPCompat::GetMaterialExpressions(Material))
		{
			if (Expression && !Expression->IsA<UMaterialExpressionComment>())
			{
				Expressions.Add(Expression);
			}
		}
		return Expressions;
	}

	static FIntPoint GetPosition(const UMaterialExpression* Expression)
	{
		return FIntPoint(Expression->MaterialExpressionEditorX, Expression->MaterialExpressionEditorY);
	}

	static FVector2D EstimateGraphNodeSize(const UEdGraphNode* Node)
	{
		if (!Node)
		{
			return FVector2D(200.f, 80.f);
		}
		if (Node->NodeWidth > 0 && Node->NodeHeight > 0)
		{
			return FVector2D(Node->NodeWidth, Node->NodeHeight);
		}

		int32 InputPins = 0;
		int32 OutputPins = 0;
		int32 MaxNameLength = Node->GetNodeTitle(ENodeTitleType::ListView).ToString().Len();
		for (const UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin) continue;
			if (Pin->Direction == EGPD_Input) ++InputPins;
			else if (Pin->Direction == EGPD_Output) ++OutputPins;
			MaxNameLength = FMath::Max(MaxNameLength, Pin->PinName.ToString().Len());
		}
		return FVector2D(
			FMath::Clamp(160.f + MaxNameLength * 4.f, 160.f, 420.f),
			48.f + FMath::Max(InputPins, OutputPins) * 24.f);
	}

	static bool GraphNodesOverlap(const UEdGraphNode* A, const UEdGraphNode* B, float Margin)
	{
		if (!A || !B) return false;
		const FVector2D APos(A->NodePosX, A->NodePosY);
		const FVector2D BPos(B->NodePosX, B->NodePosY);
		const FVector2D ASize = EstimateGraphNodeSize(A);
		const FVector2D BSize = EstimateGraphNodeSize(B);
		return APos.X - Margin < BPos.X + BSize.X + Margin
			&& BPos.X - Margin < APos.X + ASize.X + Margin
			&& APos.Y - Margin < BPos.Y + BSize.Y + Margin
			&& BPos.Y - Margin < APos.Y + ASize.Y + Margin;
	}

	static bool VerifyRootDirectedLayout(
		FAutomationTestBase& Test,
		const FString& Label,
		UMaterial* Material,
		int32 MinimumExpressionRows,
		int32 ExpectedRootBranches)
	{
		UMaterialGraph* Graph = Material ? Material->MaterialGraph : nullptr;
		bool bPassed = Test.TestNotNull(Label + TEXT(" material graph"), Graph);
		if (!Graph) return false;

		UMaterialGraphNode_Root* Root = nullptr;
		TArray<UEdGraphNode*> ExpressionNodes;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			if (UMaterialGraphNode_Root* CandidateRoot = Cast<UMaterialGraphNode_Root>(Node))
			{
				Root = CandidateRoot;
			}
			else if (UMaterialGraphNode* MaterialNode = Cast<UMaterialGraphNode>(Node))
			{
				if (MaterialNode->MaterialExpression
					&& !MaterialNode->MaterialExpression->IsA<UMaterialExpressionComment>())
				{
					ExpressionNodes.Add(Node);
				}
			}
		}
		bPassed &= Test.TestNotNull(Label + TEXT(" material root"), Root);
		if (!Root) return false;

		TSet<int32> ExpressionRows;
		int32 MinY = MAX_int32;
		int32 MaxY = MIN_int32;
		for (const UEdGraphNode* Node : ExpressionNodes)
		{
			ExpressionRows.Add(Node->NodePosY);
			MinY = FMath::Min(MinY, Node->NodePosY);
			MaxY = FMath::Max(MaxY, Node->NodePosY);
			bPassed &= Test.TestTrue(Label + TEXT(" keeps the root to the right of expressions"),
				Node->NodePosX + EstimateGraphNodeSize(Node).X < Root->NodePosX);
		}
		bPassed &= Test.TestTrue(Label + TEXT(" spans readable expression rows"),
			ExpressionRows.Num() >= MinimumExpressionRows);

		TSet<UEdGraphNode*> DirectProducers;
		TArray<UEdGraphNode*> OrderedDirectProducers;
		for (UEdGraphPin* RootPin : Root->Pins)
		{
			if (!RootPin || RootPin->Direction != EGPD_Input) continue;
			for (UEdGraphPin* LinkedPin : RootPin->LinkedTo)
			{
				if (LinkedPin && LinkedPin->GetOwningNode())
				{
					UEdGraphNode* Producer = LinkedPin->GetOwningNode();
					DirectProducers.Add(Producer);
					OrderedDirectProducers.AddUnique(Producer);
				}
			}
		}
		bPassed &= Test.TestEqual(Label + TEXT(" root branch count"),
			DirectProducers.Num(), ExpectedRootBranches);

		TSet<int32> DirectRows;
		for (UEdGraphNode* Producer : DirectProducers)
		{
			DirectRows.Add(Producer->NodePosY);
		}
		TArray<UEdGraphNode*> OrderedDirectOnlyProducers;
		for (UEdGraphNode* Producer : OrderedDirectProducers)
		{
			TSet<UEdGraphNode*> Targets;
			for (UEdGraphPin* Pin : Producer->Pins)
			{
				if (!Pin || Pin->Direction != EGPD_Output) continue;
				for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
				{
					if (LinkedPin && LinkedPin->GetOwningNode())
					{
						Targets.Add(LinkedPin->GetOwningNode());
					}
				}
			}
			if (Targets.Num() == 1 && Targets.Contains(Root))
			{
				OrderedDirectOnlyProducers.Add(Producer);
				const float GapToRoot = Root->NodePosX
					- (Producer->NodePosX + EstimateGraphNodeSize(Producer).X);
				bPassed &= Test.TestTrue(
					Label + TEXT(" keeps direct-only root inputs in the adjacent column"),
					GapToRoot > 0.f && GapToRoot <= 640.f);
			}
		}
		for (int32 ProducerIndex = 1;
			ProducerIndex < OrderedDirectOnlyProducers.Num(); ++ProducerIndex)
		{
			const UEdGraphNode* Previous = OrderedDirectOnlyProducers[ProducerIndex - 1];
			const UEdGraphNode* Current = OrderedDirectOnlyProducers[ProducerIndex];
			const float PreviousBottom = Previous->NodePosY + EstimateGraphNodeSize(Previous).Y;
			bPassed &= Test.TestTrue(
				FString::Printf(
					TEXT("%s direct-only root branch %d follows pin order without vertical overlap"),
					*Label, ProducerIndex),
				Current->NodePosY >= PreviousBottom + 8.f);
		}
		const int32 DirectOnlyCount = OrderedDirectOnlyProducers.Num();
		bPassed &= Test.TestTrue(Label + TEXT(" exercises direct-only root inputs"), DirectOnlyCount > 0);

		for (int32 AIndex = 0; AIndex < ExpressionNodes.Num(); ++AIndex)
		{
			for (int32 BIndex = AIndex + 1; BIndex < ExpressionNodes.Num(); ++BIndex)
			{
				bPassed &= Test.TestFalse(Label + TEXT(" expression bounds do not overlap"),
					GraphNodesOverlap(ExpressionNodes[AIndex], ExpressionNodes[BIndex], 8.f));
			}
		}

		Test.AddInfo(FString::Printf(
			TEXT("%s layout metrics: expressions=%d rows=%d y-span=%d root-branches=%d root-rows=%d direct-only=%d"),
			*Label,
			ExpressionNodes.Num(),
			ExpressionRows.Num(),
			ExpressionNodes.Num() > 0 ? MaxY - MinY : 0,
			DirectProducers.Num(),
			DirectRows.Num(),
			DirectOnlyCount));
		return bPassed;
	}

	static void SetPosition(UMaterialExpression* Expression, const FIntPoint& Position)
	{
		Expression->MaterialExpressionEditorX = Position.X;
		Expression->MaterialExpressionEditorY = Position.Y;
	}

	struct FExpressionPositionSnapshot
	{
		UMaterialExpression* Expression = nullptr;
		FIntPoint Position = FIntPoint::ZeroValue;
		FString Name;
	};

	static TArray<FExpressionPositionSnapshot> SnapshotPinnedExpressions(
		UMaterial* Material,
		const TSet<UMaterialExpression*>& MovableExpressions)
	{
		TArray<FExpressionPositionSnapshot> Snapshots;
		for (UMaterialExpression* Expression : GetLayoutExpressions(Material))
		{
			if (!MovableExpressions.Contains(Expression))
			{
				FExpressionPositionSnapshot Snapshot;
				Snapshot.Expression = Expression;
				Snapshot.Position = GetPosition(Expression);
				Snapshot.Name = Expression->GetName();
				Snapshots.Add(MoveTemp(Snapshot));
			}
		}
		return Snapshots;
	}

	static bool VerifyPinnedExpressions(
		FAutomationTestBase& Test,
		const FString& Label,
		const TArray<UMaterialExpression*>& CurrentExpressions,
		const TArray<FExpressionPositionSnapshot>& Snapshots)
	{
		bool bPassed = true;
		for (const FExpressionPositionSnapshot& Snapshot : Snapshots)
		{
			const FString ExpressionLabel = Label + TEXT(" preserves pinned node ") + Snapshot.Name;
			const bool bStillPresent = CurrentExpressions.Contains(Snapshot.Expression);
			bPassed &= Test.TestTrue(ExpressionLabel + TEXT(" object"), bStillPresent);
			if (bStillPresent)
			{
				bPassed &= Test.TestEqual(ExpressionLabel + TEXT(" position"),
					GetPosition(Snapshot.Expression), Snapshot.Position);
			}
		}
		return bPassed;
	}

	static bool VerifyArranged(
		FAutomationTestBase& Test,
		const FString& Label,
		UMaterial* Material,
		int32 ExpectedExpressions,
		int32 MinimumColumns,
		int32 MinimumRows,
		int32 ExpectedRootBranches)
	{
		const TArray<UMaterialExpression*> Expressions = GetLayoutExpressions(Material);
		bool bPassed = Test.TestEqual(Label + TEXT(" expression count"), Expressions.Num(), ExpectedExpressions);
		TSet<FIntPoint> UniquePositions;
		TSet<int32> UniqueColumns;
		bool bAnyNonZero = false;
		for (const UMaterialExpression* Expression : Expressions)
		{
			const FIntPoint Position = GetPosition(Expression);
			UniquePositions.Add(Position);
			UniqueColumns.Add(Position.X);
			bAnyNonZero |= Position != FIntPoint::ZeroValue;
		}

		bPassed &= Test.TestTrue(Label + TEXT(" has non-zero layout positions"), bAnyNonZero);
		bPassed &= Test.TestEqual(Label + TEXT(" has no stacked expression positions"),
			UniquePositions.Num(), Expressions.Num());
		bPassed &= Test.TestTrue(Label + TEXT(" spans the expected layout columns"),
			UniqueColumns.Num() >= MinimumColumns);
		bPassed &= VerifyRootDirectedLayout(
			Test, Label, Material, MinimumRows, ExpectedRootBranches);
		return bPassed;
	}

	template <typename ExpressionType>
	static ExpressionType* FindExpression(UMaterial* Material)
	{
		for (UMaterialExpression* Expression : GetLayoutExpressions(Material))
		{
			if (ExpressionType* TypedExpression = Cast<ExpressionType>(Expression))
			{
				return TypedExpression;
			}
		}
		return nullptr;
	}

	static bool RunSingleNodeIncrementalTest(
		FAutomationTestBase& Test,
		const FString& Label,
		const FString& AfterSource,
		UMaterialExpression* Target,
		UMaterialExpression* Untouched,
		UMaterial* Material)
	{
		if (!Target || !Untouched)
		{
			return false;
		}

		const TArray<UMaterialExpression*> BeforeExpressions = GetLayoutExpressions(Material);
		const FIntPoint UntouchedDisplaced = GetPosition(Untouched) + FIntPoint(-4096, 2048);
		const FIntPoint TargetDisplaced = GetPosition(Target) + FIntPoint(6144, -3072);
		SetPosition(Untouched, UntouchedDisplaced);
		SetPosition(Target, TargetDisplaced);
		TSet<UMaterialExpression*> MovableExpressions;
		MovableExpressions.Add(Target);
		const TArray<FExpressionPositionSnapshot> PinnedSnapshots =
			SnapshotPinnedExpressions(Material, MovableExpressions);

		const FMatBPImporter::FUpdateResult UpdateResult =
			FMatBPImporter::UpdateMaterialDetailed(Material, AfterSource);
		bool bPassed = true;
		bPassed &= Test.TestTrue(Label + TEXT(" update succeeds"), UpdateResult.bSuccess);
		bPassed &= Test.TestTrue(Label + TEXT(" uses the incremental patcher"), UpdateResult.bUsedIncrementalPatch);
		bPassed &= Test.TestEqual(Label + TEXT(" has no structural changes"), UpdateResult.NumStructuralChanges, 0);
		bPassed &= Test.TestEqual(Label + TEXT(" reports one semantic change"), UpdateResult.NumChanges, 1);
		bPassed &= Test.TestEqual(Label + TEXT(" reports one property/input change"), UpdateResult.NumPropertyChanges, 1);
		bPassed &= Test.TestEqual(Label + TEXT(" has no patch failures"), UpdateResult.NumFailed, 0);
		if (!UpdateResult.bSuccess || !UpdateResult.bUsedIncrementalPatch)
		{
			return false;
		}

		const TArray<UMaterialExpression*> AfterExpressions = GetLayoutExpressions(Material);
		bPassed &= Test.TestEqual(Label + TEXT(" preserves expression count"),
			AfterExpressions.Num(), BeforeExpressions.Num());
		bPassed &= Test.TestTrue(Label + TEXT(" preserves the target object"), AfterExpressions.Contains(Target));
		bPassed &= Test.TestTrue(Label + TEXT(" preserves the untouched object"), AfterExpressions.Contains(Untouched));
		bPassed &= VerifyPinnedExpressions(Test, Label, AfterExpressions, PinnedSnapshots);
		if (AfterExpressions.Contains(Target))
		{
			bPassed &= Test.TestNotEqual(Label + TEXT(" runs selection layout for the changed node"),
				GetPosition(Target), TargetDisplaced);
		}
		return bPassed;
	}
}

MBP_AUTOLAYOUT_TEST(FullImportExamples)
bool FMatBPAutoLayoutFullImportExamples::RunTest(const FString& Parameters)
{
	bool bShouldSkip = false;
	if (!MatBP2FPAutoLayoutTests::EnsureAutoLayoutModule(*this, bShouldSkip) || bShouldSkip)
	{
		return bShouldSkip;
	}

	struct FExample
	{
		const TCHAR* RelativePath;
		const TCHAR* Label;
		int32 ExpectedExpressions;
		int32 MinimumColumns;
		int32 MinimumRows;
		int32 ExpectedRootBranches;
	};
	static const FExample Examples[] = {
		{ TEXT("Tests/Fixtures/baseline_pbr.matlang"), TEXT("Baseline PBR"), 4, 2, 3, 4 },
		{ TEXT("DSL/Examples/emissive_rim.matlang"), TEXT("Emissive rim"), 8, 3, 4, 4 },
		{ TEXT("Tests/Fixtures/dag_shared_inputs.matlang"), TEXT("Shared-input DAG"), 8, 3, 3, 3 },
	};

	bool bPassed = true;
	for (const FExample& Example : Examples)
	{
		FString Source;
		FString Error;
		if (!MatBP2FPAutoLayoutTests::LoadPluginFile(Example.RelativePath, Source, Error))
		{
			AddError(Error);
			bPassed = false;
			continue;
		}

		UMaterial* Material = MatBP2FPAutoLayoutTests::CreateTransientMaterial();
		const FMatBPImporter::FImportResult Result = FMatBPImporter::UpdateMaterial(Material, Source);
		bPassed &= TestTrue(FString(Example.Label) + TEXT(" import succeeds"), Result.bSuccess);
		if (Result.bSuccess)
		{
			bPassed &= MatBP2FPAutoLayoutTests::VerifyArranged(
				*this,
				Example.Label,
				Material,
				Example.ExpectedExpressions,
				Example.MinimumColumns,
				Example.MinimumRows,
				Example.ExpectedRootBranches);
		}
		MatBP2FPAutoLayoutTests::ReleaseTransientMaterial(Material);
	}
	return bPassed;
}

MBP_AUTOLAYOUT_TEST(IncrementalPropertyKeepsUntouchedNodesPinned)
bool FMatBPAutoLayoutIncrementalPropertyKeepsUntouchedNodesPinned::RunTest(const FString& Parameters)
{
	bool bShouldSkip = false;
	if (!MatBP2FPAutoLayoutTests::EnsureAutoLayoutModule(*this, bShouldSkip) || bShouldSkip)
	{
		return bShouldSkip;
	}

	FString BeforeSource;
	FString Error;
	if (!MatBP2FPAutoLayoutTests::LoadPluginFile(
		TEXT("Tests/Fixtures/incremental_before.matlang"), BeforeSource, Error))
	{
		AddError(Error);
		return false;
	}
	FString AfterSource = BeforeSource;
	if (!TestEqual(TEXT("Property fixture replacement is unique"),
		AfterSource.ReplaceInline(TEXT(":default 0.35"), TEXT(":default 0.8")), 1))
	{
		return false;
	}

	UMaterial* Material = MatBP2FPAutoLayoutTests::CreateTransientMaterial();
	const FMatBPImporter::FImportResult ImportResult = FMatBPImporter::UpdateMaterial(Material, BeforeSource);
	bool bPassed = TestTrue(TEXT("Incremental property fixture imports"), ImportResult.bSuccess);
	if (ImportResult.bSuccess)
	{
		UMaterialExpressionScalarParameter* Target =
			MatBP2FPAutoLayoutTests::FindExpression<UMaterialExpressionScalarParameter>(Material);
		UMaterialExpressionConstant3Vector* Untouched =
			MatBP2FPAutoLayoutTests::FindExpression<UMaterialExpressionConstant3Vector>(Material);
		bPassed &= TestNotNull(TEXT("Incremental target scalar exists"), Target);
		bPassed &= TestNotNull(TEXT("Untouched vector exists"), Untouched);
		bPassed &= MatBP2FPAutoLayoutTests::RunSingleNodeIncrementalTest(
			*this, TEXT("Property-only"), AfterSource,
			Target, Untouched, Material);
		const TArray<UMaterialExpression*> CurrentExpressions =
			MatBP2FPAutoLayoutTests::GetLayoutExpressions(Material);
		if (Target && CurrentExpressions.Contains(Target))
		{
			bPassed &= TestTrue(TEXT("Scalar default value was patched"),
				FMath::IsNearlyEqual(Target->DefaultValue, 0.8f));
		}
	}
	MatBP2FPAutoLayoutTests::ReleaseTransientMaterial(Material);
	return bPassed;
}

MBP_AUTOLAYOUT_TEST(IncrementalConnectionKeepsUntouchedNodesPinned)
bool FMatBPAutoLayoutIncrementalConnectionKeepsUntouchedNodesPinned::RunTest(const FString& Parameters)
{
	bool bShouldSkip = false;
	if (!MatBP2FPAutoLayoutTests::EnsureAutoLayoutModule(*this, bShouldSkip) || bShouldSkip)
	{
		return bShouldSkip;
	}

	FString BeforeSource;
	FString Error;
	if (!MatBP2FPAutoLayoutTests::LoadPluginFile(
		TEXT("Tests/Fixtures/dag_shared_inputs.matlang"), BeforeSource, Error))
	{
		AddError(Error);
		return false;
	}
	FString AfterSource = BeforeSource;
	if (!TestEqual(TEXT("Connection fixture replacement is unique"),
		AfterSource.ReplaceInline(TEXT(":b (connect $vparam1 0)"), TEXT(":b (connect $const7 0)")), 1))
	{
		return false;
	}

	UMaterial* Material = MatBP2FPAutoLayoutTests::CreateTransientMaterial();
	const FMatBPImporter::FImportResult ImportResult = FMatBPImporter::UpdateMaterial(Material, BeforeSource);
	bool bPassed = TestTrue(TEXT("Incremental connection fixture imports"), ImportResult.bSuccess);
	if (ImportResult.bSuccess)
	{
		UMaterialExpressionVectorParameter* Tint =
			MatBP2FPAutoLayoutTests::FindExpression<UMaterialExpressionVectorParameter>(Material);
		UMaterialExpressionConstant* Constant =
			MatBP2FPAutoLayoutTests::FindExpression<UMaterialExpressionConstant>(Material);
		UMaterialExpressionMultiply* Target = nullptr;
		for (UMaterialExpression* Expression : MatBP2FPAutoLayoutTests::GetLayoutExpressions(Material))
		{
			UMaterialExpressionMultiply* Multiply = Cast<UMaterialExpressionMultiply>(Expression);
			if (Multiply && Multiply->B.Expression == Tint)
			{
				Target = Multiply;
				break;
			}
		}
		bPassed &= TestNotNull(TEXT("Tint vector parameter exists"), Tint);
		bPassed &= TestNotNull(TEXT("Replacement constant exists"), Constant);
		bPassed &= TestNotNull(TEXT("Rewired multiply target exists"), Target);
		bPassed &= MatBP2FPAutoLayoutTests::RunSingleNodeIncrementalTest(
			*this, TEXT("Connection-only"), AfterSource,
			Target, Tint, Material);
		const TArray<UMaterialExpression*> CurrentExpressions =
			MatBP2FPAutoLayoutTests::GetLayoutExpressions(Material);
		if (Target && Constant && CurrentExpressions.Contains(Target))
		{
			bPassed &= TestTrue(TEXT("Multiply input now uses the replacement constant"),
				Target->B.Expression == Constant);
		}
	}
	MatBP2FPAutoLayoutTests::ReleaseTransientMaterial(Material);
	return bPassed;
}

MBP_AUTOLAYOUT_TEST(IncrementalConnectionKeepsReadableLanes)
bool FMatBPAutoLayoutIncrementalConnectionKeepsReadableLanes::RunTest(const FString& Parameters)
{
	bool bShouldSkip = false;
	if (!MatBP2FPAutoLayoutTests::EnsureAutoLayoutModule(*this, bShouldSkip) || bShouldSkip)
	{
		return bShouldSkip;
	}

	FString BeforeSource;
	FString Error;
	if (!MatBP2FPAutoLayoutTests::LoadPluginFile(
		TEXT("Tests/Fixtures/dag_shared_inputs.matlang"), BeforeSource, Error))
	{
		AddError(Error);
		return false;
	}
	FString AfterSource = BeforeSource;
	if (!TestEqual(TEXT("Readable connection fixture replacement is unique"),
		AfterSource.ReplaceInline(TEXT(":b (connect $vparam1 0)"), TEXT(":b (connect $const7 0)")), 1))
	{
		return false;
	}

	UMaterial* Material = MatBP2FPAutoLayoutTests::CreateTransientMaterial();
	const FMatBPImporter::FImportResult ImportResult =
		FMatBPImporter::UpdateMaterial(Material, BeforeSource);
	bool bPassed = TestTrue(TEXT("Readable connection fixture imports"), ImportResult.bSuccess);
	if (ImportResult.bSuccess)
	{
		bPassed &= MatBP2FPAutoLayoutTests::VerifyArranged(
			*this, TEXT("Readable connection initial"), Material, 8, 3, 3, 3);

		auto ApplyConnectionPatch = [this, Material, &bPassed](
			const FString& Source, const FString& Label)
		{
			const FMatBPImporter::FUpdateResult UpdateResult =
				FMatBPImporter::UpdateMaterialDetailed(Material, Source);
			bPassed &= TestTrue(Label + TEXT(" succeeds"), UpdateResult.bSuccess);
			bPassed &= TestTrue(Label + TEXT(" uses the incremental patcher"),
				UpdateResult.bUsedIncrementalPatch);
			bPassed &= TestEqual(Label + TEXT(" has no structural changes"),
				UpdateResult.NumStructuralChanges, 0);
			bPassed &= TestEqual(Label + TEXT(" reports one semantic change"),
				UpdateResult.NumChanges, 1);
			bPassed &= TestEqual(Label + TEXT(" reports one connection change"),
				UpdateResult.NumPropertyChanges, 1);
			bPassed &= TestEqual(Label + TEXT(" has no patch failures"),
				UpdateResult.NumFailed, 0);
			return UpdateResult.bSuccess && UpdateResult.bUsedIncrementalPatch;
		};

		if (ApplyConnectionPatch(AfterSource, TEXT("Readable connection forward patch")))
		{
			bPassed &= MatBP2FPAutoLayoutTests::VerifyArranged(
				*this, TEXT("Readable connection forward"), Material, 8, 3, 3, 3);

			TMap<UMaterialExpression*, FIntPoint> FirstForwardPositions;
			for (UMaterialExpression* Expression :
				MatBP2FPAutoLayoutTests::GetLayoutExpressions(Material))
			{
				FirstForwardPositions.Add(
					Expression, MatBP2FPAutoLayoutTests::GetPosition(Expression));
			}
			UMaterialGraphNode_Root* FirstRoot =
				Material->MaterialGraph ? Material->MaterialGraph->RootNode : nullptr;
			bPassed &= TestNotNull(TEXT("Readable connection root exists"), FirstRoot);
			const FIntPoint FirstRootPosition = FirstRoot
				? FIntPoint(FirstRoot->NodePosX, FirstRoot->NodePosY)
				: FIntPoint::ZeroValue;

			if (ApplyConnectionPatch(BeforeSource, TEXT("Readable connection reverse patch")))
			{
				bPassed &= MatBP2FPAutoLayoutTests::VerifyArranged(
					*this, TEXT("Readable connection reverse"), Material, 8, 3, 3, 3);
				if (ApplyConnectionPatch(AfterSource, TEXT("Readable connection repeat patch")))
				{
					bPassed &= MatBP2FPAutoLayoutTests::VerifyArranged(
						*this, TEXT("Readable connection repeated"), Material, 8, 3, 3, 3);
					const TArray<UMaterialExpression*> RepeatedExpressions =
						MatBP2FPAutoLayoutTests::GetLayoutExpressions(Material);
					for (const TPair<UMaterialExpression*, FIntPoint>& Pair : FirstForwardPositions)
					{
						const bool bStillPresent = RepeatedExpressions.Contains(Pair.Key);
						bPassed &= TestTrue(
							TEXT("Repeated connection topology preserves expression objects"),
							bStillPresent);
						if (bStillPresent)
						{
							bPassed &= TestEqual(
								TEXT("Repeated connection topology is position-idempotent"),
								MatBP2FPAutoLayoutTests::GetPosition(Pair.Key), Pair.Value);
						}
					}
					UMaterialGraphNode_Root* RepeatedRoot =
						Material->MaterialGraph ? Material->MaterialGraph->RootNode : nullptr;
					bPassed &= TestNotNull(TEXT("Repeated connection root exists"), RepeatedRoot);
					if (RepeatedRoot)
					{
						bPassed &= TestEqual(
							TEXT("Repeated connection topology preserves the root position"),
							FIntPoint(RepeatedRoot->NodePosX, RepeatedRoot->NodePosY),
							FirstRootPosition);
					}
				}
			}
		}
	}
	MatBP2FPAutoLayoutTests::ReleaseTransientMaterial(Material);
	return bPassed;
}

MBP_AUTOLAYOUT_TEST(IncrementalMaterialPropertyMovesRootAndPinsExpressions)
bool FMatBPAutoLayoutIncrementalMaterialPropertyMovesRootAndPinsExpressions::RunTest(const FString& Parameters)
{
	bool bShouldSkip = false;
	if (!MatBP2FPAutoLayoutTests::EnsureAutoLayoutModule(*this, bShouldSkip) || bShouldSkip)
	{
		return bShouldSkip;
	}

	FString BeforeSource;
	FString Error;
	if (!MatBP2FPAutoLayoutTests::LoadPluginFile(
		TEXT("Tests/Fixtures/incremental_before.matlang"), BeforeSource, Error))
	{
		AddError(Error);
		return false;
	}
	FString AfterSource = BeforeSource;
	if (!TestEqual(TEXT("Material property fixture replacement is unique"),
		AfterSource.ReplaceInline(TEXT(":two-sided false"), TEXT(":two-sided true")), 1))
	{
		return false;
	}

	UMaterial* Material = MatBP2FPAutoLayoutTests::CreateTransientMaterial();
	const FMatBPImporter::FImportResult ImportResult = FMatBPImporter::UpdateMaterial(Material, BeforeSource);
	bool bPassed = TestTrue(TEXT("Material property fixture imports"), ImportResult.bSuccess);
	UMaterialGraphNode_Root* RootBefore =
		Material->MaterialGraph ? Material->MaterialGraph->RootNode : nullptr;
	bPassed &= TestNotNull(TEXT("Material root exists before property update"), RootBefore);
	if (ImportResult.bSuccess && RootBefore)
	{
		const TSet<UMaterialExpression*> NoMovableExpressions;
		const TArray<MatBP2FPAutoLayoutTests::FExpressionPositionSnapshot> PinnedSnapshots =
			MatBP2FPAutoLayoutTests::SnapshotPinnedExpressions(Material, NoMovableExpressions);
		const FIntPoint RootDisplaced(
			RootBefore->NodePosX + 8192,
			RootBefore->NodePosY + 4096);
		Material->EditorX = RootDisplaced.X;
		Material->EditorY = RootDisplaced.Y;
		RootBefore->NodePosX = RootDisplaced.X;
		RootBefore->NodePosY = RootDisplaced.Y;

		const FMatBPImporter::FUpdateResult UpdateResult =
			FMatBPImporter::UpdateMaterialDetailed(Material, AfterSource);
		bPassed &= TestTrue(TEXT("Material property update succeeds"), UpdateResult.bSuccess);
		bPassed &= TestTrue(TEXT("Material property update uses the incremental patcher"),
			UpdateResult.bUsedIncrementalPatch);
		bPassed &= TestEqual(TEXT("Material property update has no structural changes"),
			UpdateResult.NumStructuralChanges, 0);
		bPassed &= TestEqual(TEXT("Material property update reports one semantic change"),
			UpdateResult.NumChanges, 1);
		bPassed &= TestEqual(TEXT("Material property update reports one property change"),
			UpdateResult.NumPropertyChanges, 1);
		bPassed &= TestEqual(TEXT("Material property update has no patch failures"),
			UpdateResult.NumFailed, 0);
		bPassed &= TestTrue(TEXT("Two-sided property was patched"), Material->TwoSided != 0);

		UMaterialGraphNode_Root* RootAfter =
			Material->MaterialGraph ? Material->MaterialGraph->RootNode : nullptr;
		bPassed &= TestNotNull(TEXT("Material root exists after property update"), RootAfter);
		if (RootAfter)
		{
			bPassed &= TestNotEqual(TEXT("Material property update lays out the root"),
				FIntPoint(RootAfter->NodePosX, RootAfter->NodePosY), RootDisplaced);
		}
		bPassed &= MatBP2FPAutoLayoutTests::VerifyPinnedExpressions(
			*this,
			TEXT("Material property"),
			MatBP2FPAutoLayoutTests::GetLayoutExpressions(Material),
			PinnedSnapshots);
	}
	MatBP2FPAutoLayoutTests::ReleaseTransientMaterial(Material);
	return bPassed;
}

MBP_AUTOLAYOUT_TEST(IncrementalOutputChangeTriggersSelectionLayout)
bool FMatBPAutoLayoutIncrementalOutputChangeTriggersSelectionLayout::RunTest(const FString& Parameters)
{
	bool bShouldSkip = false;
	if (!MatBP2FPAutoLayoutTests::EnsureAutoLayoutModule(*this, bShouldSkip) || bShouldSkip)
	{
		return bShouldSkip;
	}

	FString BeforeSource;
	FString Error;
	if (!MatBP2FPAutoLayoutTests::LoadPluginFile(
		TEXT("Tests/Fixtures/dag_shared_inputs.matlang"), BeforeSource, Error))
	{
		AddError(Error);
		return false;
	}
	FString AfterSource = BeforeSource;
	if (!TestEqual(TEXT("Output fixture replacement is unique"),
		AfterSource.ReplaceInline(
			TEXT(":base-color (connect $mul4 0)"),
			TEXT(":base-color (connect $mul6 0)")), 1))
	{
		return false;
	}

	UMaterial* Material = MatBP2FPAutoLayoutTests::CreateTransientMaterial();
	const FMatBPImporter::FImportResult ImportResult = FMatBPImporter::UpdateMaterial(Material, BeforeSource);
	bool bPassed = TestTrue(TEXT("Output-change fixture imports"), ImportResult.bSuccess);
	if (ImportResult.bSuccess)
	{
		FExpressionInput* BaseColorBefore =
			MatBP2FPCompat::GetMaterialInput(Material, TEXT("base-color"));
		FExpressionInput* EmissiveBefore =
			MatBP2FPCompat::GetMaterialInput(Material, TEXT("emissive-color"));
		UMaterialExpressionMultiply* OldProducer =
			BaseColorBefore ? Cast<UMaterialExpressionMultiply>(BaseColorBefore->Expression) : nullptr;
		UMaterialExpressionMultiply* NewProducer =
			EmissiveBefore ? Cast<UMaterialExpressionMultiply>(EmissiveBefore->Expression) : nullptr;
		UMaterialExpressionVectorParameter* Untouched =
			MatBP2FPAutoLayoutTests::FindExpression<UMaterialExpressionVectorParameter>(Material);
		UMaterialGraphNode_Root* RootBefore =
			Material->MaterialGraph ? Material->MaterialGraph->RootNode : nullptr;
		bPassed &= TestNotNull(TEXT("Old output producer exists"), OldProducer);
		bPassed &= TestNotNull(TEXT("New output producer exists"), NewProducer);
		bPassed &= TestNotNull(TEXT("Untouched output control exists"), Untouched);
		bPassed &= TestNotNull(TEXT("Material root exists before output update"), RootBefore);
		bPassed &= TestTrue(TEXT("Output producers are distinct"), OldProducer != NewProducer);
		if (OldProducer && NewProducer && Untouched && RootBefore && OldProducer != NewProducer)
		{
			const FIntPoint UntouchedDisplaced =
				MatBP2FPAutoLayoutTests::GetPosition(Untouched) + FIntPoint(-4096, -2048);
			const FIntPoint OldProducerDisplaced =
				MatBP2FPAutoLayoutTests::GetPosition(OldProducer) + FIntPoint(-6144, 3072);
			const FIntPoint NewProducerDisplaced =
				MatBP2FPAutoLayoutTests::GetPosition(NewProducer) + FIntPoint(6144, 3072);
			const FIntPoint RootDisplaced(
				RootBefore->NodePosX + 8192,
				RootBefore->NodePosY - 4096);
			MatBP2FPAutoLayoutTests::SetPosition(Untouched, UntouchedDisplaced);
			MatBP2FPAutoLayoutTests::SetPosition(OldProducer, OldProducerDisplaced);
			MatBP2FPAutoLayoutTests::SetPosition(NewProducer, NewProducerDisplaced);
			Material->EditorX = RootDisplaced.X;
			Material->EditorY = RootDisplaced.Y;
			RootBefore->NodePosX = RootDisplaced.X;
			RootBefore->NodePosY = RootDisplaced.Y;
			TSet<UMaterialExpression*> MovableExpressions;
			MovableExpressions.Add(OldProducer);
			MovableExpressions.Add(NewProducer);
			const TArray<MatBP2FPAutoLayoutTests::FExpressionPositionSnapshot> PinnedSnapshots =
				MatBP2FPAutoLayoutTests::SnapshotPinnedExpressions(Material, MovableExpressions);

			const FMatBPImporter::FUpdateResult UpdateResult =
				FMatBPImporter::UpdateMaterialDetailed(Material, AfterSource);
				bPassed &= TestTrue(TEXT("Output-only update succeeds"), UpdateResult.bSuccess);
				bPassed &= TestTrue(TEXT("Output-only update uses the incremental patcher"),
					UpdateResult.bUsedIncrementalPatch);
				bPassed &= TestEqual(TEXT("Output-only update has no structural changes"),
					UpdateResult.NumStructuralChanges, 0);
				bPassed &= TestEqual(TEXT("Output-only update reports one semantic change"),
					UpdateResult.NumChanges, 1);
				bPassed &= TestEqual(TEXT("Output-only update reports one output change"),
					UpdateResult.NumPropertyChanges, 1);
				bPassed &= TestEqual(TEXT("Output-only update has no patch failures"),
					UpdateResult.NumFailed, 0);

			if (UpdateResult.bSuccess && UpdateResult.bUsedIncrementalPatch)
			{
				const TArray<UMaterialExpression*> AfterExpressions =
					MatBP2FPAutoLayoutTests::GetLayoutExpressions(Material);
				bPassed &= TestTrue(TEXT("Output-only update preserves the old producer object"),
					AfterExpressions.Contains(OldProducer));
				bPassed &= TestTrue(TEXT("Output-only update preserves the new producer object"),
					AfterExpressions.Contains(NewProducer));
				bPassed &= MatBP2FPAutoLayoutTests::VerifyPinnedExpressions(
					*this, TEXT("Output-only"), AfterExpressions, PinnedSnapshots);
				if (AfterExpressions.Contains(OldProducer))
				{
					bPassed &= TestNotEqual(TEXT("Output-only update lays out the old producer"),
						MatBP2FPAutoLayoutTests::GetPosition(OldProducer), OldProducerDisplaced);
				}
				if (AfterExpressions.Contains(NewProducer))
				{
					bPassed &= TestNotEqual(TEXT("Output-only update lays out the new producer"),
						MatBP2FPAutoLayoutTests::GetPosition(NewProducer), NewProducerDisplaced);
				}
				UMaterialGraphNode_Root* RootAfter =
					Material->MaterialGraph ? Material->MaterialGraph->RootNode : nullptr;
				bPassed &= TestNotNull(TEXT("Material root exists after output update"), RootAfter);
				if (RootAfter)
				{
					bPassed &= TestNotEqual(TEXT("Output-only update lays out the root"),
						FIntPoint(RootAfter->NodePosX, RootAfter->NodePosY), RootDisplaced);
				}
				const FExpressionInput* BaseColor =
					MatBP2FPCompat::GetMaterialInput(Material, TEXT("base-color"));
				bPassed &= TestTrue(TEXT("Base Color now uses the new producer"),
					BaseColor && BaseColor->Expression == NewProducer);
			}
		}
	}
	MatBP2FPAutoLayoutTests::ReleaseTransientMaterial(Material);
	return bPassed;
}

#endif // WITH_DEV_AUTOMATION_TESTS
