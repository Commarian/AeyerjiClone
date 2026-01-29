// File: Source/Aeyerji/Public/Attributes/AeyerjiStatEngineComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayEffectTypes.h"
#include "AeyerjiStatEngineComponent.generated.h"

class UAbilitySystemComponent;
class UAeyerjiAttributeSet;
class UGameplayEffect;

/**
 * Derives secondary stats from primary attributes via a passive infinite GE.
 * - Computes magnitudes from a DataAsset (UAeyerjiAttributeTuning)
 * - Applies a SetByCaller-powered GE (UGE_SecondaryStatsFromPrimaries)
 * - Re-applies whenever primary attributes change
 */
UCLASS(ClassGroup=(Aeyerji), meta=(BlueprintSpawnableComponent))
class AEYERJI_API UAeyerjiStatEngineComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UAeyerjiStatEngineComponent();

protected:
    virtual void BeginPlay() override;

public:
    /** Removes any active regen gameplay effect, preventing further HP/Mana regen. */
    UFUNCTION(BlueprintCallable, Category="Aeyerji|Stats")
    void StopRegeneration();

private:
    UAbilitySystemComponent*        GetASC() const;
    const UAeyerjiAttributeSet*     GetAttr() const;
    void                            SubscribeToPrimaries();
    void                            ReapplyDerivedEffect();
    void                            OnPrimaryChanged(const FOnAttributeChangeData& Data);
    // Applies the regen effect once the ASC and attributes are ready.
    void                            TryApplyRegen();
    // Queues a short retry if the regen effect cannot be applied yet.
    void                            QueueRegenRetry();

private:
    UPROPERTY(EditDefaultsOnly, Category="Aeyerji|Stats")
    TSubclassOf<UGameplayEffect> DerivedEffectClass; // Default = UGE_SecondaryStatsFromPrimaries

    /** Optional: infinite periodic regen GE that reads HPRegen/ManaRegen via attribute-based magnitudes. */
    UPROPERTY(EditDefaultsOnly, Category="Aeyerji|Stats")
    TSubclassOf<UGameplayEffect> RegenEffectClass;   // Configure in BP (infinite, Period>0, AttributeBased magnitudes)

    mutable TWeakObjectPtr<UAbilitySystemComponent>    CachedASC;
    mutable TWeakObjectPtr<const UAeyerjiAttributeSet> CachedAttr;
    FActiveGameplayEffectHandle                        ActiveDerivedHandle;
    FActiveGameplayEffectHandle                        ActiveRegenHandle;
    bool                                               bRegenRetryQueued = false;
};
