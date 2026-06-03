// STT_MoveToLastKnownTargetTask.cpp

#include "Enemy/Tasks/STT_MoveToLastKnownTargetTask.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "AIController.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "Enemy/EnemyAIController.h"
#include "GameFramework/Pawn.h"
#include "NavigationSystem.h"
#include "StateTreeExecutionContext.h"

USTT_MoveToLastKnownTargetTask::USTT_MoveToLastKnownTargetTask(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bShouldCallTick = true;
}

EStateTreeRunStatus USTT_MoveToLastKnownTargetTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& /*Transition*/)
{
	AEnemyAIController* EnemyAI = Cast<AEnemyAIController>(Context.GetOwner());
	APawn* Pawn = EnemyAI ? EnemyAI->GetPawn() : nullptr;
	if (!EnemyAI || !Pawn)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (EnemyAI->GetTargetActor() != nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (!EnemyAI->HasLastKnownTarget())
	{
		return EStateTreeRunStatus::Failed;
	}

	CachedDestination = EnemyAI->GetLastKnownTargetLocation();
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Pawn->GetWorld());
	if (!NavSys)
	{
		EnemyAI->ClearLastKnownTarget();
		return EStateTreeRunStatus::Failed;
	}

	FNavLocation Projected;
	const FVector SearchExtents(500.f, 500.f, 1000.f);
	if (!NavSys->ProjectPointToNavigation(CachedDestination, Projected, SearchExtents))
	{
		EnemyAI->ClearLastKnownTarget();
		return EStateTreeRunStatus::Failed;
	}

	CachedDestination = Projected.Location;
	if (FVector::Dist2D(Pawn->GetActorLocation(), CachedDestination) <= AcceptableRadius)
	{
		EnemyAI->ClearLastKnownTarget();
		return EStateTreeRunStatus::Succeeded;
	}

	if (const UWorld* World = Pawn->GetWorld())
	{
		ChaseStartTime = World->GetTimeSeconds();
	}
	else
	{
		ChaseStartTime = 0.0;
	}

	MoveRequestId = EnemyAI->MoveToLocation(
		CachedDestination,
		AcceptableRadius,
		/*bStopOnOverlap=*/true,
		/*bUsePathfinding=*/true,
		/*bProjectGoalLocation=*/false,
		/*bCanStrafe=*/false,
		/*FilterClass=*/nullptr,
		/*bAllowPartialPath=*/true);

	if (!MoveRequestId.IsValid())
	{
		EnemyAI->ClearLastKnownTarget();
		return EStateTreeRunStatus::Failed;
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus USTT_MoveToLastKnownTargetTask::Tick(FStateTreeExecutionContext& Context, const float /*DeltaTime*/)
{
	AEnemyAIController* EnemyAI = Cast<AEnemyAIController>(Context.GetOwner());
	APawn* Pawn = EnemyAI ? EnemyAI->GetPawn() : nullptr;
	if (!EnemyAI || !Pawn)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (EnemyAI->GetTargetActor() != nullptr)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (!EnemyAI->HasLastKnownTarget())
	{
		return EStateTreeRunStatus::Failed;
	}

	const UWorld* World = Pawn->GetWorld();
	if (World && TimeoutSeconds > 0.f && ChaseStartTime >= 0.0
		&& (World->GetTimeSeconds() - ChaseStartTime) >= TimeoutSeconds)
	{
		EnemyAI->ClearLastKnownTarget();
		return EStateTreeRunStatus::Failed;
	}

	if (FVector::Dist2D(Pawn->GetActorLocation(), CachedDestination) <= AcceptableRadius)
	{
		EnemyAI->ClearLastKnownTarget();
		return EStateTreeRunStatus::Succeeded;
	}

	EPathFollowingStatus::Type MoveStatus = EPathFollowingStatus::Idle;
	if (const UPathFollowingComponent* PathFollowing = EnemyAI->GetPathFollowingComponent())
	{
		MoveStatus = PathFollowing->GetStatus();
	}

	if (MoveStatus == EPathFollowingStatus::Idle
		|| MoveStatus == EPathFollowingStatus::Paused
		|| MoveStatus == EPathFollowingStatus::Waiting)
	{
		MoveRequestId = EnemyAI->MoveToLocation(
			CachedDestination,
			AcceptableRadius,
			/*bStopOnOverlap=*/true,
			/*bUsePathfinding=*/true,
			/*bProjectGoalLocation=*/false,
			/*bCanStrafe=*/false,
			/*FilterClass=*/nullptr,
			/*bAllowPartialPath=*/true);

		if (!MoveRequestId.IsValid())
		{
			EnemyAI->ClearLastKnownTarget();
			return EStateTreeRunStatus::Failed;
		}
	}

	return EStateTreeRunStatus::Running;
}

void USTT_MoveToLastKnownTargetTask::ExitState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& Transition)
{
	if (Transition.CurrentRunStatus == EStateTreeRunStatus::Running)
	{
		if (AAIController* AI = Cast<AAIController>(Context.GetOwner()))
		{
			AI->StopMovement();
		}
	}
}
