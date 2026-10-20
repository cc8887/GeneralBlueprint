// Copyright (c) 2026 cc8887. All Rights Reserved.

#include "CoreMinimal.h"
#include "Interfaces/IPluginManager.h"
#include "Materials/Material.h"
#include "MatBPExporter.h"
#include "MatBPImporter.h"
#include "MatLangDiffer.h"
#include "MatLangParser.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Runtime/Launch/Resources/Version.h"

#include <limits>

#if WITH_DEV_AUTOMATION_TESTS

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 5)
#define MBP_DIFF_TEST_FLAGS (EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
#else
#define MBP_DIFF_TEST_FLAGS (EAutomationTestFlags::ApplicationContextMask | EAutomationTestFlags::ProductFilter)
#endif

#define MBP_DIFF_TEST(Name) \
	IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMatBPDiff##Name, "MatBP2FP.Compatibility.Diff." #Name, MBP_DIFF_TEST_FLAGS)

namespace MatBP2FPDifferTests
{
	static bool LoadFixture(const TCHAR* FileName, FString& OutSource)
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("GeneralBlueprint"));
		if (!Plugin.IsValid())
		{
			return false;
		}
		return FFileHelper::LoadFileToString(OutSource,
			*FPaths::Combine(Plugin->GetBaseDir(), TEXT("Tests"), TEXT("Fixtures"), FileName));
	}

	static TSharedPtr<FMaterialGraphAST> ParseFixture(const TCHAR* FileName)
	{
		FString Source;
		if (!LoadFixture(FileName, Source))
		{
			return nullptr;
		}
		TArray<FMatLangParseError> Errors;
		return FMatLangParser::Parse(Source, Errors);
	}

	static void ApplyIdRemap(FMaterialGraphAST& Graph, const TMap<FString, FString>& Remap)
	{
		for (const TSharedPtr<FMatExpressionAST>& Expression : Graph.Expressions)
		{
			if (!Expression.IsValid())
			{
				continue;
			}
			for (FMatLangInput& Input : Expression->Inputs)
			{
				if (Input.IsConnected())
				{
					if (const FString* NewTarget = Remap.Find(Input.Connection->TargetId))
					{
						Input.Connection->TargetId = *NewTarget;
					}
				}
			}
		}
		for (auto& Pair : Graph.Outputs.Slots)
		{
			if (Pair.Value.IsConnected())
			{
				if (const FString* NewTarget = Remap.Find(Pair.Value.Connection->TargetId))
				{
					Pair.Value.Connection->TargetId = *NewTarget;
				}
			}
		}
		for (const TSharedPtr<FMatExpressionAST>& Expression : Graph.Expressions)
		{
			if (Expression.IsValid())
			{
				if (const FString* NewId = Remap.Find(Expression->Id))
				{
					Expression->Id = *NewId;
				}
			}
		}
	}

	static void RemapAllIds(FMaterialGraphAST& Graph, int32 FirstNumber = 101)
	{
		TMap<FString, FString> Remap;
		for (int32 Index = 0; Index < Graph.Expressions.Num(); ++Index)
		{
			if (Graph.Expressions[Index].IsValid())
			{
				Remap.Add(Graph.Expressions[Index]->Id,
					FString::Printf(TEXT("$ren%d"), FirstNumber + Index));
			}
		}

		ApplyIdRemap(Graph, Remap);
	}

	static void ReverseExpressions(FMaterialGraphAST& Graph)
	{
		for (int32 Left = 0, Right = Graph.Expressions.Num() - 1; Left < Right; ++Left, --Right)
		{
			Graph.Expressions.Swap(Left, Right);
		}
	}

	static bool SetInputConnection(
		const TSharedPtr<FMatExpressionAST>& Expression,
		const FString& InputName,
		const FString& TargetId)
	{
		if (!Expression.IsValid())
		{
			return false;
		}
		for (FMatLangInput& Input : Expression->Inputs)
		{
			if (Input.Name == InputName)
			{
				Input.Connection = FMatLangConnection(TargetId, 0);
				Input.LiteralValue.Reset();
				return true;
			}
		}
		return false;
	}

	static int32 CountOperation(const FMatLangDiffResult& Result, EMatLangDiffOp Operation)
	{
		int32 Count = 0;
		for (const FMatLangDiffEntry& Entry : Result.Entries)
		{
			if (Entry.Op == Operation)
			{
				++Count;
			}
		}
		return Count;
	}

	static UMaterial* CreateTransientMaterial()
	{
		UMaterial* Material = NewObject<UMaterial>(GetTransientPackage(), NAME_None, RF_Transient);
		Material->AddToRoot();
		return Material;
	}

	static TSharedPtr<FMaterialGraphAST> MakeConstantPairGraph(
		const FString& FirstId,
		const FString& SecondId,
		const FString& FirstValue,
		const FString& SecondValue)
	{
		TSharedPtr<FMaterialGraphAST> Graph = MakeShared<FMaterialGraphAST>();
		Graph->Name = TEXT("M_SymmetricConstants");
		auto AddConstant = [&Graph](const FString& Id, const FString& Value)
		{
			TSharedPtr<FMatExpressionAST> Expression = MakeShared<FMatExpressionAST>();
			Expression->ExprType = TEXT("constant");
			Expression->Id = Id;
			Expression->Properties.Add(TEXT("value"), Value);
			Graph->Expressions.Add(Expression);
		};
		AddConstant(FirstId, FirstValue);
		AddConstant(SecondId, SecondValue);
		return Graph;
	}

	static TSharedPtr<FMaterialGraphAST> MakeLiteralGraph(const FString& Id, const FString& Value)
	{
		TSharedPtr<FMaterialGraphAST> Graph = MakeShared<FMaterialGraphAST>();
		Graph->Name = TEXT("M_Literal");
		TSharedPtr<FMatExpressionAST> Expression = MakeShared<FMatExpressionAST>();
		Expression->ExprType = TEXT("multiply");
		Expression->Id = Id;
		Expression->AddInput(TEXT("a"), Value);
		Graph->Expressions.Add(Expression);

		FMatLangInput Output;
		Output.Name = TEXT("base-color");
		Output.Connection = FMatLangConnection(Id, 0);
		Graph->Outputs.Slots.Add(Output.Name, Output);
		return Graph;
	}
}

