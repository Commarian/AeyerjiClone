// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "Navigation/PathFollowingComponent.h"
#include "STT_MoveToLocationTask.generated.h"
#pragma once

/**
 * StateTree task: orders the controller to move to a world-space location.
 *
 * Behaviour
 * ─────────
 * • EnterState()  → issues a MoveTo request and returns Running
 * • Tick()        → polls the PathFollowing status
 *      – Succeeded when the pawn is within AcceptableRadius of Destination
 *      – Failed     if the move request is invalid or is aborted
 * • ExitState()   → optionally stops movement if the task is still running
 */
UCLASS(Blueprintable, meta = (DisplayName = "Move To Location"))
class AEYERJI_API USTT_MoveToLocationTask : public UStateTreeTaskBlueprintBase
{
	GENERATED_BODY()

public:
	/** Goal the pawn should reach (world-space). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MoveTo")
	FVector Destination = FVector::ZeroVector;

	/** Distance from Destination that counts as success (cms). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MoveTo", meta = (ClampMin = "0.0"))
	float AcceptableRadius = 100.f;
	USTT_MoveToLocationTask(const FObjectInitializer& ObjectInitializer);   // ← NEW
protected:
	/** Handle returned by AIController::MoveTo… – used to verify the move on Tick. */
	FAIRequestID MoveRequestId;

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context,
										   const FStateTreeTransitionResult& Transition) override;

	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context,
									  const float DeltaTime) override;

	virtual void ExitState(FStateTreeExecutionContext& Context,
						   const FStateTreeTransitionResult& Transition) override;
};
