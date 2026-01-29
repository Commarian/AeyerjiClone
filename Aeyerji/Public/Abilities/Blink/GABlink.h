#pragma once

#include "CoreMinimal.h"
#include "Abilities/GA_AeyerjiBase.h"
#include "GameplayTagContainer.h"
#include "Abilities/Blink/DA_Blink.h"
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

	/* -------- Tunables -------- */
	UPROPERTY(EditDefaultsOnly, Category = "Blink|Tuning", meta=(ClampMin="0"))
	float MaxBlinkDistance = 1000.f;

	/** Optional data asset that defines range/mana/cooldown per blink variant. */
	UPROPERTY(EditDefaultsOnly, Category="Blink|Tuning")
	TObjectPtr<UDA_Blink> BlinkConfig = nullptr;

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