MBP_DIFF_TEST(AllIdsRenamedAndOrderReversed_IsEmpty)
bool FMatBPDiffAllIdsRenamedAndOrderReversed_IsEmpty::RunTest(const FString& Parameters)
{
	TSharedPtr<FMaterialGraphAST> OldGraph = MatBP2FPDifferTests::ParseFixture(TEXT("dag_shared_inputs.matlang"));
	TSharedPtr<FMaterialGraphAST> NewGraph = MatBP2FPDifferTests::ParseFixture(TEXT("dag_shared_inputs.matlang"));
	if (!TestTrue(TEXT("DAG fixtures parse"), OldGraph.IsValid() && NewGraph.IsValid()))
	{
		return false;
	}
	MatBP2FPDifferTests::RemapAllIds(*NewGraph);
	MatBP2FPDifferTests::ReverseExpressions(*NewGraph);
	const FString OldBefore = OldGraph->ToString();
	const FString NewBefore = NewGraph->ToString();

	const FMatLangDiffResult Result = FMatLangDiffer::Diff(OldGraph, NewGraph);
	TestTrue(TEXT("Renaming and reordering IDs is semantically empty"), Result.IsEmpty());
	TestEqual(TEXT("Every expression is matched"), Result.MatchResult.ExpressionMatches.Num(), 8);
	TestEqual(TEXT("Forward match index is complete"), Result.MatchResult.OldToNewIds.Num(), 8);
	TestEqual(TEXT("Reverse match index is complete"), Result.MatchResult.NewToOldIds.Num(), 8);
	TestEqual(TEXT("No old expression is unmatched"), Result.MatchResult.UnmatchedOldExprIds.Num(), 0);
	TestEqual(TEXT("No new expression is unmatched"), Result.MatchResult.UnmatchedNewExprIds.Num(), 0);
	TestEqual(TEXT("Diff does not mutate old AST"), OldGraph->ToString(), OldBefore);
	TestEqual(TEXT("Diff does not mutate new AST"), NewGraph->ToString(), NewBefore);
	return true;
}

MBP_DIFF_TEST(NullAST_IsStructuralInEitherDirection)
bool FMatBPDiffNullAST_IsStructuralInEitherDirection::RunTest(const FString& Parameters)
{
	TSharedPtr<FMaterialGraphAST> Graph = MatBP2FPDifferTests::ParseFixture(TEXT("dag_shared_inputs.matlang"));
	if (!TestTrue(TEXT("DAG fixture parses"), Graph.IsValid()))
	{
		return false;
	}
	const TSharedPtr<FMaterialGraphAST> NullGraph;
	const FMatLangDiffResult Added = FMatLangDiffer::Diff(NullGraph, Graph);
	const FMatLangDiffResult Removed = FMatLangDiffer::Diff(Graph, NullGraph);
	const FMatLangDiffResult BothNull = FMatLangDiffer::Diff(NullGraph, NullGraph);
	TestEqual(TEXT("Adding an entire graph is structural"), Added.NumStructural, 1);
	TestEqual(TEXT("Removing an entire graph is structural"), Removed.NumStructural, 1);
	TestTrue(TEXT("Two absent graphs are equivalent"), BothNull.IsEmpty());
	return true;
}

MBP_DIFF_TEST(MaterialFunctionSignature_IsComparedSemantically)
bool FMatBPDiffMaterialFunctionSignature_IsComparedSemantically::RunTest(const FString& Parameters)
{
	auto MakeFunctionGraph = []()
	{
		TSharedPtr<FMaterialGraphAST> Graph = MakeShared<FMaterialGraphAST>();
		Graph->Kind = EMatLangGraphKind::MaterialFunction;
		Graph->Name = TEXT("MF_Signature");
		FMatParameterDef FirstInput;
		FirstInput.Name = TEXT("First");
		FirstInput.SortPriority = 0;
		FMatParameterDef SecondInput;
		SecondInput.Name = TEXT("Second");
		SecondInput.SortPriority = 1;
		FMatParameterDef Output;
		Output.Name = TEXT("Result");
		Output.SortPriority = 0;
		Graph->FunctionInputs.Add(FirstInput);
		Graph->FunctionInputs.Add(SecondInput);
		Graph->FunctionOutputs.Add(Output);
		return Graph;
	};

	TSharedPtr<FMaterialGraphAST> OldGraph = MakeFunctionGraph();
	TSharedPtr<FMaterialGraphAST> ReorderedGraph = MakeFunctionGraph();
	ReorderedGraph->FunctionInputs.Swap(0, 1);
	TestTrue(TEXT("Signature array order alone is not semantic"),
		FMatLangDiffer::Diff(OldGraph, ReorderedGraph).IsEmpty());

	TSharedPtr<FMaterialGraphAST> PriorityGraph = MakeFunctionGraph();
	PriorityGraph->FunctionInputs[1].SortPriority = 7;
	const FMatLangDiffResult PriorityDiff = FMatLangDiffer::Diff(OldGraph, PriorityGraph);
	TestEqual(TEXT("Input signature edit creates one change"), PriorityDiff.TotalChanges(), 1);
	TestEqual(TEXT("Function signature edits require rebuild"), PriorityDiff.NumStructural, 1);
	if (PriorityDiff.Entries.Num() == 1)
	{
		TestEqual(TEXT("Input signature key is reported"),
			PriorityDiff.Entries[0].Key, FString(TEXT("function-inputs")));
	}

	TSharedPtr<FMaterialGraphAST> OutputGraph = MakeFunctionGraph();
	OutputGraph->FunctionOutputs[0].Name = TEXT("RenamedResult");
	const FMatLangDiffResult OutputDiff = FMatLangDiffer::Diff(OldGraph, OutputGraph);
	TestEqual(TEXT("Output rename creates one structural change"), OutputDiff.NumStructural, 1);
	return true;
}

