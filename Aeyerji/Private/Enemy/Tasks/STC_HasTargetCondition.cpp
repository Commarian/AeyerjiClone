// STC_HasTargetCondition.cpp
#include "Enemy/Tasks/STC_HasTargetCondition.h"
#include "Enemy/EnemyAIController.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "AIController.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "StateTreeExecutionContext.h"

bool USTC_HasTargetCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
    AAIController* AI = Cast<AAIController>(Context.GetOwner());
    if (!AI)
    {
        return bNegate ? true : false; // No AI -> treat as no target
    }

    AActor* Target = nullptr;
	AEnemyAIController* EnemyAI = Cast<AEnemyAIController>(AI);
    if (EnemyAI)
    {
		Target = EnemyAI->GetTargetActor();
	}

	bool bHas = Target != nullptr;
	if (bHas && bRequireAliveTarget && EnemyAI)
	{
		bHas = EnemyAI->EnsureCurrentTargetIsLive();
	}

	return bNegate ? !bHas : bHas;
}
