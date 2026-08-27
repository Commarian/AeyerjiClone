// File: Source/Aeyerji/Public/Attributes/AeyerjiAttributeSet.h
#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "TimerManager.h"
#include "AeyerjiAttributeSet.generated.h"

#define AEYERJI_ATTR_ACCESSORS(Class, Prop) \
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(Class, Prop) \
    GAMEPLAYATTRIBUTE_VALUE_GETTER(Prop)           \
    GAMEPLAYATTRIBUTE_VALUE_SETTER(Prop)           \
    GAMEPLAYATTRIBUTE_VALUE_INITTER(Prop)

/** Called once, server-side, the first frame HP hits 0. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOutOfHealthDelegate, AActor*, Victim, AActor*, Instigator, float, DamageTaken);
/** Called server-side whenever this set consumes positive incoming damage. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FAeyerjiDamageTakenDelegate, AActor*, Victim, AActor*, Instigator, float, DamageTaken, FGameplayTag, DamageType);

/**
 *  Single source of truth for ALL gameplay stats.
 *  No external mirrors, no legacy component hooks.
 */
UCLASS()
class AEYERJI_API UAeyerjiAttributeSet : public UAttributeSet
{
    GENERATED_BODY()

public:
    UAeyerjiAttributeSet();

    /* ---------- Combat ---------- */
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Armor,            Category="Stats|Combat",   SaveGame) FGameplayAttributeData Armor;             AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, Armor)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_AttackAngle,      Category="Stats|Combat",   SaveGame) FGameplayAttributeData AttackAngle;       AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, AttackAngle)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_AttackCooldown,   Category="Stats|Combat",   SaveGame) FGameplayAttributeData AttackCooldown;    AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, AttackCooldown)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_AttackDamage,     Category="Stats|Combat",   SaveGame) FGameplayAttributeData AttackDamage;      AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, AttackDamage)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_AttackDamageVariance, Category="Stats|Combat", SaveGame) FGameplayAttributeData AttackDamageVariance; AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, AttackDamageVariance)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_AttackRange,      Category="Stats|Combat",   SaveGame) FGameplayAttributeData AttackRange;       AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, AttackRange)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_AttackSpeed,      Category="Stats|Combat",   SaveGame) FGameplayAttributeData AttackSpeed;       AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, AttackSpeed)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_PhysicalDamageBonus, Category="Stats|Combat", SaveGame) FGameplayAttributeData PhysicalDamageBonus; AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, PhysicalDamageBonus)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_ArmorPenetration, Category="Stats|Combat", SaveGame) FGameplayAttributeData ArmorPenetration; AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, ArmorPenetration)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_LifeSteal, Category="Stats|Combat", SaveGame) FGameplayAttributeData LifeSteal; AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, LifeSteal)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_StaggerPower, Category="Stats|Combat", SaveGame) FGameplayAttributeData StaggerPower; AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, StaggerPower)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_StaggerResistance, Category="Stats|Combat", SaveGame) FGameplayAttributeData StaggerResistance; AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, StaggerResistance)

    /* ---------- Resources ---------- */
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_HP,               Category="Stats|Resource", SaveGame) FGameplayAttributeData HP;                AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, HP)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_HPMax,            Category="Stats|Resource", SaveGame) FGameplayAttributeData HPMax;             AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, HPMax)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Mana,             Category="Stats|Resource", SaveGame) FGameplayAttributeData Mana;              AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, Mana)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_ManaMax,          Category="Stats|Resource", SaveGame) FGameplayAttributeData ManaMax;           AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, ManaMax)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Poise,            Category="Stats|Resource", SaveGame) FGameplayAttributeData Poise;             AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, Poise)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_PoiseMax,         Category="Stats|Resource", SaveGame) FGameplayAttributeData PoiseMax;          AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, PoiseMax)

    /* ---------- Execution Meta Attributes ---------- */
    UPROPERTY(BlueprintReadOnly, Category="Stats|Meta") FGameplayAttributeData IncomingDamage; AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, IncomingDamage)
    UPROPERTY(BlueprintReadOnly, Category="Stats|Meta") FGameplayAttributeData IncomingDodge; AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, IncomingDodge)
    UPROPERTY(BlueprintReadOnly, Category="Stats|Meta") FGameplayAttributeData IncomingStagger; AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, IncomingStagger)

    /* ---------- Utility ---------- */
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_PatrolRadius,              Category="Stats|Utility", SaveGame) FGameplayAttributeData PatrolRadius;               AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, PatrolRadius)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_ProjectilePredictionAmount,Category="Stats|Utility", SaveGame) FGameplayAttributeData ProjectilePredictionAmount; AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, ProjectilePredictionAmount)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_ProjectileSpeedRanged,     Category="Stats|Utility", SaveGame) FGameplayAttributeData ProjectileSpeedRanged;      AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, ProjectileSpeedRanged)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_RunSpeed,                  Category="Stats|Utility", SaveGame) FGameplayAttributeData RunSpeed;                    AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, RunSpeed)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_WalkSpeed,                 Category="Stats|Utility", SaveGame) FGameplayAttributeData WalkSpeed;                   AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, WalkSpeed)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_VisionRange,               Category="Stats|Utility", SaveGame) FGameplayAttributeData VisionRange;                AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, VisionRange)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_HearingRange,              Category="Stats|Utility", SaveGame) FGameplayAttributeData HearingRange;               AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, HearingRange)

    /* ---------- Core Attributes ---------- */
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Strength,          Category="Stats|Core",    SaveGame) FGameplayAttributeData Strength;           AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, Strength)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Agility,           Category="Stats|Core",    SaveGame) FGameplayAttributeData Agility;            AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, Agility)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Intellect,           Category="Stats|Core",    SaveGame) FGameplayAttributeData Intellect;            AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, Intellect)

    /* ---------- Ailments ---------- */
    // Ailment Amount is flat DPS added when that ailment is applied (1.0 = +1 damage/sec).
    // Ailment Duration is flat seconds added to that ailment's duration (1.0 = +1 second).
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_PoisonAmount,      Category="Stats|Ailment", SaveGame) FGameplayAttributeData PoisonAmount;       AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, PoisonAmount)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_PoisonDuration,    Category="Stats|Ailment", SaveGame) FGameplayAttributeData PoisonDuration;     AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, PoisonDuration)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_TraumaAmount,      Category="Stats|Ailment", SaveGame) FGameplayAttributeData TraumaAmount;       AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, TraumaAmount)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_TraumaDuration,    Category="Stats|Ailment", SaveGame) FGameplayAttributeData TraumaDuration;     AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, TraumaDuration)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CorruptionAmount,  Category="Stats|Ailment", SaveGame) FGameplayAttributeData CorruptionAmount;   AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, CorruptionAmount)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CorruptionDuration,Category="Stats|Ailment", SaveGame) FGameplayAttributeData CorruptionDuration; AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, CorruptionDuration)

    /* ---------- Derived (from Core) ---------- */
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CritChance,        Category="Stats|Derived", SaveGame) FGameplayAttributeData CritChance;         AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, CritChance)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CriticalDamageMultiplier, Category="Stats|Derived", SaveGame) FGameplayAttributeData CriticalDamageMultiplier; AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, CriticalDamageMultiplier)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_DodgeChance,       Category="Stats|Derived", SaveGame) FGameplayAttributeData DodgeChance;        AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, DodgeChance)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_SpellPower,        Category="Stats|Derived", SaveGame) FGameplayAttributeData SpellPower;         AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, SpellPower)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_MagicAmp,          Category="Stats|Derived", SaveGame) FGameplayAttributeData MagicAmp;           AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, MagicAmp)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_ManaRegen,         Category="Stats|Derived", SaveGame) FGameplayAttributeData ManaRegen;          AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, ManaRegen)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_HPRegen,           Category="Stats|Derived", SaveGame) FGameplayAttributeData HPRegen;            AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, HPRegen)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CooldownReduction, Category="Stats|Derived", SaveGame) FGameplayAttributeData CooldownReduction;  AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, CooldownReduction)

    /* ---------- XP / Level ---------- */
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_XP,                Category="Stats|XP", SaveGame) FGameplayAttributeData XP;       AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, XP)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_XPMax,             Category="Stats|XP", SaveGame) FGameplayAttributeData XPMax;    AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, XPMax)

    /** Player / Unit level used by ScalableFloats & curve tables (drives GE levels). */
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Level,             Category="Stats|XP", SaveGame) FGameplayAttributeData Level;     AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, Level)

    /* === Rep-Notify callbacks === */
    UFUNCTION() void OnRep_Armor                     (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, Armor, Old); }
    UFUNCTION() void OnRep_AttackAngle               (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, AttackAngle, Old); }
    UFUNCTION() void OnRep_AttackCooldown            (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, AttackCooldown, Old); }
    UFUNCTION() void OnRep_AttackDamage              (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, AttackDamage, Old); }
    UFUNCTION() void OnRep_AttackDamageVariance      (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, AttackDamageVariance, Old); }
    UFUNCTION() void OnRep_AttackRange               (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, AttackRange, Old); }
    UFUNCTION() void OnRep_AttackSpeed               (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, AttackSpeed, Old); }
    UFUNCTION() void OnRep_PhysicalDamageBonus       (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, PhysicalDamageBonus, Old); }
    UFUNCTION() void OnRep_ArmorPenetration          (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, ArmorPenetration, Old); }
    UFUNCTION() void OnRep_LifeSteal                 (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, LifeSteal, Old); }
    UFUNCTION() void OnRep_StaggerPower              (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, StaggerPower, Old); }
    UFUNCTION() void OnRep_StaggerResistance         (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, StaggerResistance, Old); }

    UFUNCTION() void OnRep_HP                        (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, HP, Old); }
    UFUNCTION() void OnRep_HPMax                     (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, HPMax, Old); }
    UFUNCTION() void OnRep_Mana                      (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, Mana, Old); }
    UFUNCTION() void OnRep_ManaMax                   (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, ManaMax, Old); }
    UFUNCTION() void OnRep_Poise                     (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, Poise, Old); }
    UFUNCTION() void OnRep_PoiseMax                  (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, PoiseMax, Old); }

    UFUNCTION() void OnRep_PatrolRadius              (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, PatrolRadius, Old); }
    UFUNCTION() void OnRep_ProjectilePredictionAmount(const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, ProjectilePredictionAmount, Old); }
    UFUNCTION() void OnRep_ProjectileSpeedRanged     (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, ProjectileSpeedRanged, Old); }
    UFUNCTION() void OnRep_RunSpeed                  (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, RunSpeed, Old); }
    UFUNCTION() void OnRep_WalkSpeed                 (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, WalkSpeed, Old); }
    UFUNCTION() void OnRep_VisionRange               (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, VisionRange, Old); }
    UFUNCTION() void OnRep_HearingRange              (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, HearingRange, Old); }

    UFUNCTION() void OnRep_Strength                  (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, Strength, Old); }
    UFUNCTION() void OnRep_Agility                   (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, Agility, Old); }
    UFUNCTION() void OnRep_Intellect                   (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, Intellect, Old); }
    UFUNCTION() void OnRep_PoisonAmount              (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, PoisonAmount, Old); }
    UFUNCTION() void OnRep_PoisonDuration            (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, PoisonDuration, Old); }
    UFUNCTION() void OnRep_TraumaAmount              (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, TraumaAmount, Old); }
    UFUNCTION() void OnRep_TraumaDuration            (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, TraumaDuration, Old); }
    UFUNCTION() void OnRep_CorruptionAmount          (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, CorruptionAmount, Old); }
    UFUNCTION() void OnRep_CorruptionDuration        (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, CorruptionDuration, Old); }

    UFUNCTION() void OnRep_CritChance                (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, CritChance, Old); }
    UFUNCTION() void OnRep_CriticalDamageMultiplier  (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, CriticalDamageMultiplier, Old); }
    UFUNCTION() void OnRep_DodgeChance               (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, DodgeChance, Old); }
    UFUNCTION() void OnRep_SpellPower                (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, SpellPower, Old); }
    UFUNCTION() void OnRep_MagicAmp                  (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, MagicAmp, Old); }
    UFUNCTION() void OnRep_ManaRegen                 (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, ManaRegen, Old); }
    UFUNCTION() void OnRep_HPRegen                   (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, HPRegen, Old); }
    UFUNCTION() void OnRep_CooldownReduction         (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, CooldownReduction, Old); }

    UFUNCTION() void OnRep_XP                        (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, XP, Old); }
    UFUNCTION() void OnRep_XPMax                     (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, XPMax, Old); }
    UFUNCTION() void OnRep_Level                     (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, Level, Old); }

    /** Clears the one-shot death guard so pooled actors can broadcast out-of-health again after reuse. */
    void ResetDeathStateForReuse();
