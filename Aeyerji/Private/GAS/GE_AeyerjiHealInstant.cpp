#include "GAS/GE_AeyerjiHealInstant.h"

#include "AeyerjiGameplayTags.h"
#include "Attributes/AeyerjiAttributeSet.h"

UGE_AeyerjiHealInstant::UGE_AeyerjiHealInstant(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FGameplayModifierInfo HealModifier;
	HealModifier.Attribute = UAeyerjiAttributeSet::GetHPAttribute();
	HealModifier.ModifierOp = EGameplayModOp::Additive;

	FSetByCallerFloat HealMagnitude;
	HealMagnitude.DataTag = AeyerjiTags::SBC_Heal_Instant;
	HealModifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(HealMagnitude);

	Modifiers.Add(HealModifier);
}
