// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/AeyerjiAbilityData.h"
#include "Curves/CurveFloat.h"
#include "DA_Blink.generated.h"

USTRUCT(BlueprintType)
struct FBlinkTuning
{
	GENERATED_BODY()
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Blink Tuning") float MaxRange = 1200.f;

	/** Optional: range by level curve (X = level, Y = range in cm). Uses MaxRange if empty. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Blink Tuning")
	FRuntimeFloatCurve RangeByLevel;

	/** Per-ability scalar to quickly nudge this blink relative to baseline. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Blink Tuning", meta=(ClampMin="0.0"))
	float RangeScalar = 1.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Blink Tuning") float ManaCost = 30.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Blink Tuning") float Cooldown = 1.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Blink Tuning", meta=(ClampMin="500.0", UIMin="500.0")) float StoppingDeceleration = 2000.f;
	/**Chaos chance in % as in the value 50 will equate to 50% chance. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Abdomino Tuning") float ChaosChance = 20.f;
	/**The damage radius upon landing to apply damage to enemies within. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Abdomino Tuning") float LandingRadius = 500.f;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Abdomino Tuning") float InstantDamage = 50.f;
};

UCLASS(BlueprintType)
class AEYERJI_API UDA_Blink : public UAeyerjiAbilityData
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Blink Configuration")
	FBlinkTuning Tunables;

	virtual FAeyerjiAbilityCost EvaluateCost(const UAbilitySystemComponent* ASC) const override;
};