MBP_DIFF_TEST(RenamedPropertyChange_IsLocal)
bool FMatBPDiffRenamedPropertyChange_IsLocal::RunTest(const FString& Parameters)
{
	TSharedPtr<FMaterialGraphAST> OldGraph = MatBP2FPDifferTests::ParseFixture(TEXT("dag_shared_inputs.matlang"));
	TSharedPtr<FMaterialGraphAST> NewGraph = MatBP2FPDifferTests::ParseFixture(TEXT("dag_shared_inputs.matlang"));
	if (!TestTrue(TEXT("DAG fixtures parse"), OldGraph.IsValid() && NewGraph.IsValid()))
	{
		return false;
	}
	NewGraph->FindExpression(TEXT("$sparam2"))->Properties.Add(TEXT("default"), TEXT("0.25"));
	MatBP2FPDifferTests::RemapAllIds(*NewGraph, 201);

	const FMatLangDiffResult Result = FMatLangDiffer::Diff(OldGraph, NewGraph);
	TestEqual(TEXT("Property edit has no structural changes"), Result.NumStructural, 0);
	TestEqual(TEXT("Exactly one property change is reported"), Result.NumProperty, 1);
	TestEqual(TEXT("Exactly one diff entry is reported"), Result.TotalChanges(), 1);
	if (Result.Entries.Num() == 1)
	{
		const FMatLangDiffEntry& Entry = Result.Entries[0];
		TestEqual(TEXT("Changed property key"), Entry.Key, FString(TEXT("default")));
		TestEqual(TEXT("Entry uses old current ID"), Entry.OldExprId, FString(TEXT("$sparam2")));
		TestTrue(TEXT("Entry carries a distinct new ID"), !Entry.NewExprId.IsEmpty() && Entry.NewExprId != Entry.OldExprId);
	}
	TestTrue(TEXT("Unique property edit remains patchable"), Result.CanApplyIncrementally());
	return true;
}

MBP_DIFF_TEST(NumericSpelling_IsEquivalent)
bool FMatBPDiffNumericSpelling_IsEquivalent::RunTest(const FString& Parameters)
{
	TSharedPtr<FMaterialGraphAST> OldGraph = MatBP2FPDifferTests::ParseFixture(TEXT("dag_shared_inputs.matlang"));
	TSharedPtr<FMaterialGraphAST> NewGraph = MatBP2FPDifferTests::ParseFixture(TEXT("dag_shared_inputs.matlang"));
	if (!TestTrue(TEXT("DAG fixtures parse"), OldGraph.IsValid() && NewGraph.IsValid()))
	{
		return false;
	}
	NewGraph->FindExpression(TEXT("$vparam1"))->Properties.Add(
		TEXT("default"), TEXT("(0.120 0.3500 8e-1 1)"));

	const FMatLangDiffResult Result = FMatLangDiffer::Diff(OldGraph, NewGraph);
	TestTrue(TEXT("Equivalent numeric spellings do not create a diff"), Result.IsEmpty());
	return true;
}

MBP_DIFF_TEST(PropertyRemoval_RequiresFullRebuild)
bool FMatBPDiffPropertyRemoval_RequiresFullRebuild::RunTest(const FString& Parameters)
{
	TSharedPtr<FMaterialGraphAST> OldGraph = MatBP2FPDifferTests::ParseFixture(TEXT("dag_shared_inputs.matlang"));
	TSharedPtr<FMaterialGraphAST> NewGraph = MatBP2FPDifferTests::ParseFixture(TEXT("dag_shared_inputs.matlang"));
	if (!TestTrue(TEXT("DAG fixtures parse"), OldGraph.IsValid() && NewGraph.IsValid()))
	{
		return false;
	}
	NewGraph->FindExpression(TEXT("$sparam2"))->Properties.Remove(TEXT("group"));

	const FMatLangDiffResult Result = FMatLangDiffer::Diff(OldGraph, NewGraph);
	TestEqual(TEXT("One property removal is reported"), Result.TotalChanges(), 1);
	TestEqual(TEXT("The operation retains property severity"), Result.NumProperty, 1);
	TestEqual(TEXT("The graph shape remains unchanged"), Result.NumStructural, 0);
	TestEqual(TEXT("The removal operation is identified"),
		MatBP2FPDifferTests::CountOperation(Result, EMatLangDiffOp::ExprPropertyRemoved), 1);
	TestTrue(TEXT("Native defaults make property removal unsupported in-place"),
		Result.HasUnsupportedIncrementalChanges());
	TestFalse(TEXT("Property removal is rebuilt before mutating the material"),
		Result.CanApplyIncrementally());

	NewGraph->FindExpression(TEXT("$sparam2"))->Properties.Add(TEXT("group"), TEXT("\"Surface\""));
	OldGraph->ExtraProperties.Add(TEXT("custom-flag"), TEXT("true"));
	const FMatLangDiffResult MaterialRemoval = FMatLangDiffer::Diff(OldGraph, NewGraph);
	TestEqual(TEXT("Only the material extra-property removal remains"),
		MaterialRemoval.TotalChanges(), 1);
	TestTrue(TEXT("Material extra-property removal is also preflighted"),
		MaterialRemoval.HasUnsupportedIncrementalChanges());
	TestFalse(TEXT("Material extra-property removal cannot mutate in-place"),
		MaterialRemoval.CanApplyIncrementally());
	return true;
}

