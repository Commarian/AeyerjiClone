// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "STT_FocusTargetTask.generated.h"
#pragma once

/**
 * StateTree Task that continuously focuses the AI controller on its target actor.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Focus Target"))
class AEYERJI_API USTT_FocusTargetTask : public UStateTreeTaskBlueprintBase
{
	GENERATED_BODY()

public:
    // Called when the state starts: sets initial focus and completes immediately.
    virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) override;

    // On exit, clear the focus.
    virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) override;
};
