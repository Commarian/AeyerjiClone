#pragma once

#include "CoreMinimal.h"
#include "Abilities/GA_AeyerjiBase.h"
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

	/** Blueprint hook for cosmetics after validated effects are applied on the server. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|Ability")
	void BP_OnTargetedAbilityApplied(const TArray<AActor*>& Targets, FVector TargetLocation);

private:
	bool ResolveTargetLocation(const FGameplayAbilityActorInfo& ActorInfo, const FGameplayEventData* TriggerEventData, FVector& OutLocation) const;
	void ResolveTargets(const FGameplayAbilityActorInfo& ActorInfo, const FGameplayEventData* TriggerEventData, const FAeyerjiAbilityTableRow& Row, TArray<AActor*>& OutTargets, FVector& OutTargetLocation) const;
	void GatherShapeTargets(const FGameplayAbilityActorInfo& ActorInfo, const FAeyerjiAbilityTableRow& Row, const FVector& TargetLocation, TArray<AActor*>& OutTargets) const;
	bool IsTargetAllowed(const FGameplayAbilityActorInfo& ActorInfo, const FAeyerjiAbilityTableRow& Row, AActor* Target) const;
	bool IsWithinRange(const FGameplayAbilityActorInfo& ActorInfo, const FAeyerjiAbilityTableRow& Row, const FVector& TargetLocation) const;
	float EvaluateMagnitude(const FGameplayAbilityActorInfo& ActorInfo, const FAeyerjiAbilityMagnitude& Magnitude) const;
	void ApplyRowEffects(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo& ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo, const FAeyerjiAbilityTableRow& Row, const TArray<AActor*>& Targets) const;
};
