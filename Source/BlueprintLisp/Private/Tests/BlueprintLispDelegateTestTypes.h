// Copyright (c) 2026 cc8887. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "BlueprintLispDelegateTestTypes.generated.h"

UENUM(BlueprintType)
enum class EBlueprintLispDamageResult : uint8 { None, Blocked, Damaged };

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FBlueprintLispDamageNotification,
	EBlueprintLispDamageResult, Result, FGameplayTag, DamageRule);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FBlueprintLispNotification);
DECLARE_DYNAMIC_DELEGATE(FBlueprintLispSingleNotification);

UCLASS(BlueprintType, Blueprintable)
class UBlueprintLispDelegateTestObject : public UObject
{
	GENERATED_BODY()
public:
	// Runtime fixture helper for Blueprint-defined dispatchers, whose signatures
	// are not always exposed as typed Python MulticastDelegate wrappers.
	UFUNCTION(BlueprintCallable, Category="BlueprintLispTests")
	static bool SetMulticastBinding(UObject* Emitter, const FString& DelegateName,
		UObject* Receiver, const FString& Callback, bool bRemove = false);

	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="BlueprintLispTests")
	FBlueprintLispDamageNotification OnTakeDamageType;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="BlueprintLispTests")
	FBlueprintLispNotification OnRecvDamageFromEnemy;
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category="BlueprintLispTests")
	FBlueprintLispNotification OnPlayerEnterDeath;
	UPROPERTY(BlueprintAssignable, Category="BlueprintLispTests")
	FBlueprintLispNotification BindOnly;
	UPROPERTY(BlueprintReadWrite, Category="BlueprintLispTests")
	FBlueprintLispSingleNotification Single;
	UPROPERTY(BlueprintReadWrite, Category="BlueprintLispTests")
	UBlueprintLispDelegateTestObject* Other = nullptr;
	UPROPERTY(BlueprintReadWrite, Category="BlueprintLispTests")
	EBlueprintLispDamageResult Result = EBlueprintLispDamageResult::Blocked;
	UPROPERTY(BlueprintReadWrite, Category="BlueprintLispTests")
	FGameplayTag DamageRule;
	UPROPERTY(BlueprintReadOnly, Category="BlueprintLispTests")
	int32 DamageCount = 0;
	UPROPERTY(BlueprintReadOnly, Category="BlueprintLispTests")
	int32 ReceiptCount = 0;
	UPROPERTY(BlueprintReadOnly, Category="BlueprintLispTests")
	int32 DeathCount = 0;
	UPROPERTY(BlueprintReadOnly, Category="BlueprintLispTests")
	int32 GetterCount = 0;
	UPROPERTY(BlueprintReadOnly, Category="BlueprintLispTests")
	int32 AfterCount = 0;
	UPROPERTY(BlueprintReadOnly, Category="BlueprintLispTests")
	EBlueprintLispDamageResult ReceivedResult = EBlueprintLispDamageResult::None;
	UPROPERTY(BlueprintReadOnly, Category="BlueprintLispTests")
	FGameplayTag ReceivedRule;

	UFUNCTION(BlueprintPure, Category="BlueprintLispTests")
	UBlueprintLispDelegateTestObject* GetModel() const { return Other; }
	UFUNCTION(BlueprintCallable, Category="BlueprintLispTests")
	UBlueprintLispDelegateTestObject* GetModelImpure() { ++GetterCount; return Other; }
	UFUNCTION(BlueprintCallable, Category="BlueprintLispTests")
	void AfterBroadcast() { ++AfterCount; }
	UFUNCTION()
	void ReceiveDamage(EBlueprintLispDamageResult InResult, FGameplayTag InRule)
	{ ++DamageCount; ReceivedResult = InResult; ReceivedRule = InRule; }
	UFUNCTION()
	void ReceiveReceipt() { ++ReceiptCount; }
	UFUNCTION()
	void ReceiveDeath() { ++DeathCount; }
};
