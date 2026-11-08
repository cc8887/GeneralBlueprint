// Copyright (c) 2026 cc8887. All Rights Reserved.
#include "Tests/BlueprintLispDelegateTestTypes.h"
#include "UObject/UnrealType.h"

bool UBlueprintLispDelegateTestObject::SetMulticastBinding(UObject* Emitter, const FString& DelegateName,
	UObject* Receiver, const FString& Callback, bool bRemove)
{
	if (!Emitter || !Receiver) return false;
	auto* Property = FindFProperty<FMulticastDelegateProperty>(Emitter->GetClass(), FName(*DelegateName));
	UFunction* Function = Receiver->FindFunction(FName(*Callback));
	if (!Property || !Property->SignatureFunction || !Function
		|| !Function->IsSignatureCompatibleWith(Property->SignatureFunction)) return false;
	FScriptDelegate Binding; Binding.BindUFunction(Receiver, FName(*Callback));
	void* Value = Property->ContainerPtrToValuePtr<void>(Emitter);
	if (bRemove) Property->RemoveDelegate(Binding, Emitter, Value);
	else Property->AddDelegate(Binding, Emitter, Value);
	return true;
}

#if WITH_DEV_AUTOMATION_TESTS && (ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8))
#include "BlueprintLispConverter.h"
#include "Misc/AutomationTest.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EdGraph/EdGraph.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_CallFunction.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "GameplayTagsManager.h"

static FGameplayTag TestDamageTag()
{
	// UE 5.8 permits direct native additions after startup. Register only when
	// running a fixture; FNativeGameplayTag startup sources require a Runtime
	// module and must not be declared by this Editor test module.
	static const FGameplayTag Tag = UGameplayTagsManager::Get().AddNativeGameplayTag(TEXT("BlueprintLisp.Tests.Damage"));
	return Tag;
}

