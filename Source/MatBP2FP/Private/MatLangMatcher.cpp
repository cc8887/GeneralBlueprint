// MatLangMatcher.cpp - Content-based expression matcher for MatLang graphs
// Copyright (c) 2026 cc8887. All Rights Reserved.

#include "MatLangMatcher.h"
#include "MatLangTokenizer.h"

namespace MatLangMatcherPrivate
{
	struct FConsumerRef
	{
		int32 ConsumerIndex = INDEX_NONE;
		FString InputName;
		int32 SourceOutputIndex = 0;
	};

	struct FNodeInfo
	{
		const FMatExpressionAST* Expression = nullptr;
		int32 OriginalIndex = INDEX_NONE;
		FString ExactSignature;
		TArray<FConsumerRef> Consumers;
		TArray<FString> OutputSlots;
	};

	struct FGraphInfo
	{
		const FMaterialGraphAST* Graph = nullptr;
		TArray<FNodeInfo> Nodes;
		TMap<FString, int32> IdToIndex;
	};

	static void AppendSignaturePart(FString& Signature, const FString& Part)
	{
		Signature += FString::FromInt(Part.Len());
		Signature += TEXT(":");
		Signature += Part;
		Signature += TEXT("|");
	}

	static bool IsNumericToken(EMatLangTokenType Type)
	{
		return Type == EMatLangTokenType::Integer || Type == EMatLangTokenType::Float;
	}

	static bool TryCanonicalizeNumber(const FMatLangToken& Token, FString& OutCanonical)
	{
		if (!IsNumericToken(Token.Type) || Token.Value.IsEmpty())
		{
			return false;
		}

		FString Text = Token.Value;
		bool bNegative = Text[0] == '-';
		if (bNegative)
		{
			Text.RightChopInline(1);
		}

		int32 ExponentIndex = Text.Find(TEXT("e"), ESearchCase::IgnoreCase);
		FString Mantissa = ExponentIndex == INDEX_NONE ? Text : Text.Left(ExponentIndex);
		FString ExponentText = ExponentIndex == INDEX_NONE ? FString() : Text.Mid(ExponentIndex + 1);
		int64 Exponent = 0;
		if (!ExponentText.IsEmpty())
		{
			bool bExponentNegative = ExponentText[0] == '-';
			if (bExponentNegative || ExponentText[0] == '+')
			{
				ExponentText.RightChopInline(1);
			}
			if (ExponentText.IsEmpty())
			{
				return false;
			}
			for (const TCHAR Character : ExponentText)
			{
				if (!FChar::IsDigit(Character))
				{
					return false;
				}
				Exponent = Exponent * 10 + (Character - '0');
				if (Exponent > 1000000)
				{
					return false;
				}
			}
			if (bExponentNegative)
			{
				Exponent = -Exponent;
			}
		}

		const int32 DecimalIndex = Mantissa.Find(TEXT("."));
		const int32 FractionDigits = DecimalIndex == INDEX_NONE ? 0 : Mantissa.Len() - DecimalIndex - 1;
		FString Digits = Mantissa.Replace(TEXT("."), TEXT(""));
		for (const TCHAR Character : Digits)
		{
			if (!FChar::IsDigit(Character))
			{
				return false;
			}
		}

		int32 FirstNonZero = 0;
		while (FirstNonZero < Digits.Len() && Digits[FirstNonZero] == '0')
		{
			++FirstNonZero;
		}
		if (FirstNonZero == Digits.Len())
		{
			OutCanonical = TEXT("0");
			return true;
		}
		Digits.RightChopInline(FirstNonZero);

		int64 DecimalExponent = Exponent - FractionDigits;
		while (Digits.Len() > 1 && Digits.EndsWith(TEXT("0")))
		{
			Digits.LeftChopInline(1);
			++DecimalExponent;
		}

		OutCanonical = bNegative ? TEXT("-") : FString();
		OutCanonical += Digits;
		OutCanonical += TEXT("e");
		OutCanonical += FString::Printf(TEXT("%lld"), DecimalExponent);
		return true;
	}

	static bool TokenizeValue(const FString& Value, TArray<FMatLangToken>& OutTokens)
	{
		TArray<FMatLangLexError> Errors;
		if (!FMatLangTokenizer::Tokenize(Value, OutTokens, Errors) || Errors.Num() > 0)
		{
			OutTokens.Reset();
			return false;
		}
		if (OutTokens.Num() > 0 && OutTokens.Last().Type == EMatLangTokenType::EndOfFile)
		{
			OutTokens.Pop();
		}
		return true;
	}

	static FString ValueSignature(const FString& Value)
	{
		TArray<FMatLangToken> Tokens;
		if (!TokenizeValue(Value, Tokens))
		{
			return Value;
		}

		FString Signature;
		for (const FMatLangToken& Token : Tokens)
		{
			FString TokenValue = Token.Value;
			const bool bCanonicalNumber = TryCanonicalizeNumber(Token, TokenValue);
			const FString TokenType = bCanonicalNumber
				? TEXT("number") : FString::FromInt(static_cast<int32>(Token.Type));
			AppendSignaturePart(Signature, TokenType);
			AppendSignaturePart(Signature, TokenValue);
		}
		return Signature;
	}

