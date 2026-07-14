// Copyright (c) 2025 Aeyerji.

#include "Enemy/Tasks/STC_HasDefenseObjectiveTargetCondition.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AeyerjiGameplayTags.h"
#include "Enemy/EnemyAIController.h"
#include "StateTreeExecutionContext.h"

namespace
{
	bool IsDefenseActorDead(const AActor* Actor)
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

bool USTC_HasDefenseObjectiveTargetCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const AEnemyAIController* EnemyAI = Cast<AEnemyAIController>(Context.GetOwner());
	const AActor* Objective = EnemyAI ? EnemyAI->GetDefenseObjectiveTargetActor() : nullptr;
	bool bHasObjective = IsValid(Objective);

	if (bHasObjective && bRequireAliveTarget)
	{
		bHasObjective = !IsDefenseActorDead(Objective);
	}

	return bNegate ? !bHasObjective : bHasObjective;
}
