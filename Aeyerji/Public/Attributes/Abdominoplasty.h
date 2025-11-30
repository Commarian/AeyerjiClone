//  Abdominoplasty.h
#pragma once

#include "CoreMinimal.h"
#include "AttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Abdominoplasty.generated.h"

/** ---------------------------------------------------------------------------
 *  4 tiny inline helpers are produced per attribute:
 *      GetXAttribute(),  GetX(),  SetX(),  InitX()
 * --------------------------------------------------------------------------*/
#define ATTRIBUTE_ACCESSORS(ClassName, PropertyName)                   \
    GAMEPLAYATTRIBUTE_PROPERTY_GETTER(ClassName, PropertyName)         \
    GAMEPLAYATTRIBUTE_VALUE_GETTER(PropertyName)                       \
    GAMEPLAYATTRIBUTE_VALUE_SETTER(PropertyName)                       \
    GAMEPLAYATTRIBUTE_VALUE_INITTER(PropertyName)

/**
 *  All tunable values used by the “Abdominoplasty” Blink-variant ability live
 *  in this one Attribute Set so that designers can adjust them (and gameplay
 *  effects can modify them) without touching code.
 */
UCLASS()
class AEYERJI_API UAbdominoplasty : public UAttributeSet
{
    GENERATED_BODY()

public:

    /* ------------ Teleport reach ------------------------------------------------ */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Abdominoplasty|Range",
              ReplicatedUsing = OnRep_MaxRange)
    FGameplayAttributeData MaxRange;
    ATTRIBUTE_ACCESSORS(UAbdominoplasty, MaxRange)

    /* ------------ Immediate damage on landing ----------------------------------- */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Abdominoplasty|Damage",
              ReplicatedUsing = OnRep_InstantDamage)
    FGameplayAttributeData InstantDamage;
    ATTRIBUTE_ACCESSORS(UAbdominoplasty, InstantDamage)

    /* ------------ Stack count (buffs / items can raise this) -------------------- */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Abdominoplasty|Stacks",
              ReplicatedUsing = OnRep_NoStacks)
    FGameplayAttributeData NoStacks;
    ATTRIBUTE_ACCESSORS(UAbdominoplasty, NoStacks)

    /* ------------ Landing area radius ------------------------------------------- */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Abdominoplasty|Radius",
              ReplicatedUsing = OnRep_LandingRadius)
    FGameplayAttributeData LandingRadius;
    ATTRIBUTE_ACCESSORS(UAbdominoplasty, LandingRadius)

    /* ------------ Probability the blink endpoint is “chaotic” ------------------- */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Abdominoplasty|Chaos",
              ReplicatedUsing = OnRep_ChaosChance)
    FGameplayAttributeData ChaosChance;          //  0-1 range
    ATTRIBUTE_ACCESSORS(UAbdominoplasty, ChaosChance)

    /* --------------------------------------------------------------------------- */
    /** RepNotify callbacks – one per attribute                                    */
    UFUNCTION() void OnRep_MaxRange     (const FGameplayAttributeData& Old);
    UFUNCTION() void OnRep_InstantDamage(const FGameplayAttributeData& Old);
    UFUNCTION() void OnRep_NoStacks     (const FGameplayAttributeData& Old);
    UFUNCTION() void OnRep_LandingRadius(const FGameplayAttributeData& Old);
    UFUNCTION() void OnRep_ChaosChance  (const FGameplayAttributeData& Old);

    /** Register what we replicate and ensure OnRep fires even with prediction */
    virtual void GetLifetimeReplicatedProps(
        TArray<FLifetimeProperty>& OutProps) const override;
};
