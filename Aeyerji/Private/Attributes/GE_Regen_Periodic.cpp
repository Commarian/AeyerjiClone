// File: Source/Aeyerji/Private/Attributes/GE_Regen_Periodic.cpp
#include "Attributes/GE_Regen_Periodic.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "GameplayEffectTypes.h" // EGameplayEffectAttributeCaptureSource, FGameplayEffectAttributeCaptureDefinition

UGE_Regen_Periodic::UGE_Regen_Periodic()
{
    DurationPolicy = EGameplayEffectDurationType::Infinite;
    const float TickSeconds = 0.1f; // 100 ms for smoother regen
    Period.Value = TickSeconds;
    bExecutePeriodicEffectOnApplication = false;

    // Helper to build an AttributeBased magnitude from a source attribute
    auto MakeAB = [TickSeconds](const FGameplayAttribute& SourceAttr)
    {
        FGameplayModifierInfo Info;
        Info.ModifierOp = EGameplayModOp::Additive;
        FAttributeBasedFloat AB;
        // Capture Source.<Attr> with no snapshot so it updates as attributes change
        AB.BackingAttribute = FGameplayEffectAttributeCaptureDefinition(
            SourceAttr,
            EGameplayEffectAttributeCaptureSource::Source,
            /*bSnapshot*/ false);
        AB.AttributeCalculationType = EAttributeBasedFloatCalculationType::AttributeMagnitude;
        // Convert per-second regen rate into per-tick magnitude
        AB.Coefficient = TickSeconds;
        Info.ModifierMagnitude = FGameplayEffectModifierMagnitude(AB);
        return Info;
    };

    // HP += Source.HPRegen each tick
    {
        FGameplayModifierInfo HPInfo = MakeAB(UAeyerjiAttributeSet::GetHPRegenAttribute());
        HPInfo.Attribute = UAeyerjiAttributeSet::GetHPAttribute();
        Modifiers.Add(HPInfo);
    }
    // Mana += Source.ManaRegen each tick
    {
        FGameplayModifierInfo ManaInfo = MakeAB(UAeyerjiAttributeSet::GetManaRegenAttribute());
        ManaInfo.Attribute = UAeyerjiAttributeSet::GetManaAttribute();
        Modifiers.Add(ManaInfo);
    }
}