protected:
    /** Adjust an attribute when its corresponding Max changes (keeps same percent). */
    void AdjustAttributeForMaxChange(FGameplayAttributeData& AffectedAttribute,
                                     const FGameplayAttributeData& MaxAttribute,
                                     float NewMaxValue,
                                     const FGameplayAttribute& AffectedAttributeProperty);

    /* === Replication list === */
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

    /* Clamp before changes (keep set passive) */
    virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;

    /** Applies the combat dodge cap to direct base-value writes from items and gameplay effects. */
    virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;

    virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

    /** Applies a resolved hit, life steal, gameplay events, cues, and death handling. */
    void HandleIncomingDamage(const FGameplayEffectModCallbackData& Data);

    /** Emits the authoritative dodge event and cosmetic cue. */
    void HandleIncomingDodge(const FGameplayEffectModCallbackData& Data);

    /** Applies poise damage and the stagger effect when the threshold is crossed. */
    void HandleIncomingStagger(const FGameplayEffectModCallbackData& Data);

    /** Restarts delayed server-side poise recovery after a stagger-capable hit. */
    void SchedulePoiseRecovery();

    /** Restores poise over time using combat tuning values. */
    void TickPoiseRecovery();

    /** Performs the shared one-shot death transition for direct and meta damage paths. */
    void HandleOutOfHealth(const FGameplayEffectModCallbackData& Data, float DamageTaken);

    /** Broadcast once when HP hits 0. */
public:
    FOutOfHealthDelegate OnOutOfHealth;
    FAeyerjiDamageTakenDelegate OnDamageTaken;
private:
    /** Set after the delegate fires once. */
    UPROPERTY() uint8 bIsDead : 1;

    FTimerHandle PoiseRecoveryDelayHandle;
    FTimerHandle PoiseRecoveryTickHandle;
};


