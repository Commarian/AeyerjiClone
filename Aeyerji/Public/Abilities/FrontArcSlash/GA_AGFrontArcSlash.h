// GameplayAbility shell for Astral Guardian's front-arc light slash.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GA_AeyerjiTargetedEffectBase.h"
#include "GA_AGFrontArcSlash.generated.h"

/**
 * Light frontal arc swing. Think of it as the "generator" light filler.
 * Real hit logic will live in ActivateAbility; right now we only wire cost/cooldown + config.
 */
UCLASS()
class AEYERJI_API UGA_AGFrontArcSlash : public UGA_AeyerjiTargetedEffectBase
{
	GENERATED_BODY()

public:
	UGA_AGFrontArcSlash();
};