	template <typename ValueType>
	static TArray<FString> SortedMapKeys(const TMap<FString, ValueType>& Map)
	{
		TArray<FString> Keys;
		Map.GetKeys(Keys);
		Keys.Sort();
		return Keys;
	}

	static const FMatLangInput* FindInput(const FMatExpressionAST& Expression, const FString& Name)
	{
		for (const FMatLangInput& Input : Expression.Inputs)
		{
			if (Input.Name == Name)
			{
				return &Input;
			}
		}
		return nullptr;
	}

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

	static FString ConnectedType(const FGraphInfo& Graph, const FMatLangInput& Input)
	{
		if (!Input.IsConnected())
		{
			return FString();
		}
		const int32* Index = Graph.IdToIndex.Find(Input.Connection->TargetId);
		if (!Index || !Graph.Nodes.IsValidIndex(*Index) || !Graph.Nodes[*Index].Expression)
		{
			return TEXT("<missing>");
		}
		return Graph.Nodes[*Index].Expression->ExprType;
	}

	static FString MakeExactSignature(const FGraphInfo& Graph, int32 NodeIndex)
	{
		const FNodeInfo& Node = Graph.Nodes[NodeIndex];
		const FMatExpressionAST& Expression = *Node.Expression;
		FString Signature;
		AppendSignaturePart(Signature, Expression.ExprType);

		const TArray<FString> PropertyKeys = SortedMapKeys(Expression.Properties);
		for (const FString& Key : PropertyKeys)
		{
			AppendSignaturePart(Signature, TEXT("P") + Key);
			AppendSignaturePart(Signature, ValueSignature(Expression.Properties.FindRef(Key)));
		}

		const TArray<FString> InputNames = SortedInputNames(Expression);
		for (const FString& Name : InputNames)
		{
			const FMatLangInput* Input = FindInput(Expression, Name);
			AppendSignaturePart(Signature, TEXT("I") + Name);
			if (Input && Input->IsConnected())
			{
				AppendSignaturePart(Signature, FString::Printf(TEXT("C%s#%d"),
					*ConnectedType(Graph, *Input), Input->Connection->OutputIndex));
			}
			else if (Input && Input->IsLiteral())
			{
				AppendSignaturePart(Signature, TEXT("L") + ValueSignature(*Input->LiteralValue));
			}
			else
			{
				AppendSignaturePart(Signature, TEXT("E"));
			}
		}

		TArray<FString> ConsumerSignatures;
		for (const FConsumerRef& Consumer : Node.Consumers)
		{
			const FString ConsumerType = Graph.Nodes.IsValidIndex(Consumer.ConsumerIndex)
				&& Graph.Nodes[Consumer.ConsumerIndex].Expression
				? Graph.Nodes[Consumer.ConsumerIndex].Expression->ExprType
				: TEXT("<missing>");
			ConsumerSignatures.Add(FString::Printf(TEXT("%s:%s:%d"),
				*ConsumerType, *Consumer.InputName, Consumer.SourceOutputIndex));
		}
		ConsumerSignatures.Sort();
		for (const FString& ConsumerSignature : ConsumerSignatures)
		{
			AppendSignaturePart(Signature, TEXT("C") + ConsumerSignature);
		}

		for (const FString& Slot : Node.OutputSlots)
		{
			AppendSignaturePart(Signature, TEXT("O") + Slot);
		}
		return Signature;
	}

	static FGraphInfo BuildGraphInfo(const FMaterialGraphAST& Graph)
	{
		FGraphInfo Info;
		Info.Graph = &Graph;
		Info.Nodes.SetNum(Graph.Expressions.Num());

		for (int32 Index = 0; Index < Graph.Expressions.Num(); ++Index)
		{
			Info.Nodes[Index].Expression = Graph.Expressions[Index].Get();
			Info.Nodes[Index].OriginalIndex = Index;
			if (Graph.Expressions[Index].IsValid())
			{
				Info.IdToIndex.Add(Graph.Expressions[Index]->Id, Index);
			}
		}

		for (int32 ConsumerIndex = 0; ConsumerIndex < Graph.Expressions.Num(); ++ConsumerIndex)
		{
			const TSharedPtr<FMatExpressionAST>& Consumer = Graph.Expressions[ConsumerIndex];
			if (!Consumer.IsValid())
			{
				continue;
			}
			for (const FMatLangInput& Input : Consumer->Inputs)
			{
				if (!Input.IsConnected())
				{
					continue;
				}
				const int32* SourceIndex = Info.IdToIndex.Find(Input.Connection->TargetId);
				if (SourceIndex && Info.Nodes.IsValidIndex(*SourceIndex))
				{
					FConsumerRef Ref;
					Ref.ConsumerIndex = ConsumerIndex;
					Ref.InputName = Input.Name;
					Ref.SourceOutputIndex = Input.Connection->OutputIndex;
					Info.Nodes[*SourceIndex].Consumers.Add(MoveTemp(Ref));
				}
			}
		}

		const TArray<FString> OutputSlots = SortedMapKeys(Graph.Outputs.Slots);
		for (const FString& Slot : OutputSlots)
		{
			const FMatLangInput* Output = Graph.Outputs.Slots.Find(Slot);
			if (!Output || !Output->IsConnected())
			{
				continue;
			}
			const int32* SourceIndex = Info.IdToIndex.Find(Output->Connection->TargetId);
			if (SourceIndex && Info.Nodes.IsValidIndex(*SourceIndex))
			{
				Info.Nodes[*SourceIndex].OutputSlots.Add(Slot);
			}
		}

		for (int32 Index = 0; Index < Info.Nodes.Num(); ++Index)
		{
			Info.Nodes[Index].OutputSlots.Sort();
			if (Info.Nodes[Index].Expression)
			{
				Info.Nodes[Index].ExactSignature = MakeExactSignature(Info, Index);
			}
		}
		return Info;
	}

