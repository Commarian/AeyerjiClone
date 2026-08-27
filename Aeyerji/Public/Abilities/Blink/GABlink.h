#pragma once

#include "CoreMinimal.h"
#include "Abilities/GA_AeyerjiBase.h"
#include "GameplayTagContainer.h"
#include "GABlink.generated.h"

/**
 * Server-authoritative short-range teleport implemented with GAS.
 *  - Plays out / in GameplayCues
 *  - Consumes mana + starts cooldown via CommitAbility
 */
UCLASS()
class AEYERJI_API UGABlink : public UGA_AeyerjiBase
{
	GENERATED_BODY()

public:
	UGABlink();

protected:
	/* -------- UGameplayAbility -------- */
	virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
								 const FGameplayAbilityActorInfo* ActorInfo,
								 const FGameplayAbilityActivationInfo ActivationInfo,
								 const FGameplayEventData* TriggerEventData) override;

	/** Clears the pending teleport timer before the shared cast lock is released. */
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
							  const FGameplayAbilityActorInfo* ActorInfo,
							  const FGameplayAbilityActivationInfo ActivationInfo,
							  bool bReplicateEndAbility,
							  bool bWasCancelled) override;

	virtual void ApplyCooldown(const FGameplayAbilitySpecHandle Handle,
							   const FGameplayAbilityActorInfo* ActorInfo,
							   const FGameplayAbilityActivationInfo ActivationInfo) const override;

	virtual const FGameplayTagContainer* GetCooldownTags() const override;

	/** Maximum fallback travel distance when no greater data-driven or attribute range is configured. */
	UPROPERTY(EditDefaultsOnly, Category = "Blink|Tuning", meta=(ClampMin="0"))
	float MaxBlinkDistance = 1000.f;

	/** Fallback cooldown used when this legacy Blink instance has no table row and no explicit cooldown GE. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Blink|Tuning", meta=(ClampMin="0"))
	float FallbackCooldownSeconds = 1.f;

	/** Cooldown tags applied by this ability when no table-driven tag set overrides them. */
	UPROPERTY(EditDefaultsOnly, Category="Blink|Tags")
	FGameplayTagContainer CooldownTags;

	/** Gameplay cue executed at the authoritative departure location. */
	UPROPERTY(EditDefaultsOnly, Category="Blink|Tags")
	FGameplayTag BlinkOutCue;

	/** Gameplay cue executed after the authoritative teleport succeeds. */
	UPROPERTY(EditDefaultsOnly, Category="Blink|Tags")
	FGameplayTag BlinkInCue;

	/** Executes the committed teleport at the configured cast impact time. */
	void ExecuteBlinkImpact(FGameplayAbilitySpecHandle Handle,
							const FGameplayAbilityActorInfo* ActorInfo,
							FGameplayAbilityActivationInfo ActivationInfo,
							FVector DesiredLocation,
							FRotator DesiredRotation);

	FTimerHandle BlinkImpactTimerHandle;

public:
	float GetMaxBlinkRange(const UAbilitySystemComponent* ASC) const;
};
