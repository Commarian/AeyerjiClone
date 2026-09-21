// STT_MoveToHomeTask.cpp

#include "Enemy/Tasks/STT_MoveToHomeTask.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "AIController.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "Enemy/EnemyAIController.h"
#include "GameFramework/Pawn.h"
#include "Navigation/AeyerjiNavSafetyLibrary.h"
#include "NavigationSystem.h"
#include "StateTreeExecutionContext.h"
#include "Testing/AeyerjiCombatBalanceTestHarness.h"

USTT_MoveToHomeTask::USTT_MoveToHomeTask(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bShouldCallTick = true;
}

EStateTreeRunStatus USTT_MoveToHomeTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& /*Transition*/)
{
	AEnemyAIController* EnemyAI = Cast<AEnemyAIController>(Context.GetOwner());
	APawn* Pawn = EnemyAI ? EnemyAI->GetPawn() : nullptr;
	if (!EnemyAI || !Pawn)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (!EnemyAI->HasHomeLocation())
	{
		return EStateTreeRunStatus::Failed;
	}

	FAeyerjiNavSafetyResolveParams NavParams;
	NavParams.ProjectionExtent = FVector(500.f, 500.f, 1000.f);
	NavParams.SearchRadius = 600.f;

	FVector SafePawnLocation = Pawn->GetActorLocation();
	if (!UAeyerjiNavSafetyLibrary::EnsurePawnOnSafeNav(Pawn, NavParams, /*bRecoverIfOffNav=*/true, SafePawnLocation))
	{
		return EStateTreeRunStatus::Failed;
	}

	CachedDestination = EnemyAI->GetHomeLocation();
	FAeyerjiNavSafetyResult DestinationResult;
	if (!UAeyerjiNavSafetyLibrary::ResolveSafeNavLocationForPawn(Pawn, CachedDestination, Pawn, NavParams, DestinationResult))
	{
		return EStateTreeRunStatus::Failed;
	}

	CachedDestination = DestinationResult.NavLocation;
	if (FVector::Dist2D(Pawn->GetActorLocation(), CachedDestination) <= AcceptableRadius)
	{
		AAeyerjiCombatBalanceTestHarness::RecordTargetingEvent(
			EnemyAI,
			EnemyAI->GetTargetActor(),
			nullptr,
			EAeyerjiCombatTargetingTelemetryEvent::EnemyLeashTargetCleared,
			TEXT("LeashReturnHome"));
		EnemyAI->DropCurrentTarget();
		EnemyAI->ClearLastKnownTarget();
		return EStateTreeRunStatus::Succeeded;
	}

	if (const UWorld* World = Pawn->GetWorld())
	{
		ReturnStartTime = World->GetTimeSeconds();
	}
	else
	{
		ReturnStartTime = 0.0;
	}

	MoveRequestId = EnemyAI->MoveToLocation(
		CachedDestination,
		AcceptableRadius,
		/*bStopOnOverlap=*/true,
		/*bUsePathfinding=*/true,
		/*bProjectGoalLocation=*/false,
		/*bCanStrafe=*/false,
		/*FilterClass=*/nullptr,
		/*bAllowPartialPath=*/false);

	if (!MoveRequestId.IsValid())
	{
		return EStateTreeRunStatus::Failed;
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus USTT_MoveToHomeTask::Tick(FStateTreeExecutionContext& Context, const float /*DeltaTime*/)
{
	AEnemyAIController* EnemyAI = Cast<AEnemyAIController>(Context.GetOwner());
	APawn* Pawn = EnemyAI ? EnemyAI->GetPawn() : nullptr;
	if (!EnemyAI || !Pawn)
	{
		return EStateTreeRunStatus::Failed;
	}

	// The return is committed: a visible target neither aborts the walk nor
	// refreshes the timeout. Arrival drops the chase; failure resumes it.
	const UWorld* World = Pawn->GetWorld();
	if (World && TimeoutSeconds > 0.f && ReturnStartTime >= 0.0
		&& (World->GetTimeSeconds() - ReturnStartTime) >= TimeoutSeconds)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (FVector::Dist2D(Pawn->GetActorLocation(), CachedDestination) <= AcceptableRadius)
	{
		AAeyerjiCombatBalanceTestHarness::RecordTargetingEvent(
			EnemyAI,
			EnemyAI->GetTargetActor(),
			nullptr,
			EAeyerjiCombatTargetingTelemetryEvent::EnemyLeashTargetCleared,
			TEXT("LeashReturnHome"));
		EnemyAI->DropCurrentTarget();
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
		FAeyerjiNavSafetyResolveParams NavParams;
		NavParams.ProjectionExtent = FVector(500.f, 500.f, 1000.f);
		FVector SafePawnLocation = Pawn->GetActorLocation();
		if (!UAeyerjiNavSafetyLibrary::EnsurePawnOnSafeNav(Pawn, NavParams, /*bRecoverIfOffNav=*/true, SafePawnLocation))
		{
			return EStateTreeRunStatus::Failed;
		}

		MoveRequestId = EnemyAI->MoveToLocation(
			CachedDestination,
			AcceptableRadius,
			/*bStopOnOverlap=*/true,
			/*bUsePathfinding=*/true,
			/*bProjectGoalLocation=*/false,
			/*bCanStrafe=*/false,
			/*FilterClass=*/nullptr,
			/*bAllowPartialPath=*/false);

		if (!MoveRequestId.IsValid())
		{
			return EStateTreeRunStatus::Failed;
		}
	}

	return EStateTreeRunStatus::Running;
}

void USTT_MoveToHomeTask::ExitState(
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
