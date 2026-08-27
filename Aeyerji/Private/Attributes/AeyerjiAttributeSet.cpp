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
	float AttributeFiniteOrDefault(const float Value, const float DefaultValue = 0.f)
	{
		return FMath::IsFinite(Value) ? Value : DefaultValue;
	}

	float AttributeMaximum(const float Value, const float Minimum)
	{
		return FMath::Max(Minimum, AttributeFiniteOrDefault(Value, Minimum));
	}

    FAeyerjiCombatLimitsTuning ResolveCombatLimits()
    {
		FAeyerjiCombatLimitsTuning Result;
        if (const UAeyerjiAttributeTuning* Tuning = UAeyerjiStatSettings::Get())
        {
			Result = Tuning->CombatLimits;
        }

		Result.MaxCritChance = FMath::Clamp(AttributeFiniteOrDefault(Result.MaxCritChance, 1.f), 0.f, 1.f);
		Result.MaxDodgeChance = Result.GetSafeMaxDodgeChance();
		Result.MaxCriticalDamageMultiplier = FMath::Max(1.f, AttributeFiniteOrDefault(Result.MaxCriticalDamageMultiplier, 5.f));
		Result.MaxArmorPenetration = FMath::Clamp(AttributeFiniteOrDefault(Result.MaxArmorPenetration, 0.75f), 0.f, 1.f);
		Result.MaxLifeSteal = FMath::Clamp(AttributeFiniteOrDefault(Result.MaxLifeSteal, 0.25f), 0.f, 1.f);
		Result.MaxStaggerResistance = FMath::Clamp(AttributeFiniteOrDefault(Result.MaxStaggerResistance, 0.9f), 0.f, 1.f);
		Result.StaggerDuration = FMath::Max(0.01f, AttributeFiniteOrDefault(Result.StaggerDuration, 0.35f));
		Result.PoiseRecoveryDelay = FMath::Max(0.01f, AttributeFiniteOrDefault(Result.PoiseRecoveryDelay, 1.5f));
		Result.PoiseRecoveryPerSecond = FMath::Max(0.f, AttributeFiniteOrDefault(Result.PoiseRecoveryPerSecond, 35.f));
		return Result;
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
	NewMaxValue = AttributeFiniteOrDefault(NewMaxValue);
    const float CurrentMax = AttributeFiniteOrDefault(MaxAttribute.GetCurrentValue());
    if (!ASC || FMath::IsNearlyEqual(CurrentMax, NewMaxValue))
    {
        return;
    }
	const float CurrentValue = AttributeFiniteOrDefault(AffectedAttribute.GetCurrentValue());
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
	if (FMath::IsFinite(NewDelta))
	{
		ASC->ApplyModToAttributeUnsafe(AffectedAttributeProperty, EGameplayModOp::Additive, NewDelta);
	}
}

void UAeyerjiAttributeSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);

	// Base-value writes use a distinct GAS callback, so item and effect modifiers cannot bypass the combat dodge cap.
	if (Attribute == GetDodgeChanceAttribute())
	{
		NewValue = FMath::Clamp(AttributeFiniteOrDefault(NewValue), 0.f, ResolveCombatLimits().GetSafeMaxDodgeChance());
	}
}