MBP_DIFF_TEST(LiteralNumericSpelling_IsEquivalentButRealChangeDiffs)
bool FMatBPDiffLiteralNumericSpelling_IsEquivalentButRealChangeDiffs::RunTest(const FString& Parameters)
{
	TSharedPtr<FMaterialGraphAST> OldGraph = MatBP2FPDifferTests::MakeLiteralGraph(TEXT("$old"), TEXT("1.0"));
	TSharedPtr<FMaterialGraphAST> NewGraph = MatBP2FPDifferTests::MakeLiteralGraph(TEXT("$new"), TEXT("1e0"));
	TestTrue(TEXT("Numeric literal spellings are semantically equal"),
		FMatLangDiffer::Diff(OldGraph, NewGraph).IsEmpty());
	TestTrue(TEXT("Scalar numeric values compare by value"),
		FMatLangMatcher::AreValuesEquivalent(TEXT("1"), TEXT("1.0")));
	TestFalse(TEXT("Quoted numeric text remains a string"),
		FMatLangMatcher::AreValuesEquivalent(TEXT("\"1\""), TEXT("\"1.0\"")));
	TestFalse(TEXT("Large adjacent integers remain distinct"),
		FMatLangMatcher::AreValuesEquivalent(TEXT("9007199254740992"), TEXT("9007199254740993")));

	NewGraph->Expressions[0]->Inputs[0].LiteralValue = FString(TEXT("1.01"));
	const FMatLangDiffResult Changed = FMatLangDiffer::Diff(OldGraph, NewGraph);
	TestEqual(TEXT("A real literal edit creates one change"), Changed.TotalChanges(), 1);
	TestEqual(TEXT("The literal edit is an input change"),
		MatBP2FPDifferTests::CountOperation(Changed, EMatLangDiffOp::ExprInputChanged), 1);
	TestEqual(TEXT("The literal edit is not structural"), Changed.NumStructural, 0);
	TestTrue(TEXT("The unambiguous literal edit remains patchable"), Changed.CanApplyIncrementally());
	return true;
}

MBP_DIFF_TEST(SameTypeIdsAndOrderSwapped_IsEmpty)
bool FMatBPDiffSameTypeIdsAndOrderSwapped_IsEmpty::RunTest(const FString& Parameters)
{
	TSharedPtr<FMaterialGraphAST> OldGraph = MatBP2FPDifferTests::ParseFixture(TEXT("dag_shared_inputs.matlang"));
	TSharedPtr<FMaterialGraphAST> NewGraph = MatBP2FPDifferTests::ParseFixture(TEXT("dag_shared_inputs.matlang"));
	if (!TestTrue(TEXT("DAG fixtures parse"), OldGraph.IsValid() && NewGraph.IsValid()))
	{
		return false;
	}
	TMap<FString, FString> SwapIds;
	SwapIds.Add(TEXT("$mul4"), TEXT("$mul6"));
	SwapIds.Add(TEXT("$mul6"), TEXT("$mul4"));
	MatBP2FPDifferTests::ApplyIdRemap(*NewGraph, SwapIds);
	int32 FirstMultiplyIndex = INDEX_NONE;
	int32 SecondMultiplyIndex = INDEX_NONE;
	for (int32 Index = 0; Index < NewGraph->Expressions.Num(); ++Index)
	{
		if (NewGraph->Expressions[Index]->Id == TEXT("$mul6"))
		{
			FirstMultiplyIndex = Index;
		}
		else if (NewGraph->Expressions[Index]->Id == TEXT("$mul4"))
		{
			SecondMultiplyIndex = Index;
		}
	}
	if (FirstMultiplyIndex != INDEX_NONE && SecondMultiplyIndex != INDEX_NONE)
	{
		NewGraph->Expressions.Swap(FirstMultiplyIndex, SecondMultiplyIndex);
	}

	const FMatLangDiffResult Result = FMatLangDiffer::Diff(OldGraph, NewGraph);
	TestTrue(TEXT("Swapping same-type IDs and order is semantically empty"), Result.IsEmpty());
	TestEqual(TEXT("All expressions remain matched"), Result.MatchResult.ExpressionMatches.Num(), 8);
	TestEqual(TEXT("First multiply follows its content"),
		Result.MatchResult.FindNewId(TEXT("$mul4")), FString(TEXT("$mul6")));
	TestEqual(TEXT("Second multiply follows its content"),
		Result.MatchResult.FindNewId(TEXT("$mul6")), FString(TEXT("$mul4")));
	TestFalse(TEXT("Distinct multiply nodes are not ambiguous"), Result.MatchResult.IsAmbiguous());
	return true;
}

