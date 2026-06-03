#pragma once

#include "CoreMinimal.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "STT_MoveOutsideAttackRangeTask.generated.h"

/**
 * StateTree task that moves the AI pawn to just outside attack range of its current target.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Move Outside Attack Range"))
class AEYERJI_API USTT_MoveOutsideAttackRangeTask : public UStateTreeTaskBlueprintBase
{
	GENERATED_BODY()

public:
	// The amount added on top of the pawn's attack range before the task succeeds.
	// Positive values keep the kiting pawn slightly outside of its live attack range.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move", meta = (ClampMin = "0.0"))
	float AttackRangeExtension = 50.0f;

	// Acceptance radius used for the projected retreat destination.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move", meta = (ClampMin = "0.0"))
	float AcceptableRadius = 50.0f;

	// The minimum change in retreat destination before the task reissues MoveToLocation.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move", meta = (ClampMin = "0.0"))
	float RepathDistanceThreshold = 100.0f;

	USTT_MoveOutsideAttackRangeTask(const FObjectInitializer& ObjectInitializer);

	// Starts a retreat move that places the pawn outside attack range of its current target.
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) override;

	// Keeps updating the retreat destination until the pawn is safely outside attack range.
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) override;

	// Stops any active movement request when the task exits or is aborted.
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) override;

private:
	// Cached retreat point so the task only repaths when the target shifts meaningfully.
	FVector CachedRetreatDestination = FVector::ZeroVector;

	// Tracks whether CachedRetreatDestination currently contains a valid nav location.
	bool bHasCachedRetreatDestination = false;
};
