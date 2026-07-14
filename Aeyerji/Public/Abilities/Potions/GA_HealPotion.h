#pragma once

#include "CoreMinimal.h"
#include "Abilities/GA_AeyerjiBase.h"
#include "GA_HealPotion.generated.h"

class UDA_Potions;

/** Native heal potion ability that uses the standard GAS commit/cooldown path. */
UCLASS()
class AEYERJI_API UGA_HealPotion : public UGA_AeyerjiBase
{
	GENERATED_BODY()

public:
	UGA_HealPotion();

protected:
	virtual void ActivateAbility(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	virtual void ApplyCooldown(
		const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo) const override;

	virtual const FGameplayTagContainer* GetCooldownTags() const override;

	/** Optional legacy potion data asset used for heal percentage tuning. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Potion")
	TObjectPtr<UDA_Potions> PotionData = nullptr;

	/** Fallback fraction of max HP restored when PotionData is not assigned. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Potion", meta=(ClampMin="0.0"))
	float HealPercentageOfMaxHP = 0.25f;

	/** Fallback cooldown used when this potion is authored without a tuning-table row or explicit cooldown GE. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Potion", meta=(ClampMin="0.0"))
	float FallbackCooldownSeconds = 1.f;

private:
	mutable FGameplayTagContainer PotionCooldownTags;
};
