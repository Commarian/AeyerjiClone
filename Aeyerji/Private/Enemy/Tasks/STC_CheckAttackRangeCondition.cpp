// Fill out your copyright notice in the Description page of Project Settings.


#include "Enemy/Tasks/STC_CheckAttackRangeCondition.h"
#include "Enemy/Tasks/STC_CheckAttackRangeCondition.h"
#include "AIController.h"
#include "AbilitySystemComponent.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "GameFramework/Pawn.h"
#include "StateTreeExecutionContext.h"
#include "Enemy/EnemyAIController.h"

bool USTC_CheckAttackRangeCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	AAIController* AI = Cast<AAIController>(Context.GetOwner());
	APawn* Pawn = AI ? AI->GetPawn() : nullptr;
	if (!AI || !Pawn)
	{
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
		return false;  // No target means not in range
	}

	// Get attack range value from the pawn's attributes
	float AttackRange = 0.f;
	if (UAbilitySystemComponent* ASC = Pawn->FindComponentByClass<UAbilitySystemComponent>())
	{
		AttackRange = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetAttackRangeAttribute());
	}
	if (AttackRange <= 0.f)
	{
		AttackRange = 150.0f; // default fallback
	}

	AttackRange += RangeTolerance; // apply any extra tolerance
	float Distance = FVector::Dist(Pawn->GetActorLocation(), Target->GetActorLocation());
	
	return Distance <= AttackRange;
}
