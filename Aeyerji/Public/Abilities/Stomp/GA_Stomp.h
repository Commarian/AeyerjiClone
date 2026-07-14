#pragma once

#include "CoreMinimal.h"
#include "Abilities/GA_AeyerjiTargetedEffectBase.h"
#include "GA_Stomp.generated.h"

/**
 * Instant owner-radius stomp ability.
 *
 * Runtime tuning comes from DT_AeyerjiAbilityTuning through Ability.Stomp.
 * The base class handles:
 * - cost
 * - cooldown
 * - owner-radius target collection
 * - enemy filtering
 * - damage application
 * - additional effects such as stun
 */
UCLASS()
class AEYERJI_API UGA_Stomp : public UGA_AeyerjiTargetedEffectBase
{
	GENERATED_BODY()

public:
	UGA_Stomp();
};
