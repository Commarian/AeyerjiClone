// Copyright (c) 2025 Aeyerji.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "STC_TargetIsDefenseObjectiveCondition.generated.h"

/** StateTree condition that passes when CurrentTarget is the assigned survival defense objective. */
UCLASS(Blueprintable, meta=(DisplayName="Target Is Defense Objective?"))
class AEYERJI_API USTC_TargetIsDefenseObjectiveCondition : public UStateTreeConditionBlueprintBase
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Condition")
	bool bRequireAliveTarget = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Condition")
	bool bNegate = false;

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};
