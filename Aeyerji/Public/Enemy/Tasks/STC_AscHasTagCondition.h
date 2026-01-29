#pragma once

#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "GameplayTagContainer.h"
#include "STC_AscHasTagCondition.generated.h"

/**
 * StateTree condition: checks if the pawn's ASC owns matching gameplay tags.
 * Useful for guarding boss/miniboss branches. Logs reasons when it fails.
 */
UCLASS()
class AEYERJI_API USTC_AscHasTagCondition : public UStateTreeConditionBlueprintBase
{
	GENERATED_BODY()

public:
	/** Tag container to check on the ASC. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="ASC")
	FGameplayTagContainer Tags;

	/** If true, require all Tags to be present. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="ASC")
	bool bMatchAll = false;

	/** If true, require ASC's owned tags to match exactly the provided Tags (same set, same count). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="ASC")
	bool bMatchExactly = false;

	/** Optional explicit actor whose ASC will be queried; if null, uses the owner/pawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="ASC")
	TObjectPtr<AActor> AscOwner = nullptr;

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};
