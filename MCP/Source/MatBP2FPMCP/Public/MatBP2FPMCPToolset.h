// MatBP2FPMCPToolset.h - UE 5.8 native MCP tool surface.
// Copyright (c) 2026 cc8887. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "MatBP2FPPythonBridge.h"
#include "ToolsetRegistry/ToolsetDefinition.h"
#include "MatBP2FPMCPToolset.generated.h"

USTRUCT(BlueprintType)
struct MATBP2FPMCP_API FMatBP2FPMCPDiagnostic
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="MatBP2FP|MCP") FString RuleId;
	UPROPERTY(BlueprintReadOnly, Category="MatBP2FP|MCP") FString Severity;
	UPROPERTY(BlueprintReadOnly, Category="MatBP2FP|MCP") FString Message;
	UPROPERTY(BlueprintReadOnly, Category="MatBP2FP|MCP") int32 Line = 0;
	UPROPERTY(BlueprintReadOnly, Category="MatBP2FP|MCP") int32 Column = 0;
};

USTRUCT(BlueprintType)
struct MATBP2FPMCP_API FMatBP2FPMCPLintResult
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category="MatBP2FP|MCP") bool bSuccess = false;
	UPROPERTY(BlueprintReadOnly, Category="MatBP2FP|MCP") FString CanonicalText;
	UPROPERTY(BlueprintReadOnly, Category="MatBP2FP|MCP") TArray<FMatBP2FPMCPDiagnostic> Diagnostics;
};

/**
 * Read-only material and MatLang operations exposed through UE 5.8 MCP.
 * MCP clients should inspect a material before calling an edit tool.
 */
UCLASS(BlueprintType, Hidden)
class MATBP2FPMCP_API UMatBP2FPInspectToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override
	{
		return TEXT("1.1.0");
	}

	/** Export a project UMaterial to MatLang DSL text without modifying assets. */
	UFUNCTION(meta=(AICallable, AIAccessMode="Read"), Category="MatBP2FP|Inspect")
	static FMatBP2FPPythonResult ExportMaterialToText(const FString& MaterialPath);

	/** Export a project UMaterialFunction to in-memory MatLang text. Function import is not supported by this toolset. */
	UFUNCTION(meta=(AICallable, AIAccessMode="Read"), Category="MatBP2FP|Inspect")
	static FMatBP2FPPythonResult ExportMaterialFunctionToText(const FString& FunctionPath);

	/** Parse and lint MatLang syntax, IDs and graph references without loading or modifying assets. Does not compile shaders. */
	UFUNCTION(meta=(AICallable, AIAccessMode="Read"), Category="MatBP2FP|Inspect")
	static FMatBP2FPMCPLintResult ValidateMatLang(const FString& DSLText);

	/** Validate the export/parse round trip for a project UMaterial. */
	UFUNCTION(meta=(AICallable, AIAccessMode="Read"), Category="MatBP2FP|Inspect")
	static FMatBP2FPPythonResult ValidateMaterialRoundTrip(const FString& MaterialPath);

	/** Return the current Material-to-DSL mapping registry. */
	UFUNCTION(meta=(AICallable, AIAccessMode="Read"), Category="MatBP2FP|Inspect")
	static FMatBP2FPPythonResult GetMappingTable();

	/** Look up one project Material in the Material-to-DSL mapping registry. */
	UFUNCTION(meta=(AICallable, AIAccessMode="Read"), Category="MatBP2FP|Inspect")
	static FMatBP2FPPythonResult FindMappingByMaterial(const FString& MaterialPath);

	/** Convert a project Material path to its Saved/BP2DSL file path. */
	UFUNCTION(meta=(AICallable, AIAccessMode="Read"), Category="MatBP2FP|Inspect")
	static FMatBP2FPPythonResult MaterialPathToDSLPath(const FString& MaterialPath);
};

/**
 * Explicitly mutating material operations exposed through UE 5.8 MCP.
 * save_package is a required argument so persistence is always explicit.
 */
UCLASS(BlueprintType, Hidden)
class MATBP2FPMCP_API UMatBP2FPEditToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	virtual FString GetToolsetVersion() const override
	{
		return TEXT("1.1.0");
	}

	/** Import DSL text as a new project UMaterial. */
	UFUNCTION(meta=(AICallable, AIAccessMode="Write"), Category="MatBP2FP|Edit")
	static FMatBP2FPPythonResult ImportMaterialFromText(
		const FString& DSLText, const FString& DestinationFolder, bool bSavePackage);

	/** Update a project UMaterial from DSL text using incremental patch when possible. */
	UFUNCTION(meta=(AICallable, AIAccessMode="Write"), Category="MatBP2FP|Edit")
	static FMatBP2FPPythonResult UpdateMaterialFromText(
		const FString& MaterialPath, const FString& DSLText, bool bSavePackage);
};
