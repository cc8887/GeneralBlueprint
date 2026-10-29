// MatBP2FPMCPToolset.cpp - UE 5.8 native MCP tool surface.
// Copyright (c) 2026 cc8887. All Rights Reserved.

#include "MatBP2FPMCPToolset.h"
#include "Editor.h"
#include "MatBPExporter.h"
#include "MatLangLinter.h"
#include "Materials/MaterialFunction.h"
#include "Misc/PackageName.h"

#if defined(MATBP2FP_WITH_MCP) && MATBP2FP_WITH_MCP

namespace MatBP2FPMCP
{
	static bool CheckEditorContext(FString& OutError, bool bMutation = false)
	{
		if (!IsInGameThread())
		{
			OutError = TEXT("MatBP2FP tools must run on the editor game thread");
			return false;
		}
		if (bMutation && GEditor && GEditor->PlayWorld)
		{
			OutError = TEXT("Stop Play In Editor before changing material assets");
			return false;
		}
		return true;
	}

	static FMatBP2FPPythonResult MakeFailure(const FString& Message)
	{
		FMatBP2FPPythonResult Result;
		Result.bSuccess = false;
		Result.Message = Message;
		return Result;
	}

	static bool IsSafeProjectAssetPath(const FString& InPath, FString& OutPath, FString& OutError)
	{
		OutPath = InPath.TrimStartAndEnd();
		if (OutPath.IsEmpty())
		{
			OutError = TEXT("Asset path is empty");
			return false;
		}

		if (!OutPath.StartsWith(TEXT("/Game/")) ||
			OutPath.Contains(TEXT("..")) ||
			OutPath.Contains(TEXT("\\")) ||
			OutPath.Contains(TEXT("'")) || OutPath.Contains(TEXT(":")) ||
			!FPackageName::IsValidLongPackageName(FPackageName::ObjectPathToPackageName(OutPath)) ||
			(OutPath.Contains(TEXT(".")) && !FPackageName::IsValidObjectPath(OutPath)))
		{
			OutError = FString::Printf(
				TEXT("Only /Game asset paths without traversal are allowed: %s"), *OutPath);
			return false;
		}
		if (OutPath.Contains(TEXT(".")))
		{
			const FString PackagePath = FPackageName::ObjectPathToPackageName(OutPath);
			if (OutPath != PackagePath + TEXT(".") + FPackageName::GetLongPackageAssetName(PackagePath))
			{
				OutError = TEXT("Object path must name the asset matching its package leaf");
				return false;
			}
		}
		return true;
	}

	static bool IsSafeProjectFolder(const FString& InPath, FString& OutPath, FString& OutError)
	{
		OutPath = InPath.TrimStartAndEnd();
		OutPath.RemoveFromEnd(TEXT("/"));
		if (OutPath == TEXT("/Game")) return true;
		if (!IsSafeProjectAssetPath(OutPath, OutPath, OutError))
		{
			return false;
		}

		if (OutPath.Contains(TEXT(".")))
		{
			OutError = FString::Printf(
				TEXT("DestinationFolder must be a content folder, not an object path: %s"), *OutPath);
			return false;
		}
		return true;
	}

	static bool ParseMaterial(const FString& DSLText, TSharedPtr<FMaterialGraphAST>& OutAST, FString& OutError)
	{
		const FMatLangParseResult Parsed = FMatLangParser::ParseDocument(DSLText);
		if (!Parsed.AST.IsValid() || Parsed.HasErrors())
		{
			OutError = TEXT("Invalid MatLang; call ValidateMatLang for source diagnostics");
			return false;
		}
		if (Parsed.AST->Kind != EMatLangGraphKind::Material)
		{
			OutError = TEXT("Material editing accepts a material document; material-function import is unsupported");
			return false;
		}
		OutAST = Parsed.AST;
		return true;
	}

	static FMatBP2FPPythonResult RequireSaved(FMatBP2FPPythonResult Result, const bool bSavePackage)
	{
		if (Result.bSuccess && bSavePackage && !Result.bSavedPackage)
		{
			Result.bSuccess = false;
			Result.Message += TEXT("; requested package persistence did not succeed");
		}
		return Result;
	}
}

FMatBP2FPPythonResult UMatBP2FPInspectToolset::ExportMaterialToText(const FString& MaterialPath)
{
	FString SafePath;
	FString Error;
	if (!MatBP2FPMCP::CheckEditorContext(Error)
		|| !MatBP2FPMCP::IsSafeProjectAssetPath(MaterialPath, SafePath, Error))
	{
		return MatBP2FPMCP::MakeFailure(Error);
	}
	return UMatBP2FPPythonBridge::ExportMaterialToText(SafePath);
}

FMatBP2FPPythonResult UMatBP2FPInspectToolset::ExportMaterialFunctionToText(const FString& FunctionPath)
{
	FString SafePath, Error;
	if (!MatBP2FPMCP::CheckEditorContext(Error)
		|| !MatBP2FPMCP::IsSafeProjectAssetPath(FunctionPath, SafePath, Error))
	{
		return MatBP2FPMCP::MakeFailure(Error);
	}
	const FString PackagePath = FPackageName::ObjectPathToPackageName(SafePath);
	const FString ObjectPath = PackagePath + TEXT(".") + FPackageName::GetLongPackageAssetName(PackagePath);
	UMaterialFunction* Function = LoadObject<UMaterialFunction>(nullptr, *ObjectPath);
	if (!Function) return MatBP2FPMCP::MakeFailure(TEXT("Asset is not a loadable UMaterialFunction"));
	FMatBP2FPPythonResult Result;
	Result.AssetPath = Function->GetPathName();
	Result.DSLText = FMatBPExporter::ExportMaterialFunctionToString(Function);
	Result.bSuccess = !Result.DSLText.IsEmpty();
	Result.Message = Result.bSuccess ? TEXT("Exported Material Function to MatLang") : TEXT("Material Function export failed");
	return Result;
}

