// Fill out your copyright notice in the Description page of Project Settings.


#include "Enemy/Tasks/STT_FindPatrolTask.h"
#include "AIController.h"
#include "NavigationSystem.h"
#include "AbilitySystemComponent.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "GameFramework/Pawn.h"
#include "StateTreeExecutionContext.h"
#include "Enemy/EnemyAIController.h"

USTT_FindPatrolTask::USTT_FindPatrolTask(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer), OverridePatrolRadius(0.0f), PatrolLocation(FVector::ZeroVector)
{
}

EStateTreeRunStatus USTT_FindPatrolTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition)
{
    // Get AI controller and pawn
    AAIController* AI = Cast<AAIController>(Context.GetOwner());
    APawn* Pawn = AI ? AI->GetPawn() : nullptr;
    if (!Pawn)
    {
        return EStateTreeRunStatus::Failed;
    }

    // If we already have a combat target, skip finding patrol and let higher-priority
    // states handle chasing/attacking immediately.
    if (AI && AI->IsA<AEnemyAIController>())
    {
        if (Cast<AEnemyAIController>(AI)->GetTargetActor() != nullptr)
        {
            return EStateTreeRunStatus::Failed;
        }
    }

	// Determine the base "home" location for patrolling
	FVector Home = HomeLocation;
	if (Home.IsNearlyZero())
	{
		// If HomeLocation not explicitly set, use the pawn's stored home (spawn) location if available
		if (AI->IsA<AEnemyAIController>())
		{
			Home = Cast<AEnemyAIController>(AI)->GetHomeLocation();
		}
		if (Home.IsNearlyZero())
		{
			Home = Pawn->GetActorLocation();
		}
	}
	
	// Determine patrol radius: use override if set, otherwise use attribute from GAS
	float Radius = OverridePatrolRadius;
	if (Radius <= 0.f)  // no override
	{
		if (UAbilitySystemComponent* ASC = Pawn->FindComponentByClass<UAbilitySystemComponent>())
		{
			Radius = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetPatrolRadiusAttribute());
		}
	}
	// Safety clamp on radius
	if (Radius < 50.f)
	{
		return EStateTreeRunStatus::Failed;
	}

	// Find a random reachable point on the nav-mesh
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Pawn->GetWorld());
	if (!NavSys)
	{
		return EStateTreeRunStatus::Failed;
	}

	/* Make sure the centre is actually on the nav-mesh. */
	// Expand projection so a mid-capsule origin still reaches a thin nav mesh.
	FVector QueryExtent(150.f, 150.f, 400.f);
	const float CollisionRadius = Pawn->GetSimpleCollisionRadius();
	const float CollisionHalfHeight = Pawn->GetSimpleCollisionHalfHeight();
	if (CollisionRadius > 0.f)
	{
		QueryExtent.X = QueryExtent.Y = FMath::Max(QueryExtent.X, CollisionRadius + 30.f);
	}
	if (CollisionHalfHeight > 0.f)
	{
		QueryExtent.Z = FMath::Max(QueryExtent.Z, CollisionHalfHeight + 100.f);
	}

	FNavLocation NavHome;
	if (!NavSys->ProjectPointToNavigation(
			Home,               // point we’d like to start from
			NavHome,            // out: projected point
			QueryExtent,        // search box extents tuned to pawn
			nullptr,            // nav-data (null = default for this agent)
			nullptr))           // filter
	{
		UE_LOG(LogTemp, Display, TEXT("STT_FindPatrolTask: Home (%s) not on nav-mesh."),
			   *Home.ToString());
		return EStateTreeRunStatus::Failed;
	}

	/*Now look for a reachable point inside Radius. */
	FNavLocation RandomPoint;
	const bool bFound = NavSys->GetRandomReachablePointInRadius(
			NavHome.Location,   // centre we just projected
			Radius,
			RandomPoint);

	if (!bFound)
	{
		UE_LOG(LogTemp, Display,
			TEXT("STT_FindPatrolTask: No patrol point found (R=%.0f, Centre=%s)."),
			Radius, *NavHome.Location.ToString());
		return EStateTreeRunStatus::Failed;
	}

	/* 3??  Success – write the output and finish. */
	PatrolLocation = RandomPoint.Location;
	return EStateTreeRunStatus::Succeeded;
}
