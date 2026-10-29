// AnimBP2FPMCPToolset.cpp - UE 5.8 MCP toolset for AnimBP2FP

#include "AnimBP2FPMCPToolset.h"
#include "AnimBP2FPVersionCompat.h"

#if ANIMBP2FP_HAS_MODERN_RIGVM_AUTHORING

#include "AnimBP2FPMCPToolsetImpl.h"
#include "AnimBP2FPPythonBridge.h"
#include "AnimBPExporter.h"
#include "AnimBPImporter.h"
#include "AnimLangDiagnostics.h"
#include "FBP2FPMappingRegistry.h"
#include "RigLangExporter.h"
#include "Animation/AnimBlueprint.h"
#include "Editor.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

namespace AnimBP2FPMCP
{
	template<typename TResult>
	static TResult Failure(const FString& Error)
	{
		TResult Result;
		Result.Error = Error;
		return Result;
	}

	static bool CheckEditorContext(FString& OutError, const bool bMutation = false)
	{
		if (!IsInGameThread())
		{
			OutError = TEXT("AnimBP2FP tools must run on the editor game thread");
			return false;
		}
		if (bMutation && GEditor && GEditor->PlayWorld)
		{
			OutError = TEXT("Stop Play In Editor before changing animation assets");
			return false;
		}
		return true;
	}

	static FString DiagnosticSeverity(const EAnimLangDiagSeverity Severity)
	{
		switch (Severity)
		{
		case EAnimLangDiagSeverity::Error: return TEXT("error");
		case EAnimLangDiagSeverity::Warning: return TEXT("warning");
		case EAnimLangDiagSeverity::Info: return TEXT("info");
		case EAnimLangDiagSeverity::Hint: return TEXT("hint");
		}
		return TEXT("unknown");
	}

	static FString DiagnosticCategory(const EAnimLangDiagCategory Category)
	{
		switch (Category)
		{
		case EAnimLangDiagCategory::Lex: return TEXT("lex");
		case EAnimLangDiagCategory::Parse: return TEXT("parse");
		case EAnimLangDiagCategory::Type: return TEXT("type");
		case EAnimLangDiagCategory::Semantic: return TEXT("semantic");
		case EAnimLangDiagCategory::Import: return TEXT("import");
		case EAnimLangDiagCategory::RoundTrip: return TEXT("roundtrip");
		case EAnimLangDiagCategory::Module: return TEXT("module");
		case EAnimLangDiagCategory::Capability: return TEXT("capability");
		}
		return TEXT("unknown");
	}

	static FString MappingState(const EBP2FPSyncState State)
	{
		switch (State)
		{
		case EBP2FPSyncState::Synced: return TEXT("Synced");
		case EBP2FPSyncState::BPOnly: return TEXT("BPOnly");
		case EBP2FPSyncState::DSLOnly: return TEXT("DSLOnly");
		case EBP2FPSyncState::OutOfSync: return TEXT("OutOfSync");
		}
		return TEXT("Unknown");
	}

	static FString NormalizePackagePath(const FString& InAssetPath)
	{
		FString Path = InAssetPath.TrimStartAndEnd();
		if (Path.Contains(TEXT(".")))
		{
			const FString PackagePath = FPackageName::ObjectPathToPackageName(Path);
			if (!PackagePath.IsEmpty())
			{
				Path = PackagePath;
			}
		}
		return Path;
	}

	static bool ValidateAssetPath(const FString& InPath, FString& OutPath, FString& OutError)
	{
		const FString Path = InPath.TrimStartAndEnd();
		OutPath = NormalizePackagePath(Path);
		if (!Path.StartsWith(TEXT("/Game/")) || Path.Contains(TEXT(".."))
			|| Path.Contains(TEXT("\\")) || Path.Contains(TEXT("'")) || Path.Contains(TEXT(":"))
			|| !FPackageName::IsValidLongPackageName(OutPath)
			|| (Path.Contains(TEXT(".")) && !FPackageName::IsValidObjectPath(Path)))
		{
			OutError = TEXT("Provide a valid /Game package or object path without traversal or subobjects");
			return false;
		}
		if (Path.Contains(TEXT(".")) && Path != OutPath + TEXT(".") + FPackageName::GetLongPackageAssetName(OutPath))
		{
			OutError = TEXT("Object path must name the asset matching its package leaf");
			return false;
		}
		return true;
	}

