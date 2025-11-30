// Astral Guardian's light front-arc slash: hits enemies in an arc and can apply the weapon's ailment.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/AeyerjiAbilityData.h"
#include "DA_AGFrontArcSlash.generated.h"

USTRUCT(BlueprintType)
struct FAGFrontArcSlashTuning
{
	GENERATED_BODY()

	// Light hit value; keep it simple for now.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="FrontArcSlash|Damage", meta=(ClampMin="0.0"))
	float Damage = 35.f;

	// Width of the frontal cone in degrees (0..180+).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="FrontArcSlash|Area", meta=(ClampMin="0.0", ClampMax="180.0"))
	float ArcAngleDegrees = 90.f;

	// How far the arc reaches forward in centimeters.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="FrontArcSlash|Area", meta=(ClampMin="0.0"))
	float ArcRange = 400.f;

	// How many stacks of the active ailment to apply on a successful hit.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="FrontArcSlash|Status", meta=(ClampMin="0"))
	int32 AilmentStacks = 1;

	// Optional guard: only apply ailment if the target is actually susceptible (designer can flip this off).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="FrontArcSlash|Status")
	bool bRequireSusceptibleTarget = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="FrontArcSlash|Cost")
	FAeyerjiAbilityCost Cost = {20.f, 4.f};
};

UCLASS(BlueprintType)
class AEYERJI_API UDA_AGFrontArcSlash : public UAeyerjiAbilityData
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="FrontArcSlash Configuration")
	FAGFrontArcSlashTuning Tunables;

	virtual FAeyerjiAbilityCost EvaluateCost(const UAbilitySystemComponent* ASC) const override { return Tunables.Cost; }
};