/* Clamp or derive stats here if you wish */
void UAeyerjiAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
    Super::PreAttributeChange(Attribute, NewValue);

	// A single NaN in a persistent aggregator poisons downstream combat math and replication.
	if (!FMath::IsFinite(NewValue))
	{
		const float CurrentValue = Attribute.GetNumericValue(this);
		NewValue = AttributeFiniteOrDefault(CurrentValue);
	}

    if (Attribute == GetHPAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, AttributeMaximum(HPMax.GetCurrentValue(), 1.f));
    }
    else if (Attribute == GetManaAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, AttributeMaximum(ManaMax.GetCurrentValue(), 0.f));
    }
    else if (Attribute == GetPoiseAttribute())
    {
        NewValue = FMath::Clamp(NewValue, 0.f, AttributeMaximum(PoiseMax.GetCurrentValue(), 1.f));
    }
    else if (Attribute == GetXPAttribute())
    {
        // XP is 0..XPMax (the manager will roll over & raise Level)
        NewValue = FMath::Clamp(NewValue, 0.f, AttributeMaximum(XPMax.GetCurrentValue(), 1.f));
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
		NewValue = static_cast<float>(FMath::RoundToInt(FMath::Clamp(
			NewValue,
			1.f,
			static_cast<float>(UAeyerjiDifficultySettings::GetMaxGameplayLevel()))));
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
	else if (Attribute == GetArmorAttribute()
		|| Attribute == GetAttackDamageAttribute()
		|| Attribute == GetAttackRangeAttribute()
		|| Attribute == GetPatrolRadiusAttribute()
		|| Attribute == GetProjectilePredictionAmountAttribute())
	{
		NewValue = FMath::Max(0.f, NewValue);
	}
	else if (Attribute == GetAttackAngleAttribute())
	{
		NewValue = FMath::Clamp(NewValue, 0.f, 180.f);
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
        NewValue = FMath::Clamp(NewValue, 0.f, ResolveCombatLimits().GetSafeMaxDodgeChance());
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
		const float CurrentAS = FMath::Clamp(AttributeFiniteOrDefault(GetAttackSpeed(), 100.f), 0.01f, 1000.f);
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
		const float SafeHPMax = AttributeMaximum(GetHPMax(), 1.f);
		SetHP(FMath::Clamp(AttributeFiniteOrDefault(GetHP()), 0.f, SafeHPMax));

        if (!bIsDead && GetHP() <= 0.f)
        {
            const float DamageTaken = FMath::Max(-Data.EvaluatedData.Magnitude, 0.f);
            HandleOutOfHealth(Data, DamageTaken);
        }
    }
    else if (Data.EvaluatedData.Attribute == GetManaAttribute())
    {
		const float SafeManaMax = AttributeMaximum(GetManaMax(), 0.f);
		SetMana(FMath::Clamp(AttributeFiniteOrDefault(GetMana()), 0.f, SafeManaMax));
    }
    else if (Data.EvaluatedData.Attribute == GetXPAttribute())
    {
		const float SafeXPMax = AttributeMaximum(GetXPMax(), 1.f);
		SetXP(FMath::Clamp(AttributeFiniteOrDefault(GetXP()), 0.f, SafeXPMax));
    }

}

