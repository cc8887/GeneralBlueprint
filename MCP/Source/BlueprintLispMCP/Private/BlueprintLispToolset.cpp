// BlueprintLispToolset.cpp - UE 5.8 native MCP Toolset adapter

#include "BlueprintLispToolset.h"

#include "Editor.h"
#include "Misc/PackageName.h"

namespace BlueprintLispMCP
{
	static FBlueprintLispPythonResult Failure(const FString& Error)
	{
		FBlueprintLispPythonResult Result;
		Result.Message = Error;
		return Result;
	}

	static bool CheckContext(bool bMutating, FString& OutError)
	{
		if (!IsInGameThread())
		{
			OutError = TEXT("Blueprint tools must run on the editor game thread.");
			return false;
		}
		if (bMutating && GEditor && GEditor->PlayWorld)
		{
			OutError = TEXT("Stop Play In Editor or Simulate In Editor before modifying Blueprint assets.");
			return false;
		}
		return true;
	}

	static bool CheckAssetPath(const FString& AssetPath, bool bMutating, FString& OutPath, FString& OutError)
	{
		if (!CheckContext(bMutating, OutError))
		{
			return false;
		}
		OutPath = AssetPath.TrimStartAndEnd();
		if (!OutPath.StartsWith(TEXT("/Game/")) || OutPath.Contains(TEXT("..")) ||
			OutPath.Contains(TEXT("\\")) || OutPath.Contains(TEXT("'")) || OutPath.Contains(TEXT(":")))
		{
			OutError = TEXT("Use a /Game/ package or object path without traversal or subobjects.");
			return false;
		}
		const FString PackagePath = FPackageName::ObjectPathToPackageName(OutPath);
		if (!FPackageName::IsValidLongPackageName(PackagePath))
		{
			OutError = TEXT("AssetPath is not a valid /Game/ package path.");
			return false;
		}
		if (OutPath.Contains(TEXT(".")) &&
			(!FPackageName::IsValidObjectPath(OutPath) ||
			FPackageName::ObjectPathToObjectName(OutPath) != FPackageName::GetLongPackageAssetName(PackagePath)))
		{
			OutError = TEXT("Use the Blueprint asset object path, not a generated class or subobject path.");
			return false;
		}
		return true;
	}

	static bool CheckGraph(const FString& GraphName, FString& OutError)
	{
		if (GraphName.TrimStartAndEnd().IsEmpty())
		{
			OutError = TEXT("GraphName is empty; list Blueprint graphs to choose a graph.");
			return false;
		}
		return true;
	}
}

FBlueprintLispPythonResult UBlueprintLispToolset::ExportBlueprintGraph(
	const FString& AssetPath,
	const FString& GraphName,
	bool bIncludePositions,
	bool bStableIds)
{
	FString SafePath, Error;
	if (!BlueprintLispMCP::CheckAssetPath(AssetPath, false, SafePath, Error) ||
		!BlueprintLispMCP::CheckGraph(GraphName, Error))
	{
		return BlueprintLispMCP::Failure(Error);
	}
	return UBlueprintLispPythonBridge::ExportGraphToText(
		SafePath, GraphName, bIncludePositions, bStableIds);
}

FBlueprintLispPythonResult UBlueprintLispToolset::ListBlueprintGraphs(const FString& AssetPath)
{
	FString SafePath, Error;
	if (!BlueprintLispMCP::CheckAssetPath(AssetPath, false, SafePath, Error))
	{
		return BlueprintLispMCP::Failure(Error);
	}
	return UBlueprintLispPythonBridge::ListGraphs(SafePath);
}

FBlueprintLispPythonResult UBlueprintLispToolset::ListBlueprintMemberVariables(const FString& AssetPath)
{
	FString SafePath, Error;
	if (!BlueprintLispMCP::CheckAssetPath(AssetPath, false, SafePath, Error))
	{
		return BlueprintLispMCP::Failure(Error);
	}
	return UBlueprintLispPythonBridge::ListMemberVariables(SafePath);
}

FBlueprintLispPythonResult UBlueprintLispToolset::InspectBlueprintMemberVariable(
	const FString& AssetPath,
	const FString& VariableName)
{
	FString SafePath, Error;
	if (!BlueprintLispMCP::CheckAssetPath(AssetPath, false, SafePath, Error))
	{
		return BlueprintLispMCP::Failure(Error);
	}
	if (VariableName.TrimStartAndEnd().IsEmpty())
	{
		return BlueprintLispMCP::Failure(TEXT("VariableName is empty."));
	}
	return UBlueprintLispPythonBridge::InspectMemberVariable(SafePath, VariableName);
}

FBlueprintLispPythonResult UBlueprintLispToolset::ValidateBlueprintLisp(const FString& DSLText)
{
	FString Error;
	if (!BlueprintLispMCP::CheckContext(false, Error))
	{
		return BlueprintLispMCP::Failure(Error);
	}
	return UBlueprintLispPythonBridge::ValidateDSL(DSLText);
}

FBlueprintLispPythonResult UBlueprintLispToolset::ReplaceBlueprintGraph(
	const FString& AssetPath,
	const FString& GraphName,
	const FString& DSLText,
	bool bCompile,
	bool bSavePackage)
{
	FString SafePath, Error;
	if (!BlueprintLispMCP::CheckAssetPath(AssetPath, true, SafePath, Error) ||
		!BlueprintLispMCP::CheckGraph(GraphName, Error))
	{
		return BlueprintLispMCP::Failure(Error);
	}
	return UBlueprintLispPythonBridge::ImportGraphFromText(
		SafePath,
		GraphName,
		DSLText,
		EBlueprintLispPythonImportMode::ReplaceGraph,
		bCompile,
		bSavePackage);
}

FBlueprintLispPythonResult UBlueprintLispToolset::MergeBlueprintGraph(
	const FString& AssetPath,
	const FString& GraphName,
	const FString& DSLText,
	bool bCompile,
	bool bSavePackage)
{
	FString SafePath, Error;
	if (!BlueprintLispMCP::CheckAssetPath(AssetPath, true, SafePath, Error) ||
		!BlueprintLispMCP::CheckGraph(GraphName, Error))
	{
		return BlueprintLispMCP::Failure(Error);
	}
	return UBlueprintLispPythonBridge::ImportGraphFromText(
		SafePath,
		GraphName,
		DSLText,
		EBlueprintLispPythonImportMode::MergeAppend,
		bCompile,
		bSavePackage);
}