	static float StringDistance(const FString& A, const FString& B)
	{
		if (A == B)
		{
			return 0.0f;
		}
		if (A.IsEmpty() || B.IsEmpty())
		{
			return 1.0f;
		}

		TArray<int32> Previous;
		TArray<int32> Current;
		Previous.SetNumUninitialized(B.Len() + 1);
		Current.SetNumUninitialized(B.Len() + 1);
		for (int32 J = 0; J <= B.Len(); ++J)
		{
			Previous[J] = J;
		}
		for (int32 I = 1; I <= A.Len(); ++I)
		{
			Current[0] = I;
			for (int32 J = 1; J <= B.Len(); ++J)
			{
				const int32 Substitution = Previous[J - 1] + (A[I - 1] == B[J - 1] ? 0 : 1);
				Current[J] = FMath::Min3(Previous[J] + 1, Current[J - 1] + 1, Substitution);
			}
			Swap(Previous, Current);
		}
		return static_cast<float>(Previous[B.Len()]) / static_cast<float>(FMath::Max(A.Len(), B.Len()));
	}

	static float PropertyWeight(const FString& Key)
	{
		if (Key.Contains(TEXT("guid"), ESearchCase::IgnoreCase))
		{
			return 5.0f;
		}
		if (Key == TEXT("name") || Key.Contains(TEXT("function"), ESearchCase::IgnoreCase)
			|| Key.Contains(TEXT("texture"), ESearchCase::IgnoreCase)
			|| Key.Contains(TEXT("declaration"), ESearchCase::IgnoreCase))
		{
			return 4.0f;
		}
		if (Key == TEXT("group"))
		{
			return 2.0f;
		}
		return 1.0f;
	}

	static float PropertyDistance(const FMatExpressionAST& OldExpression, const FMatExpressionAST& NewExpression)
	{
		TSet<FString> KeySet;
		for (const auto& Pair : OldExpression.Properties)
		{
			KeySet.Add(Pair.Key);
		}
		for (const auto& Pair : NewExpression.Properties)
		{
			KeySet.Add(Pair.Key);
		}
		TArray<FString> Keys = KeySet.Array();
		Keys.Sort();
		if (Keys.Num() == 0)
		{
			return 0.0f;
		}

		float Difference = 0.0f;
		float TotalWeight = 0.0f;
		for (const FString& Key : Keys)
		{
			const float Weight = PropertyWeight(Key);
			TotalWeight += Weight;
			const FString* OldValue = OldExpression.Properties.Find(Key);
			const FString* NewValue = NewExpression.Properties.Find(Key);
			Difference += Weight * (OldValue && NewValue
				? (FMatLangMatcher::AreValuesEquivalent(*OldValue, *NewValue)
					? 0.0f : StringDistance(*OldValue, *NewValue))
				: 1.0f);
		}
		return TotalWeight > 0.0f ? Difference / TotalWeight : 0.0f;
	}

	static int32 ConnectionTargetIndex(const FGraphInfo& Graph, const FMatLangInput& Input)
	{
		if (!Input.IsConnected())
		{
			return INDEX_NONE;
		}
		const int32* Index = Graph.IdToIndex.Find(Input.Connection->TargetId);
		return Index ? *Index : INDEX_NONE;
	}