void UAeyerjiAttributeSet::HandleIncomingDamage(const FGameplayEffectModCallbackData& Data)
{
	AActor* TargetActor = GetOwningActor();
	if (!TargetActor || !TargetActor->HasAuthority())
	{
		SetIncomingDamage(0.f);
		return;
	}

	const float PendingDamage = FMath::Max(0.f, AttributeFiniteOrDefault(GetIncomingDamage()));
    SetIncomingDamage(0.f);
    if (PendingDamage <= KINDA_SMALL_NUMBER)
    {
        return;
    }

	FGameplayEffectContextHandle ContextHandle = Data.EffectSpec.GetContext();
	FAeyerjiGameplayEffectContext* AeyerjiContext = FAeyerjiGameplayEffectContext::ExtractMutable(ContextHandle);
	FAeyerjiDamageResult* Result = AeyerjiContext ? &AeyerjiContext->GetMutableDamageResult() : nullptr;

	const float HPBeforeDamage = FMath::Max(0.f, AttributeFiniteOrDefault(GetHP()));
	if (bIsDead || HPBeforeDamage <= KINDA_SMALL_NUMBER)
	{
		if (Result)
		{
			Result->FinalDamage = 0.f;
			Result->bWasFatal = false;
			Result->StaggerDamage = 0.f;
			Result->bTriggeredStagger = false;
		}
		return;
	}

    const float ActualDamage = FMath::Min(PendingDamage, FMath::Max(0.f, HPBeforeDamage));
	const float SafeHPMax = AttributeMaximum(GetHPMax(), 1.f);
    SetHP(FMath::Clamp(HPBeforeDamage - PendingDamage, 0.f, SafeHPMax));

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
	OnDamageTaken.Broadcast(TargetActor, SourceActor, ActualDamage, Result ? Result->DamageType : FGameplayTag());
    

    const bool bCanLifeSteal = Result && Result->RuleTags.HasTagExact(AeyerjiTags::DamageRule_CanLifeSteal);
    if (bCanLifeSteal && SourceASC && SourceASC != TargetASC && ActualDamage > KINDA_SMALL_NUMBER)
    {
        const float LifeStealFraction = FMath::Clamp(Result->LifeStealFraction, 0.f, ResolveCombatLimits().MaxLifeSteal);
		const float SourceHP = AttributeFiniteOrDefault(SourceASC->GetNumericAttribute(GetHPAttribute()));
		const float SourceHPMax = FMath::Max(0.f, AttributeFiniteOrDefault(SourceASC->GetNumericAttribute(GetHPMaxAttribute())));
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
	AActor* TargetActor = GetOwningActor();
	if (!TargetActor || !TargetActor->HasAuthority() || AttributeFiniteOrDefault(GetHP()) <= 0.f || bIsDead)
	{
		return;
	}

    FGameplayEffectContextHandle ContextHandle = Data.EffectSpec.GetContext();
    if (UAbilitySystemComponent* TargetASC = GetOwningAbilitySystemComponent())
    {
        TargetASC->ExecuteGameplayCue(AeyerjiTags::GameplayCue_Combat_Hit_Dodged, ContextHandle);
    }
}

void UAeyerjiAttributeSet::HandleIncomingStagger(const FGameplayEffectModCallbackData& Data)
{
	AActor* TargetActor = GetOwningActor();
	const float PendingStagger = FMath::Max(0.f, AttributeFiniteOrDefault(GetIncomingStagger()));
    SetIncomingStagger(0.f);
	const float SafePoiseMax = AttributeMaximum(GetPoiseMax(), 1.f);
	const float SafeHP = AttributeFiniteOrDefault(GetHP());
	if (!TargetActor || !TargetActor->HasAuthority()
		|| PendingStagger <= KINDA_SMALL_NUMBER
		|| SafeHP <= 0.f
		|| bIsDead)
    {
        return;
    }

    FGameplayEffectContextHandle ContextHandle = Data.EffectSpec.GetContext();
    FAeyerjiGameplayEffectContext* AeyerjiContext = FAeyerjiGameplayEffectContext::ExtractMutable(ContextHandle);
    FAeyerjiDamageResult* Result = AeyerjiContext ? &AeyerjiContext->GetMutableDamageResult() : nullptr;
    const bool bFatalDamageAlreadyResolved = Result && Result->bWasFatal;
    const bool bThisHitWillKillBeforeDamageCallback = Result
        && Result->FinalDamage > KINDA_SMALL_NUMBER
		&& SafeHP > 0.f
		&& AttributeFiniteOrDefault(Result->FinalDamage) >= SafeHP - KINDA_SMALL_NUMBER;
    if (bFatalDamageAlreadyResolved || bThisHitWillKillBeforeDamageCallback)
    {
        if (Result)
        {
            Result->StaggerDamage = 0.f;
            Result->bTriggeredStagger = false;
        }
        return;
    }

	const float NewPoise = FMath::Max(0.f, AttributeFiniteOrDefault(GetPoise()) - PendingStagger);
    if (NewPoise > KINDA_SMALL_NUMBER)
    {
        SetPoise(NewPoise);
        SchedulePoiseRecovery();
        return;
    }

    SetPoise(SafePoiseMax);
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
	AActor* OwnerActor = GetOwningActor();
	const float SafePoiseMax = AttributeMaximum(GetPoiseMax(), 1.f);
	const float SafePoise = FMath::Clamp(AttributeFiniteOrDefault(GetPoise()), 0.f, SafePoiseMax);
	if (!World || !OwnerActor || !OwnerActor->HasAuthority()
		|| SafePoise >= SafePoiseMax || AttributeFiniteOrDefault(GetHP()) <= 0.f)
    {
        if (World)
        {
            World->GetTimerManager().ClearTimer(PoiseRecoveryTickHandle);
        }
        return;
    }

    constexpr float RecoveryInterval = 0.1f;
	SetPoise(FMath::Min(
		SafePoiseMax,
		SafePoise + ResolveCombatLimits().PoiseRecoveryPerSecond * RecoveryInterval));
	if (GetPoise() < SafePoiseMax && !World->GetTimerManager().IsTimerActive(PoiseRecoveryTickHandle))
    {
        World->GetTimerManager().SetTimer(PoiseRecoveryTickHandle, this, &UAeyerjiAttributeSet::TickPoiseRecovery, RecoveryInterval, true);
    }
}

void UAeyerjiAttributeSet::ResetDeathStateForReuse()
{
	AActor* OwnerActor = GetOwningActor();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return;
	}

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
	AActor* OwnerActor = GetOwningActor();
	if (!OwnerActor || !OwnerActor->HasAuthority() || bIsDead || GetHP() > 0.f)
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

	OnOutOfHealth.Broadcast(OwnerActor, Data.EffectSpec.GetContext().GetOriginalInstigator(), FMath::Max(0.f, AttributeFiniteOrDefault(DamageTaken)));
}