MBP_DIFF_TEST(SymmetricDuplicates_OnlyBlockAmbiguousChanges)
bool FMatBPDiffSymmetricDuplicates_OnlyBlockAmbiguousChanges::RunTest(const FString& Parameters)
{
	TSharedPtr<FMaterialGraphAST> OldGraph = MatBP2FPDifferTests::MakeConstantPairGraph(
		TEXT("$a"), TEXT("$b"), TEXT("0.5"), TEXT("0.5"));
	TSharedPtr<FMaterialGraphAST> EquivalentGraph = MatBP2FPDifferTests::MakeConstantPairGraph(
		TEXT("$x"), TEXT("$y"), TEXT("0.5"), TEXT("0.5"));
	const FMatLangDiffResult Equivalent = FMatLangDiffer::Diff(OldGraph, EquivalentGraph);
	TestTrue(TEXT("Equivalent symmetric nodes have no changes"), Equivalent.IsEmpty());
	TestEqual(TEXT("Both symmetric nodes are matched"), Equivalent.MatchResult.ExpressionMatches.Num(), 2);
	TestTrue(TEXT("Symmetric correspondence is marked ambiguous"), Equivalent.MatchResult.IsAmbiguous());
	TestFalse(TEXT("Unchanged ambiguity is harmless"), Equivalent.bHasAmbiguousChanges);
	TestTrue(TEXT("An empty ambiguous diff needs no rebuild"), Equivalent.CanApplyIncrementally());

	TSharedPtr<FMaterialGraphAST> ChangedGraph = MatBP2FPDifferTests::MakeConstantPairGraph(
		TEXT("$x"), TEXT("$y"), TEXT("0.5"), TEXT("0.6"));
	const FMatLangDiffResult Changed = FMatLangDiffer::Diff(OldGraph, ChangedGraph);
	TestEqual(TEXT("Only the real duplicate property edit is reported"), Changed.TotalChanges(), 1);
	TestEqual(TEXT("Both changed graph nodes remain matched"), Changed.MatchResult.ExpressionMatches.Num(), 2);
	TestEqual(TEXT("No duplicate node is structurally replaced"), Changed.NumStructural, 0);
	TestTrue(TEXT("The changed correspondence is ambiguous"), Changed.bHasAmbiguousChanges);
	TestFalse(TEXT("Ambiguous changes require a full rebuild"), Changed.CanApplyIncrementally());
	if (Changed.Entries.Num() == 1)
	{
		TestEqual(TEXT("The changed new duplicate is identified"),
			Changed.Entries[0].NewExprId, FString(TEXT("$y")));
		const FString ExpectedEntry = Changed.Entries[0].ToString();
		for (int32 Run = 0; Run < 10; ++Run)
		{
			const FMatLangDiffResult Repeated = FMatLangDiffer::Diff(OldGraph, ChangedGraph);
			TestEqual(TEXT("Ambiguous edit script remains deterministic"),
				Repeated.Entries[0].ToString(), ExpectedEntry);
		}
	}
	return true;
}

MBP_DIFF_TEST(AmbiguousTypeBucket_BlocksOtherProximityChange)
bool FMatBPDiffAmbiguousTypeBucket_BlocksOtherProximityChange::RunTest(const FString& Parameters)
{
	auto MakeGraph = [](const TCHAR* Prefix, const FString& ChangedValue)
	{
		TSharedPtr<FMaterialGraphAST> Graph = MakeShared<FMaterialGraphAST>();
		Graph->Name = TEXT("M_AmbiguousTypeBucket");
		auto AddConstant = [&Graph, Prefix](int32 Index, const FString& Value)
		{
			TSharedPtr<FMatExpressionAST> Expression = MakeShared<FMatExpressionAST>();
			Expression->ExprType = TEXT("constant");
			Expression->Id = FString::Printf(TEXT("$%s%d"), Prefix, Index);
			Expression->Properties.Add(TEXT("value"), Value);
			Graph->Expressions.Add(Expression);
		};
		AddConstant(1, TEXT("9"));
		AddConstant(2, TEXT("9"));
		AddConstant(3, ChangedValue);
		return Graph;
	};

	const TSharedPtr<FMaterialGraphAST> OldGraph = MakeGraph(TEXT("old"), TEXT("0.701234"));
	const TSharedPtr<FMaterialGraphAST> NewGraph = MakeGraph(TEXT("new"), TEXT("0.701235"));
	const FMatLangDiffResult Result = FMatLangDiffer::Diff(OldGraph, NewGraph);

	TestEqual(TEXT("Only the distinct constant changed"), Result.NumProperty, 1);
	TestEqual(TEXT("No structural churn"), Result.NumStructural, 0);
	const FMatLangExpressionMatch* ChangedMatch = Result.MatchResult.FindByOldId(TEXT("$old3"));
	TestNotNull(TEXT("Changed constant is matched"), ChangedMatch);
	if (ChangedMatch)
	{
		TestEqual(TEXT("Changed constant maps by proximity"), ChangedMatch->NewExprId, FString(TEXT("$new3")));
		TestTrue(TEXT("Ambiguity propagates across its type bucket"), ChangedMatch->bAmbiguous);
	}
	TestTrue(TEXT("Ambiguous type bucket blocks incremental application"), Result.bHasAmbiguousChanges);
	TestFalse(TEXT("Ambiguous type bucket is not incrementally patchable"), Result.CanApplyIncrementally());
	return true;
}

