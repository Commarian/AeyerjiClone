
#pragma once

#include "CoreMinimal.h"
#include "Tasks/AITask.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "STT_MoveToAttackRangeTask.generated.h"

/**
 * StateTree Task that moves the AI pawn to within attack range of its current target.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Move To Attack Range"))
class AEYERJI_API USTT_MoveToAttackRangeTask : public UStateTreeTaskBlueprintBase
{
	GENERATED_BODY()

public:

	// The amount subtracted from base attack range to determine how close to get before succeeding.
	// Makes absolutely sure that the target it within attack range.
	// Positive values make the AI go closer than the attack range, negative values make it stop further away.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move", meta=(ClampMin="0.0"))
	float AttackRangeReduction = 50.0f;

	/** Initial pause after navigation rejects a move request, preventing StateTree re-entry from retrying every frame. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move|Retry", meta=(ClampMin="0.05", UIMin="0.05"))
	float MoveFailureInitialBackoffSeconds = 0.25f;

	/** Maximum exponential pause between repeated rejected move requests for this task instance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Move|Retry", meta=(ClampMin="0.05", UIMin="0.05"))
	float MoveFailureMaximumBackoffSeconds = 2.0f;

	// Called when the state containing this task is entered.
	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) override;
	
	// Called each tick while this task is running.
	virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, const float DeltaTime) override;

	// Called when the state containing this task is exited or the task is aborted.
	virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) override;

	USTT_MoveToAttackRangeTask(const FObjectInitializer& ObjectInitializer);

private:
	bool IsMoveRequestBackoffActive(const UWorld* World) const;
	float RecordMoveRequestFailure(const UWorld* World);
	void ResetMoveRequestBackoff();

	double NextMoveRequestWorldTime = 0.0;
	int32 ConsecutiveMoveRequestFailures = 0;
};
