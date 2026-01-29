// Magic amplification aura data asset. Aura applies to nearby allies if radius > 0.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/AeyerjiAbilityData.h"
#include "DA_MagicAmpAura.generated.h"

USTRUCT(BlueprintType)
struct FMagicAmpAuraTuning
{
	GENERATED_BODY()

	/** Aura radius in cm. Set to 0 to disable the aura radius and apply only to the owner. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MagicAmpAura|Area", meta=(ClampMin="0.0"))
	float Radius = 0.f;

	/** Flat magic amplification applied to affected targets. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MagicAmpAura|Effect", meta=(ClampMin="0.0"))
	float FlatMagicAmp = 0.f;

	/** Percent magic amplification applied to affected targets [0..100]. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MagicAmpAura|Effect", meta=(ClampMin="0.0", ClampMax="100.0"))
	float PercentMagicAmp = 0.f;
};

UCLASS(BlueprintType)
class AEYERJI_API UDA_MagicAmpAura : public UAeyerjiAbilityData
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="MagicAmpAura Configuration")
	FMagicAmpAuraTuning Tunables;

	/** Returns zero costs; aura tuning is handled by the owning ability. */
	virtual FAeyerjiAbilityCost EvaluateCost(const UAbilitySystemComponent* ASC) const override;
};
