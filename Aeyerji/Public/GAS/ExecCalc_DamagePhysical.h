// ExecCalc_DamagePhysical.h
#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectExecutionCalculation.h"

#include "ExecCalc_DamagePhysical.generated.h"

/** Result of rolling pre-mitigation attack damage. */
struct AEYERJI_API FAeyerjiDamageRollResult
{
	float DamageBeforeMitigation = 0.f;
	bool bWasCritical = false;
};

/**
 * Executes physical damage mitigation using target armor and a soft-cap curve.
 */
UCLASS()
class AEYERJI_API UExecCalc_DamagePhysical : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	UExecCalc_DamagePhysical();

	/** Resolves average damage into a rolled pre-mitigation value using caller-supplied random samples. */
	static FAeyerjiDamageRollResult ResolveDamageRoll(float AverageDamage,
	                                                   float VarianceFraction,
	                                                   float CritChanceFraction,
	                                                   float CriticalMultiplier,
	                                                   bool bUseVariance,
	                                                   bool bCanCrit,
	                                                   float DamageRollAlpha,
	                                                   float CritRollAlpha);

	/** Returns true only when the source opted into dodge and the roll is below target dodge chance. */
	static bool ResolveDodge(bool bCanBeDodged, float DodgeChanceFraction, float DodgeRollAlpha);

	/** Combines persistent and per-spec penetration under the configured cap. */
	static float ResolveArmorPenetration(float SourcePenetration, float SpecPenetration, float MaximumPenetration);

	/** Calculates post-overkill life steal without exceeding the source's missing health. */
	static float ResolveLifeSteal(float ActualDamage, float LifeStealFraction, float MissingHealth, bool bCanLifeSteal);

	// Resolves a server-authoritative physical hit and writes combat meta attributes.
	virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
	                                    FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;

private:
	struct FArmorTuning
	{
		float ArmorK = 1000.f;
		float ArmorSoftCap = 1000.f;
		float ArmorTailSlope = 0.00001f;
		float ArmorTailCap = 0.52f;
	};

	static FArmorTuning ResolveArmorTuning();

	// Returns damage reduction [0..1] for the provided armor value.
	static float ComputeArmorDR(float Armor, const FArmorTuning& Tuning);
};
