// Shared base classes for Aeyerji ability data assets.
// Provides a simple cost container so abilities can feed SetByCaller mana / cooldown.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AeyerjiAbilityData.generated.h"

class UAbilitySystemComponent;
class UTexture2D;

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
	/** UI-facing name for this ability (used by tooltips/selection widgets). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="UI")
	FText DisplayName;

	/** UI-facing description for this ability (used by tooltips/selection widgets). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="UI", meta=(MultiLine="true"))
	FText Description;

	/** Optional UI icon for this ability (used as a fallback when slots don't provide one). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="UI")
	TObjectPtr<UTexture2D> Icon = nullptr;

	/** Legacy baseline cost (not exposed in asset details). Override EvaluateCost in derived assets instead. */
	UPROPERTY()
	FAeyerjiAbilityCost Cost;

	/** Allows derived assets to apply level curves or other scaling. */
	virtual FAeyerjiAbilityCost EvaluateCost(const UAbilitySystemComponent* ASC) const { return Cost; }
};
