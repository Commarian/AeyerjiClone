// Fill out your copyright notice in the Description page of Project Settings.


#include "Enemy/Tasks/STT_FocusTargetTask.h"
#include "AIController.h"
#include "StateTreeExecutionContext.h"
#include "Enemy/EnemyAIController.h"

EStateTreeRunStatus USTT_FocusTargetTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition)
{
    // On entering the state, immediately set focus if a target exists.
    AAIController* AI = Cast<AAIController>(Context.GetOwner());
    if (AI && AI->GetFocusActor() == nullptr)  // GetFocusActor() returns current focused actor if any
    {
        AActor* Target = nullptr;
        if (AI->IsA<AEnemyAIController>())
        {
            Target = Cast<AEnemyAIController>(AI)->GetTargetActor();
        }
        if (Target)
        {
            AI->SetFocus(Target, EAIFocusPriority::Gameplay);
        }
    }
    // Succeed immediately so this task does not block state completion.
    return EStateTreeRunStatus::Succeeded;
}

// No Tick needed; focus remains set until ExitState.

void USTT_FocusTargetTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition)
{
    // When the state ends or this task is interrupted, clear the focus.
    if (AAIController* AI = Cast<AAIController>(Context.GetOwner()))
    {
        AI->ClearFocus(EAIFocusPriority::Gameplay);
    }
}
