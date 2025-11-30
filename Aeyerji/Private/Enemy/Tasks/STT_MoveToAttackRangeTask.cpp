// Fill out your copyright notice in the Description page of Project Settings.


#include "Enemy/Tasks/STT_MoveToAttackRangeTask.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "Navigation/PathFollowingComponent.h"
#include "AbilitySystemComponent.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "StateTreeExecutionContext.h"
#include "Enemy/EnemyAIController.h"

USTT_MoveToAttackRangeTask::USTT_MoveToAttackRangeTask(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bShouldCallTick = true;
}

EStateTreeRunStatus USTT_MoveToAttackRangeTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition)
{
	AAIController* AI = Cast<AAIController>(Context.GetOwner());
	APawn* Pawn = AI ? AI->GetPawn() : nullptr;
	if (!AI || !Pawn)
	{
		return EStateTreeRunStatus::Failed;
	}

	AActor* TargetActor = nullptr;
	if (AI->IsA<AEnemyAIController>())
	{
		TargetActor = Cast<AEnemyAIController>(AI)->GetTargetActor();
	}
	if (!TargetActor)
	{
		return EStateTreeRunStatus::Failed;
	}

	// Query attack range from the pawn's Ability System (GAS AttributeSet)
	float AttackRange = 0.f;
	if (UAbilitySystemComponent* ASC = Pawn->FindComponentByClass<UAbilitySystemComponent>())
	{
		AttackRange = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetAttackRangeAttribute());
	}
	if (AttackRange <= 0.f)
	{
		AttackRange = 150.0f;
	}

	// Already in range? succeed and let the parent transition to Attack
	if (FVector::Dist(Pawn->GetActorLocation(), TargetActor->GetActorLocation()) <= (AttackRange - AttackRangeReduction))
	{
		return EStateTreeRunStatus::Succeeded;
	}

	// Aim to stop just inside attack range
    // Aim to stop just inside attack range, measuring center-to-center only.
    const float AcceptMoveRadius = FMath::Max(0.f, AttackRange - AttackRangeReduction);
    EPathFollowingRequestResult::Type Result = AI->MoveToActor(
        TargetActor,
        AcceptMoveRadius,
        /*bStopOnOverlap=*/false /* do not add agent/goal radii */);
	if (Result == EPathFollowingRequestResult::Failed)
	{
		return EStateTreeRunStatus::Failed;
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus USTT_MoveToAttackRangeTask::Tick(FStateTreeExecutionContext& Context, float DeltaTime)
{
	AAIController* AI = Cast<AAIController>(Context.GetOwner());
	APawn* Pawn = AI ? AI->GetPawn() : nullptr;
	if (!AI || !Pawn)
	{
		return EStateTreeRunStatus::Failed;
	}

	AActor* TargetActor = nullptr;
	if (AI->IsA<AEnemyAIController>())
	{
		TargetActor = Cast<AEnemyAIController>(AI)->GetTargetActor();
	}
	if (!TargetActor)
	{
		return EStateTreeRunStatus::Failed;
	}

	float AttackRange = 0.f;
	if (UAbilitySystemComponent* ASC = Pawn->FindComponentByClass<UAbilitySystemComponent>())
	{
		AttackRange = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetAttackRangeAttribute());
	}
	if (AttackRange <= 0.f)
	{
		AttackRange = 150.f;
	}

	const float Distance = FVector::Dist(Pawn->GetActorLocation(), TargetActor->GetActorLocation());
	if (Distance <= AttackRange)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	EPathFollowingStatus::Type MoveStatus = EPathFollowingStatus::Idle;
	if (AI->GetPathFollowingComponent())
	{
		MoveStatus = AI->GetPathFollowingComponent()->GetStatus();
	}

	if (MoveStatus == EPathFollowingStatus::Idle ||
		MoveStatus == EPathFollowingStatus::Paused ||
		MoveStatus == EPathFollowingStatus::Waiting)
	{
        const float AcceptMoveRadius = FMath::Max(0.f, AttackRange - AttackRangeReduction);
        const EPathFollowingRequestResult::Type Result = AI->MoveToActor(
            TargetActor,
            AcceptMoveRadius,
            /*bStopOnOverlap=*/false);
		if (Result == EPathFollowingRequestResult::Failed)
		{
			return EStateTreeRunStatus::Failed;
		}
		return EStateTreeRunStatus::Running;
	}

	return EStateTreeRunStatus::Running;
}

void USTT_MoveToAttackRangeTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition)
{
	// Ensure movement is stopped when leaving this state (in case of abort or transition)
	if (AAIController* AI = Cast<AAIController>(Context.GetOwner()))
	{
		AI->StopMovement();
	}
}
