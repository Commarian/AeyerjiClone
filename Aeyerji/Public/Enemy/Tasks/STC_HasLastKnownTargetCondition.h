// STC_HasLastKnownTargetCondition.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "STC_HasLastKnownTargetCondition.generated.h"

/**
 * StateTree condition that passes when the enemy has a cached last-known target
 * location and, by default, no active CurrentTarget.
 */
UCLASS(Blueprintable, meta=(DisplayName="Has Last Known Target?"))
class AEYERJI_API USTC_HasLastKnownTargetCondition : public UStateTreeConditionBlueprintBase
{
	GENERATED_BODY()

public:
	/** When true, inverts the result. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Condition")
	bool bNegate = false;

	/** When true, only passes if the controller has no active CurrentTarget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Condition")
	bool bRequireNoCurrentTarget = true;

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};
