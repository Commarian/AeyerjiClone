// GA_AGGravitonPull.h
// Player-facing GameplayAbility for Astral Guardian's Graviton Pull.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GA_AeyerjiBase.h"
#include "DA_AGGravitonPull.h"
#include "GameplayTagContainer.h"
#include "GA_AGGravitonPull.generated.h"

class UGameplayEffect;
class UAbilitySystemComponent;
class UNiagaraSystem;
class UNiagaraComponent;

/**
 * Astral Graviton Pull:
 * - Trace forward from the player's view to find a target within MaxRange.
 * - If we find a valid enemy, apply damage / slow / ailment.
 * - Then teleport the target toward the Astral Guardian to simulate a pull.
 * - Plays Niagara VFX (beam + impact) on successful hit.
 *
 * All tunables come from UDA_AGGravitonPull and designer-set GameplayEffects.
 */
UCLASS()
class AEYERJI_API UGA_AGGravitonPull : public UGA_AeyerjiBase
{
	GENERATED_BODY()

public:
	UGA_AGGravitonPull();

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	                             const FGameplayAbilityActorInfo* ActorInfo,
	                             const FGameplayAbilityActivationInfo ActivationInfo,
	                             const FGameplayEventData* TriggerEventData) override;

	/** Called in all exit paths once we're done. */
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
	                        const FGameplayAbilityActorInfo* ActorInfo,
	                        const FGameplayAbilityActivationInfo ActivationInfo,
	                        bool bReplicateEndAbility,
	                        bool bWasCancelled) override;

protected:
	/** Data asset defining range, damage, pull distance, etc. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="GravitonPull|Config")
	TObjectPtr<UDA_AGGravitonPull> GravitonConfig = nullptr;

	/** GameplayEffect used to apply direct hit damage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="GravitonPull|Effects")
	TSubclassOf<UGameplayEffect> DamageEffectClass;

	/** GameplayEffect used to apply a slow debuff (optional). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="GravitonPull|Effects")
	TSubclassOf<UGameplayEffect> SlowEffectClass;

	/** GameplayEffect used to apply ailment stacks (Bleed or weapon ailment). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="GravitonPull|Effects")
	TSubclassOf<UGameplayEffect> AilmentEffectClass;

	/** SetByCaller tag used for damage magnitude. Example: "Data.Damage". */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="GravitonPull|Tags")
	FGameplayTag DamageSetByCallerTag;

	/** SetByCaller tag used for slow strength (0..1). Example: "Data.SlowPercent". */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="GravitonPull|Tags")
	FGameplayTag SlowSetByCallerTag;

	/** SetByCaller tag used for ailment stack count. Example: "Data.Stacks". */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="GravitonPull|Tags")
	FGameplayTag AilmentStacksSetByCallerTag;

	/** Niagara system for the tether/beam from AG to hit location. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="GravitonPull|VFX")
	TObjectPtr<UNiagaraSystem> BeamVFX = nullptr;

	/** Niagara system for the impact burst at the target. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="GravitonPull|VFX")
	TObjectPtr<UNiagaraSystem> ImpactVFX = nullptr;

private:
	/** Forward trace from the player's viewpoint to find a valid pull target. */
	AActor* FindTargetForPull(const FGameplayAbilityActorInfo* ActorInfo,
	                          FVector& OutHitLocation,
	                          FVector& OutHitNormal) const;

	/** Teleport the target toward the Astral Guardian by PullDistance (or less). */
	void PullTarget(AActor* Target,
	                const FVector& HitLocation,
	                const FGameplayAbilityActorInfo* ActorInfo) const;

	/** Apply damage, slow, and ailment according to GravitonConfig and Effect classes. */
	void ApplyEffectsToTarget(AActor* Target,
	                          const FGameplayAbilityActorInfo* ActorInfo) const;

	/** Spawn beam and impact VFX for a successful pull. */
	void PlayPullVisuals(AActor* Target,
	                     const FVector& HitLocation,
	                     const FVector& HitNormal,
	                     const FGameplayAbilityActorInfo* ActorInfo) const;
};