	static UObject* LoadAsset(const FString& InAssetPath, FString& OutPackagePath, FString& OutError)
	{
		if (!CheckEditorContext(OutError)
			|| !ValidateAssetPath(InAssetPath, OutPackagePath, OutError))
		{
			return nullptr;
		}
		const FString ObjectPath = OutPackagePath + TEXT(".")
			+ FPackageName::GetLongPackageAssetName(OutPackagePath);
		UObject* Asset = LoadObject<UObject>(nullptr, *ObjectPath);
		if (!Asset)
		{
			OutError = FString::Printf(TEXT("Failed to load asset: %s"), *OutPackagePath);
		}
		return Asset;
	}

	static FString LogicalSourceFile(const FString& PackagePath, const TCHAR* Extension)
	{
		FString Relative = PackagePath;
		Relative.RemoveFromStart(TEXT("/Game/"));
		Relative.ReplaceInline(TEXT("/"), TEXT("__"));
		return Relative + Extension;
	}

	static FAnimBP2FPMCPExportDocument MakeRigDocument(
		const FRigLangExportResult& Export,
		const bool bIncludeText)
	{
		FAnimBP2FPMCPExportDocument Document;
		if (!Export.Module.IsValid()) return Document;
		Document.AssetPath = Export.Module->Header.ModuleId.AssetPath;
		Document.SourceFile = LogicalSourceFile(Document.AssetPath, TEXT(".riglang"));
		Document.ModuleKind = Export.Module->Header.Properties.FindRef(TEXT("rig-kind"));
		Document.ModuleKind.TrimQuotesInline();
		Document.ContentHash = Export.Module->Header.ContentHash;
		if (bIncludeText) Document.Text = Export.Module->ToCanonicalString();
		const FRigCoverageTotals Coverage = Export.Module->GetCoverageTotals();
		Document.Exact = Coverage.Exact;
		Document.Reflected = Coverage.Reflected;
		Document.Lossy = Coverage.Lossy;
		Document.Unsupported = Coverage.Unsupported;
		return Document;
	}

	static FAnimBP2FPMCPTextResult FromTextResult(const FAnimBP2FPPythonResult& InResult)
	{
		FAnimBP2FPMCPTextResult Result;
		Result.bSuccess = InResult.bSuccess;
		Result.Error = InResult.bSuccess ? FString() : InResult.Message;
		Result.AssetPath = InResult.AssetPath;
		Result.Text = InResult.DSLText;
		Result.Warnings = InResult.Warnings;
		Result.bSavedPackage = InResult.bSavedPackage;
		return Result;
	}

	static FAnimBP2FPMCPChangeResult FromChangeResult(
		const FAnimBP2FPPythonResult& InResult,
		const bool bSaveRequested)
	{
		FAnimBP2FPMCPChangeResult Result;
		Result.bSuccess = InResult.bSuccess && (!bSaveRequested || InResult.bSavedPackage);
		Result.Error = InResult.bSuccess
			? ((!bSaveRequested || InResult.bSavedPackage) ? FString() : InResult.Message)
			: InResult.Message;
		Result.AssetPath = InResult.AssetPath;
		Result.bSavedPackage = InResult.bSavedPackage;
		Result.bMutationStarted = InResult.bSuccess;
		Result.bUsedIncrementalPatch = InResult.bUsedIncrementalPatch;
		Result.NumChanges = InResult.NumChanges;
		Result.NumPropertyChanges = InResult.NumPropertyChanges;
		Result.NumStructuralChanges = InResult.NumStructuralChanges;
		Result.AppliedOps = InResult.AppliedOps;
		Result.Warnings = InResult.Warnings;
		return Result;
	}

