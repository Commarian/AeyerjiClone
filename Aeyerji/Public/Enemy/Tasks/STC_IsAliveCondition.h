// STC_IsAliveCondition.h
#pragma once

#include "CoreMinimal.h"
#include "StateTreeConditionBase.h"
#include "STC_IsAliveCondition.generated.h"

/**
 * StateTree condition: true when the controlled pawn is alive (valid and free
 * of the State.Dead tag on both actor tags and the ASC). Struct-backed so
 * StateTree tooling can instance it directly; takes no parameters.
 */
USTRUCT(meta=(DisplayName="Is Alive?"))
struct AEYERJI_API FSTC_IsAliveCondition : public FStateTreeConditionBase
{
	GENERATED_BODY()

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};
