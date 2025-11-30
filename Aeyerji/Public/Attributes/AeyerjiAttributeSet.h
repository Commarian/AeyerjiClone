// File: Source/Aeyerji/Public/Attributes/AeyerjiAttributeSet.h
#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AeyerjiAttributeSet.generated.h"

#define AEYERJI_ATTR_ACCESSORS(Class, Prop) \
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(Class, Prop) \
    GAMEPLAYATTRIBUTE_VALUE_GETTER(Prop)           \
    GAMEPLAYATTRIBUTE_VALUE_SETTER(Prop)           \
    GAMEPLAYATTRIBUTE_VALUE_INITTER(Prop)

/** Called once, server-side, the first frame HP hits 0. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOutOfHealthDelegate, AActor*, Victim, AActor*, Instigator, float, DamageTaken);

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
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_AttackRange,      Category="Stats|Combat",   SaveGame) FGameplayAttributeData AttackRange;       AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, AttackRange)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_AttackSpeed,      Category="Stats|Combat",   SaveGame) FGameplayAttributeData AttackSpeed;       AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, AttackSpeed)

    /* ---------- Resources ---------- */
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_HP,               Category="Stats|Resource", SaveGame) FGameplayAttributeData HP;                AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, HP)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_HPMax,            Category="Stats|Resource", SaveGame) FGameplayAttributeData HPMax;             AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, HPMax)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Mana,             Category="Stats|Resource", SaveGame) FGameplayAttributeData Mana;              AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, Mana)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_ManaMax,          Category="Stats|Resource", SaveGame) FGameplayAttributeData ManaMax;           AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, ManaMax)

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
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_Ailment,           Category="Stats|Core",    SaveGame) FGameplayAttributeData Ailment;            AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, Ailment)

    /* ---------- Derived (from Core) ---------- */
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CritChance,        Category="Stats|Derived", SaveGame) FGameplayAttributeData CritChance;         AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, CritChance)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_DodgeChance,       Category="Stats|Derived", SaveGame) FGameplayAttributeData DodgeChance;        AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, DodgeChance)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_SpellPower,        Category="Stats|Derived", SaveGame) FGameplayAttributeData SpellPower;         AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, SpellPower)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_ManaRegen,         Category="Stats|Derived", SaveGame) FGameplayAttributeData ManaRegen;          AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, ManaRegen)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_HPRegen,           Category="Stats|Derived", SaveGame) FGameplayAttributeData HPRegen;            AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, HPRegen)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_CooldownReduction, Category="Stats|Derived", SaveGame) FGameplayAttributeData CooldownReduction;  AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, CooldownReduction)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_AilmentDPS,        Category="Stats|Derived", SaveGame) FGameplayAttributeData AilmentDPS;         AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, AilmentDPS)
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_AilmentDuration,   Category="Stats|Derived", SaveGame) FGameplayAttributeData AilmentDuration;    AEYERJI_ATTR_ACCESSORS(UAeyerjiAttributeSet, AilmentDuration)

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
    UFUNCTION() void OnRep_AttackRange               (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, AttackRange, Old); }
    UFUNCTION() void OnRep_AttackSpeed               (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, AttackSpeed, Old); }

    UFUNCTION() void OnRep_HP                        (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, HP, Old); }
    UFUNCTION() void OnRep_HPMax                     (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, HPMax, Old); }
    UFUNCTION() void OnRep_Mana                      (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, Mana, Old); }
    UFUNCTION() void OnRep_ManaMax                   (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, ManaMax, Old); }

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
    UFUNCTION() void OnRep_Ailment                   (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, Ailment, Old); }

    UFUNCTION() void OnRep_CritChance                (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, CritChance, Old); }
    UFUNCTION() void OnRep_DodgeChance               (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, DodgeChance, Old); }
    UFUNCTION() void OnRep_SpellPower                (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, SpellPower, Old); }
    UFUNCTION() void OnRep_ManaRegen                 (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, ManaRegen, Old); }
    UFUNCTION() void OnRep_HPRegen                   (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, HPRegen, Old); }
    UFUNCTION() void OnRep_CooldownReduction         (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, CooldownReduction, Old); }
    UFUNCTION() void OnRep_AilmentDPS                (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, AilmentDPS, Old); }
    UFUNCTION() void OnRep_AilmentDuration           (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, AilmentDuration, Old); }

    UFUNCTION() void OnRep_XP                        (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, XP, Old); }
    UFUNCTION() void OnRep_XPMax                     (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, XPMax, Old); }
    UFUNCTION() void OnRep_Level                     (const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiAttributeSet, Level, Old); }
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

    virtual void PostGameplayEffectExecute(const FGameplayEffectModCallbackData& Data) override;

    /** Broadcast once when HP hits 0. */
public:
    FOutOfHealthDelegate OnOutOfHealth;
private:
    /** Set after the delegate fires once. */
    UPROPERTY() uint8 bIsDead : 1;
};