	static float InputValueDistance(
		const FGraphInfo& OldGraph,
		const FGraphInfo& NewGraph,
		const FMatLangInput& OldInput,
		const FMatLangInput& NewInput,
		const TMap<int32, int32>& OldToNewContext)
	{
		if (OldInput.IsLiteral() && NewInput.IsLiteral())
		{
			return FMatLangMatcher::AreValuesEquivalent(*OldInput.LiteralValue, *NewInput.LiteralValue)
				? 0.0f : StringDistance(*OldInput.LiteralValue, *NewInput.LiteralValue);
		}
		if (OldInput.IsConnected() && NewInput.IsConnected())
		{
			float Difference = OldInput.Connection->OutputIndex == NewInput.Connection->OutputIndex ? 0.0f : 0.2f;
			const int32 OldTarget = ConnectionTargetIndex(OldGraph, OldInput);
			const int32 NewTarget = ConnectionTargetIndex(NewGraph, NewInput);
			if (OldTarget == INDEX_NONE || NewTarget == INDEX_NONE)
			{
				return FMath::Min(1.0f, Difference + 0.8f);
			}
			if (const int32* MappedTarget = OldToNewContext.Find(OldTarget))
			{
				Difference += *MappedTarget == NewTarget ? 0.0f : 0.8f;
			}
			else
			{
				const FString& OldType = OldGraph.Nodes[OldTarget].Expression->ExprType;
				const FString& NewType = NewGraph.Nodes[NewTarget].Expression->ExprType;
				Difference += OldType == NewType ? 0.1f : 0.8f;
			}
			return FMath::Min(1.0f, Difference);
		}
		if (!OldInput.IsConnected() && !OldInput.IsLiteral()
			&& !NewInput.IsConnected() && !NewInput.IsLiteral())
		{
			return 0.0f;
		}
		return 1.0f;
	}

	static float InputDistance(
		const FGraphInfo& OldGraph,
		const FGraphInfo& NewGraph,
		int32 OldIndex,
		int32 NewIndex,
		const TMap<int32, int32>& OldToNewContext)
	{
		const FMatExpressionAST& OldExpression = *OldGraph.Nodes[OldIndex].Expression;
		const FMatExpressionAST& NewExpression = *NewGraph.Nodes[NewIndex].Expression;
		TSet<FString> NameSet;
		for (const FMatLangInput& Input : OldExpression.Inputs)
		{
			NameSet.Add(Input.Name);
		}
		for (const FMatLangInput& Input : NewExpression.Inputs)
		{
			NameSet.Add(Input.Name);
		}
		TArray<FString> Names = NameSet.Array();
		Names.Sort();
		if (Names.Num() == 0)
		{
			return 0.0f;
		}

		float Difference = 0.0f;
		for (const FString& Name : Names)
		{
			const FMatLangInput* OldInput = FindInput(OldExpression, Name);
			const FMatLangInput* NewInput = FindInput(NewExpression, Name);
			Difference += OldInput && NewInput
				? InputValueDistance(OldGraph, NewGraph, *OldInput, *NewInput, OldToNewContext)
				: 1.0f;
		}
		return Difference / static_cast<float>(Names.Num());
	}

	static float MultisetDistance(const TArray<FString>& A, const TArray<FString>& B)
	{
		if (A.Num() == 0 && B.Num() == 0)
		{
			return 0.0f;
		}
		TMap<FString, int32> Counts;
		for (const FString& Value : A)
		{
			Counts.FindOrAdd(Value)++;
		}
		for (const FString& Value : B)
		{
			Counts.FindOrAdd(Value)--;
		}
		int32 Difference = 0;
		for (const auto& Pair : Counts)
		{
			Difference += FMath::Abs(Pair.Value);
		}
		return static_cast<float>(Difference)
			/ static_cast<float>(FMath::Max(1, A.Num() + B.Num()));
	}

	static float ConsumerDistance(
		const FGraphInfo& OldGraph,
		const FGraphInfo& NewGraph,
		int32 OldIndex,
		int32 NewIndex,
		const TMap<int32, int32>& OldToNewContext,
		const TSet<int32>& MatchedNewIndices)
	{
		TArray<FString> OldConsumers;
		for (const FConsumerRef& Consumer : OldGraph.Nodes[OldIndex].Consumers)
		{
			const int32* MappedConsumer = OldToNewContext.Find(Consumer.ConsumerIndex);
			const FString Identity = MappedConsumer
				? FString::Printf(TEXT("M%d"), *MappedConsumer)
				: TEXT("T") + OldGraph.Nodes[Consumer.ConsumerIndex].Expression->ExprType;
			OldConsumers.Add(FString::Printf(TEXT("%s:%s:%d"),
				*Identity, *Consumer.InputName, Consumer.SourceOutputIndex));
		}

		TArray<FString> NewConsumers;
		for (const FConsumerRef& Consumer : NewGraph.Nodes[NewIndex].Consumers)
		{
			const FString Identity = MatchedNewIndices.Contains(Consumer.ConsumerIndex)
				? FString::Printf(TEXT("M%d"), Consumer.ConsumerIndex)
				: TEXT("T") + NewGraph.Nodes[Consumer.ConsumerIndex].Expression->ExprType;
			NewConsumers.Add(FString::Printf(TEXT("%s:%s:%d"),
				*Identity, *Consumer.InputName, Consumer.SourceOutputIndex));
		}
		return MultisetDistance(OldConsumers, NewConsumers);
	}

	static float OutputDistance(const FNodeInfo& OldNode, const FNodeInfo& NewNode)
	{
		return MultisetDistance(OldNode.OutputSlots, NewNode.OutputSlots);
	}

