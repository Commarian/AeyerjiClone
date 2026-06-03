// STC_HasLastKnownTargetCondition.cpp

#include "Enemy/Tasks/STC_HasLastKnownTargetCondition.h"
#include "Enemy/EnemyAIController.h"
#include "StateTreeExecutionContext.h"

bool USTC_HasLastKnownTargetCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	AEnemyAIController* EnemyAI = Cast<AEnemyAIController>(Context.GetOwner());
	if (!EnemyAI)
	{
		return bNegate ? true : false;
	}

	const bool bHasCurrentTarget = (EnemyAI->GetTargetActor() != nullptr);
	const bool bHasLastKnownTarget = EnemyAI->HasLastKnownTarget();
	const bool bPass = bHasLastKnownTarget && (!bRequireNoCurrentTarget || !bHasCurrentTarget);

	return bNegate ? !bPass : bPass;
}
