#pragma once

#include "CoreMinimal.h"
#include "Abilities/GA_AeyerjiTargetedEffectBase.h"
#include "GA_AGGravitonPull.generated.h"

/**
 * Instant owner-radius targeted effect entry point for Graviton Pull.
 *
 * Runtime tuning comes from DT_AeyerjiAbilityTuning through Ability.AG.GravitonPull.
 * The base class handles:
 * - cost
 * - cooldown
 * - target collection
 * - enemy filtering
 * - damage application
 * - additional effects
 */
UCLASS()
class AEYERJI_API UGA_AGGravitonPull : public UGA_AeyerjiTargetedEffectBase
{
	GENERATED_BODY()

public:
	UGA_AGGravitonPull();

protected:
	/** Pulls validated enemies toward the caster after the shared damage pass succeeds. */
	virtual void OnTargetedAbilityAppliedNative(
		const FGameplayAbilityActorInfo& ActorInfo,
		const FAeyerjiAbilityResolvedConfig& Config,
		const TArray<AActor*>& Targets,
		FVector TargetLocation) const override;
};
