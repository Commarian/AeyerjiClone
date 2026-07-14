// Copyright (c) 2025 Aeyerji.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "STC_HasDefenseObjectiveTargetCondition.generated.h"

/** StateTree condition that checks whether this AI has a live survival defense objective assigned. */
UCLASS(Blueprintable, meta=(DisplayName="Has Defense Objective Target?"))
class AEYERJI_API USTC_HasDefenseObjectiveTargetCondition : public UStateTreeConditionBlueprintBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Condition")
	bool bRequireAliveTarget = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Condition")
	bool bNegate = false;

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};
