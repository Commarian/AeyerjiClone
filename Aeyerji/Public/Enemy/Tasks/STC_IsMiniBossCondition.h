#pragma once

#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "GameplayTagContainer.h"
#include "STC_IsMiniBossCondition.generated.h"

class UGameplayAbility;

/**
 * StateTree condition that answers "is this pawn a mini boss?" without requiring a separate tree.
 * Checks actor tags, optional gameplay tag on the ASC, and/or presence of a specific ability.
 */
UCLASS()
class AEYERJI_API USTC_IsMiniBossCondition : public UStateTreeConditionBlueprintBase
{
	GENERATED_BODY()

public:
	/** Actor tag added by the spawner when bIsMiniBoss is true. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MiniBoss")
	FName MiniBossActorTag = TEXT("MiniBoss");

	/** Gameplay tag to probe on the pawn's ASC (added by the spawner when configured). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MiniBoss")
	FGameplayTag MiniBossGameplayTag;

	/** Optional signature ability; if set, we can require or simply prefer its presence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MiniBoss")
	TSubclassOf<UGameplayAbility> RequiredAbility;

	/** When true, condition fails unless RequiredAbility is found on the ASC. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MiniBoss")
	bool bRequireAbility = false;

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};
