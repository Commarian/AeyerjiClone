// File: Source/Aeyerji/Private/Attributes/AeyerjiAttributeSet.cpp
#include "Attributes/AeyerjiAttributeSet.h"
#include "Net/UnrealNetwork.h"
#include "GameplayEffectExtension.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagsManager.h"
#include "Abilities/GA_Death.h"

UAeyerjiAttributeSet::UAeyerjiAttributeSet()
    : bIsDead(false)
{
    // Safe defaults so designers don't forget to set values in DataAssets.
    InitHP(100.f);
    InitHPMax(100.f);

    // New: initialize XP / Level baseline for players and test pawns.
    InitXP(0.f);
    InitXPMax(100.f);   // first threshold; you'll drive real values from curve
    InitLevel(1.f);

    // Establish sensible combat defaults so dependent attributes behave.
    // Convention: AttackSpeed is a rating where 100 == 1 attack/sec; AttackCooldown = 100 / AttackSpeed seconds.
    InitAttackSpeed(1.f);
    InitAttackCooldown(1.f);
    InitAttackDamage(10.f);

    // Core attributes and common derived baselines
    InitStrength(0.f);
    InitAgility(0.f);
    InitIntellect(0.f);
    InitAilment(0.f);

    InitCritChance(0.f);
    InitDodgeChance(0.f);
    InitSpellPower(0.f);
    InitMagicAmp(0.f);
    InitManaRegen(0.f);
    InitHPRegen(0.f);
    InitCooldownReduction(0.f);
    InitAilmentDPS(0.f);
    InitAilmentDuration(0.f);

    InitVisionRange(1500.f);
    InitHearingRange(750.f);
}

void UAeyerjiAttributeSet::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, Armor                     , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, AttackAngle               , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, AttackCooldown            , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, AttackDamage              , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, AttackRange               , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, AttackSpeed               , COND_None, REPNOTIFY_Always);

    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, HP                        , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, HPMax                     , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, Mana                      , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, ManaMax                   , COND_None, REPNOTIFY_Always);

    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, PatrolRadius              , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, ProjectilePredictionAmount, COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, ProjectileSpeedRanged     , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, RunSpeed                  , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, WalkSpeed                 , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, VisionRange               , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, HearingRange              , COND_None, REPNOTIFY_Always);

    // Core attributes
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, Strength                  , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, Agility                   , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, Intellect                , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, Ailment                   , COND_None, REPNOTIFY_Always);

    // Derived attributes
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, CritChance                , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, DodgeChance               , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, SpellPower                , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, MagicAmp                  , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, ManaRegen                 , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, HPRegen                   , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, CooldownReduction         , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, AilmentDPS                , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, AilmentDuration           , COND_None, REPNOTIFY_Always);

    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, XP                        , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, XPMax                     , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, Level                     , COND_None, REPNOTIFY_Always); // NEW

}
void UAeyerjiAttributeSet::AdjustAttributeForMaxChange(FGameplayAttributeData& AffectedAttribute,
                                     const FGameplayAttributeData& MaxAttribute,
                                     float NewMaxValue,
                                     const FGameplayAttribute& AffectedAttributeProperty)
{
    UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent();
    const float CurrentMax = MaxAttribute.GetCurrentValue();
    if (!ASC || FMath::IsNearlyEqual(CurrentMax, NewMaxValue))
    {
        return;
    }
    const float CurrentValue = AffectedAttribute.GetCurrentValue();
    float NewDelta = 0.f;
    if (CurrentMax > 0.f)
    {
        const float NewValue = CurrentValue * NewMaxValue / CurrentMax;
        NewDelta = NewValue - CurrentValue;
    }
    else
    {
        NewDelta = NewMaxValue;
    }
    ASC->ApplyModToAttributeUnsafe(AffectedAttributeProperty, EGameplayModOp::Additive, NewDelta);
}

