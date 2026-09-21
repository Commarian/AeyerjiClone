// STT_MoveToHomeTask.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "Navigation/PathFollowingComponent.h"
#include "STT_MoveToHomeTask.generated.h"

/**
 * StateTree task that walks a leashed pawn back to the controller's checkout
 * home location and drops the chase on arrival. The return is committed: unlike
 * the last-known-target chase, a visible target does not abort the walk home.
 * Success clears both the current target and the last-known memory so the pawn
 * settles instead of ping-ponging between search and leash states.
 */
UCLASS(Blueprintable, meta=(DisplayName="Move To Home"))
class AEYERJI_API USTT_MoveToHomeTask : public UStateTreeTaskBlueprintBase
{
	GENERATED_BODY()

public:
	/** Distance from home that counts as arrived. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Move", meta=(ClampMin="0.0"))
	float AcceptableRadius = 500.f;

	/** Maximum return duration before the task fails and the chase may resume. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Move", meta=(ClampMin="0.0"))
	float TimeoutSeconds = 30.f;

	USTT_MoveToHomeTask(const FObjectInitializer& ObjectInitializer);

protected:
	/** Handle returned by MoveToLocation so the task can tell whether the move started successfully. */
	FAIRequestID MoveRequestId;

	/** Cached world-space destination captured on state entry. */
	FVector CachedDestination = FVector::ZeroVector;

	/** Start time for timeout tracking. */
	double ReturnStartTime = -1.0;

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) override;
};
