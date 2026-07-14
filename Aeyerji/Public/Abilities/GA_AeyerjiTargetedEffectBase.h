#pragma once

#include "CoreMinimal.h"
#include "Abilities/GA_AeyerjiBase.h"
#include "TimerManager.h"
#include "GA_AeyerjiTargetedEffectBase.generated.h"

struct FGameplayEventData;

/** Table-driven targeted damage/control ability. Server validates target data before committing. */
UCLASS(Abstract)
class AEYERJI_API UGA_AeyerjiTargetedEffectBase : public UGA_AeyerjiBase
{
	GENERATED_BODY()

public:
	UGA_AeyerjiTargetedEffectBase();

protected:
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
	                             const FGameplayAbilityActorInfo* ActorInfo,
	                             const FGameplayAbilityActivationInfo ActivationInfo,
	                             const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
	                        const FGameplayAbilityActorInfo* ActorInfo,
	                        const FGameplayAbilityActivationInfo ActivationInfo,
	                        bool bReplicateEndAbility,
	                        bool bWasCancelled) override;

	/** Blueprint hook for cosmetics after validated effects are applied on the server. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|Ability")
	void BP_OnTargetedAbilityApplied(const TArray<AActor*>& Targets, FVector TargetLocation);

	/** Native hook for ability-specific cosmetics after the server has committed and applied row effects. */
	virtual void OnTargetedAbilityAppliedNative(
		const FGameplayAbilityActorInfo& ActorInfo,
		const FAeyerjiAbilityResolvedConfig& Config,
		const TArray<AActor*>& Targets,
		FVector TargetLocation) const;

private:
	float CalculateImpactDelay(const FAeyerjiAbilityResolvedConfig& Config) const;
	void ApplyCastingLock(const FGameplayAbilityActorInfo& ActorInfo) const;
	void RemoveCastingLock(const FGameplayAbilityActorInfo* ActorInfo) const;
	void PlayAbilityCastMontageNative(const FGameplayAbilityActorInfo& ActorInfo, const FAeyerjiAbilityResolvedConfig& Config) const;
	void ExecuteTargetedImpact(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FAeyerjiAbilityResolvedConfig Config, const TArray<AActor*> Targets, FVector TargetLocation);
	bool ResolveTargetLocation(const FGameplayAbilityActorInfo& ActorInfo, const FGameplayEventData* TriggerEventData, FVector& OutLocation) const;
	void ResolveTargets(const FGameplayAbilityActorInfo& ActorInfo, const FGameplayEventData* TriggerEventData, const FAeyerjiAbilityResolvedConfig& Config, TArray<AActor*>& OutTargets, FVector& OutTargetLocation) const;
	void GatherShapeTargets(const FGameplayAbilityActorInfo& ActorInfo, const FAeyerjiAbilityResolvedConfig& Config, const FVector& TargetLocation, TArray<AActor*>& OutTargets) const;
	bool IsTargetAllowed(const FGameplayAbilityActorInfo& ActorInfo, const FAeyerjiAbilityResolvedConfig& Config, AActor* Target) const;
	bool IsWithinRange(const FGameplayAbilityActorInfo& ActorInfo, const FAeyerjiAbilityResolvedConfig& Config, const FVector& TargetLocation) const;
	float EvaluateMagnitude(const FGameplayAbilityActorInfo& ActorInfo, const FAeyerjiAbilityMagnitude& Magnitude) const;
	void ApplyResolvedConfigEffects(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo& ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FAeyerjiAbilityResolvedConfig& Config, const TArray<AActor*>& Targets) const;

	FTimerHandle DelayedImpactTimerHandle;
};
