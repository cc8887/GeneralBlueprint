// MatLangDiffer.cpp - Semantic AST diff engine for MatLang DSL
// Copyright (c) 2026 cc8887. All Rights Reserved.

#include "MatLangDiffer.h"

namespace MatLangDifferPrivate
{
	static TArray<FString> SortedInputNames(const FMatExpressionAST& Expression)
	{
		TArray<FString> Names;
		for (const FMatLangInput& Input : Expression.Inputs)
		{
			Names.AddUnique(Input.Name);
		}
		Names.Sort();
		return Names;
	}

	static TArray<FMatParameterDef> SortedFunctionParameters(const TArray<FMatParameterDef>& Parameters)
	{
		TArray<FMatParameterDef> Sorted = Parameters;
		Sorted.Sort([](const FMatParameterDef& A, const FMatParameterDef& B)
		{
			int32 Order = A.Name.Compare(B.Name);
			if (Order != 0) return Order < 0;
			Order = A.Type.Compare(B.Type);
			if (Order != 0) return Order < 0;
			Order = A.Group.Compare(B.Group);
			if (Order != 0) return Order < 0;
			if (A.SortPriority != B.SortPriority) return A.SortPriority < B.SortPriority;
			return A.DefaultValue.Compare(B.DefaultValue) < 0;
		});
		return Sorted;
	}

	static bool FunctionParametersEquivalent(
		const TArray<FMatParameterDef>& A,
		const TArray<FMatParameterDef>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		const TArray<FMatParameterDef> SortedA = SortedFunctionParameters(A);
		const TArray<FMatParameterDef> SortedB = SortedFunctionParameters(B);
		for (int32 Index = 0; Index < SortedA.Num(); ++Index)
		{
			const FMatParameterDef& Left = SortedA[Index];
			const FMatParameterDef& Right = SortedB[Index];
			if (Left.Name != Right.Name
				|| Left.Type != Right.Type
				|| Left.Group != Right.Group
				|| Left.SortPriority != Right.SortPriority
				|| !FMatLangMatcher::AreValuesEquivalent(Left.DefaultValue, Right.DefaultValue))
			{
				return false;
			}
		}
		return true;
	}

	static FString FunctionParametersToString(const TArray<FMatParameterDef>& Parameters)
	{
		const TArray<FMatParameterDef> Sorted = SortedFunctionParameters(Parameters);
		FString Result;
		for (const FMatParameterDef& Parameter : Sorted)
		{
			if (!Result.IsEmpty())
			{
				Result += TEXT(", ");
			}
			Result += FString::Printf(TEXT("%s@%d"), *Parameter.Name, Parameter.SortPriority);
		}
		return Result;
	}
}

FString FMatLangDiffEntry::ToString() const
{
	FString SeverityText;
	switch (Severity)
	{
		case EMatLangDiffSeverity::Cosmetic: SeverityText = TEXT("cosmetic"); break;
		case EMatLangDiffSeverity::Property: SeverityText = TEXT("property"); break;
		case EMatLangDiffSeverity::Structural: SeverityText = TEXT("STRUCTURAL"); break;
	}

	FString OperationText;
	switch (Op)
	{
		case EMatLangDiffOp::MaterialPropertyChanged: OperationText = TEXT("MatPropChanged"); break;
		case EMatLangDiffOp::ExpressionAdded: OperationText = TEXT("ExprAdded"); break;
		case EMatLangDiffOp::ExpressionRemoved: OperationText = TEXT("ExprRemoved"); break;
		case EMatLangDiffOp::ExpressionTypeChanged: OperationText = TEXT("ExprTypeChanged"); break;
		case EMatLangDiffOp::ExprPropertyChanged: OperationText = TEXT("ExprPropChanged"); break;
		case EMatLangDiffOp::ExprPropertyAdded: OperationText = TEXT("ExprPropAdded"); break;
		case EMatLangDiffOp::ExprPropertyRemoved: OperationText = TEXT("ExprPropRemoved"); break;
		case EMatLangDiffOp::ExprInputChanged: OperationText = TEXT("ExprInputChanged"); break;
		case EMatLangDiffOp::ExprInputAdded: OperationText = TEXT("ExprInputAdded"); break;
		case EMatLangDiffOp::ExprInputRemoved: OperationText = TEXT("ExprInputRemoved"); break;
		case EMatLangDiffOp::OutputChanged: OperationText = TEXT("OutputChanged"); break;
		case EMatLangDiffOp::OutputAdded: OperationText = TEXT("OutputAdded"); break;
		case EMatLangDiffOp::OutputRemoved: OperationText = TEXT("OutputRemoved"); break;
	}

	FString ExpressionText = ExprId;
	if (!OldExprId.IsEmpty() && !NewExprId.IsEmpty() && OldExprId != NewExprId)
	{
		ExpressionText = OldExprId + TEXT(" -> ") + NewExprId;
	}
	if (ExpressionText.IsEmpty())
	{
		return FString::Printf(TEXT("[%s] %s :%s  '%s' -> '%s'"),
			*SeverityText, *OperationText, *Key, *OldValue, *NewValue);
	}
	return FString::Printf(TEXT("[%s] %s %s :%s  '%s' -> '%s'"),
		*SeverityText, *OperationText, *ExpressionText, *Key, *OldValue, *NewValue);
}

