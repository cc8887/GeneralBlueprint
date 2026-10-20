// MatLangMatcher.h - Content-based expression matcher for MatLang graphs
// Copyright (c) 2026 cc8887. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MatLangAST.h"

/** How an expression pair was established. */
enum class EMatLangMatchKind : uint8
{
	ExactContent,
	Proximity,
};

/** A one-to-one correspondence between expressions in two graph revisions. */
struct MATBP2FP_API FMatLangExpressionMatch
{
	FString OldExprId;
	FString NewExprId;
	float Similarity = 0.0f;
	EMatLangMatchKind Kind = EMatLangMatchKind::Proximity;
	bool bAmbiguous = false;
};

/** Result of the matching phase. IDs identify results; they are never matching features. */
struct MATBP2FP_API FMatLangMatchResult
{
	TArray<FMatLangExpressionMatch> ExpressionMatches;
	TArray<FString> UnmatchedOldExprIds;
	TArray<FString> UnmatchedNewExprIds;
	TMap<FString, FString> OldToNewIds;
	TMap<FString, FString> NewToOldIds;

	const FMatLangExpressionMatch* FindByOldId(const FString& OldExprId) const;
	const FMatLangExpressionMatch* FindByNewId(const FString& NewExprId) const;
	FString FindNewId(const FString& OldExprId) const;
	FString FindOldId(const FString& NewExprId) const;
	bool IsAmbiguous() const;
};

/** Configuration for content/proximity matching. */
struct MATBP2FP_API FMatLangMatchOptions
{
	/** Maximum normalized distance accepted as the same logical expression. */
	float MaximumDistance = 0.64f;

	/** Re-run assignment with neighbourhood information until it stabilizes. */
	int32 RefinementPasses = 3;
};

/**
 * Matches expressions before differencing, following EMF Compare's separation
 * of correspondence from change detection. The implementation is native C++
 * and deliberately ignores the spelling and ordering of expression IDs.
 */
class MATBP2FP_API FMatLangMatcher
{
public:
	/** Compare DSL values while ignoring representation-only numeric spelling changes. */
	static bool AreValuesEquivalent(const FString& OldValue, const FString& NewValue);

	static FMatLangMatchResult Match(
		const FMaterialGraphAST& OldGraph,
		const FMaterialGraphAST& NewGraph,
		const FMatLangMatchOptions& Options = FMatLangMatchOptions());
};
