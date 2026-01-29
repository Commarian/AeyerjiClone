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

	// Higher number wins if multiple rules match.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Priority = 0;

	// Tag query allows "contains"/hierarchy matching and complex logic.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FGameplayTagQuery MatchQuery;

	// The profile to apply if this rule matches.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FLootContext Profile;
};

UCLASS(BlueprintType)
class AEYERJI_API ULootSourceRuleSet : public UDataAsset
{
	GENERATED_BODY()

public:
	// Used when no rules match (your "common mob" default).
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FLootContext DefaultProfile;

	// Ordered/priority-based overrides.
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TArray<FLootSourceRule> Rules;

	// Resolve the best matching profile for SourceTags and apply tuning to BaseContext.
	UFUNCTION(BlueprintCallable, BlueprintPure)
	FLootContext ResolveContext(const FLootContext& BaseContext, const FGameplayTagContainer& SourceTags) const;

private:
	static float ClampNonNegative(float V) { return (V < 0.0f) ? 0.0f : V; }
};
