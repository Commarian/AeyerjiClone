// Copyright (c) 2025 Aeyerji.

#include "Enemy/Tasks/STC_TargetIsDefenseObjectiveCondition.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AeyerjiGameplayTags.h"
#include "Enemy/EnemyAIController.h"
#include "StateTreeExecutionContext.h"

namespace
{
	bool IsTargetObjectiveDead(const AActor* Actor)
	{
		if (!IsValid(Actor))
		{
			return true;
		}

		if (Actor->Tags.Contains(AeyerjiTags::State_Dead.GetTag().GetTagName()))
		{
			return true;
		}

		const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor, /*LookForComponent=*/true);
		return ASC && ASC->HasMatchingGameplayTag(AeyerjiTags::State_Dead);
	}
}

bool USTC_TargetIsDefenseObjectiveCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const AEnemyAIController* EnemyAI = Cast<AEnemyAIController>(Context.GetOwner());
	const AActor* Objective = EnemyAI ? EnemyAI->GetDefenseObjectiveTargetActor() : nullptr;
	const AActor* Target = EnemyAI ? EnemyAI->GetTargetActor() : nullptr;

	bool bIsObjective = IsValid(Objective) && Target == Objective;
	if (bIsObjective && bRequireAliveTarget)
	{
		bIsObjective = !IsTargetObjectiveDead(Objective);
	}

	return bNegate ? !bIsObjective : bIsObjective;
}