	static FAnimBP2FPMCPDiagnostic ToDiagnostic(const FAnimLangDiagnostic& InDiagnostic)
	{
		FAnimBP2FPMCPDiagnostic Result;
		Result.Severity = DiagnosticSeverity(InDiagnostic.Severity);
		Result.Category = DiagnosticCategory(InDiagnostic.Category);
		Result.Message = InDiagnostic.Message;
		Result.SourceFile = InDiagnostic.Location.SourceFile;
		Result.Line = InDiagnostic.Location.Line;
		Result.Column = InDiagnostic.Location.Column;
		return Result;
	}

	static bool IsSafeSourceName(const FString& SourceFile)
	{
		return !SourceFile.IsEmpty()
			&& FPaths::IsRelative(SourceFile)
			&& !SourceFile.Contains(TEXT(".."))
			&& !SourceFile.Contains(TEXT(":"))
			&& !SourceFile.Contains(TEXT("\\"))
			&& (SourceFile.EndsWith(TEXT(".animlang")) || SourceFile.EndsWith(TEXT(".riglang")));
	}
}

FAnimBP2FPMCPTextResult UAnimBP2FPToolset::ExportAnimBlueprint(const FString& AssetPath)
{
	FString SafePath, Error;
	if (!AnimBP2FPMCP::CheckEditorContext(Error)
		|| !AnimBP2FPMCP::ValidateAssetPath(AssetPath, SafePath, Error))
	{
		return AnimBP2FPMCP::Failure<FAnimBP2FPMCPTextResult>(Error);
	}
	return AnimBP2FPMCP::FromTextResult(
		UAnimBP2FPPythonBridge::ExportAnimBlueprintToText(SafePath));
}

FAnimBP2FPMCPExportBundleResult UAnimBP2FPToolset::ExportAnimBlueprintBundle(
	const FString& AssetPath,
	const bool bIncludeText)
{
	FAnimBP2FPMCPExportBundleResult Result;
	FString PackagePath;
	UObject* Asset = AnimBP2FPMCP::LoadAsset(AssetPath, PackagePath, Result.Error);
	UAnimBlueprint* AnimBlueprint = Cast<UAnimBlueprint>(Asset);
	if (!AnimBlueprint)
	{
		if (Result.Error.IsEmpty())
		{
			Result.Error = FString::Printf(TEXT("Asset is not an AnimBlueprint: %s"), *PackagePath);
		}
		return Result;
	}

	FAnimBPExporter::FExportOptions Options;
	Options.bPrettyPrint = true;
	Options.IndentSize = 2;
	TMap<FString, FRigLangExportResult> RigExports;
	TSharedPtr<FAnimGraphAST> AnimAST;
	const FString AnimText = FAnimBPExporter::ExportWithOptions(
		AnimBlueprint, Options, RigExports, &AnimAST);
	if (!AnimAST.IsValid() || AnimText.TrimStart().StartsWith(TEXT("; Error:")))
	{
		Result.Error = AnimText.IsEmpty() ? TEXT("Failed to export AnimBlueprint AST") : AnimText;
		return Result;
	}

	Result.AnimBlueprint.AssetPath = PackagePath;
	Result.AnimBlueprint.SourceFile = AnimBP2FPMCP::LogicalSourceFile(PackagePath, TEXT(".animlang"));
	Result.AnimBlueprint.ModuleKind = TEXT("anim");
	Result.AnimBlueprint.ContentHash = FRigLangExporter::ComputeContentHash(AnimText);
	if (bIncludeText) Result.AnimBlueprint.Text = AnimText;
	AnimAST->VisitNodes([&Result](const TSharedPtr<FAnimNodeAST>& Node)
	{
		switch (Node->Coverage)
		{
		case EAnimNodeCoverage::Exact: ++Result.AnimBlueprint.Exact; break;
		case EAnimNodeCoverage::Reflected: ++Result.AnimBlueprint.Reflected; break;
		case EAnimNodeCoverage::Lossy: ++Result.AnimBlueprint.Lossy; break;
		case EAnimNodeCoverage::Unsupported: ++Result.AnimBlueprint.Unsupported; break;
		}
	});

	TArray<FString> RigPaths;
	RigExports.GetKeys(RigPaths);
	RigPaths.Sort();
	bool bRigExportsComplete = true;
	for (const FString& RigPath : RigPaths)
	{
		const FRigLangExportResult& RigExport = RigExports.FindChecked(RigPath);
		if (!RigExport.bSuccess || !RigExport.Module.IsValid())
		{
			bRigExportsComplete = false;
			Result.Warnings.Add(FString::Printf(TEXT("Rig export failed for %s: %s"),
				*RigPath, *FString::Join(RigExport.Errors, TEXT("; "))));
			continue;
		}
		FAnimBP2FPMCPExportDocument Document =
			AnimBP2FPMCP::MakeRigDocument(RigExport, bIncludeText);
		bRigExportsComplete &= Document.Lossy == 0 && Document.Unsupported == 0;
		Result.RigModules.Add(MoveTemp(Document));
	}

	Result.bSuccess = true;
	Result.bComplete = bRigExportsComplete
		&& Result.AnimBlueprint.Lossy == 0
		&& Result.AnimBlueprint.Unsupported == 0;
	if (!Result.bComplete)
	{
		Result.Warnings.Add(TEXT("Bundle exported with lossy or unsupported coverage"));
	}
	return Result;
}

