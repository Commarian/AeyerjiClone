#pragma once

#include "CoreMinimal.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "GameplayTagContainer.h"
#include "STT_CrowdControlHoldTask.generated.h"

class AAIController;
class APawn;
class UAbilitySystemComponent;

/**
 * Holds an AI StateTree branch while one or more crowd-control gameplay tags are active.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Crowd Control Hold"))
class AEYERJI_API USTT_CrowdControlHoldTask : public UStateTreeTaskBlueprintBase
{
	GENERATED_BODY()

public:
	USTT_CrowdControlHoldTask(const FObjectInitializer& ObjectInitializer);

	/** Gameplay tags that keep this hold state running while present on the pawn's ASC. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move|Combat")
	FGameplayTagContainer ActiveTags;

	/** When enabled, every ActiveTags entry must be present before the hold continues running. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move|Combat")
	bool bMatchAll = false;

	/** When enabled, tags must match exact leaf tags instead of matching parent/child relationships. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move|Combat")
	bool bMatchExactly = false;

	/** Stops the current AI path-following and pawn movement immediately when the state starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move|Combat")
	bool bStopMovementOnEnter = true;

	/** Clears gameplay and movement focus when the state starts so the pawn stops aiming at its old target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move|Combat")
	bool bClearFocusOnEnter = true;

	/** Cancels currently active abilities on authority so attacks do not continue during crowd control. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move|Combat")
	bool bCancelAbilitiesOnAuthority = true;

	/** Keeps movement and focus stopped on every tick while the configured tags remain active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move|Combat")
	bool bKeepMovementStoppedOnTick = true;

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) override;

private:
	UAbilitySystemComponent* ResolveAbilitySystemComponent() const;
	bool HasActiveCrowdControlTags(const UAbilitySystemComponent& ASC) const;
	void StopMovementAndFocus(bool bClearFocus) const;

	TWeakObjectPtr<AAIController> CachedAI;
	TWeakObjectPtr<APawn> CachedPawn;
};