FString FMatLangDiffResult::GetSummary() const
{
	return FString::Printf(TEXT("Diff: %d total (%d structural, %d property, %d cosmetic, %d matches%s)"),
		Entries.Num(), NumStructural, NumProperty, NumCosmetic,
		MatchResult.ExpressionMatches.Num(), bHasAmbiguousChanges ? TEXT(", ambiguous changes") : TEXT(""));
}

FMatLangDiffResult FMatLangDiffer::Diff(
	const TSharedPtr<FMaterialGraphAST>& OldAST,
	const TSharedPtr<FMaterialGraphAST>& NewAST)
{
	return Diff(OldAST, NewAST, true);
}

FMatLangDiffResult FMatLangDiffer::Diff(
	const TSharedPtr<FMaterialGraphAST>& OldAST,
	const TSharedPtr<FMaterialGraphAST>& NewAST,
	bool bInCompareEditorMetadata)
{
	FMatLangDiffer Differ;
	Differ.bCompareEditorMetadata = bInCompareEditorMetadata;
	if (!OldAST || !NewAST)
	{
		if (!OldAST && NewAST)
		{
			Differ.AddEntry(EMatLangDiffOp::MaterialPropertyChanged, EMatLangDiffSeverity::Structural,
				FString(), FString(), TEXT("material"), TEXT("<none>"), NewAST->Name);
		}
		else if (OldAST && !NewAST)
		{
			Differ.AddEntry(EMatLangDiffOp::MaterialPropertyChanged, EMatLangDiffSeverity::Structural,
				FString(), FString(), TEXT("material"), OldAST->Name, TEXT("<none>"));
		}
		return Differ.Result;
	}

	Differ.Result.MatchResult = FMatLangMatcher::Match(*OldAST, *NewAST);
	Differ.DiffMaterialProperties(*OldAST, *NewAST);
	Differ.DiffExpressions(*OldAST, *NewAST);
	Differ.DiffOutputs(OldAST->Outputs, NewAST->Outputs);
	return Differ.Result;
}

