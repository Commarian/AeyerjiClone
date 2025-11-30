// File: Source/Aeyerji/Public/Attributes/AeyerjiRewardAttributeSet.h
#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "AeyerjiRewardAttributeSet.generated.h"

#define AEYERJI_REWARD_ACCESSORS(Class, Prop) \
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(Class, Prop) \
    GAMEPLAYATTRIBUTE_VALUE_GETTER(Prop)           \
    GAMEPLAYATTRIBUTE_VALUE_SETTER(Prop)           \
    GAMEPLAYATTRIBUTE_VALUE_INITTER(Prop)

/**
 * Lightweight AttributeSet to hold reward-related values (e.g., XP dropped on death).
 * Kept separate from the main UAeyerjiAttributeSet to avoid clutter.
 */
UCLASS()
class AEYERJI_API UAeyerjiRewardAttributeSet : public UAttributeSet
{
    GENERATED_BODY()

public:
    UAeyerjiRewardAttributeSet();

    /* ---------- Rewards ---------- */
    /** Base XP this unit should grant at player level 1. */
    UPROPERTY(BlueprintReadOnly, ReplicatedUsing = OnRep_XPRewardBase, Category="Stats|Reward", SaveGame)
    FGameplayAttributeData XPRewardBase; AEYERJI_REWARD_ACCESSORS(UAeyerjiRewardAttributeSet, XPRewardBase)

    /* === Rep-Notify callbacks === */
    UFUNCTION() void OnRep_XPRewardBase(const FGameplayAttributeData& Old) const { GAMEPLAYATTRIBUTE_REPNOTIFY(UAeyerjiRewardAttributeSet, XPRewardBase, Old); }

protected:
    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
};

