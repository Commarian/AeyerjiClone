// STC_HasTargetCondition.cpp
#include "Enemy/Tasks/STC_HasTargetCondition.h"
#include "Enemy/EnemyAIController.h"
#include "AIController.h"
#include "StateTreeExecutionContext.h"

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
        bHas = !Target->Tags.Contains("State.Dead");
    }

    return bNegate ? !bHas : bHas;
}

