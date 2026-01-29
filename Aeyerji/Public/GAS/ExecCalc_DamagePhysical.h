// ExecCalc_DamagePhysical.h
#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectExecutionCalculation.h"

#include "ExecCalc_DamagePhysical.generated.h"

/**
 * Executes physical damage mitigation using target armor and a soft-cap curve.
 */
UCLASS()
class AEYERJI_API UExecCalc_DamagePhysical : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	UExecCalc_DamagePhysical();

	// Applies physical damage mitigation and writes the final HP delta.
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
