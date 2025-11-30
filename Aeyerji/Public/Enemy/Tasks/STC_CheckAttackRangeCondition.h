// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "STC_CheckAttackRangeCondition.generated.h"
#pragma once

/**
 * StateTree Condition that checks if the AI's target is within attack range.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Target In Attack Range?"))
class AEYERJI_API USTC_CheckAttackRangeCondition : public UStateTreeConditionBlueprintBase
{
	GENERATED_BODY()

public:
	// Optional tolerance or extension to attack range (e.g., 0 for exact, or a buffer value)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Range")
	float RangeTolerance = 0.0f;

	// Evaluate the condition: returns true if target is in range.
	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};