namespace BlueprintLispDelegateTests
{
struct FFixture { UBlueprint* BP; UEdGraph* Graph; UK2Node_FunctionEntry* Entry; };
static FFixture Fixture()
{
	TestDamageTag();
	FFixture F = {};
	F.BP = FKismetEditorUtilities::CreateBlueprint(UBlueprintLispDelegateTestObject::StaticClass(), GetTransientPackage(),
		MakeUniqueObjectName(GetTransientPackage(), UBlueprint::StaticClass(), TEXT("BL_Delegate")),
		BPTYPE_Normal, UBlueprint::StaticClass(), UBlueprintGeneratedClass::StaticClass());
	F.Graph = FBlueprintEditorUtils::CreateNewGraph(F.BP, TEXT("Run"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddFunctionGraph<UFunction>(F.BP, F.Graph, true, nullptr);
	for (UEdGraphNode* N : F.Graph->Nodes) if (auto* E = Cast<UK2Node_FunctionEntry>(N)) F.Entry = E;
	return F;
}
static FBlueprintLispConverter::FImportOptions Options(bool bMerge = false, bool bCompile = true)
{
	FBlueprintLispConverter::FImportOptions O; O.bAutoLayout = false; O.bCompile = bCompile;
	O.ImportMode = bMerge ? FBlueprintLispConverter::EImportMode::MergeAppend : FBlueprintLispConverter::EImportMode::ReplaceGraph;
	return O;
}
static bool Link(UEdGraphPin* A, UEdGraphPin* B)
{ return A && B && GetDefault<UEdGraphSchema_K2>()->TryCreateConnection(A, B); }
static UK2Node_CallDelegate* Broadcast(const FFixture& F, const TCHAR* Name, bool bExternal = false)
{
	auto* N = NewObject<UK2Node_CallDelegate>(F.Graph);
	N->SetFromProperty(FindFProperty<FMulticastDelegateProperty>(UBlueprintLispDelegateTestObject::StaticClass(), Name),
		!bExternal, UBlueprintLispDelegateTestObject::StaticClass());
	F.Graph->AddNode(N, false, false); N->CreateNewGuid(); N->AllocateDefaultPins(); return N;
}
static UK2Node_CallFunction* Call(const FFixture& F, const TCHAR* Name)
{
	auto* N = NewObject<UK2Node_CallFunction>(F.Graph);
	N->SetFromFunction(UBlueprintLispDelegateTestObject::StaticClass()->FindFunctionByName(Name));
	F.Graph->AddNode(N, false, false); N->CreateNewGuid(); N->AllocateDefaultPins(); return N;
}
static UK2Node_VariableGet* Get(const FFixture& F, const TCHAR* Name, bool bExternal = false)
{
	auto* N = NewObject<UK2Node_VariableGet>(F.Graph);
	if (bExternal) N->VariableReference.SetExternalMember(Name, UBlueprintLispDelegateTestObject::StaticClass());
	else N->VariableReference.SetSelfMember(Name);
	F.Graph->AddNode(N, false, false); N->CreateNewGuid(); N->AllocateDefaultPins(); return N;
}
struct FRuntime
{
	UBlueprintLispDelegateTestObject* Root;
	UBlueprintLispDelegateTestObject* Target;
	UBlueprintLispDelegateTestObject* First;
	UBlueprintLispDelegateTestObject* Second;
};
static FRuntime Run(const FFixture& F)
{
	FRuntime R = {};
	R.Root = NewObject<UBlueprintLispDelegateTestObject>(GetTransientPackage(), F.BP->GeneratedClass);
	R.Root->DamageRule = TestDamageTag();
	R.Root->Other = NewObject<UBlueprintLispDelegateTestObject>();
	R.Target = R.Root->Other->Other = NewObject<UBlueprintLispDelegateTestObject>();
	R.First = NewObject<UBlueprintLispDelegateTestObject>(); R.Second = NewObject<UBlueprintLispDelegateTestObject>();
	for (auto* Listener : { R.First, R.Second })
	{
		R.Root->OnTakeDamageType.AddDynamic(Listener, &UBlueprintLispDelegateTestObject::ReceiveDamage);
		R.Root->OnRecvDamageFromEnemy.AddDynamic(Listener, &UBlueprintLispDelegateTestObject::ReceiveReceipt);
		R.Target->OnPlayerEnterDeath.AddDynamic(Listener, &UBlueprintLispDelegateTestObject::ReceiveDeath);
	}
	if (auto* Fn = R.Root->FindFunction(TEXT("Run"))) R.Root->ProcessEvent(Fn, nullptr);
	return R;
}
static const TCHAR* Owner = TEXT("/Script/BlueprintLisp.BlueprintLispDelegateTestObject");
static FString DeepTarget(const TCHAR* Model)
{
	return FString::Printf(TEXT("(get-member :owner \"%s\" :name Other :object %s)"), Owner, Model);
}
}

#define BL_DELEGATE_TEST(Name) IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBLDelegate##Name, "BlueprintLisp.Delegate." #Name, \
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::CommandletContext | EAutomationTestFlags::ProductFilter)

BL_DELEGATE_TEST(AuthoredBroadcastsExecute)
bool FBLDelegateAuthoredBroadcastsExecute::RunTest(const FString& Parameters)
{
	using namespace BlueprintLispDelegateTests;
	const FString Code = FString::Printf(TEXT("(function Run ")
		TEXT("(broadcast :delegate OnTakeDamageType :args (:Result (self.Result) :DamageRule (self.DamageRule))) ")
		TEXT("(broadcast :delegate OnRecvDamageFromEnemy) ")
		TEXT("(broadcast :delegate OnPlayerEnterDeath :owner \"%s\" :object %s) (AfterBroadcast))"),
		Owner, *DeepTarget(TEXT("(GetModel)")));
	TestTrue(TEXT("authored syntax validates"), FBlueprintLispConverter::Validate(Code).bSuccess);
	for (bool bMerge : { false, true })
	{
		const auto F = Fixture(); const int32 Graphs = F.BP->FunctionGraphs.Num();
		const auto Imported = FBlueprintLispConverter::ImportGraph(F.Graph, Code, Options(bMerge));
		TestTrue(TEXT("broadcast DSL imports and compiles: ") + Imported.Error, Imported.bSuccess);
		if (!Imported.bSuccess) return false;
		TestEqual(TEXT("broadcast creates no signature function graphs"), F.BP->FunctionGraphs.Num(), Graphs);
		const auto R = Run(F);
		for (auto* Listener : { R.First, R.Second })
		{
			TestEqual(TEXT("both subscribers receive damage exactly once"), Listener->DamageCount, 1);
			TestTrue(TEXT("enum arrives intact"), Listener->ReceivedResult == EBlueprintLispDamageResult::Blocked);
			TestTrue(TEXT("GameplayTag arrives intact"), Listener->ReceivedRule == TestDamageTag());
			TestEqual(TEXT("both subscribers receive no-arg notification"), Listener->ReceiptCount, 1);
			TestEqual(TEXT("both subscribers receive deep-target notification"), Listener->DeathCount, 1);
		}
		TestEqual(TEXT("execution continues after broadcasts"), R.Root->AfterCount, 1);
		R.Root->OnTakeDamageType.RemoveAll(R.Second);
		R.Root->ProcessEvent(R.Root->FindFunction(TEXT("Run")), nullptr);
		TestEqual(TEXT("remaining subscriber receives next broadcast"), R.First->DamageCount, 2);
		TestEqual(TEXT("unsubscribed listener receives no next broadcast"), R.Second->DamageCount, 1);
	}
	return true;
}

BL_DELEGATE_TEST(NativeRoundTrip)
bool FBLDelegateNativeRoundTrip::RunTest(const FString& Parameters)
{
	using namespace BlueprintLispDelegateTests;
	for (bool bImpure : { false, true })
	{
		const auto Source = Fixture();
		auto* Damage = Broadcast(Source, TEXT("OnTakeDamageType"));
		auto* Receipt = Broadcast(Source, TEXT("OnRecvDamageFromEnemy"));
		auto* Death = Broadcast(Source, TEXT("OnPlayerEnterDeath"), true);
		auto* Model = Call(Source, bImpure ? TEXT("GetModelImpure") : TEXT("GetModel"));
		auto* Member = Get(Source, TEXT("Other"), true);
		auto* After = Call(Source, TEXT("AfterBroadcast"));
		TestTrue(TEXT("enum input connects"), Link(Get(Source, TEXT("Result"))->GetValuePin(), Damage->FindPin(TEXT("Result"))));
		TestTrue(TEXT("tag input connects"), Link(Get(Source, TEXT("DamageRule"))->GetValuePin(), Damage->FindPin(TEXT("DamageRule"))));
		TestTrue(TEXT("model return feeds external member"), Link(Model->GetReturnValuePin(), Member->FindPin(UEdGraphSchema_K2::PN_Self)));
		TestTrue(TEXT("member feeds broadcast target"), Link(Member->GetValuePin(), Death->FindPin(UEdGraphSchema_K2::PN_Self)));
		UEdGraphPin* Exec = Source.Entry->FindPin(UEdGraphSchema_K2::PN_Then);
		if (bImpure)
		{
			TestTrue(TEXT("impure model is executed"), Link(Exec, Model->GetExecPin()));
			Exec = Model->GetThenPin();
		}
		for (auto* N : { Damage, Receipt, Death })
		{
			TestTrue(TEXT("broadcast execution connects"), Link(Exec, N->FindPin(UEdGraphSchema_K2::PN_Execute)));
			Exec = N->FindPin(UEdGraphSchema_K2::PN_Then);
		}
		TestTrue(TEXT("continuation connects"), Link(Exec, After->GetExecPin()));
		FKismetEditorUtilities::CompileBlueprint(Source.BP);
		TestTrue(TEXT("native source compiles"), Source.BP->Status != BS_Error);
		const auto Exported = FBlueprintLispConverter::ExportGraph(Source.Graph);
		TestTrue(TEXT("native source exports: ") + Exported.Error, Exported.bSuccess);
		if (!Exported.bSuccess) return false;
		TestTrue(TEXT("export preserves broadcasts"), Exported.LispCode.Contains(TEXT("broadcast")));
		TestTrue(TEXT("export preserves nested member read"), Exported.LispCode.Contains(TEXT("get-member")));
		for (bool bMerge : { false, true })
		{
			const auto Dest = Fixture();
			const auto Imported = FBlueprintLispConverter::ImportGraph(Dest.Graph, Exported.LispCode, Options(bMerge));
			TestTrue(TEXT("native roundtrip imports and compiles: ") + Imported.Error, Imported.bSuccess);
			if (!Imported.bSuccess) return false;
			const auto R = Run(Dest);
			TestEqual(TEXT("enum broadcast executes"), R.First->DamageCount, 1);
			TestTrue(TEXT("tag roundtrips"), R.First->ReceivedRule == TestDamageTag());
			TestEqual(TEXT("no-arg broadcast executes"), R.First->ReceiptCount, 1);
			TestEqual(TEXT("deep target broadcast executes"), R.First->DeathCount, 1);
			TestEqual(TEXT("impure producer executes exactly once"), R.Root->GetterCount, bImpure ? 1 : 0);
			TestEqual(TEXT("continuation executes"), R.Root->AfterCount, 1);
		}
	}
	return true;
}

BL_DELEGATE_TEST(RejectInvalidBroadcasts)
bool FBLDelegateRejectInvalidBroadcasts::RunTest(const FString& Parameters)
{
	using namespace BlueprintLispDelegateTests;
	for (const TCHAR* Form : {
		TEXT("(broadcast)"), TEXT("(broadcast :delegate OnRecvDamageFromEnemy :delegate OnPlayerEnterDeath)"),
		TEXT("(broadcast :delegate OnRecvDamageFromEnemy :unknown 1)"),
		TEXT("(broadcast :delegate OnRecvDamageFromEnemy :owner \"Bad\")"),
		TEXT("(broadcast :delegate OnRecvDamageFromEnemy :object self)"),
		TEXT("(broadcast :delegate OnRecvDamageFromEnemy :args 1)"),
		TEXT("(broadcast :delegate OnTakeDamageType :args (:Result))"),
		TEXT("(broadcast :delegate OnTakeDamageType :args (:Result 1 :Result 2))") })
	{
		const FString Code = FString::Printf(TEXT("(function Run %s)"), Form);
		TestFalse(TEXT("malformed broadcast fails validation: ") + Code, FBlueprintLispConverter::Validate(Code).bSuccess);
		const auto F = Fixture();
		TestFalse(TEXT("malformed broadcast fails import"), FBlueprintLispConverter::ImportGraph(F.Graph, Code, Options(false, false)).bSuccess);
	}
	for (const FString& Form : {
		FString(TEXT("(broadcast :delegate Missing)")), FString(TEXT("(broadcast :delegate Result)")),
		FString(TEXT("(broadcast :delegate Single)")), FString(TEXT("(broadcast :delegate BindOnly)")),
		FString(TEXT("(broadcast :delegate OnRecvDamageFromEnemy :args (:Unknown 1))")),
		FString(TEXT("(broadcast :delegate OnRecvDamageFromEnemy :args (:self self))")),
		FString::Printf(TEXT("(broadcast :delegate OnPlayerEnterDeath :owner \"%s\" :object (self.DamageCount))"), Owner),
		FString::Printf(TEXT("(broadcast :delegate OnPlayerEnterDeath :owner \"%s\" :object (GetModelImpure))"), Owner) })
	{
		const auto F = Fixture();
		const auto R = FBlueprintLispConverter::ImportGraph(F.Graph, TEXT("(function Run ") + Form + TEXT(")"), Options(false, false));
		TestFalse(TEXT("invalid delegate, input or object fails import: ") + Form, R.bSuccess);
		TestFalse(TEXT("rejection explains the failure"), R.Error.IsEmpty());
	}
	return true;
}

BL_DELEGATE_TEST(BlueprintDefinedDispatcher)
bool FBLDelegateBlueprintDefinedDispatcher::RunTest(const FString& Parameters)
{
	using namespace BlueprintLispDelegateTests;
	const auto F = Fixture();
	const FName DelegateName(TEXT("OnBlueprintNotice"));
	auto* Signature = FBlueprintEditorUtils::CreateNewGraph(F.BP, DelegateName,
		UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::CreateFunctionGraph<UFunction>(F.BP, Signature, true, nullptr);
	F.BP->DelegateSignatureGraphs.Add(Signature);
	FEdGraphPinType Type; Type.PinCategory = UEdGraphSchema_K2::PC_MCDelegate;
	TestTrue(TEXT("Blueprint dispatcher variable is added"), FBlueprintEditorUtils::AddMemberVariable(F.BP, DelegateName, Type));
	for (auto& Var : F.BP->NewVariables) if (Var.VarName == DelegateName)
		Var.PropertyFlags |= CPF_BlueprintAssignable | CPF_BlueprintCallable;
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(F.BP);
	FKismetEditorUtilities::CompileBlueprint(F.BP);
	TestTrue(TEXT("Blueprint dispatcher declaration compiles"), F.BP->Status != BS_Error);
	const int32 Functions = F.BP->FunctionGraphs.Num();
	const int32 Signatures = F.BP->DelegateSignatureGraphs.Num();
	const auto Imported = FBlueprintLispConverter::ImportGraph(F.Graph,
		TEXT("(function Run (broadcast :delegate OnBlueprintNotice))"), Options());
	TestTrue(TEXT("Blueprint-defined dispatcher imports: ") + Imported.Error, Imported.bSuccess);
	if (!Imported.bSuccess) return false;
	TestEqual(TEXT("no duplicate function graph"), F.BP->FunctionGraphs.Num(), Functions);
	TestEqual(TEXT("original delegate signature graph is retained"), F.BP->DelegateSignatureGraphs.Num(), Signatures);
	for (UEdGraph* G : F.BP->FunctionGraphs)
		TestFalse(TEXT("no ordinary __DelegateSignature function graph"), G->GetName().EndsWith(TEXT("__DelegateSignature")));
	auto* Property = FindFProperty<FMulticastDelegateProperty>(F.BP->GeneratedClass, DelegateName);
	TestNotNull(TEXT("compiled Blueprint dispatcher property exists"), Property);
	if (!Property) return false;
	auto* Object = NewObject<UBlueprintLispDelegateTestObject>(GetTransientPackage(), F.BP->GeneratedClass);
	auto* Listener = NewObject<UBlueprintLispDelegateTestObject>();
	FScriptDelegate Binding; Binding.BindUFunction(Listener, TEXT("ReceiveReceipt"));
	Property->AddDelegate(Binding, Object, Property->ContainerPtrToValuePtr<void>(Object));
	Object->ProcessEvent(Object->FindFunction(TEXT("Run")), nullptr);
	TestEqual(TEXT("Blueprint-defined dispatcher delivers notification"), Listener->ReceiptCount, 1);
	const auto Exported = FBlueprintLispConverter::ExportGraph(F.Graph);
	TestTrue(TEXT("Blueprint-defined broadcast exports"), Exported.bSuccess && Exported.LispCode.Contains(TEXT("broadcast")));
	return true;
}

BL_DELEGATE_TEST(NativeImplicitSelfTargetRoundTrip)
bool FBLDelegateNativeImplicitSelfTargetRoundTrip::RunTest(const FString& Parameters)
{
	using namespace BlueprintLispDelegateTests;
	const auto Source = Fixture();
	auto* Damage = Broadcast(Source, TEXT("OnTakeDamageType"), true);
	GetDefault<UEdGraphSchema_K2>()->TrySetDefaultValue(*Damage->FindPin(TEXT("Result")), TEXT("Damaged"));
	GetDefault<UEdGraphSchema_K2>()->TrySetDefaultValue(*Damage->FindPin(TEXT("DamageRule")),
		TEXT("(TagName=\"BlueprintLisp.Tests.Damage\")"));
	TestTrue(TEXT("implicit-self source execution connects"), Link(Source.Entry->FindPin(UEdGraphSchema_K2::PN_Then),
		Damage->FindPin(UEdGraphSchema_K2::PN_Execute)));
	FKismetEditorUtilities::CompileBlueprint(Source.BP);
	TestTrue(TEXT("native external reference with implicit self compiles"), Source.BP->Status != BS_Error);
	const auto Exported = FBlueprintLispConverter::ExportGraph(Source.Graph);
	TestTrue(TEXT("implicit-self target exports: ") + Exported.Error, Exported.bSuccess);
	if (!Exported.bSuccess) return false;
	const auto Dest = Fixture();
	const auto Imported = FBlueprintLispConverter::ImportGraph(Dest.Graph, Exported.LispCode, Options());
	TestTrue(TEXT("implicit-self target roundtrips: ") + Imported.Error, Imported.bSuccess);
	if (!Imported.bSuccess) return false;
	const auto R = Run(Dest);
	TestEqual(TEXT("implicit self keeps its subscriber"), R.First->DamageCount, 1);
	TestTrue(TEXT("literal enum roundtrips"), R.First->ReceivedResult == EBlueprintLispDamageResult::Damaged);
	TestTrue(TEXT("literal GameplayTag roundtrips"), R.First->ReceivedRule == TestDamageTag());
	return true;
}

BL_DELEGATE_TEST(IncrementalEventReuse)
bool FBLDelegateIncrementalEventReuse::RunTest(const FString& Parameters)
{
	using namespace BlueprintLispDelegateTests;
	const auto F = Fixture();
	auto* Graph = FBlueprintEditorUtils::CreateNewGraph(F.BP, TEXT("EventGraph"), UEdGraph::StaticClass(), UEdGraphSchema_K2::StaticClass());
	FBlueprintEditorUtils::AddUbergraphPage(F.BP, Graph);
	const FString Code = TEXT("(event FireNotice :event-id \"abcd1234\" ")
		TEXT("(broadcast :delegate OnRecvDamageFromEnemy :id \"bcde2345\") (AfterBroadcast))");
	const auto Initial = FBlueprintLispConverter::Import(F.BP, TEXT("EventGraph"), Code, Options(true));
	TestTrue(TEXT("initial event imports: ") + Initial.Error, Initial.bSuccess);
	if (!Initial.bSuccess) return false;
	UK2Node_CallDelegate* Original = nullptr;
	for (UEdGraphNode* N : Graph->Nodes) if (auto* D = Cast<UK2Node_CallDelegate>(N)) Original = D;
	TestNotNull(TEXT("broadcast node exists"), Original);
	if (!Original) return false;
	const FGuid Guid = Original->NodeGuid;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		const auto Reimported = FBlueprintLispConverter::Import(F.BP, TEXT("EventGraph"), Code, Options(true));
		TestTrue(TEXT("repeated event imports: ") + Reimported.Error, Reimported.bSuccess);
		if (!Reimported.bSuccess) return false;
		int32 Count = 0;
		for (UEdGraphNode* N : Graph->Nodes) if (auto* D = Cast<UK2Node_CallDelegate>(N))
		{
			++Count; TestTrue(TEXT("compatible broadcast object and GUID are reused"), D == Original && D->NodeGuid == Guid);
		}
		TestEqual(TEXT("reimport creates no extra broadcast"), Count, 1);
		auto* Object = NewObject<UBlueprintLispDelegateTestObject>(GetTransientPackage(), F.BP->GeneratedClass);
		auto* Listener = NewObject<UBlueprintLispDelegateTestObject>();
		Object->OnRecvDamageFromEnemy.AddDynamic(Listener, &UBlueprintLispDelegateTestObject::ReceiveReceipt);
		Object->ProcessEvent(Object->FindFunction(TEXT("FireNotice")), nullptr);
		TestEqual(TEXT("reused broadcast fires once"), Listener->ReceiptCount, 1);
		TestEqual(TEXT("reused broadcast keeps continuation"), Object->AfterCount, 1);
	}
	return true;
}
#endif
