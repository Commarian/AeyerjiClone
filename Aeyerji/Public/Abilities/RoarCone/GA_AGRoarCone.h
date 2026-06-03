// GameplayAbility shell for Astral Guardian's roar cone (applies Bleed stacks, no direct damage).

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GA_AeyerjiTargetedEffectBase.h"
#include "GA_AGRoarCone.generated.h"

UCLASS()
class AEYERJI_API UGA_AGRoarCone : public UGA_AeyerjiTargetedEffectBase
{
	GENERATED_BODY()

public:
	UGA_AGRoarCone();
};
