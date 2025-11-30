// Astral Guardian's spinning finisher that ends in a shockwave, consuming Bleed stacks for bonus damage.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/AeyerjiAbilityData.h"
#include "DA_AGSpinTornado.generated.h"

USTRUCT(BlueprintType)
struct FAGSpinTornadoTuning
{
	GENERATED_BODY()

	// Damage per spin tick while rotating.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SpinTornado|Damage", meta=(ClampMin="0.0"))
	float SpinDamage = 25.f;

	// Radius for the spinning portion.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SpinTornado|Area", meta=(ClampMin="0.0"))
	float SpinRadius = 350.f;

	// How many full rotations before the shockwave.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SpinTornado|Timing", meta=(ClampMin="1.0"))
	float RequiredRotations = 3.f;

	// Base damage for the final shockwave (before stacks).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SpinTornado|Damage", meta=(ClampMin="0.0"))
	float ShockwaveDamage = 60.f;

	// Radius for the final shockwave.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SpinTornado|Area", meta=(ClampMin="0.0"))
	float ShockwaveRadius = 600.f;

	// Bonus damage added for each Bleed stack consumed on an enemy.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SpinTornado|Damage", meta=(ClampMin="0.0"))
	float BonusDamagePerBleedStack = 12.f;

	// Optional cap on stacks consumed per target.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SpinTornado|Damage", meta=(ClampMin="0"))
	int32 MaxStacksConsumed = 10;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SpinTornado|Cost")
	FAeyerjiAbilityCost Cost = {45.f, 10.f};
};

UCLASS(BlueprintType)
class AEYERJI_API UDA_AGSpinTornado : public UAeyerjiAbilityData
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="SpinTornado Configuration")
	FAGSpinTornadoTuning Tunables;

	virtual FAeyerjiAbilityCost EvaluateCost(const UAbilitySystemComponent* ASC) const override { return Tunables.Cost; }
};