	static float ExpressionDistance(
		const FGraphInfo& OldGraph,
		const FGraphInfo& NewGraph,
		int32 OldIndex,
		int32 NewIndex,
		const TMap<int32, int32>& OldToNewContext,
		const TSet<int32>& MatchedNewIndices)
	{
		const FMatExpressionAST& OldExpression = *OldGraph.Nodes[OldIndex].Expression;
		const FMatExpressionAST& NewExpression = *NewGraph.Nodes[NewIndex].Expression;
		if (OldExpression.ExprType != NewExpression.ExprType)
		{
			return 1.0f;
		}
		const float Properties = PropertyDistance(OldExpression, NewExpression);
		const float Inputs = InputDistance(OldGraph, NewGraph, OldIndex, NewIndex, OldToNewContext);
		const float Consumers = ConsumerDistance(
			OldGraph, NewGraph, OldIndex, NewIndex, OldToNewContext, MatchedNewIndices);
		const float Outputs = OutputDistance(OldGraph.Nodes[OldIndex], NewGraph.Nodes[NewIndex]);
		return FMath::Clamp(0.45f * Properties + 0.30f * Inputs + 0.15f * Consumers + 0.10f * Outputs,
			0.0f, 1.0f);
	}

	static TArray<int32> HungarianAssignment(const TArray<TArray<double>>& Cost)
	{
		const int32 Size = Cost.Num();
		TArray<int32> Assignment;
		Assignment.Init(INDEX_NONE, Size);
		if (Size == 0)
		{
			return Assignment;
		}

		TArray<double> U;
		TArray<double> V;
		TArray<double> MinValue;
		TArray<int32> P;
		TArray<int32> Way;
		TArray<bool> Used;
		U.Init(0.0, Size + 1);
		V.Init(0.0, Size + 1);
		P.Init(0, Size + 1);
		Way.Init(0, Size + 1);

		for (int32 I = 1; I <= Size; ++I)
		{
			P[0] = I;
			int32 J0 = 0;
			MinValue.Init(TNumericLimits<double>::Max(), Size + 1);
			Used.Init(false, Size + 1);
			do
			{
				Used[J0] = true;
				const int32 I0 = P[J0];
				double Delta = TNumericLimits<double>::Max();
				int32 J1 = 0;
				for (int32 J = 1; J <= Size; ++J)
				{
					if (Used[J])
					{
						continue;
					}
					const double Current = Cost[I0 - 1][J - 1] - U[I0] - V[J];
					if (Current < MinValue[J])
					{
						MinValue[J] = Current;
						Way[J] = J0;
					}
					if (MinValue[J] < Delta)
					{
						Delta = MinValue[J];
						J1 = J;
					}
				}
				for (int32 J = 0; J <= Size; ++J)
				{
					if (Used[J])
					{
						U[P[J]] += Delta;
						V[J] -= Delta;
					}
					else
					{
						MinValue[J] -= Delta;
					}
				}
				J0 = J1;
			}
			while (P[J0] != 0);

			do
			{
				const int32 J1 = Way[J0];
				P[J0] = P[J1];
				J0 = J1;
			}
			while (J0 != 0);
		}

		for (int32 J = 1; J <= Size; ++J)
		{
			if (P[J] > 0)
			{
				Assignment[P[J] - 1] = J - 1;
			}
		}
		return Assignment;
	}

	static void SortNodeIndices(const FGraphInfo& Graph, TArray<int32>& Indices)
	{
		Indices.Sort([&Graph](int32 A, int32 B)
		{
			const int32 SignatureOrder = Graph.Nodes[A].ExactSignature.Compare(Graph.Nodes[B].ExactSignature);
			return SignatureOrder == 0 ? Graph.Nodes[A].OriginalIndex < Graph.Nodes[B].OriginalIndex : SignatureOrder < 0;
		});
	}

	static bool MapsEqual(const TMap<int32, int32>& A, const TMap<int32, int32>& B)
	{
		if (A.Num() != B.Num())
		{
			return false;
		}
		for (const auto& Pair : A)
		{
			const int32* Other = B.Find(Pair.Key);
			if (!Other || *Other != Pair.Value)
			{
				return false;
			}
		}
		return true;
	}

	static TSet<int32> MatchedNewIndices(const TMap<int32, int32>& Matches)
	{
		TSet<int32> Result;
		for (const auto& Pair : Matches)
		{
			Result.Add(Pair.Value);
		}
		return Result;
	}

	static bool IsUniqueBestCandidate(
		const TArray<float>& CandidateDistances,
		float SelectedDistance,
		float MaximumDistance)
	{
		if (CandidateDistances.Num() == 0 || SelectedDistance > MaximumDistance)
		{
			return false;
		}
		float BestDistance = TNumericLimits<float>::Max();
		for (const float Distance : CandidateDistances)
		{
			BestDistance = FMath::Min(BestDistance, Distance);
		}
		int32 BestChoices = 0;
		for (const float Distance : CandidateDistances)
		{
			if (FMath::IsNearlyEqual(Distance, BestDistance, 1.0e-4f))
			{
				++BestChoices;
			}
		}
		return BestChoices == 1
			&& FMath::IsNearlyEqual(SelectedDistance, BestDistance, 1.0e-4f);
	}
}

