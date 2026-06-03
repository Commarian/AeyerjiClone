#include "GAS/GE_AeyerjiAbilityCooldown.h"

#include "AeyerjiGameplayTags.h"

UGE_AeyerjiAbilityCooldown::UGE_AeyerjiAbilityCooldown(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::HasDuration;

	FSetByCallerFloat CooldownDuration;
	CooldownDuration.DataTag = AeyerjiTags::SBC_CooldownSeconds;
	DurationMagnitude = FGameplayEffectModifierMagnitude(CooldownDuration);
}
