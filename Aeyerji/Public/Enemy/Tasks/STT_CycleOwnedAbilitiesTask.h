// StateTree task that iterates through owned abilities for bosses/minibosses.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "GameplayAbilitySpec.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "STT_CycleOwnedAbilitiesTask.generated.h"

class UAbilitySystemComponent;
class AActor;
class APawn;
struct FAbilityEndedData;
struct FGameplayAbilitySpec;
struct FGameplayAbilitySpecHandle;

/**
 * StateTree Task that walks through the pawn's owned Gameplay Abilities (GAS) and activates them one at a time.
 * - Remembers the last successfully cast spec to rotate through the pool.
 * - Optional random traversal and tag filtering.
 * - If activation fails, advances to the next ability; if all fail, the task can fail or succeed based on settings.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Cycle Owned Abilities"))
class AEYERJI_API USTT_CycleOwnedAbilitiesTask : public UStateTreeTaskBlueprintBase
{
	GENERATED_BODY()

public:
	USTT_CycleOwnedAbilitiesTask(const FObjectInitializer& ObjectInitializer);

	/** When true, shuffle the available abilities each time the task runs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Abilities")
	bool bRandomizeAbilityOrder = false;

	/** Only consider abilities whose tags match this query (optional). Checks ability, asset, and dynamic spec tags. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Abilities")
	FGameplayTagQuery AbilityTagQuery;

	/** Ignore abilities that have any of these tags. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Abilities")
	FGameplayTagContainer IgnoreAbilityTags;

	/** If true, require a valid target actor before attempting to activate abilities (abilities read it from controller/state). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Abilities")
	bool bPassTargetActorAsEventData = true;

	/** If no abilities can be activated, return Failed (true) or Succeeded (false). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Abilities")
	bool bFailWhenNoAbilityCouldActivate = true;

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) override;

private:
	void BuildAbilityQueue(UAbilitySystemComponent& ASC);
	bool DoesSpecPassFilters(const FGameplayAbilitySpec& Spec) const;
	bool IsSpecActive(const UAbilitySystemComponent& ASC, const FGameplayAbilitySpecHandle& Handle) const;
	bool TryActivateSpec(UAbilitySystemComponent& ASC, const FGameplayAbilitySpecHandle& Handle, APawn* Avatar, AActor* TargetActor);
	void HandleAbilityEnded(const FAbilityEndedData& EndData);
	void RegisterASC(UAbilitySystemComponent& ASC);
	void UnregisterASC();

	// Runtime state (per component instance)
	UPROPERTY(Transient)
	TArray<FGameplayAbilitySpecHandle> PendingAbilityOrder;

	UPROPERTY(Transient)
	FGameplayAbilitySpecHandle ActiveAbilityHandle;

	UPROPERTY(Transient)
	FGameplayAbilitySpecHandle LastSuccessfulAbilityHandle;

	UPROPERTY(Transient)
	FGameplayAbilitySpecHandle PendingEndedHandle;

	UPROPERTY(Transient)
	bool bHasPendingEndData = false;

	UPROPERTY(Transient)
	bool bPendingEndDataWasCancelled = false;

	UPROPERTY(Transient)
	TWeakObjectPtr<UAbilitySystemComponent> CachedASC;

	FDelegateHandle AbilityEndedHandle;
};
