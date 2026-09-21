#pragma once

#include "CoreMinimal.h"
#include "Abilities/GA_AeyerjiTargetedEffectBase.h"
#include "GA_AGMeteorStrike.generated.h"

class AAeyerjiMeteorStrike;

/**
 * Ground-targeted meteor strike.
 *
 * Runtime tuning comes from DT_AeyerjiAbilityTuning through Ability.AG.MeteorStrike.
 * The base class handles cost, cooldown, ground-radius target collection, enemy filtering
 * and authoritative GAS damage. This class only owns the falling visual:
 * - at cast start it spawns a replicated AAeyerjiMeteorStrike that falls for ImpactDelaySeconds
 * - at impact (same callstack as damage) it lands the meteor and presents the fracture
 * - on early end it cancels the meteor so no fracture plays without damage
 */
UCLASS()
class AEYERJI_API UGA_AGMeteorStrike : public UGA_AeyerjiTargetedEffectBase
{
	GENERATED_BODY()

public:
	UGA_AGMeteorStrike();

protected:
	virtual void OnTargetedAbilityCastStartedNative(
		const FGameplayAbilityActorInfo& ActorInfo,
		const FAeyerjiAbilityResolvedConfig& Config,
		FVector TargetLocation,
		float ImpactDelaySeconds) const override;

	virtual void OnTargetedAbilityAppliedNative(
		const FGameplayAbilityActorInfo& ActorInfo,
		const FAeyerjiAbilityResolvedConfig& Config,
		const TArray<AActor*>& Targets,
		FVector TargetLocation) const override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

private:
	// Visual meteor owned by this instance. Mutable because the base hooks are const;
	// only the server ever writes it, and only between cast start and ability end.
	mutable TWeakObjectPtr<AAeyerjiMeteorStrike> ActiveMeteor;
};
