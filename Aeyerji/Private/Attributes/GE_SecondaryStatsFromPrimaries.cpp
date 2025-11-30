// File: Source/Aeyerji/Private/Attributes/GE_SecondaryStatsFromPrimaries.cpp
#include "Attributes/GE_SecondaryStatsFromPrimaries.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "AeyerjiGameplayTags.h"

UGE_SecondaryStatsFromPrimaries::UGE_SecondaryStatsFromPrimaries()
{
    DurationPolicy = EGameplayEffectDurationType::Infinite;

    auto MakeSBC = [](const FGameplayAttribute& Attr, const FGameplayTag& Tag)
    {
        FGameplayModifierInfo Info;
        Info.Attribute = Attr;
        Info.ModifierOp = EGameplayModOp::Additive;
        FSetByCallerFloat SBC; SBC.DataTag = Tag;
        Info.ModifierMagnitude = FGameplayEffectModifierMagnitude(SBC);
        return Info;
    };

    Modifiers.Reserve(10);
    // HPMax from Strength
    Modifiers.Add(MakeSBC(UAeyerjiAttributeSet::GetHPMaxAttribute(),         AeyerjiTags::SBC_PrimaryDerived_HPMax));
    // Armor from Strength
    Modifiers.Add(MakeSBC(UAeyerjiAttributeSet::GetArmorAttribute(),         AeyerjiTags::SBC_PrimaryDerived_Armor));
    // AttackSpeed from Agility
    Modifiers.Add(MakeSBC(UAeyerjiAttributeSet::GetAttackSpeedAttribute(),   AeyerjiTags::SBC_PrimaryDerived_AttackSpeed));
    // DodgeChance from Agility
    Modifiers.Add(MakeSBC(UAeyerjiAttributeSet::GetDodgeChanceAttribute(),   AeyerjiTags::SBC_PrimaryDerived_DodgeChance));
    // SpellPower from Intellect
    Modifiers.Add(MakeSBC(UAeyerjiAttributeSet::GetSpellPowerAttribute(),    AeyerjiTags::SBC_PrimaryDerived_SpellPower));
    // ManaMax from Intellect
    Modifiers.Add(MakeSBC(UAeyerjiAttributeSet::GetManaMaxAttribute(),       AeyerjiTags::SBC_PrimaryDerived_ManaMax));
    // ManaRegen from Intellect
    Modifiers.Add(MakeSBC(UAeyerjiAttributeSet::GetManaRegenAttribute(),     AeyerjiTags::SBC_PrimaryDerived_ManaRegen));
    // HPRegen from Strength
    Modifiers.Add(MakeSBC(UAeyerjiAttributeSet::GetHPRegenAttribute(),       AeyerjiTags::SBC_PrimaryDerived_HPRegen));
    // Ailment DPS & Duration from Ailment
    Modifiers.Add(MakeSBC(UAeyerjiAttributeSet::GetAilmentDPSAttribute(),    AeyerjiTags::SBC_PrimaryDerived_AilmentDPS));
    Modifiers.Add(MakeSBC(UAeyerjiAttributeSet::GetAilmentDurationAttribute(),AeyerjiTags::SBC_PrimaryDerived_AilmentDuration));
}
