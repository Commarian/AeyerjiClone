#pragma once

#include "CoreMinimal.h"
#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "Systems/AeyerjiWorldStateTypes.h"
#include "STC_WorldStateCondition.generated.h"

/**
 * StateTree condition that queries the central Aeyerji world-state registry.
 */
UCLASS(Blueprintable, meta=(DisplayName="World State Condition"))
class AEYERJI_API USTC_WorldStateCondition : public UStateTreeConditionBlueprintBase
{
	GENERATED_BODY()

public:
	/** World-state tag to query. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World State")
	FGameplayTag StateTag;

	/** Optional instance id for a unique registered object or fact. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World State")
	FName InstanceId = NAME_None;

	/** Optional owner id for character/profile-scoped facts. Leave empty for global and run facts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World State")
	FName OwnerId = NAME_None;

	/** Operation used to compare the actual registry value against ExpectedValue. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World State")
	EAeyerjiWorldStateCompareOp CompareOp = EAeyerjiWorldStateCompareOp::Exists;

	/** Expected value used by comparison operations other than Exists/DoesNotExist. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World State", meta=(EditCondition="CompareOp != EAeyerjiWorldStateCompareOp::Exists && CompareOp != EAeyerjiWorldStateCompareOp::DoesNotExist"))
	FAeyerjiWorldStateValue ExpectedValue;

	/** Inverts the final condition result. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World State")
	bool bInvert = false;

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

private:
	bool CompareValues(const FAeyerjiWorldStateValue& ActualValue) const;
};
