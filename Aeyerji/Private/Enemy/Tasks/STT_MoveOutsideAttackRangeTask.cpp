#include "Enemy/Tasks/STT_MoveOutsideAttackRangeTask.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "AIController.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AeyerjiCharacter.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Enemy/EnemyAIController.h"
#include "GameFramework/MovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Navigation/AeyerjiNavSafetyLibrary.h"
#include "Navigation/PathFollowingComponent.h"
#include "StateTreeExecutionContext.h"

namespace
{
	constexpr float FallbackAttackRange = 150.0f;
	const FVector NavigationProjectionExtent(500.0f, 500.0f, 1000.0f);

	bool StopMoveOutsideAttackRangeTaskIfCrowdControlled(AAIController* AI, APawn* Pawn)
	{
		const AAeyerjiCharacter* ControlledCharacter = Cast<AAeyerjiCharacter>(Pawn);
		if (!ControlledCharacter || !ControlledCharacter->IsCrowdControlled())
		{
			return false;
		}

		if (AI)
		{
			AI->StopMovement();
			AI->ClearFocus(EAIFocusPriority::Gameplay);
			AI->ClearFocus(EAIFocusPriority::Move);
		}

		if (UMovementComponent* MovementComponent = Pawn ? Pawn->GetMovementComponent() : nullptr)
		{
			MovementComponent->StopMovementImmediately();
		}

		return true;
	}

	// Pulls the live attack range from GAS and falls back to a small default when it is unavailable.
	float GetMoveOutsideAttackRangeTask_AttackRange(const APawn* Pawn)
	{
		if (!Pawn)
		{
			return FallbackAttackRange;
		}

		if (const UAbilitySystemComponent* AbilitySystemComponent =
			UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn, /*LookForComponent=*/true))
		{
			const float AttackRange = AbilitySystemComponent->GetNumericAttribute(UAeyerjiAttributeSet::GetAttackRangeAttribute());
			if (AttackRange > 0.0f)
			{
				return AttackRange;
			}
		}

		return FallbackAttackRange;
	}

	// Resolves the current combat target from the custom enemy controller.
	AActor* GetMoveOutsideAttackRangeTask_TargetActor(AAIController* AI)
	{
		if (AEnemyAIController* EnemyAI = Cast<AEnemyAIController>(AI))
		{
			return EnemyAI->GetTargetActor();
		}

		return nullptr;
	}

	// Builds a nav-valid point on the line extending away from the target at the requested distance.
	bool GetMoveOutsideAttackRangeTask_RetreatDestination(APawn* Pawn, AActor* TargetActor, const float DesiredDistance, FVector& OutDestination)
	{
		if (!Pawn || !TargetActor)
		{
			return false;
		}

		FVector AwayDirection = Pawn->GetActorLocation() - TargetActor->GetActorLocation();
		if (!AwayDirection.Normalize())
		{
			AwayDirection = Pawn->GetActorForwardVector();
			if (!AwayDirection.Normalize())
			{
				AwayDirection = FVector::ForwardVector;
			}
		}

		const FVector DesiredLocation = TargetActor->GetActorLocation() + (AwayDirection * DesiredDistance);
		FAeyerjiNavSafetyResolveParams NavParams;
		NavParams.ProjectionExtent = NavigationProjectionExtent;
		NavParams.SearchRadius = 600.f;
		FAeyerjiNavSafetyResult DestinationResult;
		if (!UAeyerjiNavSafetyLibrary::ResolveSafeNavLocationForPawn(Pawn, DesiredLocation, Pawn, NavParams, DestinationResult))
		{
			return false;
		}

		OutDestination = DestinationResult.NavLocation;
		return true;
	}

	// Submits a server-side pathing request toward the current retreat destination.
	EPathFollowingRequestResult::Type RequestMoveOutsideAttackRangeTask_Move(AAIController* AI, const FVector& Destination, const float AcceptableRadius)
	{
		if (!AI)
		{
			return EPathFollowingRequestResult::Failed;
		}

		return AI->MoveToLocation(
			Destination,
			AcceptableRadius,
			/*bStopOnOverlap=*/false,
			/*bUsePathfinding=*/true,
			/*bProjectGoalLocation=*/false,
			/*bCanStrafe=*/false,
			/*FilterClass=*/nullptr,
			/*bAllowPartialPath=*/false);
	}
}

USTT_MoveOutsideAttackRangeTask::USTT_MoveOutsideAttackRangeTask(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bShouldCallTick = true;
}