bool FMatLangMatcher::AreValuesEquivalent(const FString& OldValue, const FString& NewValue)
{
	if (OldValue == NewValue)
	{
		return true;
	}

	TArray<FMatLangToken> OldTokens;
	TArray<FMatLangToken> NewTokens;
	if (!MatLangMatcherPrivate::TokenizeValue(OldValue, OldTokens)
		|| !MatLangMatcherPrivate::TokenizeValue(NewValue, NewTokens)
		|| OldTokens.Num() != NewTokens.Num())
	{
		return false;
	}

	for (int32 Index = 0; Index < OldTokens.Num(); ++Index)
	{
		const FMatLangToken& OldToken = OldTokens[Index];
		const FMatLangToken& NewToken = NewTokens[Index];
		if (MatLangMatcherPrivate::IsNumericToken(OldToken.Type)
			&& MatLangMatcherPrivate::IsNumericToken(NewToken.Type))
		{
			FString OldNumber;
			FString NewNumber;
			if (!MatLangMatcherPrivate::TryCanonicalizeNumber(OldToken, OldNumber)
				|| !MatLangMatcherPrivate::TryCanonicalizeNumber(NewToken, NewNumber)
				|| OldNumber != NewNumber)
			{
				return false;
			}
		}
		else if (OldToken.Type != NewToken.Type || OldToken.Value != NewToken.Value)
		{
			return false;
		}
	}
	return true;
}

const FMatLangExpressionMatch* FMatLangMatchResult::FindByOldId(const FString& OldExprId) const
{
	for (const FMatLangExpressionMatch& Match : ExpressionMatches)
	{
		if (Match.OldExprId == OldExprId)
		{
			return &Match;
		}
	}
	return nullptr;
}

const FMatLangExpressionMatch* FMatLangMatchResult::FindByNewId(const FString& NewExprId) const
{
	for (const FMatLangExpressionMatch& Match : ExpressionMatches)
	{
		if (Match.NewExprId == NewExprId)
		{
			return &Match;
		}
	}
	return nullptr;
}

FString FMatLangMatchResult::FindNewId(const FString& OldExprId) const
{
	return OldToNewIds.FindRef(OldExprId);
}

FString FMatLangMatchResult::FindOldId(const FString& NewExprId) const
{
	return NewToOldIds.FindRef(NewExprId);
}

bool FMatLangMatchResult::IsAmbiguous() const
{
	for (const FMatLangExpressionMatch& Match : ExpressionMatches)
	{
		if (Match.bAmbiguous)
		{
			return true;
		}
	}
	return false;
}

