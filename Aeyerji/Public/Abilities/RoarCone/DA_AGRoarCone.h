// Astral Guardian's roar cone: applies Bleed stacks in a forward cone without direct damage.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/AeyerjiAbilityData.h"
#include "DA_AGRoarCone.generated.h"

USTRUCT(BlueprintType)
struct FAGRoarConeTuning
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RoarCone|Area", meta=(ClampMin="0.0"))
	float Range = 800.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RoarCone|Area", meta=(ClampMin="0.0", ClampMax="180.0"))
	float ConeAngleDegrees = 70.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RoarCone|Status", meta=(ClampMin="0"))
	int32 BleedStacks = 1;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RoarCone|Cost")
	FAeyerjiAbilityCost Cost = {15.f, 6.f};
};

UCLASS(BlueprintType)
class AEYERJI_API UDA_AGRoarCone : public UAeyerjiAbilityData
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="RoarCone Configuration")
	FAGRoarConeTuning Tunables;

	virtual FAeyerjiAbilityCost EvaluateCost(const UAbilitySystemComponent* ASC) const override { return Tunables.Cost; }
};