MBP_DIFF_TEST(EqualBuckets_CanLeaveDissimilarNodesUnmatched)
bool FMatBPDiffEqualBuckets_CanLeaveDissimilarNodesUnmatched::RunTest(const FString& Parameters)
{
	TSharedPtr<FMaterialGraphAST> OldGraph = MakeShared<FMaterialGraphAST>();
	TSharedPtr<FMaterialGraphAST> NewGraph = MakeShared<FMaterialGraphAST>();
	auto AddMultiply = [](FMaterialGraphAST& Graph, const FString& Id, const FString& Name,
		bool bAddLiteral, bool bFeedsBaseColor)
	{
		TSharedPtr<FMatExpressionAST> Expression = MakeShared<FMatExpressionAST>();
		Expression->ExprType = TEXT("multiply");
		Expression->Id = Id;
		Expression->Properties.Add(TEXT("name"), Name);
		if (bAddLiteral)
		{
			Expression->AddInput(TEXT("a"), TEXT("0"));
		}
		Graph.Expressions.Add(Expression);
		if (bFeedsBaseColor)
		{
			FMatLangInput Output;
			Output.Name = TEXT("base-color");
			Output.Connection = FMatLangConnection(Id, 0);
			Graph.Outputs.Slots.Add(Output.Name, Output);
		}
	};
	AddMultiply(*OldGraph, TEXT("$old-near"), TEXT("alpha"), true, true);
	AddMultiply(*OldGraph, TEXT("$old-only"), TEXT("left"), true, false);
	AddMultiply(*NewGraph, TEXT("$new-near"), TEXT("alphb"), true, true);
	AddMultiply(*NewGraph, TEXT("$new-only"), TEXT("right"), false, false);

	FMatLangMatchOptions Options;
	Options.MaximumDistance = 0.2f;
	const FMatLangMatchResult Result = FMatLangMatcher::Match(*OldGraph, *NewGraph, Options);
	TestEqual(TEXT("Only the close pair is matched"), Result.ExpressionMatches.Num(), 1);
	TestEqual(TEXT("The close old node matches by proximity"),
		Result.FindNewId(TEXT("$old-near")), FString(TEXT("$new-near")));
	TestEqual(TEXT("The dissimilar old node remains unmatched"), Result.UnmatchedOldExprIds.Num(), 1);
	TestEqual(TEXT("The dissimilar new node remains unmatched"), Result.UnmatchedNewExprIds.Num(), 1);
	return true;
}

MBP_DIFF_TEST(InvalidMatchOptions_AreSanitized)
bool FMatBPDiffInvalidMatchOptions_AreSanitized::RunTest(const FString& Parameters)
{
	TSharedPtr<FMaterialGraphAST> OldGraph = MatBP2FPDifferTests::MakeConstantPairGraph(
		TEXT("$old-a"), TEXT("$old-b"), TEXT("0.5"), TEXT("0.75"));
	TSharedPtr<FMaterialGraphAST> NewGraph = MatBP2FPDifferTests::MakeConstantPairGraph(
		TEXT("$new-a"), TEXT("$new-b"), TEXT("0.51"), TEXT("0.76"));

	FMatLangMatchOptions Options;
	Options.MaximumDistance = std::numeric_limits<float>::quiet_NaN();
	Options.RefinementPasses = MAX_int32;
	const FMatLangMatchResult NaNResult = FMatLangMatcher::Match(*OldGraph, *NewGraph, Options);
	TestEqual(TEXT("NaN distance falls back to the default threshold"),
		NaNResult.ExpressionMatches.Num(), 2);

	Options.MaximumDistance = std::numeric_limits<float>::infinity();
	Options.RefinementPasses = 0;
	const FMatLangMatchResult InfinityResult = FMatLangMatcher::Match(*OldGraph, *NewGraph, Options);
	TestEqual(TEXT("Infinite distance cannot poison assignment"),
		InfinityResult.ExpressionMatches.Num(), 2);

	Options.MaximumDistance = -1.0f;
	const FMatLangMatchResult ClampedResult = FMatLangMatcher::Match(*OldGraph, *NewGraph, Options);
	TestEqual(TEXT("Negative distance clamps to exact-only matching"),
		ClampedResult.ExpressionMatches.Num(), 0);
	return true;
}

