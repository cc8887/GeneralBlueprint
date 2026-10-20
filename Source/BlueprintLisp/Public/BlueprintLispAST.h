// Copyright (c) 2026 cc8887. All Rights Reserved.
// BlueprintLispAST.h - S-expression AST and Parser for Blueprint DSL
//
// Derived from ECABridge/BlueprintLisp.h (Epic Games, Experimental)
// Original author: Jon Olick
//
// Syntax overview:
//   (event ReceiveBeginPlay
//     (call-parent ReceiveBeginPlay)
//     (let player (GetPlayerCharacter 0))
//     (branch (IsValid player)
//       :true  (PrintString "Hello")
//       :false (PrintString "No player")))
//
// Core forms:
//   (event Name ...)                 - Event node
//   (func Name ...)                  - FunctionGraph declaration
//   (function Name ...)              - FunctionGraph body
//   (macro Name ...)                 - MacroGraph body
//   (call-parent Name [:pin value])  - Parent override call in EventGraph
//   (call-macro Name [:pin value])   - Macro instance call
//   (let var expr)                   - Local variable / reusable value alias
//   (set var expr)                   - Set variable
//   (seq ...)                        - Execution sequence
//   (branch cond :true :false)       - Branch node
//   (foreach item coll ...)          - ForEach loop
//   (call target func args...)       - Call method on object
//   (delay seconds)                  - Delay node
//   (cast Type var body)             - Cast with success exec
//   (vec x y z)                      - Vector literal
//   (rot r p y)                      - Rotator literal
//
// Naming rule:
//   Top-level event/function/macro names and call-parent/call-macro targets may use
//   string atoms when the name contains spaces, e.g. (function "Villager Select" ...).


#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

// ----------------------------------------------------------------------------------
// S-expression AST
// ----------------------------------------------------------------------------------

/** Token types */
enum class ELispNodeType : uint8
{
	Nil,        // Empty / null
	Symbol,     // Identifier: foo, BeginPlay, GetHealth
	Keyword,    // Keyword:   :true, :false, :pin-name
	Number,     // Numeric literal: 42, 3.14
	String,     // String literal: "hello"
	List,       // List: (a b c)
};

struct FLispNode;
using FLispNodePtr = TSharedPtr<FLispNode>;

/** A node in the S-expression AST */
struct BLUEPRINTLISP_API FLispNode
{
	ELispNodeType Type = ELispNodeType::Nil;

	FString StringValue;        // Symbol / Keyword / String
	double  NumberValue = 0.0;  // Number
	TArray<FLispNodePtr> Children; // List

	// Source location (for diagnostics)
	int32 Line   = 0;
	int32 Column = 0;

	FLispNode() = default;

	// --- Factory methods ---
	static FLispNodePtr MakeNil();
	static FLispNodePtr MakeSymbol(const FString& Name);
	static FLispNodePtr MakeKeyword(const FString& Name);
	static FLispNodePtr MakeNumber(double Value);
	static FLispNodePtr MakeString(const FString& Value);
	static FLispNodePtr MakeList(const TArray<FLispNodePtr>& Items = {});

	// --- Type predicates ---
	bool IsNil()     const { return Type == ELispNodeType::Nil;     }
	bool IsSymbol()  const { return Type == ELispNodeType::Symbol;  }
	bool IsKeyword() const { return Type == ELispNodeType::Keyword; }
	bool IsNumber()  const { return Type == ELispNodeType::Number;  }
	bool IsString()  const { return Type == ELispNodeType::String;  }
	bool IsList()    const { return Type == ELispNodeType::List;    }

	/** True if this is a list whose first element is the given symbol */
	bool IsForm(const FString& FormName) const;

	/** First element of a list if it is a symbol; empty string otherwise */
	FString GetFormName() const;

	// --- List accessors ---
	int32       Num()              const { return Children.Num(); }
	FLispNodePtr operator[](int32 Index) const;
	FLispNodePtr Get(int32 Index)  const;

	/** Find value of keyword argument: (foo :key value) -> value for ":key" */
	FLispNodePtr GetKeywordArg(const FString& Keyword) const;
	bool         HasKeyword(const FString& Keyword)    const;

	/** Serialize to string */
	FString ToString(bool bPretty = false, int32 IndentLevel = 0) const;
};

// ----------------------------------------------------------------------------------
// Parser
// ----------------------------------------------------------------------------------

/** Result returned by FLispParser::Parse() */
struct BLUEPRINTLISP_API FLispParseResult
{
	bool bSuccess = false;
	FString Error;
	int32 ErrorLine   = 0;
	int32 ErrorColumn = 0;
	TArray<FLispNodePtr> Nodes; // Top-level expressions

	static FLispParseResult Success(const TArray<FLispNodePtr>& Nodes);
	static FLispParseResult Failure(const FString& Error, int32 Line = 0, int32 Column = 0);
};

/** Recursive-descent S-expression parser */
class BLUEPRINTLISP_API FLispParser
{
public:
	/** Parse a string containing one or more S-expressions */
	static FLispParseResult Parse(const FString& Source);

private:
	FLispParser(const FString& Source);

	FLispParseResult  DoParse();
	FLispNodePtr      ParseExpr();
	FLispNodePtr      ParseList();
	FLispNodePtr      ParseAtom();
	FLispNodePtr      ParseString();
	FLispNodePtr      ParseNumber();
	FLispNodePtr      ParseSymbolOrKeyword();

	void  SkipWhitespaceAndComments();
	TCHAR Peek()   const;
	TCHAR Advance();
	bool  IsAtEnd() const;
	bool  Match(TCHAR Expected);

	FLispParseResult MakeError(const FString& Message);

	const FString& Source;
	int32 Position = 0;
	int32 Line     = 1;
	int32 Column   = 1;
};

// ----------------------------------------------------------------------------------
// Utility helpers
// ----------------------------------------------------------------------------------

namespace BlueprintLisp
{
	/** Re-format Lisp code with proper indentation */
	BLUEPRINTLISP_API FString PrettyPrint(const FString& LispCode);

	/** Minify Lisp code (remove extra whitespace and comments) */
	BLUEPRINTLISP_API FString Minify(const FString& LispCode);

	/** Extract all symbol names from Lisp code */
	BLUEPRINTLISP_API TArray<FString> ExtractSymbols(const FString& LispCode);

	/** Return true if Name is a valid symbol identifier */
	BLUEPRINTLISP_API bool IsValidSymbol(const FString& Name);
}
