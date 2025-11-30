#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/CurveTable.h"          // FCurveTableRowHandle (5.6)
#include "GameplayAbilitySpec.h"
#include "AeyerjiLevelingComponent.generated.h"

class UAbilitySystemComponent;
class UAeyerjiAttributeSet;
class UGameplayAbility;
class UGameplayEffect;

USTRUCT(BlueprintType)
struct FLevelScaledAbility
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite) TSubclassOf<UGameplayAbility> Ability = nullptr;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) int32  InputID = -1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite) bool   bScaleWithLevel = true;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FAeyerjiOnLevelUp, int32, OldLevel, int32, NewLevel);

/** Server-side XP/Level manager. AttributeSet stays the source of truth. */
UCLASS(ClassGroup=(Aeyerji), meta=(BlueprintSpawnableComponent))
class AEYERJI_API UAeyerjiLevelingComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UAeyerjiLevelingComponent();

    /** Add XP (server only). */
    UFUNCTION(BlueprintCallable, Category="Aeyerji|Leveling")
    void AddXP(float DeltaXP);

    /** Set Level directly (server). Recomputes XPMax and refreshes abilities/effects. */
    UFUNCTION(BlueprintCallable, Category="Aeyerji|Leveling")
    void SetLevel(int32 NewLevel);
    /** Ensure this infinite GE is reapplied whenever level changes. No-op if already present. */
    UFUNCTION(BlueprintCallable, Category="Aeyerji|Leveling")
    void AddReapplyInfiniteEffect(TSubclassOf<UGameplayEffect> GEClass);

    /** Re-run internal refresh for current level (re-grant abilities and re-apply infinite GEs). */
    UFUNCTION(BlueprintCallable, Category="Aeyerji|Leveling")
    void ForceRefreshForCurrentLevel();

    UPROPERTY(BlueprintAssignable, Category="Aeyerji|Leveling")
    FAeyerjiOnLevelUp OnLevelUp;

protected:
    virtual void BeginPlay() override;

private:
    /* ---------- Setup ---------- */
    UAbilitySystemComponent*        GetASC() const;
    const UAeyerjiAttributeSet*     GetAttr() const;   // 5.6: GetSet<T>() is const -> returns const T*

    /** Initialize XPMax from curve at BeginPlay based on current Level. */
    UPROPERTY(EditAnywhere, Category="Aeyerji|Leveling")
    bool bInitializeXPMaxFromCurve = true;

    /** Curve row: X = Level, Y = XP required to reach NEXT level. */
    UPROPERTY(EditAnywhere, Category="Aeyerji|Leveling")
    FCurveTableRowHandle XPToReachLevelRow;

    /** Infinite effects to re-apply on level change (so their ScalableFloat re-evaluates level). */
    UPROPERTY(EditAnywhere, Category="Aeyerji|Leveling")
    TArray<TSubclassOf<UGameplayEffect>> ReapplyInfiniteEffectsOnLevelUp;

    /** Abilities owned by this pawn; we re-grant them on level changes. */
    UPROPERTY(EditAnywhere, Category="Aeyerji|Leveling")
    TArray<FLevelScaledAbility> AbilitiesToOwn;

    UPROPERTY(EditAnywhere, Category="Aeyerji|Leveling")
    bool bGrantAbilitiesOnBeginPlay = true;

    /* ---------- Ops ---------- */
    void  TryProcessLevelUps();
    float ComputeXPMaxForLevel(int32 Level) const;   // uses RowHandle.Eval()
    void  RefreshOwnedAbilities() const;
    void  ReapplyInfiniteEffects() const;

    void  ServerSetXP(float NewXP) const;
    void  ServerSetXPMax(float NewXPMax) const;
    void  ServerSetLevel(int32 NewLevel) const;

private:
    mutable TWeakObjectPtr<UAbilitySystemComponent>        CachedASC;
    mutable TWeakObjectPtr<const UAeyerjiAttributeSet>     CachedAttr; // const matches GetSet<T>() const
};
