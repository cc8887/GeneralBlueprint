// MatLangDiffer.h - AST Diff Engine for MatLang DSL
// Copyright (c) 2026 cc8887. All Rights Reserved.
//
// Compares two FMaterialGraphAST revisions and produces semantic diff entries.
// Expression correspondence is established by FMatLangMatcher before changes
// are detected; the spelling and ordering of $id values do not affect matches.
//
// Diff dimensions:
//   1. Material-level properties (domain, blend-mode, shading-model, two-sided, ...)
//   2. Expression list changes  (added / removed / type-changed)
//   3. Expression property changes  (value of :key changed)
//   4. Expression input/connection changes
//   5. Material output slot changes

#pragma once

#include "CoreMinimal.h"
#include "MatLangAST.h"
#include "MatLangMatcher.h"

// ---- Diff Operation Types ----

enum class EMatLangDiffOp : uint8
{
	// Material-level properties
	MaterialPropertyChanged,      // :domain, :blend-mode, :shading-model, :two-sided, etc.

	// Expression list
	ExpressionAdded,              // New $id not present in old
	ExpressionRemoved,            // Old $id not present in new
	ExpressionTypeChanged,        // Reserved legacy op; type-bucket matching emits remove + add

	// Expression properties
	ExprPropertyChanged,          // Same $id, property value changed
	ExprPropertyAdded,            // Same $id, new property key
	ExprPropertyRemoved,          // Same $id, old property key removed

	// Expression inputs/connections
	ExprInputChanged,             // Same $id, same input name, connection/value changed
	ExprInputAdded,               // Same $id, new input name
	ExprInputRemoved,             // Same $id, old input name removed

	// Material output slots
	OutputChanged,                // Same output slot, different connection/value
	OutputAdded,                  // New output slot
	OutputRemoved,                // Old output slot removed
};

/**
 * Severity of a diff entry
 *   0 = Cosmetic  — no functional change (e.g. editor position, comment)
 *   1 = Property  — property/input value change; some operations may still require rebuild
 *   2 = Structural — expression added/removed/type-changed, requires rebuild
 */
enum class EMatLangDiffSeverity : uint8
{
	Cosmetic   = 0,
	Property   = 1,
	Structural = 2,
};

/**
 * Single diff entry describing one change between old and new AST
 */
struct MATBP2FP_API FMatLangDiffEntry
{
	EMatLangDiffOp Op;
	EMatLangDiffSeverity Severity;

	/** Legacy/current-side ID. Kept for API compatibility with existing callers. */
	FString ExprId;
	FString Key;             // Property/input/output name
	FString OldValue;        // Previous value (empty for Add ops)
	FString NewValue;        // New value (empty for Remove ops)

	/** Old/current and new/desired IDs. Appended to preserve legacy aggregate initialization. */
	FString OldExprId;
	FString NewExprId;

	FString ToString() const;
};

/**
 * Result of a diff operation
 */
struct MATBP2FP_API FMatLangDiffResult
{
	TArray<FMatLangDiffEntry> Entries;
	FMatLangMatchResult MatchResult;

	int32 NumCosmetic;
	int32 NumProperty;
	int32 NumStructural;
	bool bHasAmbiguousChanges;

	FMatLangDiffResult()
		: NumCosmetic(0)
		, NumProperty(0)
		, NumStructural(0)
		, bHasAmbiguousChanges(false)
	{
	}

	bool HasStructuralChanges() const { return NumStructural > 0; }
	bool HasPropertyChanges() const { return NumProperty > 0; }
	bool IsEmpty() const { return Entries.Num() == 0; }
	bool HasUnsupportedIncrementalChanges() const
	{
		for (const FMatLangDiffEntry& Entry : Entries)
		{
			// A missing DSL property means "restore the node's native default".
			// That default is type-specific and cannot be represented by NewValue="".
			if (Entry.Op == EMatLangDiffOp::ExprPropertyRemoved
				|| (Entry.Op == EMatLangDiffOp::MaterialPropertyChanged && Entry.NewValue.IsEmpty()))
			{
				return true;
			}
		}
		return false;
	}
	bool CanApplyIncrementally() const
	{
		return !HasStructuralChanges()
			&& !bHasAmbiguousChanges
			&& !HasUnsupportedIncrementalChanges();
	}
	int32 TotalChanges() const { return Entries.Num(); }

	FString GetSummary() const;
};

/**
 * Differ engine — compares two FMaterialGraphAST
 */
class MATBP2FP_API FMatLangDiffer
{
public:
	/**
	 * Compute diff between two ASTs
	 * @param OldAST  Current material state
	 * @param NewAST  Desired material state
	 * @return Diff result with all entries
	 */
	static FMatLangDiffResult Diff(
		const TSharedPtr<FMaterialGraphAST>& OldAST,
		const TSharedPtr<FMaterialGraphAST>& NewAST);

	/**
	 * Compute a diff with control over editor-only metadata. MatLang source does
	 * not serialize positions or comments, so DSL update paths pass false while
	 * direct AST callers retain the default explicit-field behavior above.
	 */
	static FMatLangDiffResult Diff(
		const TSharedPtr<FMaterialGraphAST>& OldAST,
		const TSharedPtr<FMaterialGraphAST>& NewAST,
		bool bCompareEditorMetadata);

private:
	FMatLangDiffResult Result;
	bool bCompareEditorMetadata = true;

	void DiffMaterialProperties(const FMaterialGraphAST& Old, const FMaterialGraphAST& New);
	void DiffExpressions(const FMaterialGraphAST& Old, const FMaterialGraphAST& New);
	void DiffExpressionProperties(const FMatLangExpressionMatch& Match,
		const FMatExpressionAST& Old, const FMatExpressionAST& New);
	void DiffExpressionInputs(const FMatLangExpressionMatch& Match,
		const FMatExpressionAST& Old, const FMatExpressionAST& New);
	void DiffOutputs(const FMatOutputsAST& Old, const FMatOutputsAST& New);

	void AddEntry(EMatLangDiffOp Op, EMatLangDiffSeverity Severity,
		const FString& OldExprId, const FString& NewExprId, const FString& Key,
		const FString& OldValue, const FString& NewValue);

	// Serialize an FMatLangInput to a comparable string
	static FString InputToString(const FMatLangInput& Input);
	bool InputsEquivalent(const FMatLangInput& Old, const FMatLangInput& New) const;
};
