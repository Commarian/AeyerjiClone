#pragma once

#include "CoreMinimal.h"

#include "AeyerjiDamageRules.generated.h"

struct FGameplayEffectSpec;

/**
 * Explicit opt-in rules and per-source overrides for a physical damage spec.
 * Damage.Type.Physical selects mitigation only; these flags enable optional mechanics.
 */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiDamageRuleConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Damage|Rules")
	bool bUseVariance = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Damage|Rules")
	bool bCanCrit = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Damage|Rules")
	bool bCanBeDodged = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Damage|Rules")
	bool bCanLifeSteal = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Damage|Rules")
	bool bCanTriggerOnHit = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Damage|Rules")
	bool bCanStagger = false;

	/** Negative values use the source AttackDamageVariance attribute. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Damage|Overrides", meta=(ClampMin="-1.0", ClampMax="0.95"))
	float VarianceOverride = -1.f;

	/** Values at or below zero use the source CriticalDamageMultiplier attribute. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Damage|Overrides", meta=(ClampMin="0.0"))
	float CriticalMultiplierOverride = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Damage|Overrides", meta=(ClampMin="0.0"))
	float ArmorShred = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Damage|Overrides", meta=(ClampMin="0.0", ClampMax="1.0"))
	float ArmorPenetration = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Damage|Overrides", meta=(ClampMin="0.0"))
	float StaggerMultiplier = 1.f;

	/** Adds only this source's enabled rules and overrides to the outgoing spec. */
	void ApplyToSpec(FGameplayEffectSpec& Spec) const;
};
