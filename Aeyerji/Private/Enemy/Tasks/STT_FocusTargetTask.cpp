// Fill out your copyright notice in the Description page of Project Settings.


#include "Enemy/Tasks/STT_FocusTargetTask.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "AIController.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "AeyerjiCharacter.h"
#include "StateTreeExecutionContext.h"
#include "Enemy/EnemyAIController.h"

EStateTreeRunStatus USTT_FocusTargetTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition)
{
    // On entering the state, immediately set focus if a target exists.
    AAIController* AI = Cast<AAIController>(Context.GetOwner());
    if (AI)
    {
        if (const AAeyerjiCharacter* ControlledCharacter = Cast<AAeyerjiCharacter>(AI->GetPawn()))
        {
            if (ControlledCharacter->IsCrowdControlled())
            {
                AI->ClearFocus(EAIFocusPriority::Gameplay);
                AI->ClearFocus(EAIFocusPriority::Move);
                return EStateTreeRunStatus::Succeeded;
            }
        }
    }

    if (AI)
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
        else
        {
            AI->ClearFocus(EAIFocusPriority::Gameplay);
        }
    }
    // Succeed immediately so this task does not block state completion.
    return EStateTreeRunStatus::Succeeded;
}

// No Tick needed; AEnemyAIController owns target/focus lifetime between StateTree branch transitions.

void USTT_FocusTargetTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition)
{
    if (AAIController* AI = Cast<AAIController>(Context.GetOwner()))
    {
        bool bKeepEnemyTargetFocus = false;
        if (AEnemyAIController* EnemyAI = Cast<AEnemyAIController>(AI))
        {
            const AAeyerjiCharacter* ControlledCharacter = Cast<AAeyerjiCharacter>(AI->GetPawn());
            bKeepEnemyTargetFocus = IsValid(EnemyAI->GetTargetActor())
                && (!ControlledCharacter || !ControlledCharacter->IsCrowdControlled());
        }

        // A StateTree combat branch ending is not a target-loss event. Retaining the
        // controller-owned target prevents desired yaw from falling back to Detour
        // Crowd's rapidly alternating avoidance direction between branch transitions.
        if (!bKeepEnemyTargetFocus)
        {
            AI->ClearFocus(EAIFocusPriority::Gameplay);
        }
    }
}