FMatBP2FPMCPLintResult UMatBP2FPInspectToolset::ValidateMatLang(const FString& DSLText)
{
	const FMatLangLintResult Lint = FMatLangLinter::Lint(DSLText);
	FMatBP2FPMCPLintResult Result;
	Result.bSuccess = Lint.AST.IsValid() && !Lint.HasErrors();
	if (Lint.AST.IsValid()) Result.CanonicalText = Lint.AST->ToString();
	for (const FMatLangDiagnostic& Diagnostic : Lint.Diagnostics)
	{
		FMatBP2FPMCPDiagnostic Item;
		Item.RuleId = Diagnostic.RuleId;
		Item.Message = Diagnostic.Message;
		Item.Severity = Diagnostic.Severity == EMatLangDiagnosticSeverity::Error ? TEXT("error")
			: Diagnostic.Severity == EMatLangDiagnosticSeverity::Warning ? TEXT("warning") : TEXT("info");
		Item.Line = Diagnostic.Span.StartLine;
		Item.Column = Diagnostic.Span.StartColumn;
		Result.Diagnostics.Add(MoveTemp(Item));
	}
	return Result;
}

FMatBP2FPPythonResult UMatBP2FPInspectToolset::ValidateMaterialRoundTrip(const FString& MaterialPath)
{
	FString SafePath;
	FString Error;
	if (!MatBP2FPMCP::CheckEditorContext(Error)
		|| !MatBP2FPMCP::IsSafeProjectAssetPath(MaterialPath, SafePath, Error))
	{
		return MatBP2FPMCP::MakeFailure(Error);
	}
	return UMatBP2FPPythonBridge::ValidateMaterialRoundTrip(SafePath);
}

FMatBP2FPPythonResult UMatBP2FPInspectToolset::GetMappingTable()
{
	FString Error;
	if (!MatBP2FPMCP::CheckEditorContext(Error)) return MatBP2FPMCP::MakeFailure(Error);
	return UMatBP2FPPythonBridge::GetMappingTable();
}

FMatBP2FPPythonResult UMatBP2FPInspectToolset::FindMappingByMaterial(const FString& MaterialPath)
{
	FString SafePath;
	FString Error;
	if (!MatBP2FPMCP::CheckEditorContext(Error)
		|| !MatBP2FPMCP::IsSafeProjectAssetPath(MaterialPath, SafePath, Error))
	{
		return MatBP2FPMCP::MakeFailure(Error);
	}
	return UMatBP2FPPythonBridge::FindMappingByMaterial(SafePath);
}

FMatBP2FPPythonResult UMatBP2FPInspectToolset::MaterialPathToDSLPath(const FString& MaterialPath)
{
	FString SafePath;
	FString Error;
	if (!MatBP2FPMCP::IsSafeProjectAssetPath(MaterialPath, SafePath, Error))
	{
		return MatBP2FPMCP::MakeFailure(Error);
	}
	return UMatBP2FPPythonBridge::MaterialPathToDSLPath(SafePath);
}

FMatBP2FPPythonResult UMatBP2FPEditToolset::ImportMaterialFromText(
	const FString& DSLText, const FString& DestinationFolder, bool bSavePackage)
{
	FString SafeFolder;
	FString Error;
	if (!MatBP2FPMCP::CheckEditorContext(Error, true)
		|| !MatBP2FPMCP::IsSafeProjectFolder(DestinationFolder, SafeFolder, Error))
	{
		return MatBP2FPMCP::MakeFailure(Error);
	}
	TSharedPtr<FMaterialGraphAST> AST;
	if (!MatBP2FPMCP::ParseMaterial(DSLText, AST, Error)) return MatBP2FPMCP::MakeFailure(Error);
	const FString TargetPackage = SafeFolder / AST->Name;
	if (AST->Name.IsEmpty() || AST->Name.Contains(TEXT("/")) || AST->Name.Contains(TEXT("\\"))
		|| !FPackageName::IsValidLongPackageName(TargetPackage))
	{
		return MatBP2FPMCP::MakeFailure(TEXT("Material name must be a valid asset leaf name"));
	}
	if (FindPackage(nullptr, *TargetPackage) || FPackageName::DoesPackageExist(TargetPackage))
	{
		return MatBP2FPMCP::MakeFailure(TEXT("Import target already exists; use UpdateMaterialFromText to edit it"));
	}
	return MatBP2FPMCP::RequireSaved(
		UMatBP2FPPythonBridge::ImportMaterialFromText(DSLText, SafeFolder, bSavePackage), bSavePackage);
}

FMatBP2FPPythonResult UMatBP2FPEditToolset::UpdateMaterialFromText(
	const FString& MaterialPath, const FString& DSLText, bool bSavePackage)
{
	FString SafePath;
	FString Error;
	if (!MatBP2FPMCP::CheckEditorContext(Error, true)
		|| !MatBP2FPMCP::IsSafeProjectAssetPath(MaterialPath, SafePath, Error))
	{
		return MatBP2FPMCP::MakeFailure(Error);
	}
	TSharedPtr<FMaterialGraphAST> AST;
	if (!MatBP2FPMCP::ParseMaterial(DSLText, AST, Error)) return MatBP2FPMCP::MakeFailure(Error);
	return MatBP2FPMCP::RequireSaved(
		UMatBP2FPPythonBridge::UpdateMaterialFromText(SafePath, DSLText, bSavePackage), bSavePackage);
}

#endif // MATBP2FP_WITH_MCP
