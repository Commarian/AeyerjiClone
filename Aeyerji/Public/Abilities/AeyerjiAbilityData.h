// Shared base classes for Aeyerji ability data assets.
// Provides a simple cost container so abilities can feed SetByCaller mana / cooldown.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AeyerjiAbilityData.generated.h"

class UAbilitySystemComponent;

USTRUCT(BlueprintType)
struct FAeyerjiAbilityCost
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Cost", meta=(ClampMin="0.0"))
	float ManaCost = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Cost", meta=(ClampMin="0.0"))
	float Cooldown = 0.f;
};

/**
 * Base data asset for any Aeyerji ability that wants to provide SetByCaller cost/cooldown.
 * Derived assets should override EvaluateCost to surface their own tuning values.
 */
UCLASS(BlueprintType, Abstract)
class AEYERJI_API UAeyerjiAbilityData : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Legacy baseline cost (not exposed in asset details). Override EvaluateCost in derived assets instead. */
	UPROPERTY()
	FAeyerjiAbilityCost Cost;

	/** Allows derived assets to apply level curves or other scaling. */
	virtual FAeyerjiAbilityCost EvaluateCost(const UAbilitySystemComponent* ASC) const { return Cost; }
};