EStateTreeRunStatus USTT_MoveOutsideAttackRangeTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition)
{
	AAIController* AI = Cast<AAIController>(Context.GetOwner());
	APawn* Pawn = AI ? AI->GetPawn() : nullptr;
	if (!AI || !Pawn)
	{
		return EStateTreeRunStatus::Failed;
	}
	if (StopMoveOutsideAttackRangeTaskIfCrowdControlled(AI, Pawn))
	{
		return EStateTreeRunStatus::Failed;
	}

	FAeyerjiNavSafetyResolveParams NavParams;
	NavParams.ProjectionExtent = NavigationProjectionExtent;
	FVector SafePawnLocation = Pawn->GetActorLocation();
	if (!UAeyerjiNavSafetyLibrary::EnsurePawnOnSafeNav(Pawn, NavParams, /*bRecoverIfOffNav=*/true, SafePawnLocation))
	{
		return EStateTreeRunStatus::Failed;
	}

	AActor* TargetActor = GetMoveOutsideAttackRangeTask_TargetActor(AI);
	if (!TargetActor)
	{
		return EStateTreeRunStatus::Failed;
	}

	bHasCachedRetreatDestination = false;
	CachedRetreatDestination = FVector::ZeroVector;

	const float AttackRange = GetMoveOutsideAttackRangeTask_AttackRange(Pawn);
	const float DesiredDistance = AttackRange + AttackRangeExtension;
	const float RetreatGoalDistance = DesiredDistance + AcceptableRadius;
	const float CurrentDistance = FVector::Dist(Pawn->GetActorLocation(), TargetActor->GetActorLocation());

	if (CurrentDistance >= DesiredDistance)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	// Push the goal slightly farther out so MoveToLocation acceptance does not leave the pawn inside true attack range.
	if (!GetMoveOutsideAttackRangeTask_RetreatDestination(Pawn, TargetActor, RetreatGoalDistance, CachedRetreatDestination))
	{
		return EStateTreeRunStatus::Failed;
	}

	bHasCachedRetreatDestination = true;

	const EPathFollowingRequestResult::Type MoveResult =
		RequestMoveOutsideAttackRangeTask_Move(AI, CachedRetreatDestination, AcceptableRadius);

	if (MoveResult != EPathFollowingRequestResult::RequestSuccessful)
	{
		return EStateTreeRunStatus::Failed;
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus USTT_MoveOutsideAttackRangeTask::Tick(FStateTreeExecutionContext& Context, const float DeltaTime)
{
	AAIController* AI = Cast<AAIController>(Context.GetOwner());
	APawn* Pawn = AI ? AI->GetPawn() : nullptr;
	if (!AI || !Pawn)
	{
		return EStateTreeRunStatus::Failed;
	}
	if (StopMoveOutsideAttackRangeTaskIfCrowdControlled(AI, Pawn))
	{
		return EStateTreeRunStatus::Failed;
	}

	FAeyerjiNavSafetyResolveParams NavParams;
	NavParams.ProjectionExtent = NavigationProjectionExtent;
	FVector SafePawnLocation = Pawn->GetActorLocation();
	if (!UAeyerjiNavSafetyLibrary::EnsurePawnOnSafeNav(Pawn, NavParams, /*bRecoverIfOffNav=*/true, SafePawnLocation))
	{
		return EStateTreeRunStatus::Failed;
	}

	AActor* TargetActor = GetMoveOutsideAttackRangeTask_TargetActor(AI);
	if (!TargetActor)
	{
		return EStateTreeRunStatus::Failed;
	}

	const float AttackRange = GetMoveOutsideAttackRangeTask_AttackRange(Pawn);
	const float DesiredDistance = AttackRange + AttackRangeExtension;
	const float RetreatGoalDistance = DesiredDistance + AcceptableRadius;
	const float CurrentDistance = FVector::Dist(Pawn->GetActorLocation(), TargetActor->GetActorLocation());
	if (CurrentDistance >= DesiredDistance)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	EPathFollowingStatus::Type MoveStatus = EPathFollowingStatus::Idle;
	if (const UPathFollowingComponent* PathFollowingComponent = AI->GetPathFollowingComponent())
	{
		MoveStatus = PathFollowingComponent->GetStatus();
	}

	FVector RetreatDestination = CachedRetreatDestination;
	const bool bHasFreshRetreatDestination =
		GetMoveOutsideAttackRangeTask_RetreatDestination(Pawn, TargetActor, RetreatGoalDistance, RetreatDestination);

	bool bShouldRepath = (MoveStatus == EPathFollowingStatus::Idle)
		|| (MoveStatus == EPathFollowingStatus::Paused)
		|| (MoveStatus == EPathFollowingStatus::Waiting);

	if (bHasFreshRetreatDestination)
	{
		const bool bRetreatDestinationChanged = !bHasCachedRetreatDestination
			|| FVector::DistSquared(CachedRetreatDestination, RetreatDestination) > FMath::Square(RepathDistanceThreshold);

		if (bRetreatDestinationChanged)
		{
			CachedRetreatDestination = RetreatDestination;
			bHasCachedRetreatDestination = true;
			bShouldRepath = true;
		}
	}
	else if (!bHasCachedRetreatDestination)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (bShouldRepath)
	{
		const EPathFollowingRequestResult::Type MoveResult =
			RequestMoveOutsideAttackRangeTask_Move(AI, CachedRetreatDestination, AcceptableRadius);

		if (MoveResult != EPathFollowingRequestResult::RequestSuccessful)
		{
			return EStateTreeRunStatus::Failed;
		}
	}

	return EStateTreeRunStatus::Running;
}

void USTT_MoveOutsideAttackRangeTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition)
{
	if (AAIController* AI = Cast<AAIController>(Context.GetOwner()))
	{
		AI->StopMovement();
	}
}
