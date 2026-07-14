// STC_HasTargetCondition.cpp
#include "Enemy/Tasks/STC_HasTargetCondition.h"
#include "Enemy/EnemyAIController.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AeyerjiGameplayTags.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "AIController.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "StateTreeExecutionContext.h"

namespace
{
	bool HasDeadState(const AActor* Actor)
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

bool USTC_HasTargetCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
    AAIController* AI = Cast<AAIController>(Context.GetOwner());
    if (!AI)
    {
        return bNegate ? true : false; // No AI -> treat as no target
    }

    AActor* Target = nullptr;
    if (AI->IsA<AEnemyAIController>())
    {
        Target = Cast<AEnemyAIController>(AI)->GetTargetActor();
	}

	bool bHas = Target != nullptr;
	if (bHas && bRequireAliveTarget)
	{
		bHas = !HasDeadState(Target);
	}

	return bNegate ? !bHas : bHas;
}