/* Clamp or derive stats here if you wish */
void UAeyerjiAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
    Super::PreAttributeChange(Attribute, NewValue);

    if (Attribute == GetHPAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, HPMax.GetCurrentValue());
    }
    else if (Attribute == GetManaAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, ManaMax.GetCurrentValue());
    }
    else if (Attribute == GetXPAttribute())
    {
        // XP is 0..XPMax (the manager will roll over & raise Level)
        NewValue = FMath::Clamp(NewValue, 0.f, XPMax.GetCurrentValue());
    }
    else if (Attribute == GetXPMaxAttribute())
    {
        NewValue = FMath::Max(NewValue, 1.f);
    }
    else if (Attribute == GetHPMaxAttribute())
    {
        NewValue = FMath::Max(NewValue, 1.f);
        AdjustAttributeForMaxChange(HP, HPMax, NewValue, GetHPAttribute());
    }
    else if (Attribute == GetManaMaxAttribute())
    {
        NewValue = FMath::Max(NewValue, 0.f);
        AdjustAttributeForMaxChange(Mana, ManaMax, NewValue, GetManaAttribute());
    }
    else if (Attribute == GetLevelAttribute())
    {
        // Keep a sane, non-zero level for scalable float lookups.
        NewValue = FMath::Clamp(NewValue, 1.f, 999.f);
    }

    // Core attributes are non-negative
    else if (Attribute == GetStrengthAttribute())
    {
        NewValue = FMath::Max(0.f, NewValue);
    }
    else if (Attribute == GetAgilityAttribute())
    {
        NewValue = FMath::Max(0.f, NewValue);
    }
    else if (Attribute == GetIntellectAttribute())
    {
        NewValue = FMath::Max(0.f, NewValue);
    }
    else if (Attribute == GetAilmentAttribute())
    {
        NewValue = FMath::Max(0.f, NewValue);
    }
    else if (Attribute == GetVisionRangeAttribute())
    {
        NewValue = FMath::Max(0.f, NewValue);
    }
    else if (Attribute == GetHearingRangeAttribute())
    {
        NewValue = FMath::Max(0.f, NewValue);
    }

    // Derived clamps
    else if (Attribute == GetCritChanceAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, 1.f);
    }
    else if (Attribute == GetDodgeChanceAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, 1.f);
    }
    else if (Attribute == GetSpellPowerAttribute())
    {
        NewValue = FMath::Max(0.f, NewValue);
    }
    else if (Attribute == GetMagicAmpAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, 1.f);
    }
    else if (Attribute == GetManaRegenAttribute())
    {
        NewValue = FMath::Max(0.f, NewValue);
    }
    else if (Attribute == GetHPRegenAttribute())
    {
        NewValue = FMath::Max(0.f, NewValue);
    }
    else if (Attribute == GetCooldownReductionAttribute())
    {
        // Cap around 40% by design
        NewValue = FMath::Clamp(NewValue, 0.f, 0.40f);
    }
    else if (Attribute == GetAilmentDPSAttribute())
    {
        NewValue = FMath::Max(0.f, NewValue);
    }
    else if (Attribute == GetAilmentDurationAttribute())
    {
        NewValue = FMath::Max(0.f, NewValue);
    }

    // Keep AttackSpeed and AttackCooldown in sync.
    // New design: CooldownSeconds = clamp(100 / AttackSpeed, 0.01 .. 5.0)
    else if (Attribute == GetAttackSpeedAttribute())
    {
        // Prevent zero/absurd values for AttackSpeed itself
        NewValue = FMath::Clamp(NewValue, 0.01f, 1000.f);
        const float DerivedCooldown = FMath::Clamp(100.f / NewValue, 0.01f, 5.f);
        SetAttackCooldown(DerivedCooldown);
    }
}

/* ---------------- Zero-HP detection ---------------- */
void UAeyerjiAttributeSet::PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data)
{
    Super::PostGameplayEffectExecute(Data);

    // Keep cooldown derived from AttackSpeed whenever AttackSpeed or AttackCooldown is modified by a GE.
    if (Data.EvaluatedData.Attribute == GetAttackSpeedAttribute()
        || Data.EvaluatedData.Attribute == GetAttackCooldownAttribute())
    {
        const float CurrentAS = FMath::Clamp(GetAttackSpeed(), 0.01f, 1000.f);
        const float DerivedCooldown = FMath::Clamp(100.f / CurrentAS, 0.01f, 5.f);
        if (!FMath::IsNearlyEqual(GetAttackCooldown(), DerivedCooldown))
        {
            SetAttackCooldown(DerivedCooldown);
        }
    }

    if (Data.EvaluatedData.Attribute == GetHPAttribute())
    {
        // Final clamp in case the incoming GE pushed us below zero.
        SetHP(FMath::Clamp(GetHP(), 0.f, GetHPMax()));

        if (!bIsDead && GetHP() <= 0.f)
        {
            bIsDead = true;

            // Add the tag State.Dead so Animation / Abilities react.
            if (UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent())
            {
                static const FGameplayTag DeadTag = FGameplayTag::RequestGameplayTag(TEXT("State.Dead"));
                ASC->AddLooseGameplayTag(DeadTag);
            }

            AActor* Victim     = GetOwningActor();
            AActor* Instigator = Data.EffectSpec.GetContext().GetOriginalInstigator();

            const float DamageTaken = FMath::Max(-Data.EvaluatedData.Magnitude, 0.f);

            OnOutOfHealth.Broadcast(Victim, Instigator, DamageTaken);
        }
    }
    else if (Data.EvaluatedData.Attribute == GetManaAttribute())
    {
        SetMana(FMath::Clamp(GetMana(), 0.f, GetManaMax()));
    }
    else if (Data.EvaluatedData.Attribute == GetXPAttribute())
    {
        SetXP(FMath::Clamp(GetXP(), 0.f, GetXPMax()));
    }

}




