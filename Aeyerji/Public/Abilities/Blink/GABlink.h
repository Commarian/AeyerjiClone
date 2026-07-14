#pragma once

#include "CoreMinimal.h"
#include "Abilities/GA_AeyerjiBase.h"
#include "GameplayTagContainer.h"
#include "GABlink.generated.h"

/**
 * Short-range teleport (Blink) implemented for UE 5.6 GAS.
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

	virtual void ApplyCooldown(const FGameplayAbilitySpecHandle Handle,
							   const FGameplayAbilityActorInfo* ActorInfo,
							   const FGameplayAbilityActivationInfo ActivationInfo) const override;

	virtual const FGameplayTagContainer* GetCooldownTags() const override;

	/* -------- Tunables -------- */
	UPROPERTY(EditDefaultsOnly, Category = "Blink|Tuning", meta=(ClampMin="0"))
	float MaxBlinkDistance = 1000.f;

	/** Fallback cooldown used when this legacy Blink instance has no table row and no explicit cooldown GE. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Blink|Tuning", meta=(ClampMin="0"))
	float FallbackCooldownSeconds = 1.f;

	/* Optional local container for our cooldown tag */
	UPROPERTY(EditDefaultsOnly, Category="Blink|Tags")
	FGameplayTagContainer CooldownTags;

	/* GameplayCue tags to fire */
	UPROPERTY(EditDefaultsOnly, Category="Blink|Tags")
	FGameplayTag BlinkOutCue;

	UPROPERTY(EditDefaultsOnly, Category="Blink|Tags")
	FGameplayTag BlinkInCue;

public:
	float GetMaxBlinkRange(const UAbilitySystemComponent* ASC) const;
};