FAnimBP2FPMCPExportBundleResult UAnimBP2FPToolset::ExportControlRig(
	const FString& AssetPath,
	const bool bIncludeText)
{
	FAnimBP2FPMCPExportBundleResult Result;
	FString PackagePath;
	UObject* Asset = AnimBP2FPMCP::LoadAsset(AssetPath, PackagePath, Result.Error);
	if (!Asset) return Result;

	const FRigLangExportResult Export = FRigLangExporter::Export(Asset);
	if (!Export.bSuccess || !Export.Module.IsValid())
	{
		Result.Error = FString::Join(Export.Errors, TEXT("; "));
		if (Result.Error.IsEmpty()) Result.Error = TEXT("Failed to export Control Rig");
		return Result;
	}
	FAnimBP2FPMCPExportDocument Document = AnimBP2FPMCP::MakeRigDocument(Export, bIncludeText);
	Result.RigModules.Add(MoveTemp(Document));
	Result.bSuccess = true;
	Result.bComplete = Result.RigModules[0].Lossy == 0
		&& Result.RigModules[0].Unsupported == 0;
	if (!Result.bComplete)
	{
		Result.Warnings.Add(TEXT("Control Rig exported with lossy or unsupported coverage"));
	}
	return Result;
}

FAnimBP2FPMCPTextResult UAnimBP2FPToolset::ExportEventGraph(
	const FString& AssetPath,
	const FString& GraphName,
	const bool bIncludePositions,
	const bool bStableIds)
{
	FString SafePath, Error;
	if (!AnimBP2FPMCP::CheckEditorContext(Error)
		|| !AnimBP2FPMCP::ValidateAssetPath(AssetPath, SafePath, Error))
	{
		return AnimBP2FPMCP::Failure<FAnimBP2FPMCPTextResult>(Error);
	}
	return AnimBP2FPMCP::FromTextResult(
		UAnimBP2FPPythonBridge::ExportEventGraphToText(
			SafePath, GraphName, bIncludePositions, bStableIds));
}

FAnimBP2FPMCPMappingResult UAnimBP2FPToolset::GetAnimBlueprintSyncState(const FString& AssetPath)
{
	FAnimBP2FPMCPMappingResult Result;
	FString PackagePath;
	if (!AnimBP2FPMCP::CheckEditorContext(Result.Error)
		|| !AnimBP2FPMCP::ValidateAssetPath(AssetPath, PackagePath, Result.Error)) return Result;
	const FBP2FPMappingEntry* Entry = FBP2FPMappingRegistry::Get().FindByBlueprint(PackagePath);
	if (!Entry)
	{
		Result.Error = FString::Printf(TEXT("No mapping found for AnimBlueprint: %s"), *PackagePath);
		return Result;
	}

	Result.bSuccess = true;
	Result.AssetPath = Entry->BlueprintPath;
	Result.DSLFilePath = Entry->DSLFilePath;
	Result.Category = Entry->CategoryTag;
	Result.State = AnimBP2FPMCP::MappingState(Entry->State);
	Result.bBlueprintExists = Entry->bBlueprintExists;
	Result.bDSLExists = Entry->bDSLFileExists;
	return Result;
}