FMatLangMatchResult FMatLangMatcher::Match(
	const FMaterialGraphAST& OldGraph,
	const FMaterialGraphAST& NewGraph,
	const FMatLangMatchOptions& Options)
{
	using namespace MatLangMatcherPrivate;
	const FMatLangMatchOptions DefaultOptions;
	const float MaximumDistance = FMath::IsFinite(Options.MaximumDistance)
		? FMath::Clamp(Options.MaximumDistance, 0.0f, 1.0f)
		: DefaultOptions.MaximumDistance;
	const int32 PassCount = FMath::Clamp(Options.RefinementPasses, 1, 16);

	const FGraphInfo OldInfo = BuildGraphInfo(OldGraph);
	const FGraphInfo NewInfo = BuildGraphInfo(NewGraph);
	TMap<int32, int32> AnchorMatches;
	TSet<int32> AnchoredNew;

	TMap<FString, TArray<int32>> OldBySignature;
	TMap<FString, TArray<int32>> NewBySignature;
	for (int32 Index = 0; Index < OldInfo.Nodes.Num(); ++Index)
	{
		if (OldInfo.Nodes[Index].Expression)
		{
			OldBySignature.FindOrAdd(OldInfo.Nodes[Index].ExactSignature).Add(Index);
		}
	}
	for (int32 Index = 0; Index < NewInfo.Nodes.Num(); ++Index)
	{
		if (NewInfo.Nodes[Index].Expression)
		{
			NewBySignature.FindOrAdd(NewInfo.Nodes[Index].ExactSignature).Add(Index);
		}
	}

	TArray<FString> Signatures;
	OldBySignature.GetKeys(Signatures);
	Signatures.Sort();
	for (const FString& Signature : Signatures)
	{
		const TArray<int32>* OldIndices = OldBySignature.Find(Signature);
		const TArray<int32>* NewIndices = NewBySignature.Find(Signature);
		if (OldIndices && NewIndices && OldIndices->Num() == 1 && NewIndices->Num() == 1)
		{
			AnchorMatches.Add((*OldIndices)[0], (*NewIndices)[0]);
			AnchoredNew.Add((*NewIndices)[0]);
		}
	}

	TSet<FString> TypeSet;
	for (const FNodeInfo& Node : OldInfo.Nodes)
	{
		if (Node.Expression)
		{
			TypeSet.Add(Node.Expression->ExprType);
		}
	}
	for (const FNodeInfo& Node : NewInfo.Nodes)
	{
		if (Node.Expression)
		{
			TypeSet.Add(Node.Expression->ExprType);
		}
	}
	TArray<FString> Types = TypeSet.Array();
	Types.Sort();

	TMap<int32, int32> WorkingMatches = AnchorMatches;
	bool bConverged = false;
	for (int32 Pass = 0; Pass < PassCount; ++Pass)
	{
		TMap<int32, int32> RefinedMatches = AnchorMatches;
		const TSet<int32> WorkingMatchedNew = MatchedNewIndices(WorkingMatches);
		for (const FString& Type : Types)
		{
			TArray<int32> OldBucket;
			TArray<int32> NewBucket;
			for (int32 Index = 0; Index < OldInfo.Nodes.Num(); ++Index)
			{
				if (OldInfo.Nodes[Index].Expression
					&& OldInfo.Nodes[Index].Expression->ExprType == Type
					&& !AnchorMatches.Contains(Index))
				{
					OldBucket.Add(Index);
				}
			}
			for (int32 Index = 0; Index < NewInfo.Nodes.Num(); ++Index)
			{
				if (NewInfo.Nodes[Index].Expression
					&& NewInfo.Nodes[Index].Expression->ExprType == Type
					&& !AnchoredNew.Contains(Index))
				{
					NewBucket.Add(Index);
				}
			}
			SortNodeIndices(OldInfo, OldBucket);
			SortNodeIndices(NewInfo, NewBucket);
			const int32 Size = OldBucket.Num() + NewBucket.Num();
			if (Size == 0)
			{
				continue;
			}

			constexpr double AcceptEpsilon = 1.0e-7;
			const double UnmatchedCost = static_cast<double>(MaximumDistance) * 0.5 + 1.0e-6;
			const double ForbiddenCost = 2.0 * UnmatchedCost + 1.0;
			TArray<TArray<double>> Cost;
			Cost.SetNum(Size);
			for (int32 Row = 0; Row < Size; ++Row)
			{
				Cost[Row].SetNum(Size);
				for (int32 Column = 0; Column < Size; ++Column)
				{
					if (OldBucket.IsValidIndex(Row) && NewBucket.IsValidIndex(Column))
					{
						const double SemanticCost = ExpressionDistance(
							OldInfo, NewInfo, OldBucket[Row], NewBucket[Column],
							WorkingMatches, WorkingMatchedNew);
						Cost[Row][Column] = SemanticCost
							<= static_cast<double>(MaximumDistance) + AcceptEpsilon
							? SemanticCost : ForbiddenCost;
					}
					else if (OldBucket.IsValidIndex(Row) || NewBucket.IsValidIndex(Column))
					{
						Cost[Row][Column] = UnmatchedCost;
					}
					else
					{
						Cost[Row][Column] = 0.0;
					}
				}
			}

			const TArray<int32> Assignment = HungarianAssignment(Cost);
			for (int32 Row = 0; Row < OldBucket.Num(); ++Row)
			{
				const int32 Column = Assignment.IsValidIndex(Row) ? Assignment[Row] : INDEX_NONE;
				if (NewBucket.IsValidIndex(Column)
					&& Cost[Row][Column] <= static_cast<double>(MaximumDistance) + AcceptEpsilon)
				{
					RefinedMatches.Add(OldBucket[Row], NewBucket[Column]);
				}
			}
		}

		if (MapsEqual(WorkingMatches, RefinedMatches))
		{
			WorkingMatches = MoveTemp(RefinedMatches);
			bConverged = true;
			break;
		}
		WorkingMatches = MoveTemp(RefinedMatches);
	}

	FMatLangMatchResult Result;
	TSet<int32> MatchedNew;
	const TSet<int32> FinalMatchedNew = MatchedNewIndices(WorkingMatches);
	for (int32 OldIndex = 0; OldIndex < OldInfo.Nodes.Num(); ++OldIndex)
	{
		const int32* NewIndex = WorkingMatches.Find(OldIndex);
		if (!NewIndex || !OldInfo.Nodes[OldIndex].Expression || !NewInfo.Nodes.IsValidIndex(*NewIndex)
			|| !NewInfo.Nodes[*NewIndex].Expression)
		{
			if (OldInfo.Nodes[OldIndex].Expression)
			{
				Result.UnmatchedOldExprIds.Add(OldInfo.Nodes[OldIndex].Expression->Id);
			}
			continue;
		}

		MatchedNew.Add(*NewIndex);
		FMatLangExpressionMatch Match;
		Match.OldExprId = OldInfo.Nodes[OldIndex].Expression->Id;
		Match.NewExprId = NewInfo.Nodes[*NewIndex].Expression->Id;
		Match.Kind = AnchorMatches.Contains(OldIndex)
			? EMatLangMatchKind::ExactContent : EMatLangMatchKind::Proximity;
		const float Distance = ExpressionDistance(
			OldInfo, NewInfo, OldIndex, *NewIndex, WorkingMatches, FinalMatchedNew);
		Match.Similarity = 1.0f - Distance;

		Match.bAmbiguous = Match.Kind == EMatLangMatchKind::Proximity && !bConverged;
		if (Match.Kind == EMatLangMatchKind::Proximity)
		{
			const float AmbiguityDistance = ExpressionDistance(
				OldInfo, NewInfo, OldIndex, *NewIndex, AnchorMatches, AnchoredNew);
			TArray<float> NewCandidateDistances;
			TArray<float> OldCandidateDistances;
			for (int32 CandidateNew = 0; CandidateNew < NewInfo.Nodes.Num(); ++CandidateNew)
			{
				if (NewInfo.Nodes[CandidateNew].Expression
					&& !AnchoredNew.Contains(CandidateNew)
					&& NewInfo.Nodes[CandidateNew].Expression->ExprType == OldInfo.Nodes[OldIndex].Expression->ExprType)
				{
					const float CandidateDistance = ExpressionDistance(
						OldInfo, NewInfo, OldIndex, CandidateNew, AnchorMatches, AnchoredNew);
					if (CandidateDistance <= MaximumDistance)
					{
						NewCandidateDistances.Add(CandidateDistance);
					}
				}
			}
			for (int32 CandidateOld = 0; CandidateOld < OldInfo.Nodes.Num(); ++CandidateOld)
			{
				if (OldInfo.Nodes[CandidateOld].Expression
					&& !AnchorMatches.Contains(CandidateOld)
					&& OldInfo.Nodes[CandidateOld].Expression->ExprType == NewInfo.Nodes[*NewIndex].Expression->ExprType)
				{
					const float CandidateDistance = ExpressionDistance(
						OldInfo, NewInfo, CandidateOld, *NewIndex, AnchorMatches, AnchoredNew);
					if (CandidateDistance <= MaximumDistance)
					{
						OldCandidateDistances.Add(CandidateDistance);
					}
				}
			}
			const bool bUniqueMutualNearest = IsUniqueBestCandidate(
				NewCandidateDistances, AmbiguityDistance, MaximumDistance)
				&& IsUniqueBestCandidate(
					OldCandidateDistances, AmbiguityDistance, MaximumDistance);
			Match.bAmbiguous = Match.bAmbiguous || !bUniqueMutualNearest;
		}
		Result.OldToNewIds.Add(Match.OldExprId, Match.NewExprId);
		Result.NewToOldIds.Add(Match.NewExprId, Match.OldExprId);
		Result.ExpressionMatches.Add(MoveTemp(Match));
	}

	for (int32 NewIndex = 0; NewIndex < NewInfo.Nodes.Num(); ++NewIndex)
	{
		if (NewInfo.Nodes[NewIndex].Expression && !MatchedNew.Contains(NewIndex))
		{
			Result.UnmatchedNewExprIds.Add(NewInfo.Nodes[NewIndex].Expression->Id);
		}
	}

	// A locally unique pair can still participate in an alternative optimal
	// assignment elsewhere in the same type bucket. Propagate ambiguity within
	// each proximity bucket so an incremental patch never relies on that choice.
	TSet<FString> AmbiguousTypes;
	for (const FMatLangExpressionMatch& Match : Result.ExpressionMatches)
	{
		if (Match.Kind != EMatLangMatchKind::Proximity || !Match.bAmbiguous)
		{
			continue;
		}
		if (const int32* OldIndex = OldInfo.IdToIndex.Find(Match.OldExprId))
		{
			if (OldInfo.Nodes.IsValidIndex(*OldIndex) && OldInfo.Nodes[*OldIndex].Expression)
			{
				AmbiguousTypes.Add(OldInfo.Nodes[*OldIndex].Expression->ExprType);
			}
		}
	}
	for (FMatLangExpressionMatch& Match : Result.ExpressionMatches)
	{
		if (Match.Kind != EMatLangMatchKind::Proximity)
		{
			continue;
		}
		if (const int32* OldIndex = OldInfo.IdToIndex.Find(Match.OldExprId))
		{
			if (OldInfo.Nodes.IsValidIndex(*OldIndex) && OldInfo.Nodes[*OldIndex].Expression
				&& AmbiguousTypes.Contains(OldInfo.Nodes[*OldIndex].Expression->ExprType))
			{
				Match.bAmbiguous = true;
			}
		}
	}
	Result.ExpressionMatches.Sort([](const FMatLangExpressionMatch& A, const FMatLangExpressionMatch& B)
	{
		const int32 OldOrder = A.OldExprId.Compare(B.OldExprId);
		return OldOrder == 0 ? A.NewExprId.Compare(B.NewExprId) < 0 : OldOrder < 0;
	});
	Result.UnmatchedOldExprIds.Sort();
	Result.UnmatchedNewExprIds.Sort();
	return Result;
}