void FMatLangDiffer::DiffMaterialProperties(const FMaterialGraphAST& Old, const FMaterialGraphAST& New)
{
	if (Old.Kind != New.Kind)
	{
		AddEntry(EMatLangDiffOp::MaterialPropertyChanged, EMatLangDiffSeverity::Structural,
			FString(), FString(), TEXT("graph-kind"),
			Old.Kind == EMatLangGraphKind::Material ? TEXT("material") : TEXT("material-function"),
			New.Kind == EMatLangGraphKind::Material ? TEXT("material") : TEXT("material-function"));
	}
	if (Old.Kind == EMatLangGraphKind::MaterialFunction
		&& New.Kind == EMatLangGraphKind::MaterialFunction)
	{
		if (!MatLangDifferPrivate::FunctionParametersEquivalent(Old.FunctionInputs, New.FunctionInputs))
		{
			AddEntry(EMatLangDiffOp::MaterialPropertyChanged, EMatLangDiffSeverity::Structural,
				FString(), FString(), TEXT("function-inputs"),
				MatLangDifferPrivate::FunctionParametersToString(Old.FunctionInputs),
				MatLangDifferPrivate::FunctionParametersToString(New.FunctionInputs));
		}
		if (!MatLangDifferPrivate::FunctionParametersEquivalent(Old.FunctionOutputs, New.FunctionOutputs))
		{
			AddEntry(EMatLangDiffOp::MaterialPropertyChanged, EMatLangDiffSeverity::Structural,
				FString(), FString(), TEXT("function-outputs"),
				MatLangDifferPrivate::FunctionParametersToString(Old.FunctionOutputs),
				MatLangDifferPrivate::FunctionParametersToString(New.FunctionOutputs));
		}
	}
	if (Old.Domain != New.Domain)
	{
		AddEntry(EMatLangDiffOp::MaterialPropertyChanged, EMatLangDiffSeverity::Structural,
			FString(), FString(), TEXT("domain"),
			MatLangEnums::DomainToString(Old.Domain), MatLangEnums::DomainToString(New.Domain));
	}
	if (Old.BlendMode != New.BlendMode)
	{
		AddEntry(EMatLangDiffOp::MaterialPropertyChanged, EMatLangDiffSeverity::Property,
			FString(), FString(), TEXT("blend-mode"),
			MatLangEnums::BlendModeToString(Old.BlendMode), MatLangEnums::BlendModeToString(New.BlendMode));
	}
	if (Old.ShadingModel != New.ShadingModel)
	{
		AddEntry(EMatLangDiffOp::MaterialPropertyChanged, EMatLangDiffSeverity::Property,
			FString(), FString(), TEXT("shading-model"),
			MatLangEnums::ShadingModelToString(Old.ShadingModel), MatLangEnums::ShadingModelToString(New.ShadingModel));
	}
	if (Old.bTwoSided != New.bTwoSided)
	{
		AddEntry(EMatLangDiffOp::MaterialPropertyChanged, EMatLangDiffSeverity::Property,
			FString(), FString(), TEXT("two-sided"),
			Old.bTwoSided ? TEXT("true") : TEXT("false"), New.bTwoSided ? TEXT("true") : TEXT("false"));
	}
	if (Old.bIsMasked != New.bIsMasked)
	{
		AddEntry(EMatLangDiffOp::MaterialPropertyChanged, EMatLangDiffSeverity::Property,
			FString(), FString(), TEXT("is-masked"),
			Old.bIsMasked ? TEXT("true") : TEXT("false"), New.bIsMasked ? TEXT("true") : TEXT("false"));
	}
	if (FMath::Abs(Old.OpacityMaskClipValue - New.OpacityMaskClipValue) > KINDA_SMALL_NUMBER)
	{
		AddEntry(EMatLangDiffOp::MaterialPropertyChanged, EMatLangDiffSeverity::Property,
			FString(), FString(), TEXT("opacity-mask-clip-value"),
			FString::SanitizeFloat(Old.OpacityMaskClipValue), FString::SanitizeFloat(New.OpacityMaskClipValue));
	}

	TSet<FString> ExtraKeySet;
	for (const auto& Pair : Old.ExtraProperties)
	{
		ExtraKeySet.Add(Pair.Key);
	}
	for (const auto& Pair : New.ExtraProperties)
	{
		ExtraKeySet.Add(Pair.Key);
	}
	TArray<FString> ExtraKeys = ExtraKeySet.Array();
	ExtraKeys.Sort();
	for (const FString& Key : ExtraKeys)
	{
		const FString* OldValue = Old.ExtraProperties.Find(Key);
		const FString* NewValue = New.ExtraProperties.Find(Key);
		if (!OldValue || !NewValue || !FMatLangMatcher::AreValuesEquivalent(*OldValue, *NewValue))
		{
			AddEntry(EMatLangDiffOp::MaterialPropertyChanged, EMatLangDiffSeverity::Property,
				FString(), FString(), Key,
				OldValue ? *OldValue : FString(), NewValue ? *NewValue : FString());
		}
	}
}