FAnimBP2FPMCPBundleResult UAnimBP2FPToolset::ValidateAnimBundle(
	const TArray<FAnimBP2FPMCPBundleSource>& Sources)
{
	FAnimBP2FPMCPBundleResult Result;
	if (Sources.IsEmpty())
	{
		Result.Error = TEXT("At least one bundle source is required");
		return Result;
	}
	FAnimLispWorkspace Workspace;
	TSet<FString> Names;
	for (const FAnimBP2FPMCPBundleSource& Source : Sources)
	{
		if (!AnimBP2FPMCP::IsSafeSourceName(Source.SourceFile) || Names.Contains(Source.SourceFile))
		{
			Result.Error = TEXT("Each source needs a unique relative .animlang or .riglang logical name");
			return Result;
		}
		Names.Add(Source.SourceFile);
		Workspace.AddSource(Source.SourceFile, Source.Source);
	}
	FAnimLangDiagnostics Diagnostics;
	TArray<FAnimLispImportPlanEntry> Plan;
	Result.bSuccess = Workspace.Build(Diagnostics) && Workspace.BuildImportPlan(Plan, Diagnostics);
	for (const FAnimLispImportPlanEntry& Entry : Plan) Result.ModulePlan.Add(Entry.ModuleId.ToString());
	for (const FAnimLangDiagnostic& Diagnostic : Diagnostics.Items)
		Result.Diagnostics.Add(AnimBP2FPMCP::ToDiagnostic(Diagnostic));
	if (!Result.bSuccess) Result.Error = Diagnostics.ToReport();
	return Result;
}

FAnimBP2FPMCPTextResult UAnimBP2FPToolset::ValidateAnimBlueprintRoundTrip(const FString& AssetPath)
{
	FString SafePath, Error;
	if (!AnimBP2FPMCP::CheckEditorContext(Error)
		|| !AnimBP2FPMCP::ValidateAssetPath(AssetPath, SafePath, Error))
	{
		return AnimBP2FPMCP::Failure<FAnimBP2FPMCPTextResult>(Error);
	}
	return AnimBP2FPMCP::FromTextResult(
		UAnimBP2FPPythonBridge::ValidateAnimBlueprintRoundTrip(SafePath));
}

FAnimBP2FPMCPBundleResult UAnimBP2FPToolset::ApplyAnimBundle(
	const TArray<FAnimBP2FPMCPBundleSource>& Sources,
	const FString& TargetRoot,
	const EAnimBP2FPMCPBundleMode Mode,
	const bool bCommitPersistent)
{
	FAnimBP2FPMCPBundleResult Result;
	FString SafeRoot = TargetRoot.TrimStartAndEnd();
	SafeRoot.RemoveFromEnd(TEXT("/"));
	if (!AnimBP2FPMCP::CheckEditorContext(Result.Error, true)) return Result;
	if (SafeRoot != TEXT("/Game") && (!SafeRoot.StartsWith(TEXT("/Game/"))
		|| SafeRoot.Contains(TEXT("..")) || !FPackageName::IsValidLongPackageName(SafeRoot)))
	{
		Result.Error = TEXT("TargetRoot must be a valid /Game content folder");
		return Result;
	}
	if (Sources.IsEmpty())
	{
		Result.Error = TEXT("At least one bundle source is required");
		return Result;
	}

	TArray<FAnimLispBundleSource> BundleSources;
	BundleSources.Reserve(Sources.Num());
	TSet<FString> SourceNames;
	for (int32 Index = 0; Index < Sources.Num(); ++Index)
	{
		const FAnimBP2FPMCPBundleSource& Source = Sources[Index];
		const FString SourceName = Source.SourceFile.IsEmpty()
			? FString::Printf(TEXT("mcp-source-%d.animlang"), Index)
			: Source.SourceFile;
		if (!AnimBP2FPMCP::IsSafeSourceName(SourceName))
		{
			Result.Error = FString::Printf(
				TEXT("Invalid logical bundle source name: %s"), *SourceName);
			return Result;
		}
		if (SourceNames.Contains(SourceName))
		{
			Result.Error = FString::Printf(TEXT("Duplicate bundle source name: %s"), *SourceName);
			return Result;
		}
		SourceNames.Add(SourceName);
		BundleSources.Add({SourceName, Source.Source});
	}

	FAnimLispBundleImportOptions Options;
	Options.Mode = Mode == EAnimBP2FPMCPBundleMode::Strict
		? EAnimLispBundleImportMode::Strict
		: EAnimLispBundleImportMode::Legacy;
	Options.TargetRoot = SafeRoot;
	Options.bCommitPersistent = bCommitPersistent;

	const FAnimLispBundleImportResult ImportResult = FAnimBPImporter::ImportBundle(BundleSources, Options);
	Result.bSuccess = ImportResult.bSuccess;
	Result.bMutationStarted = ImportResult.bMutationStarted;
	Result.bPersisted = bCommitPersistent && ImportResult.bSuccess;
	for (const FAnimLispImportPlanEntry& Entry : ImportResult.Plan)
	{
		Result.ModulePlan.Add(Entry.ModuleId.ToString());
	}
	for (UObject* Asset : ImportResult.StagedAssets)
	{
		if (Asset)
		{
			Result.AssetPaths.Add(Asset->GetPathName());
		}
	}
	for (const FAnimLangDiagnostic& Diagnostic : ImportResult.Diagnostics.Items)
	{
		Result.Diagnostics.Add(AnimBP2FPMCP::ToDiagnostic(Diagnostic));
	}
	if (!Result.bSuccess)
	{
		Result.Error = ImportResult.Diagnostics.ToReport();
		if (Result.Error.IsEmpty())
		{
			Result.Error = TEXT("Anim/Rig bundle import failed");
		}
	}
	return Result;
}

