// Astral Guardian's single-target pull ability: fires a tether and drags the first enemy hit.
// Pure data-only container: the actual projectile/pull logic lives in the gameplay ability / effects.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/AeyerjiAbilityData.h"
#include "DA_AGGravitonPull.generated.h"

USTRUCT(BlueprintType)
struct FAGGravitonPullTuning
{
	GENERATED_BODY()

	/** Max range of the tether in cm. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="GravitonPull|Area", meta=(ClampMin="0.0"))
	float MaxRange = 1200.f;

	/** Base distance to pull standard enemies toward the Astral Guardian. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="GravitonPull|Area", meta=(ClampMin="0.0"))
	float PullDistance = 700.f;

	/** Scalar applied to PullDistance for "heavy" targets (elites). 0.5 = half distance. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="GravitonPull|Area", meta=(ClampMin="0.0", ClampMax="1.0"))
	float HeavyTargetPullScale = 0.5f;

	/** Direct hit damage applied to the main pulled target. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="GravitonPull|Damage", meta=(ClampMin="0.0"))
	float HitDamage = 70.f;

	/** Optionally apply Bleed or the weapon's ailment on hit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="GravitonPull|Status")
	bool bApplyAilment = true;

	/** How many stacks of the applied ailment to grant on a successful hit. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="GravitonPull|Status", meta=(ClampMin="0"))
	int32 AilmentStacks = 1;

	/** Optional small slow duration after being pulled (seconds). 0 for disabled. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="GravitonPull|Status", meta=(ClampMin="0.0"))
	float SlowDuration = 0.6f;

	/** Optional slow strength in percent [0..1] of movement speed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="GravitonPull|Status", meta=(ClampMin="0.0", ClampMax="1.0"))
	float SlowPercent = 0.35f;

	/** Baseline cost data for this ability. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="GravitonPull|Cost")
	FAeyerjiAbilityCost Cost = { 30.f, 8.f };
};

UCLASS(BlueprintType)
class AEYERJI_API UDA_AGGravitonPull : public UAeyerjiAbilityData
{
	GENERATED_BODY()

public:
	/** Designer-facing tuning knobs for the pull ability. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="GravitonPull Configuration")
	FAGGravitonPullTuning Tunables;

	virtual FAeyerjiAbilityCost EvaluateCost(const UAbilitySystemComponent* ASC) const override
	{
		// No scaling yet; just return the configured cost.
		return Tunables.Cost;
	}
};