void FMatLangDiffer::DiffExpressions(const FMaterialGraphAST& Old, const FMaterialGraphAST& New)
{
	for (const FString& OldId : Result.MatchResult.UnmatchedOldExprIds)
	{
		const TSharedPtr<FMatExpressionAST> OldExpression = Old.FindExpression(OldId);
		AddEntry(EMatLangDiffOp::ExpressionRemoved, EMatLangDiffSeverity::Structural,
			OldId, FString(), TEXT("type"), OldExpression.IsValid() ? OldExpression->ExprType : FString(), FString());
	}
	for (const FString& NewId : Result.MatchResult.UnmatchedNewExprIds)
	{
		const TSharedPtr<FMatExpressionAST> NewExpression = New.FindExpression(NewId);
		AddEntry(EMatLangDiffOp::ExpressionAdded, EMatLangDiffSeverity::Structural,
			FString(), NewId, TEXT("type"), FString(), NewExpression.IsValid() ? NewExpression->ExprType : FString());
	}

	for (const FMatLangExpressionMatch& Match : Result.MatchResult.ExpressionMatches)
	{
		const TSharedPtr<FMatExpressionAST> OldExpression = Old.FindExpression(Match.OldExprId);
		const TSharedPtr<FMatExpressionAST> NewExpression = New.FindExpression(Match.NewExprId);
		if (!OldExpression.IsValid() || !NewExpression.IsValid())
		{
			continue;
		}

		const int32 EntryCountBefore = Result.Entries.Num();
		if (OldExpression->ExprType != NewExpression->ExprType)
		{
			AddEntry(EMatLangDiffOp::ExpressionTypeChanged, EMatLangDiffSeverity::Structural,
				Match.OldExprId, Match.NewExprId, TEXT("type"),
				OldExpression->ExprType, NewExpression->ExprType);
		}
		else
		{
			if (bCompareEditorMetadata
				&& !OldExpression->EditorPosition.Equals(NewExpression->EditorPosition))
			{
				AddEntry(EMatLangDiffOp::ExprPropertyChanged, EMatLangDiffSeverity::Cosmetic,
					Match.OldExprId, Match.NewExprId, TEXT("editor-position"),
					FString::Printf(TEXT("(%g %g)"), OldExpression->EditorPosition.X, OldExpression->EditorPosition.Y),
					FString::Printf(TEXT("(%g %g)"), NewExpression->EditorPosition.X, NewExpression->EditorPosition.Y));
			}
			if (bCompareEditorMetadata && OldExpression->Comment != NewExpression->Comment)
			{
				AddEntry(EMatLangDiffOp::ExprPropertyChanged, EMatLangDiffSeverity::Cosmetic,
					Match.OldExprId, Match.NewExprId, TEXT("comment"),
					OldExpression->Comment, NewExpression->Comment);
			}
			DiffExpressionProperties(Match, *OldExpression, *NewExpression);
			DiffExpressionInputs(Match, *OldExpression, *NewExpression);
		}
		if (Match.bAmbiguous && Result.Entries.Num() > EntryCountBefore)
		{
			Result.bHasAmbiguousChanges = true;
		}
	}
}

void FMatLangDiffer::DiffExpressionProperties(
	const FMatLangExpressionMatch& Match,
	const FMatExpressionAST& Old,
	const FMatExpressionAST& New)
{
	TSet<FString> KeySet;
	for (const auto& Pair : Old.Properties)
	{
		KeySet.Add(Pair.Key);
	}
	for (const auto& Pair : New.Properties)
	{
		KeySet.Add(Pair.Key);
	}
	TArray<FString> Keys = KeySet.Array();
	Keys.Sort();
	for (const FString& Key : Keys)
	{
		const FString* OldValue = Old.Properties.Find(Key);
		const FString* NewValue = New.Properties.Find(Key);
		if (!OldValue && NewValue)
		{
			AddEntry(EMatLangDiffOp::ExprPropertyAdded, EMatLangDiffSeverity::Property,
				Match.OldExprId, Match.NewExprId, Key, FString(), *NewValue);
		}
		else if (OldValue && !NewValue)
		{
			AddEntry(EMatLangDiffOp::ExprPropertyRemoved, EMatLangDiffSeverity::Property,
				Match.OldExprId, Match.NewExprId, Key, *OldValue, FString());
		}
		else if (OldValue && NewValue && !FMatLangMatcher::AreValuesEquivalent(*OldValue, *NewValue))
		{
			AddEntry(EMatLangDiffOp::ExprPropertyChanged, EMatLangDiffSeverity::Property,
				Match.OldExprId, Match.NewExprId, Key, *OldValue, *NewValue);
		}
	}
}

