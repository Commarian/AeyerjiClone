#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Systems/LootService.h"
#include "LootSourceRuleSet.generated.h"

USTRUCT(BlueprintType)
struct AEYERJI_API FLootSourceRule
{
	GENERATED_BODY()

	/** Higher number wins if multiple rules match. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Priority = 0;

	/** Tag query allows hierarchy matching and compound source conditions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FGameplayTagQuery MatchQuery;

	/** Loot tuning profile applied when this rule wins. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FLootContext Profile;
};

UCLASS(BlueprintType)
class AEYERJI_API ULootSourceRuleSet : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Fallback profile used when no rule matches. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FLootContext DefaultProfile;

	/** Priority-based source overrides; equal priorities retain authored order. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FLootSourceRule> Rules;

	/** Resolves the best profile while preserving dynamic level, player, and world-tier inputs from BaseContext. */
	UFUNCTION(BlueprintCallable, BlueprintPure)
	FLootContext ResolveContext(const FLootContext& BaseContext, const FGameplayTagContainer& SourceTags) const;

};
