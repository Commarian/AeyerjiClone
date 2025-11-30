// Astral Guardian's defensive cooldown: damage reduction + short stun pulse, optional Bleed on expiration.
// Header-only: gameplay ability will consume these knobs and drive the actual DR/stun/bleed effects.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/AeyerjiAbilityData.h"
#include "DA_AGEventHorizonGuard.generated.h"

USTRUCT(BlueprintType)
struct FAGEventHorizonGuardTuning
{
	GENERATED_BODY()

	/** Percent damage reduction while active [0..1]. 0.6 = 60% DR. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="EventHorizonGuard|Defense", meta=(ClampMin="0.0", ClampMax="1.0"))
	float DamageReductionPercent = 0.6f;

	/** Duration in seconds for which DR is active. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="EventHorizonGuard|Defense", meta=(ClampMin="0.0"))
	float Duration = 3.0f;

	/** Radius of the initial shock pulse around the Astral Guardian. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="EventHorizonGuard|Pulse", meta=(ClampMin="0.0"))
	float PulseRadius = 200.f;

	/** Stun duration (seconds) for the initial pulse. 0 disables the stun and can be used as a pure interrupt. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="EventHorizonGuard|Pulse", meta=(ClampMin="0.0"))
	float PulseStunDuration = 0.3f;

	/** Whether to apply Bleed/ailment when the buff expires in a small radius. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="EventHorizonGuard|Status")
	bool bApplyAilmentOnExpire = true;

	/** Radius for the expiration effect. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="EventHorizonGuard|Status", meta=(ClampMin="0.0"))
	float ExpireRadius = 250.f;

	/** Number of ailment stacks applied to enemies within ExpireRadius when the effect ends. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="EventHorizonGuard|Status", meta=(ClampMin="0"))
	int32 ExpireAilmentStacks = 1;

	/** Optional minor damage dealt on expiration to enemies in ExpireRadius. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="EventHorizonGuard|Damage", meta=(ClampMin="0.0"))
	float ExpireDamage = 40.f;

	/** Baseline cost data for this defensive cooldown. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="EventHorizonGuard|Cost")
	FAeyerjiAbilityCost Cost = { 35.f, 18.f };
};

UCLASS(BlueprintType)
class AEYERJI_API UDA_AGEventHorizonGuard : public UAeyerjiAbilityData
{
	GENERATED_BODY()

public:
	/** Designer-facing tuning knobs for the defensive skill. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="EventHorizonGuard Configuration")
	FAGEventHorizonGuardTuning Tunables;

	virtual FAeyerjiAbilityCost EvaluateCost(const UAbilitySystemComponent* ASC) const override
	{
		// No attribute scaling yet; just return the configured cost.
		return Tunables.Cost;
	}
};