void FMatLangDiffer::DiffExpressionInputs(
	const FMatLangExpressionMatch& Match,
	const FMatExpressionAST& Old,
	const FMatExpressionAST& New)
{
	const TArray<FString> OldNames = MatLangDifferPrivate::SortedInputNames(Old);
	const TArray<FString> NewNames = MatLangDifferPrivate::SortedInputNames(New);
	TSet<FString> NameSet;
	for (const FString& Name : OldNames)
	{
		NameSet.Add(Name);
	}
	for (const FString& Name : NewNames)
	{
		NameSet.Add(Name);
	}
	TArray<FString> Names = NameSet.Array();
	Names.Sort();

	for (const FString& Name : Names)
	{
		const FMatLangInput* OldInput = Old.FindInput(Name);
		const FMatLangInput* NewInput = New.FindInput(Name);
		if (!OldInput && NewInput)
		{
			AddEntry(EMatLangDiffOp::ExprInputAdded, EMatLangDiffSeverity::Property,
				Match.OldExprId, Match.NewExprId, Name, FString(), InputToString(*NewInput));
		}
		else if (OldInput && !NewInput)
		{
			AddEntry(EMatLangDiffOp::ExprInputRemoved, EMatLangDiffSeverity::Property,
				Match.OldExprId, Match.NewExprId, Name, InputToString(*OldInput), FString());
		}
		else if (OldInput && NewInput && !InputsEquivalent(*OldInput, *NewInput))
		{
			AddEntry(EMatLangDiffOp::ExprInputChanged, EMatLangDiffSeverity::Property,
				Match.OldExprId, Match.NewExprId, Name,
				InputToString(*OldInput), InputToString(*NewInput));
		}
	}
}

void FMatLangDiffer::DiffOutputs(const FMatOutputsAST& Old, const FMatOutputsAST& New)
{
	TSet<FString> SlotSet;
	for (const auto& Pair : Old.Slots)
	{
		SlotSet.Add(Pair.Key);
	}
	for (const auto& Pair : New.Slots)
	{
		SlotSet.Add(Pair.Key);
	}
	TArray<FString> Slots = SlotSet.Array();
	Slots.Sort();
	for (const FString& Slot : Slots)
	{
		const FMatLangInput* OldInput = Old.Slots.Find(Slot);
		const FMatLangInput* NewInput = New.Slots.Find(Slot);
		if (!OldInput && NewInput)
		{
			AddEntry(EMatLangDiffOp::OutputAdded, EMatLangDiffSeverity::Property,
				FString(), FString(), Slot, FString(), InputToString(*NewInput));
		}
		else if (OldInput && !NewInput)
		{
			AddEntry(EMatLangDiffOp::OutputRemoved, EMatLangDiffSeverity::Property,
				FString(), FString(), Slot, InputToString(*OldInput), FString());
		}
		else if (OldInput && NewInput && !InputsEquivalent(*OldInput, *NewInput))
		{
			AddEntry(EMatLangDiffOp::OutputChanged, EMatLangDiffSeverity::Property,
				FString(), FString(), Slot, InputToString(*OldInput), InputToString(*NewInput));
		}
	}
}

void FMatLangDiffer::AddEntry(
	EMatLangDiffOp Op,
	EMatLangDiffSeverity Severity,
	const FString& OldExprId,
	const FString& NewExprId,
	const FString& Key,
	const FString& OldValue,
	const FString& NewValue)
{
	FMatLangDiffEntry Entry;
	Entry.Op = Op;
	Entry.Severity = Severity;
	Entry.OldExprId = OldExprId;
	Entry.NewExprId = NewExprId;
	Entry.ExprId = !OldExprId.IsEmpty() ? OldExprId : NewExprId;
	Entry.Key = Key;
	Entry.OldValue = OldValue;
	Entry.NewValue = NewValue;
	Result.Entries.Add(MoveTemp(Entry));

	switch (Severity)
	{
		case EMatLangDiffSeverity::Cosmetic: Result.NumCosmetic++; break;
		case EMatLangDiffSeverity::Property: Result.NumProperty++; break;
		case EMatLangDiffSeverity::Structural: Result.NumStructural++; break;
	}
}

FString FMatLangDiffer::InputToString(const FMatLangInput& Input)
{
	if (Input.IsConnected())
	{
		return Input.Connection->ToString();
	}
	if (Input.IsLiteral())
	{
		return *Input.LiteralValue;
	}
	return TEXT("<empty>");
}

bool FMatLangDiffer::InputsEquivalent(const FMatLangInput& Old, const FMatLangInput& New) const
{
	if (Old.IsLiteral() && New.IsLiteral())
	{
		return FMatLangMatcher::AreValuesEquivalent(*Old.LiteralValue, *New.LiteralValue);
	}
	if (Old.IsConnected() && New.IsConnected())
	{
		if (Old.Connection->OutputIndex != New.Connection->OutputIndex)
		{
			return false;
		}
		const FString MappedTarget = Result.MatchResult.FindNewId(Old.Connection->TargetId);
		return !MappedTarget.IsEmpty() && MappedTarget == New.Connection->TargetId;
	}
	return !Old.IsLiteral() && !Old.IsConnected() && !New.IsLiteral() && !New.IsConnected();
}
