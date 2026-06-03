// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayAbilitySpec.h"
#include "GameplayTagContainer.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "STT_ActivatePrimaryAttackTask.generated.h"
#pragma once

class UAbilitySystemComponent;
struct FGameplayEventData;
struct FAbilityEndedData;

/**
 * StateTree Task that activates the pawn's primary attack ability via GAS.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Activate Primary Attack"))
class AEYERJI_API USTT_ActivatePrimaryAttackTask : public UStateTreeTaskBlueprintBase
{
    GENERATED_BODY()

public:
	// Must match Super’s signature
	USTT_ActivatePrimaryAttackTask(const FObjectInitializer& ObjectInitializer);

    virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
                                           const FStateTreeTransitionResult& Transition) override;
    virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) override;
    virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) override;
    
private:
    void HandlePrimaryAttackCompleted(const FGameplayEventData* EventData);
    void RegisterCompletionListener(UAbilitySystemComponent* ASC);
    void UnregisterCompletionListener();
    void HandleObservedAbilityEnded(const FAbilityEndedData& EndData);

    UPROPERTY(Transient)
    bool bRequestedActivation = false;
    UPROPERTY(Transient)
    bool bWasActive = false;
    UPROPERTY(Transient)
    float NextRetryTime = 0.f;
    UPROPERTY(Transient)
    bool bPrimaryAttackCompleted = false;
    UPROPERTY(Transient)
    bool bRegisteredCompletionDelegate = false;
    UPROPERTY(Transient)
    bool bLoggedWaitingForActorInfo = false;
    UPROPERTY(Transient)
    bool bLoggedMissingPrimarySpec = false;
    UPROPERTY(Transient)
    bool bLoggedActivationFailure = false;
    UPROPERTY(Transient)
    bool bLoggedPrimarySpecSnapshot = false;
    UPROPERTY(Transient)
    float ObservedPrimaryActiveStartTime = -1.f;

    FGameplayAbilitySpecHandle ObservedPrimarySpecHandle;

    FDelegateHandle PrimaryAttackCompletedHandle;
    FDelegateHandle ObservedAbilityEndedHandle;
    TWeakObjectPtr<UAbilitySystemComponent> CachedASC;
};
