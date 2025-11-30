

#include "Enemy/Tasks/STT_MoveToLocationTask.h"
#include "AIController.h"
#include "StateTreeExecutionContext.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "Enemy/EnemyAIController.h"

USTT_MoveToLocationTask::USTT_MoveToLocationTask(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bShouldCallTick = true;        // enable Tick() every frame
}
EStateTreeRunStatus USTT_MoveToLocationTask::EnterState(FStateTreeExecutionContext& Context,
                                                        const FStateTreeTransitionResult& /*Transition*/)
{
	AAIController* AI = Cast<AAIController>(Context.GetOwner());
	APawn*         Pawn = AI ? AI->GetPawn() : nullptr;

	if (!AI || !Pawn)
	{
		return EStateTreeRunStatus::Failed;
	}

	// Ensure we have a valid destination on the nav-mesh.
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Pawn->GetWorld());
	if (!NavSys)
	{
		return EStateTreeRunStatus::Failed;
	}

	FNavLocation Projected;
	const FVector SearchExtents(500.f, 500.f, 1000.f); // 5 m × 5 m × 10 m
	if (!NavSys->ProjectPointToNavigation(Destination, Projected, SearchExtents))
	{
		UE_LOG(LogTemp, Display,
			TEXT("MoveTo: ProjectPoint FAILED – Dest=%s, Ext=%s"),
			*Destination.ToString(), *SearchExtents.ToString());
		return EStateTreeRunStatus::Failed;
	}

	MoveRequestId = AI->MoveToLocation(Projected.Location,
	                                   AcceptableRadius,
	                                   /*bStopOnOverlap   =*/true,
	                                   /*bUsePathfinding  =*/true,
	                                   /*bProjectGoal     =*/false,
	                                   /*bCanStrafe       =*/false,
	                                   /*FilterClass      =*/nullptr,
	                                   /*bAllowPartialPath=*/true);

	if (!MoveRequestId.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("MoveTo task: Move request failed to start."));
		return EStateTreeRunStatus::Failed;
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus USTT_MoveToLocationTask::Tick(FStateTreeExecutionContext& Context,
                                                  const float /*DeltaTime*/)
{
    AAIController* AI = Cast<AAIController>(Context.GetOwner());
    APawn*         Pawn = AI ? AI->GetPawn() : nullptr;

    if (!AI || !Pawn)
    {
        return EStateTreeRunStatus::Failed;
    }

    // If we have acquired a combat target, abort patrolling move so higher-priority
    // states (e.g., chase/attack) can take over immediately.
    if (AI->IsA<AEnemyAIController>())
    {
        if (Cast<AEnemyAIController>(AI)->GetTargetActor() != nullptr)
        {
            return EStateTreeRunStatus::Failed;
        }
    }

    EPathFollowingStatus::Type Status = EPathFollowingStatus::Idle;
    if (const UPathFollowingComponent* PFC = AI->GetPathFollowingComponent())
    {
        Status = PFC->GetStatus();
    }

	// 1) Goal reached?
	if (Status == EPathFollowingStatus::Idle)
	{
		const float Dist2D = FVector::Dist2D(Pawn->GetActorLocation(), Destination);
		if (Dist2D <= AcceptableRadius + KINDA_SMALL_NUMBER)
		{
			return EStateTreeRunStatus::Succeeded;
		}
		return EStateTreeRunStatus::Failed; // idle but not close → cancelled/failed
	}

	// 2) Abort / blocked?
	if (Status == EPathFollowingStatus::Paused ||
	    Status == EPathFollowingStatus::Waiting)
	{
		return EStateTreeRunStatus::Failed;
	}

	// 3) Still travelling.
	return EStateTreeRunStatus::Running;
}

void USTT_MoveToLocationTask::ExitState(FStateTreeExecutionContext& Context,
                                        const FStateTreeTransitionResult& Transition)
{
	// If we were interrupted while still Running, stop the move request.
	if (Transition.CurrentRunStatus == EStateTreeRunStatus::Running)
	{
		if (AAIController* AI = Cast<AAIController>(Context.GetOwner()))
		{
			AI->StopMovement();
		}
	}
}
