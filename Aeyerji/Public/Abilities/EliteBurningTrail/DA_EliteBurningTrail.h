// Data asset for the elite Burning Trail affix ability.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/AeyerjiAbilityData.h"
#include "DA_EliteBurningTrail.generated.h"

class UAbilitySystemComponent;

USTRUCT(BlueprintType)
struct FAGEliteBurningTrailTuning
{
	GENERATED_BODY()

	/** How long each ground patch persists. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="BurningTrail")
	float PatchLifetime = 5.0f;

	/** Horizontal radius of each patch (for collision/VFX). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="BurningTrail")
	float PatchRadius = 200.0f;

	/** Damage per second dealt to players inside a patch. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="BurningTrail")
	float DamagePerSecond = 20.0f;

	/** How often we evaluate footprint placement (seconds). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="BurningTrail")
	float FootstepInterval = 0.3f;

	/** Minimum horizontal distance moved since last patch to spawn a new one. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="BurningTrail")
	float MinTravelDistanceForNewPatch = 150.0f;

	/** Maximum active patches for a single elite; older ones are destroyed when exceeded. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="BurningTrail")
	int32 MaxActivePatches = 10;
};

UCLASS(BlueprintType)
class AEYERJI_API UDA_EliteBurningTrail : public UAeyerjiAbilityData
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="BurningTrail Configuration")
	FAGEliteBurningTrailTuning Tunables;

	virtual FAeyerjiAbilityCost EvaluateCost(const UAbilitySystemComponent* ASC) const override
	{
		// Passive affix: no mana and no cooldown.
		FAeyerjiAbilityCost Result;
		Result.ManaCost = 0.f;
		Result.Cooldown = 0.f;
		return Result;
	}
};
