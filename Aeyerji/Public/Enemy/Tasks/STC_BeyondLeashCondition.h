// STC_BeyondLeashCondition.h
#pragma once

#include "CoreMinimal.h"
#include "StateTreeConditionBase.h"
#include "STC_BeyondLeashCondition.generated.h"

/**
 * StateTree condition: true when the pawn strayed past its archetype's leash
 * distance from the controller's checkout home location. Struct-backed (rather
 * than BlueprintBase like the STC_* siblings) so StateTree tooling can instance
 * it directly; it takes no parameters since the range resolves from the
 * archetype at runtime, and pawns without a home or with a non-positive range
 * never leash.
 */
USTRUCT(meta=(DisplayName="Beyond Leash Range?"))
struct AEYERJI_API FSTC_BeyondLeashCondition : public FStateTreeConditionBase
{
	GENERATED_BODY()

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};
