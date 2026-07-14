// File: Source/Aeyerji/Private/Attributes/AeyerjiAttributeSet.cpp
#include "Attributes/AeyerjiAttributeSet.h"
#include "Net/UnrealNetwork.h"
#include "GameplayEffectExtension.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagsManager.h"
#include "Abilities/GA_Death.h"
#include "AeyerjiGameplayTags.h"
#include "Attributes/AeyerjiStatTuning.h"
#include "GAS/AeyerjiGameplayEffectContext.h"
#include "GAS/ExecCalc_DamagePhysical.h"
#include "GAS/GE_Stagger.h"
#include "Systems/AeyerjiDifficultyTuning.h"

namespace
{
    FAeyerjiCombatLimitsTuning ResolveCombatLimits()
    {
        if (const UAeyerjiAttributeTuning* Tuning = UAeyerjiStatSettings::Get())
        {
            return Tuning->CombatLimits;
        }
        return FAeyerjiCombatLimitsTuning();
    }
}

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
    InitAttackSpeed(100.f);
    InitAttackCooldown(1.f);
    InitAttackDamage(10.f);
    InitAttackDamageVariance(0.125f);
    InitPhysicalDamageBonus(0.f);
    InitArmorPenetration(0.f);
    InitLifeSteal(0.f);
    InitStaggerPower(1.f);
    InitStaggerResistance(0.f);

    InitPoiseMax(100.f);
    InitPoise(100.f);
    InitIncomingDamage(0.f);
    InitIncomingDodge(0.f);
    InitIncomingStagger(0.f);

    // Core attributes and common derived baselines
    InitStrength(0.f);
    InitAgility(0.f);
    InitIntellect(0.f);

    InitPoisonAmount(0.f);
    InitPoisonDuration(0.f);
    InitTraumaAmount(0.f);
    InitTraumaDuration(0.f);
    InitCorruptionAmount(0.f);
    InitCorruptionDuration(0.f);

    InitCritChance(0.f);
    InitCriticalDamageMultiplier(2.f);
    InitDodgeChance(0.f);
    InitSpellPower(0.f);
    InitMagicAmp(0.f);
    InitManaRegen(0.f);
    InitHPRegen(0.f);
    InitCooldownReduction(0.f);

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
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, AttackDamageVariance      , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, AttackRange               , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, AttackSpeed               , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, PhysicalDamageBonus       , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, ArmorPenetration          , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, LifeSteal                 , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, StaggerPower              , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, StaggerResistance         , COND_None, REPNOTIFY_Always);

    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, HP                        , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, HPMax                     , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, Mana                      , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, ManaMax                   , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, Poise                     , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, PoiseMax                  , COND_None, REPNOTIFY_Always);

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

    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, PoisonAmount             , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, PoisonDuration           , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, TraumaAmount             , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, TraumaDuration           , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, CorruptionAmount         , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, CorruptionDuration       , COND_None, REPNOTIFY_Always);

    // Derived attributes
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, CritChance                , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, CriticalDamageMultiplier  , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, DodgeChance               , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, SpellPower                , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, MagicAmp                  , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, ManaRegen                 , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, HPRegen                   , COND_None, REPNOTIFY_Always);
    DOREPLIFETIME_CONDITION_NOTIFY(UAeyerjiAttributeSet, CooldownReduction         , COND_None, REPNOTIFY_Always);

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
    else if (Attribute == GetPoiseAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, PoiseMax.GetCurrentValue());
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
    else if (Attribute == GetPoiseMaxAttribute())
    {
        NewValue = FMath::Max(NewValue, 1.f);
        AdjustAttributeForMaxChange(Poise, PoiseMax, NewValue, GetPoiseAttribute());
    }
    else if (Attribute == GetLevelAttribute())
    {
        // Keep a sane, non-zero level for scalable float lookups.
        NewValue = static_cast<float>(UAeyerjiDifficultySettings::ClampGameplayLevel(FMath::RoundToInt(NewValue)));
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
    else if (Attribute == GetPoisonAmountAttribute()
        || Attribute == GetPoisonDurationAttribute()
        || Attribute == GetTraumaAmountAttribute()
        || Attribute == GetTraumaDurationAttribute()
        || Attribute == GetCorruptionAmountAttribute()
        || Attribute == GetCorruptionDurationAttribute())
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
    else if (Attribute == GetRunSpeedAttribute())
    {
        NewValue = FMath::Max(0.f, NewValue);
    }
    else if (Attribute == GetWalkSpeedAttribute())
    {
        NewValue = FMath::Max(0.f, NewValue);
    }
    else if (Attribute == GetProjectileSpeedRangedAttribute())
    {
        NewValue = FMath::Max(0.f, NewValue);
    }

    // Derived clamps
    else if (Attribute == GetCritChanceAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, FMath::Max(0.f, ResolveCombatLimits().MaxCritChance));
    }
    else if (Attribute == GetAttackDamageVarianceAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, 0.95f);
    }
    else if (Attribute == GetCriticalDamageMultiplierAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 1.f, FMath::Max(1.f, ResolveCombatLimits().MaxCriticalDamageMultiplier));
    }
    else if (Attribute == GetDodgeChanceAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, 1.f);
    }
    else if (Attribute == GetSpellPowerAttribute())
    {
        NewValue = FMath::Max(0.f, NewValue);
    }
    else if (Attribute == GetPhysicalDamageBonusAttribute())
    {
        NewValue = FMath::Max(-0.90f, NewValue);
    }
    else if (Attribute == GetArmorPenetrationAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, FMath::Max(0.f, ResolveCombatLimits().MaxArmorPenetration));
    }
    else if (Attribute == GetLifeStealAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, FMath::Max(0.f, ResolveCombatLimits().MaxLifeSteal));
    }
    else if (Attribute == GetStaggerPowerAttribute())
    {
        NewValue = FMath::Max(0.f, NewValue);
    }
    else if (Attribute == GetStaggerResistanceAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, FMath::Max(0.f, ResolveCombatLimits().MaxStaggerResistance));
    }
    else if (Attribute == GetMagicAmpAttribute())
    {
        NewValue = FMath::Max(0.f, NewValue);
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

    if (Data.EvaluatedData.Attribute == GetIncomingStaggerAttribute())
    {
        HandleIncomingStagger(Data);
    }
    else if (Data.EvaluatedData.Attribute == GetIncomingDamageAttribute())
    {
        HandleIncomingDamage(Data);
    }
    else if (Data.EvaluatedData.Attribute == GetIncomingDodgeAttribute())
    {
        HandleIncomingDodge(Data);
    }
    else if (Data.EvaluatedData.Attribute == GetHPAttribute())
    {
        // Final clamp in case the incoming GE pushed us below zero.
        SetHP(FMath::Clamp(GetHP(), 0.f, GetHPMax()));

        if (!bIsDead && GetHP() <= 0.f)
        {
            const float DamageTaken = FMath::Max(-Data.EvaluatedData.Magnitude, 0.f);
            HandleOutOfHealth(Data, DamageTaken);
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

void UAeyerjiAttributeSet::HandleIncomingDamage(const FGameplayEffectModCallbackData& Data)
{
    const float PendingDamage = FMath::Max(0.f, GetIncomingDamage());
    SetIncomingDamage(0.f);
    if (PendingDamage <= KINDA_SMALL_NUMBER)
    {
        return;
    }

    const float HPBeforeDamage = GetHP();
    const float ActualDamage = FMath::Min(PendingDamage, FMath::Max(0.f, HPBeforeDamage));
    SetHP(FMath::Clamp(HPBeforeDamage - PendingDamage, 0.f, GetHPMax()));

    FGameplayEffectContextHandle ContextHandle = Data.EffectSpec.GetContext();
    FAeyerjiGameplayEffectContext* AeyerjiContext = FAeyerjiGameplayEffectContext::ExtractMutable(ContextHandle);
    FAeyerjiDamageResult* Result = AeyerjiContext ? &AeyerjiContext->GetMutableDamageResult() : nullptr;
    if (Result)
    {
        Result->FinalDamage = ActualDamage;
        Result->bWasFatal = HPBeforeDamage > 0.f && GetHP() <= 0.f;
        if (Result->bWasFatal)
        {
            Result->StaggerDamage = 0.f;
            Result->bTriggeredStagger = false;
        }
    }

    UAbilitySystemComponent* TargetASC = GetOwningAbilitySystemComponent();
    UAbilitySystemComponent* SourceASC = ContextHandle.GetOriginalInstigatorAbilitySystemComponent();
    AActor* SourceActor = ContextHandle.GetOriginalInstigator();
    AActor* TargetActor = GetOwningActor();

    if (ActualDamage > KINDA_SMALL_NUMBER)
    {
        OnDamageTaken.Broadcast(TargetActor, SourceActor, ActualDamage, Result ? Result->DamageType : FGameplayTag());
    }
    

    const bool bCanLifeSteal = Result && Result->RuleTags.HasTagExact(AeyerjiTags::DamageRule_CanLifeSteal);
    if (bCanLifeSteal && SourceASC && SourceASC != TargetASC && ActualDamage > KINDA_SMALL_NUMBER)
    {
        const float LifeStealFraction = FMath::Clamp(Result->LifeStealFraction, 0.f, ResolveCombatLimits().MaxLifeSteal);
        const float SourceHP = SourceASC->GetNumericAttribute(GetHPAttribute());
        const float SourceHPMax = SourceASC->GetNumericAttribute(GetHPMaxAttribute());
        const float Healing = UExecCalc_DamagePhysical::ResolveLifeSteal(
            ActualDamage,
            LifeStealFraction,
            SourceHPMax - SourceHP,
            bCanLifeSteal);
        if (Healing > KINDA_SMALL_NUMBER)
        {
            SourceASC->ApplyModToAttributeUnsafe(GetHPAttribute(), EGameplayModOp::Additive, Healing);
        }
    }

    FGameplayEventData Payload;
    Payload.Instigator = SourceActor;
    Payload.Target = TargetActor;
    Payload.OptionalObject = ContextHandle.GetSourceObject();
    Payload.ContextHandle = ContextHandle;
    Payload.EventMagnitude = ActualDamage;

    if (SourceASC)
    {
        Payload.EventTag = AeyerjiTags::Event_Combat_DamageDealt;
        SourceASC->HandleGameplayEvent(Payload.EventTag, &Payload);
        if (Result && Result->RuleTags.HasTagExact(AeyerjiTags::DamageRule_CanTriggerOnHit))
        {
            Payload.EventTag = AeyerjiTags::Event_Combat_OnHit;
            SourceASC->HandleGameplayEvent(Payload.EventTag, &Payload);
        }
    }

    if (TargetASC)
    {
        Payload.EventTag = AeyerjiTags::Event_Combat_DamageReceived;
        TargetASC->HandleGameplayEvent(Payload.EventTag, &Payload);
        TargetASC->ExecuteGameplayCue(
            Result && Result->bWasCritical ? AeyerjiTags::GameplayCue_Combat_Hit_Critical : AeyerjiTags::GameplayCue_Combat_Hit_Physical,
            ContextHandle);
        if (Result && Result->bWasFatal)
        {
            TargetASC->ExecuteGameplayCue(AeyerjiTags::GameplayCue_Combat_Hit_Killing, ContextHandle);
        }
    }

    if (!bIsDead && GetHP() <= 0.f)
    {
        HandleOutOfHealth(Data, ActualDamage);
    }
}

void UAeyerjiAttributeSet::HandleIncomingDodge(const FGameplayEffectModCallbackData& Data)
{
    SetIncomingDodge(0.f);

    FGameplayEffectContextHandle ContextHandle = Data.EffectSpec.GetContext();
    if (UAbilitySystemComponent* TargetASC = GetOwningAbilitySystemComponent())
    {
        TargetASC->ExecuteGameplayCue(AeyerjiTags::GameplayCue_Combat_Hit_Dodged, ContextHandle);
    }
}

void UAeyerjiAttributeSet::HandleIncomingStagger(const FGameplayEffectModCallbackData& Data)
{
    const float PendingStagger = FMath::Max(0.f, GetIncomingStagger());
    SetIncomingStagger(0.f);
    if (PendingStagger <= KINDA_SMALL_NUMBER || GetPoiseMax() <= KINDA_SMALL_NUMBER || GetHP() <= 0.f)
    {
        return;
    }

    FGameplayEffectContextHandle ContextHandle = Data.EffectSpec.GetContext();
    FAeyerjiGameplayEffectContext* AeyerjiContext = FAeyerjiGameplayEffectContext::ExtractMutable(ContextHandle);
    FAeyerjiDamageResult* Result = AeyerjiContext ? &AeyerjiContext->GetMutableDamageResult() : nullptr;
    const bool bFatalDamageAlreadyResolved = Result && Result->bWasFatal;
    const bool bThisHitWillKillBeforeDamageCallback = Result
        && Result->FinalDamage > KINDA_SMALL_NUMBER
        && GetHP() > 0.f
        && Result->FinalDamage >= GetHP() - KINDA_SMALL_NUMBER;
    if (bFatalDamageAlreadyResolved || bThisHitWillKillBeforeDamageCallback)
    {
        if (Result)
        {
            Result->StaggerDamage = 0.f;
            Result->bTriggeredStagger = false;
        }
        return;
    }

    const float NewPoise = FMath::Max(0.f, GetPoise() - PendingStagger);
    if (NewPoise > KINDA_SMALL_NUMBER)
    {
        SetPoise(NewPoise);
        SchedulePoiseRecovery();
        return;
    }

    SetPoise(GetPoiseMax());
    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(PoiseRecoveryDelayHandle);
        World->GetTimerManager().ClearTimer(PoiseRecoveryTickHandle);
    }

    if (Result)
    {
        Result->bTriggeredStagger = true;
    }

    if (UAbilitySystemComponent* TargetASC = GetOwningAbilitySystemComponent())
    {
        FGameplayEffectSpecHandle StaggerSpec = TargetASC->MakeOutgoingSpec(UGE_Stagger::StaticClass(), 1.f, ContextHandle);
        if (StaggerSpec.IsValid() && StaggerSpec.Data.IsValid())
        {
            StaggerSpec.Data->SetSetByCallerMagnitude(
                AeyerjiTags::SBC_Stagger_Duration,
                FMath::Max(0.01f, ResolveCombatLimits().StaggerDuration));
            TargetASC->ApplyGameplayEffectSpecToSelf(*StaggerSpec.Data.Get());
        }
        TargetASC->ExecuteGameplayCue(AeyerjiTags::GameplayCue_Combat_Hit_Staggered, ContextHandle);
    }
}

void UAeyerjiAttributeSet::SchedulePoiseRecovery()
{
    UWorld* World = GetWorld();
    AActor* OwnerActor = GetOwningActor();
    if (!World || !OwnerActor || !OwnerActor->HasAuthority())
    {
        return;
    }

    World->GetTimerManager().ClearTimer(PoiseRecoveryTickHandle);
    World->GetTimerManager().SetTimer(
        PoiseRecoveryDelayHandle,
        this,
        &UAeyerjiAttributeSet::TickPoiseRecovery,
        FMath::Max(0.01f, ResolveCombatLimits().PoiseRecoveryDelay),
        false);
}

void UAeyerjiAttributeSet::TickPoiseRecovery()
{
    UWorld* World = GetWorld();
    if (!World || GetPoise() >= GetPoiseMax() || GetHP() <= 0.f)
    {
        if (World)
        {
            World->GetTimerManager().ClearTimer(PoiseRecoveryTickHandle);
        }
        return;
    }

    constexpr float RecoveryInterval = 0.1f;
    SetPoise(FMath::Min(GetPoiseMax(), GetPoise() + ResolveCombatLimits().PoiseRecoveryPerSecond * RecoveryInterval));
    if (GetPoise() < GetPoiseMax() && !World->GetTimerManager().IsTimerActive(PoiseRecoveryTickHandle))
    {
        World->GetTimerManager().SetTimer(PoiseRecoveryTickHandle, this, &UAeyerjiAttributeSet::TickPoiseRecovery, RecoveryInterval, true);
    }
}

void UAeyerjiAttributeSet::ResetDeathStateForReuse()
{
    bIsDead = false;
    SetIncomingDamage(0.f);
    SetIncomingDodge(0.f);
    SetIncomingStagger(0.f);

    if (UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent())
    {
        ASC->SetLooseGameplayTagCount(AeyerjiTags::State_Dead, 0);
    }

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(PoiseRecoveryDelayHandle);
        World->GetTimerManager().ClearTimer(PoiseRecoveryTickHandle);
    }
}

void UAeyerjiAttributeSet::HandleOutOfHealth(const FGameplayEffectModCallbackData& Data, const float DamageTaken)
{
    if (bIsDead || GetHP() > 0.f)
    {
        return;
    }

    bIsDead = true;
    if (UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent())
    {
        ASC->AddLooseGameplayTag(AeyerjiTags::State_Dead);
    }

    if (UWorld* World = GetWorld())
    {
        World->GetTimerManager().ClearTimer(PoiseRecoveryDelayHandle);
        World->GetTimerManager().ClearTimer(PoiseRecoveryTickHandle);
    }

    OnOutOfHealth.Broadcast(GetOwningActor(), Data.EffectSpec.GetContext().GetOriginalInstigator(), DamageTaken);
}




