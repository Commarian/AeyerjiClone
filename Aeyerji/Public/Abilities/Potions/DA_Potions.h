// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/AeyerjiAbilityData.h"
#include "DA_Potions.generated.h"

USTRUCT(BlueprintType)
struct FPotionTuning
{
	GENERATED_BODY()
	/** Percentage of max HP restored by the potion. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Potions|Effect", meta=(ClampMin="0.0"))
	float HealPercentageOfMaxHP = 0.f;

	/** Cooldown in seconds for potion use. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Potions|Cost", meta=(ClampMin="0.0"))
	float Cooldown = 1.f;
};

UCLASS(BlueprintType)
class AEYERJI_API UDA_Potions : public UAeyerjiAbilityData
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Potions Configuration")
	FPotionTuning Tunables;

	/** Returns the cooldown-only cost for potion abilities. */
	virtual FAeyerjiAbilityCost EvaluateCost(const UAbilitySystemComponent* ASC) const override;
};