FAnimBP2FPMCPChangeResult UAnimBP2FPToolset::UpdateAnimBlueprint(
	const FString& AssetPath,
	const FString& DSLText,
	const bool bSavePackage)
{
	FString SafePath, Error;
	if (!AnimBP2FPMCP::CheckEditorContext(Error, true)
		|| !AnimBP2FPMCP::ValidateAssetPath(AssetPath, SafePath, Error))
	{
		return AnimBP2FPMCP::Failure<FAnimBP2FPMCPChangeResult>(Error);
	}
	return AnimBP2FPMCP::FromChangeResult(
		UAnimBP2FPPythonBridge::UpdateAnimBlueprintFromText(SafePath, DSLText, bSavePackage),
		bSavePackage);
}

FAnimBP2FPMCPChangeResult UAnimBP2FPToolset::ReplaceEventGraph(
	const FString& AssetPath,
	const FString& GraphName,
	const FString& DSLText,
	const bool bCompile,
	const bool bSavePackage)
{
	FString SafePath, Error;
	if (!AnimBP2FPMCP::CheckEditorContext(Error, true)
		|| !AnimBP2FPMCP::ValidateAssetPath(AssetPath, SafePath, Error))
	{
		return AnimBP2FPMCP::Failure<FAnimBP2FPMCPChangeResult>(Error);
	}
	return AnimBP2FPMCP::FromChangeResult(
		UAnimBP2FPPythonBridge::ImportEventGraphFromText(
			SafePath, GraphName, DSLText, bCompile, bSavePackage),
		bSavePackage);
}

FAnimBP2FPMCPChangeResult UAnimBP2FPToolset::MergeEventGraph(
	const FString& AssetPath,
	const FString& GraphName,
	const FString& DSLText,
	const bool bCompile,
	const bool bSavePackage)
{
	FString SafePath, Error;
	if (!AnimBP2FPMCP::CheckEditorContext(Error, true)
		|| !AnimBP2FPMCP::ValidateAssetPath(AssetPath, SafePath, Error))
	{
		return AnimBP2FPMCP::Failure<FAnimBP2FPMCPChangeResult>(Error);
	}
	return AnimBP2FPMCP::FromChangeResult(
		UAnimBP2FPPythonBridge::UpdateEventGraphFromText(
			SafePath, GraphName, DSLText, bCompile, bSavePackage),
		bSavePackage);
}

#endif
