#include "GAS/GE_AeyerjiAbilityCostMana.h"

#include "AeyerjiGameplayTags.h"
#include "Attributes/AeyerjiAttributeSet.h"

UGE_AeyerjiAbilityCostMana::UGE_AeyerjiAbilityCostMana(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FGameplayModifierInfo ManaModifier;
	ManaModifier.Attribute = UAeyerjiAttributeSet::GetManaAttribute();
	ManaModifier.ModifierOp = EGameplayModOp::Additive;

	FSetByCallerFloat ManaCost;
	ManaCost.DataTag = AeyerjiTags::SBC_Cost_Mana;
	ManaModifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(ManaCost);

	Modifiers.Add(ManaModifier);
}
