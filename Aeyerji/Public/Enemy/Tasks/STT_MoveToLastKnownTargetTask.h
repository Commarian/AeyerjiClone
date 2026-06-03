// STT_MoveToLastKnownTargetTask.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "Navigation/PathFollowingComponent.h"
#include "STT_MoveToLastKnownTargetTask.generated.h"

/**
 * StateTree task that chases the controller's cached last-known target location
 * after direct perception is lost.
 */
UCLASS(Blueprintable, meta=(DisplayName="Move To Last Known Target"))
class AEYERJI_API USTT_MoveToLastKnownTargetTask : public UStateTreeTaskBlueprintBase
{
	GENERATED_BODY()

public:
	/** Distance from the cached location that counts as success. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Move", meta=(ClampMin="0.0"))
	float AcceptableRadius = 1000.f;

	/** Maximum chase duration before the task fails and clears the cached memory. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Move", meta=(ClampMin="0.0"))
	float TimeoutSeconds = 60.f;

	USTT_MoveToLastKnownTargetTask(const FObjectInitializer& ObjectInitializer);

protected:
	/** Handle returned by MoveToLocation so the task can tell whether the move started successfully. */
	FAIRequestID MoveRequestId;

	/** Cached world-space destination captured on state entry. */
	FVector CachedDestination = FVector::ZeroVector;

	/** Start time for timeout tracking. */
	double ChaseStartTime = -1.0;

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) override;
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) override;
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) override;
};