MBP_DIFF_TEST(RenamedRewireAndOutputChange_AreLocal)
bool FMatBPDiffRenamedRewireAndOutputChange_AreLocal::RunTest(const FString& Parameters)
{
	TSharedPtr<FMaterialGraphAST> OldGraph = MatBP2FPDifferTests::ParseFixture(TEXT("dag_shared_inputs.matlang"));
	TSharedPtr<FMaterialGraphAST> NewGraph = MatBP2FPDifferTests::ParseFixture(TEXT("dag_shared_inputs.matlang"));
	if (!TestTrue(TEXT("DAG fixtures parse"), OldGraph.IsValid() && NewGraph.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("Rewire source input"), MatBP2FPDifferTests::SetInputConnection(
		NewGraph->FindExpression(TEXT("$mul6")), TEXT("b"), TEXT("$const7")));
	FMatLangInput* BaseColor = NewGraph->Outputs.Slots.Find(TEXT("base-color"));
	if (BaseColor)
	{
		BaseColor->Connection = FMatLangConnection(TEXT("$mul6"), 0);
	}
	MatBP2FPDifferTests::RemapAllIds(*NewGraph, 301);

	const FMatLangDiffResult Result = FMatLangDiffer::Diff(OldGraph, NewGraph);
	TestEqual(TEXT("Rewire has no structural changes"), Result.NumStructural, 0);
	TestEqual(TEXT("One input change is reported"),
		MatBP2FPDifferTests::CountOperation(Result, EMatLangDiffOp::ExprInputChanged), 1);
	TestEqual(TEXT("One output change is reported"),
		MatBP2FPDifferTests::CountOperation(Result, EMatLangDiffOp::OutputChanged), 1);
	TestEqual(TEXT("Only the two real changes are reported"), Result.TotalChanges(), 2);
	TestEqual(TEXT("First multiply keeps its semantic identity"),
		Result.MatchResult.FindNewId(TEXT("$mul4")), FString(TEXT("$ren304")));
	TestEqual(TEXT("Second multiply keeps its semantic identity"),
		Result.MatchResult.FindNewId(TEXT("$mul6")), FString(TEXT("$ren306")));
	return true;
}

MBP_DIFF_TEST(InsertedNode_DoesNotChurnExistingNodes)
bool FMatBPDiffInsertedNode_DoesNotChurnExistingNodes::RunTest(const FString& Parameters)
{
	TSharedPtr<FMaterialGraphAST> OldGraph = MatBP2FPDifferTests::ParseFixture(TEXT("dag_shared_inputs.matlang"));
	TSharedPtr<FMaterialGraphAST> NewGraph = MatBP2FPDifferTests::ParseFixture(TEXT("dag_shared_inputs.matlang"));
	if (!TestTrue(TEXT("DAG fixtures parse"), OldGraph.IsValid() && NewGraph.IsValid()))
	{
		return false;
	}
	auto Inserted = MakeShared<FMatExpressionAST>();
	Inserted->ExprType = TEXT("constant");
	Inserted->Id = TEXT("$inserted");
	Inserted->Properties.Add(TEXT("value"), TEXT("0.123"));
	NewGraph->Expressions.Insert(Inserted, 0);
	MatBP2FPDifferTests::RemapAllIds(*NewGraph, 401);

	const FMatLangDiffResult Result = FMatLangDiffer::Diff(OldGraph, NewGraph);
	TestEqual(TEXT("Only one expression is added"),
		MatBP2FPDifferTests::CountOperation(Result, EMatLangDiffOp::ExpressionAdded), 1);
	TestEqual(TEXT("No expression is spuriously removed"),
		MatBP2FPDifferTests::CountOperation(Result, EMatLangDiffOp::ExpressionRemoved), 0);
	TestEqual(TEXT("Insertion creates one structural diff"), Result.NumStructural, 1);
	TestEqual(TEXT("Insertion creates no property churn"), Result.NumProperty, 0);
	return true;
}

MBP_DIFF_TEST(RenumberedPropertyUpdate_UsesIncrementalPatch)
bool FMatBPDiffRenumberedPropertyUpdate_UsesIncrementalPatch::RunTest(const FString& Parameters)
{
	FString BeforeSource;
	if (!TestTrue(TEXT("Load incremental before fixture"),
		MatBP2FPDifferTests::LoadFixture(TEXT("incremental_before.matlang"), BeforeSource)))
	{
		return false;
	}
	TSharedPtr<FMaterialGraphAST> Desired = MatBP2FPDifferTests::ParseFixture(TEXT("incremental_after.matlang"));
	if (!TestTrue(TEXT("Parse incremental after fixture"), Desired.IsValid()))
	{
		return false;
	}
	MatBP2FPDifferTests::RemapAllIds(*Desired, 501);
	MatBP2FPDifferTests::ReverseExpressions(*Desired);

	UMaterial* Material = MatBP2FPDifferTests::CreateTransientMaterial();
	const FMatBPImporter::FImportResult Initial = FMatBPImporter::UpdateMaterial(Material, BeforeSource);
	TestTrue(TEXT("Initial import succeeds"), Initial.bSuccess);
	const FMatBPImporter::FUpdateResult Update =
		FMatBPImporter::UpdateMaterialDetailed(Material, Desired->ToString());
	TestTrue(TEXT("Renumbered update succeeds"), Update.bSuccess);
	TestTrue(TEXT("Renumbered update uses incremental patch"), Update.bUsedIncrementalPatch);
	TestEqual(TEXT("Renumbered update has no structural changes"), Update.NumStructuralChanges, 0);
	TestEqual(TEXT("Renumbered update has no patch failures"), Update.NumFailed, 0);

	const TSharedPtr<FMaterialGraphAST> Exported = FMatBPExporter::ExportToAST(Material);
	TestTrue(TEXT("Updated material exports"), Exported.IsValid());
	if (Exported.IsValid())
	{
		TestTrue(TEXT("Updated material is semantically equal to desired graph"),
			FMatLangDiffer::Diff(Exported, Desired, false).IsEmpty());
	}
	Material->RemoveFromRoot();
	return true;
}

MBP_DIFF_TEST(PropertyRemoval_UsesFullRebuild)
bool FMatBPDiffPropertyRemoval_UsesFullRebuild::RunTest(const FString& Parameters)
{
	FString Source;
	if (!TestTrue(TEXT("Load DAG fixture"),
		MatBP2FPDifferTests::LoadFixture(TEXT("dag_shared_inputs.matlang"), Source)))
	{
		return false;
	}
	TSharedPtr<FMaterialGraphAST> Desired = MatBP2FPDifferTests::ParseFixture(TEXT("dag_shared_inputs.matlang"));
	if (!TestTrue(TEXT("Parse DAG fixture"), Desired.IsValid()))
	{
		return false;
	}
	Desired->FindExpression(TEXT("$sparam2"))->Properties.Remove(TEXT("group"));

	UMaterial* Material = MatBP2FPDifferTests::CreateTransientMaterial();
	const FMatBPImporter::FImportResult Initial = FMatBPImporter::UpdateMaterial(Material, Source);
	TestTrue(TEXT("Initial DAG import succeeds"), Initial.bSuccess);
	const FMatBPImporter::FUpdateResult Update =
		FMatBPImporter::UpdateMaterialDetailed(Material, Desired->ToString());
	TestTrue(TEXT("Property removal update succeeds"), Update.bSuccess);
	TestFalse(TEXT("Property removal uses a full rebuild"), Update.bUsedIncrementalPatch);

	const TSharedPtr<FMaterialGraphAST> Exported = FMatBPExporter::ExportToAST(Material);
	TestTrue(TEXT("Rebuilt material exports"), Exported.IsValid());
	if (Exported.IsValid())
	{
		TestTrue(TEXT("Rebuilt material matches the property-removal target"),
			FMatLangDiffer::Diff(Exported, Desired, false).IsEmpty());
	}
	const FMatBPImporter::FUpdateResult Repeated =
		FMatBPImporter::UpdateMaterialDetailed(Material, Desired->ToString());
	TestTrue(TEXT("Repeated update succeeds"), Repeated.bSuccess);
	TestEqual(TEXT("Repeated update has no changes"), Repeated.NumChanges, 0);
	Material->RemoveFromRoot();
	return true;
}

MBP_DIFF_TEST(RenumberedRewire_UsesIncrementalPatch)
bool FMatBPDiffRenumberedRewire_UsesIncrementalPatch::RunTest(const FString& Parameters)
{
	FString BeforeSource;
	if (!TestTrue(TEXT("Load DAG fixture"),
		MatBP2FPDifferTests::LoadFixture(TEXT("dag_shared_inputs.matlang"), BeforeSource)))
	{
		return false;
	}
	TSharedPtr<FMaterialGraphAST> Desired = MatBP2FPDifferTests::ParseFixture(TEXT("dag_shared_inputs.matlang"));
	if (!TestTrue(TEXT("Parse DAG fixture"), Desired.IsValid()))
	{
		return false;
	}
	TestTrue(TEXT("Rewire desired input"), MatBP2FPDifferTests::SetInputConnection(
		Desired->FindExpression(TEXT("$mul6")), TEXT("b"), TEXT("$const7")));
	FMatLangInput* BaseColor = Desired->Outputs.Slots.Find(TEXT("base-color"));
	if (BaseColor)
	{
		BaseColor->Connection = FMatLangConnection(TEXT("$mul6"), 0);
	}
	MatBP2FPDifferTests::RemapAllIds(*Desired, 601);

	UMaterial* Material = MatBP2FPDifferTests::CreateTransientMaterial();
	const FMatBPImporter::FImportResult Initial = FMatBPImporter::UpdateMaterial(Material, BeforeSource);
	TestTrue(TEXT("Initial DAG import succeeds"), Initial.bSuccess);
	const TSharedPtr<FMaterialGraphAST> BeforeUpdate = FMatBPExporter::ExportToAST(Material);
	if (TestTrue(TEXT("Initial DAG exports"), BeforeUpdate.IsValid()))
	{
		const FMatLangDiffResult BeforeDiff = FMatLangDiffer::Diff(BeforeUpdate, Desired, false);
		if (BeforeDiff.TotalChanges() != 2)
		{
			for (const FMatLangDiffEntry& Entry : BeforeDiff.Entries)
			{
				AddInfo(TEXT("Unexpected pre-update diff: ") + Entry.ToString());
			}
		}
		TestEqual(TEXT("Only the requested rewire and output change remain"), BeforeDiff.TotalChanges(), 2);
	}
	const FMatBPImporter::FUpdateResult Update =
		FMatBPImporter::UpdateMaterialDetailed(Material, Desired->ToString());
	TestTrue(TEXT("Renumbered rewire succeeds"), Update.bSuccess);
	TestTrue(TEXT("Renumbered rewire uses incremental patch"), Update.bUsedIncrementalPatch);
	TestEqual(TEXT("Renumbered rewire has no structural changes"), Update.NumStructuralChanges, 0);
	TestEqual(TEXT("Renumbered rewire has no patch failures"), Update.NumFailed, 0);

	const TSharedPtr<FMaterialGraphAST> Exported = FMatBPExporter::ExportToAST(Material);
	TestTrue(TEXT("Rewired material exports"), Exported.IsValid());
	if (Exported.IsValid())
	{
		const FMatLangDiffResult FinalDiff = FMatLangDiffer::Diff(Exported, Desired, false);
		if (!FinalDiff.IsEmpty())
		{
			for (const FMatLangDiffEntry& Entry : FinalDiff.Entries)
			{
				AddInfo(TEXT("Residual post-update diff: ") + Entry.ToString());
			}
		}
		TestTrue(TEXT("Rewired material is semantically equal to desired graph"), FinalDiff.IsEmpty());
	}
	Material->RemoveFromRoot();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
