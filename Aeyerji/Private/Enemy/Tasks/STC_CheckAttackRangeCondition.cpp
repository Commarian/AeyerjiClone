// Fill out your copyright notice in the Description page of Project Settings.


#include "Enemy/Tasks/STC_CheckAttackRangeCondition.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "AIController.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Components/BoxComponent.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Pawn.h"
#include "StateTreeExecutionContext.h"
#include "Enemy/EnemyAIController.h"
#include "Logging/AeyerjiLog.h"
#include "World/AeyerjiSurvivalDefenseObjectiveActor.h"

namespace
{
	constexpr double BossPrimaryAttackRangeLogIntervalSeconds = 0.5;

	bool TryGetClosestAttackRangeCollisionPoint(const AActor* TargetActor, const FVector& QueryOrigin, FVector& OutClosestPoint)
	{
		if (!TargetActor)
		{
			return false;
		}

		TInlineComponentArray<UPrimitiveComponent*> PrimitiveComponents;
		TargetActor->GetComponents(PrimitiveComponents);

		float BestDistanceSq = TNumericLimits<float>::Max();
		bool bFoundPoint = false;
		for (const UPrimitiveComponent* PrimitiveComponent : PrimitiveComponents)
		{
			if (!PrimitiveComponent
				|| !PrimitiveComponent->IsRegistered()
				|| PrimitiveComponent->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
			{
				continue;
			}

			float DistanceSq = 0.f;
			FVector ClosestPoint = FVector::ZeroVector;
			if (!PrimitiveComponent->GetSquaredDistanceToCollision(QueryOrigin, DistanceSq, ClosestPoint))
			{
				continue;
			}

			if (!bFoundPoint || DistanceSq < BestDistanceSq)
			{
				BestDistanceSq = DistanceSq;
				OutClosestPoint = ClosestPoint;
				bFoundPoint = true;
			}
		}

		return bFoundPoint;
	}

	bool TryGetPreferredAttackRangeCollisionPoint(const AActor* TargetActor, const FVector& QueryOrigin, FVector& OutClosestPoint)
	{
		const AAeyerjiSurvivalDefenseObjectiveActor* DefenseObjective = Cast<AAeyerjiSurvivalDefenseObjectiveActor>(TargetActor);
		const UPrimitiveComponent* TargetCollision = DefenseObjective ? DefenseObjective->TargetCollision.Get() : nullptr;
		if (!TargetCollision
			|| !TargetCollision->IsRegistered()
			|| TargetCollision->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
		{
			return false;
		}

		float DistanceSq = 0.f;
		return TargetCollision->GetSquaredDistanceToCollision(QueryOrigin, DistanceSq, OutClosestPoint);
	}

	float GetAttackRangeConditionDistance(const APawn* Pawn, const AActor* TargetActor)
	{
		if (!Pawn || !TargetActor)
		{
			return TNumericLimits<float>::Max();
		}

		if (TargetActor->IsA<APawn>())
		{
			return FVector::Dist(Pawn->GetActorLocation(), TargetActor->GetActorLocation());
		}

		FVector ClosestCollisionPoint = FVector::ZeroVector;
		if (TryGetPreferredAttackRangeCollisionPoint(TargetActor, Pawn->GetActorLocation(), ClosestCollisionPoint))
		{
			return FVector::Dist(Pawn->GetActorLocation(), ClosestCollisionPoint);
		}

		if (TryGetClosestAttackRangeCollisionPoint(TargetActor, Pawn->GetActorLocation(), ClosestCollisionPoint))
		{
			return FVector::Dist(Pawn->GetActorLocation(), ClosestCollisionPoint);
		}

		const FBox TargetBounds = TargetActor->GetComponentsBoundingBox(/*bNonColliding=*/false);
		const FVector TargetPoint = TargetBounds.IsValid
			? TargetBounds.GetClosestPointTo(Pawn->GetActorLocation())
			: TargetActor->GetActorLocation();
		return FVector::Dist(Pawn->GetActorLocation(), TargetPoint);
	}

#define BOSS_PRIMARY_AJ_LOG(Verbosity, ObjPtr, Fmt, ...) \
	do { \
		const UObject* BossPrimaryLogObj = Cast<const UObject>(ObjPtr); \
		UE_LOG(LogAeyerji, Verbosity, TEXT("[%s] %s: " Fmt), \
			Aeyerji::Detail::GetSide(BossPrimaryLogObj), \
			*Aeyerji::Detail::GetClass(BossPrimaryLogObj), \
			##__VA_ARGS__); \
	} while (0)
}

bool USTC_CheckAttackRangeCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	AAIController* AI = Cast<AAIController>(Context.GetOwner());
	APawn* Pawn = AI ? AI->GetPawn() : nullptr;
	const double Now = Pawn && Pawn->GetWorld() ? Pawn->GetWorld()->GetTimeSeconds() : 0.0;
	auto LogDecision = [&](const bool bInRange, const TCHAR* Reason, AActor* TargetActor, const float Distance, const float AttackRangeValue)
	{
		const bool bShouldLog = !bHasLoggedRangeDecision
			|| bLastRangeDecision != bInRange
			|| LastRangeDecisionLogTime < 0.0
			|| (Now - LastRangeDecisionLogTime) >= BossPrimaryAttackRangeLogIntervalSeconds;

		if (!bShouldLog)
		{
			return;
		}

		LastRangeDecisionLogTime = Now;
		bHasLoggedRangeDecision = true;
		bLastRangeDecision = bInRange;

		BOSS_PRIMARY_AJ_LOG(VeryVerbose, this, TEXT("[BossPrimaryAttack] RangeCondition result=%s reason=%s pawn=%s target=%s distance=%.1f attackRange=%.1f tolerance=%.1f"),
			bInRange ? TEXT("true") : TEXT("false"),
			Reason,
			*GetNameSafe(Pawn),
			*GetNameSafe(TargetActor),
			Distance,
			AttackRangeValue,
			RangeTolerance);
	};

	if (!AI || !Pawn)
	{
		LogDecision(false, TEXT("MissingAIOrPawn"), nullptr, -1.f, -1.f);
		return false;
	}

	// Get current target actor from our AI controller
	AActor* Target = nullptr;
	if (AI->IsA<AEnemyAIController>())
	{
		Target = Cast<AEnemyAIController>(AI)->GetTargetActor();
	}
	if (!Target)
	{
		LogDecision(false, TEXT("NoTarget"), nullptr, -1.f, -1.f);
		return false;  // No target means not in range
	}

	// Get attack range value from the pawn's attributes
	float AttackRange = 0.f;
	if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn, /*LookForComponent=*/true))
	{
		AttackRange = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetAttackRangeAttribute());
	}
	if (AttackRange <= 0.f)
	{
		AttackRange = 150.0f; // default fallback
	}

	AttackRange += RangeTolerance; // apply any extra tolerance
	const float Distance = GetAttackRangeConditionDistance(Pawn, Target);
	const bool bInRange = Distance <= AttackRange;

	LogDecision(bInRange, bInRange ? TEXT("WithinRange") : TEXT("OutOfRange"), Target, Distance, AttackRange);
	return bInRange;
}
