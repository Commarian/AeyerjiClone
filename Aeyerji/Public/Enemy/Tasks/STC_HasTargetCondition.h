// STC_HasTargetCondition.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "STC_HasTargetCondition.generated.h"

/**
 * StateTree Condition: checks if the AI controller currently has a valid target.
 * - Optionally negated to express "No Target" with the same node.
 */
UCLASS(Blueprintable, meta=(DisplayName="Has Target?"))
class AEYERJI_API USTC_HasTargetCondition : public UStateTreeConditionBlueprintBase
{
    GENERATED_BODY()

public:
    // When true, inverts the result (i.e., returns true when there is NO target).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Condition")
    bool bNegate = false;

    // When true, treats actors tagged with "State.Dead" as invalid targets.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Condition")
    bool bRequireAliveTarget = true;

    virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};

