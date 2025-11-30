#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintAsyncActionBase.h"
#include "GameplayAbilitySpec.h"
#include "GameplayTagContainer.h"
#include "AbilityAsync_WaitForAbilityFailed.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FAbilityFailedDynamic,
	UGameplayAbility*, Ability,
	FGameplayTagContainer, FailureTags);

/**
 *  Blueprint node:  AbilitySystem → Wait For Ability Failed
 *  Fires every time ASC.OnAbilityFailed broadcasts on this client.
 *  Optional filter by single tag (e.g. Cooldown.*).
 */
UCLASS(BlueprintType)   // ← Make sure “BlueprintType” is here
class AEYERJI_API UAbilityAsync_WaitForAbilityFailed : public UBlueprintAsyncActionBase
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable,
		meta = (BlueprintInternalUseOnly = "true", WorldContext = "WorldContextObject"))
	static UAbilityAsync_WaitForAbilityFailed* WaitForAbilityFailed(
		UObject* WorldContextObject,
		UAbilitySystemComponent* AbilitySystem,
		FGameplayTag FilterTag);

	UPROPERTY(BlueprintAssignable)
	FAbilityFailedDynamic OnFailed;

	virtual void Activate() override;
	virtual void SetReadyToDestroy() override;

private:
	void HandleFailure(const UGameplayAbility* GA, const FGameplayTagContainer& Tags);

	UAbilitySystemComponent* ASC;
	FGameplayTag SingleFilter;
	FDelegateHandle Handle;
};