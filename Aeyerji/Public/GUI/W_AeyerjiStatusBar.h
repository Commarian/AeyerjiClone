// W_AeyerjiStatusBar.h

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"
#include "W_AeyerjiStatusBar.generated.h"

class UAbilitySystemComponent;
class UProgressBar;
class UImage;
class UTextBlock;

UENUM(BlueprintType)
enum class EResourceVisibilityPolicy : uint8
{
    Auto,       // Show resource if the character actually uses one (auto-detect)
    ForceShow,  // Always show resource bar
    ForceHide   // Never show resource bar
};

UCLASS()
class AEYERJI_API UW_AeyerjiStatusBar : public UUserWidget
{
    GENERATED_BODY()

public:
    // ----- Designer widgets (optional; null-safe) -----
    UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
    UProgressBar* HealthBar = nullptr;

    UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
    UProgressBar* HealthBar_Ghost = nullptr;

    UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
    UProgressBar* ManaBar = nullptr;

    UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
    UProgressBar* ManaBar_Ghost = nullptr;

    // Optional XP bar (null-safe)
    UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
    UProgressBar* XPBar = nullptr;

    UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
    UProgressBar* XPBar_Ghost = nullptr;

    // Optional text labels
    UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* LevelText = nullptr;
    UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* XPText = nullptr;

    // Optional numeric value labels (HUD/overlay may choose to place these)
    UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* HPValueText = nullptr;        // current HP (floored)
    UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* HPMaxValueText = nullptr;     // max HP (floored)
    UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* ManaValueText = nullptr;      // current Mana (floored)
    UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* ManaMaxValueText = nullptr;   // max Mana (floored)

    // Optional regen labels shown inside bars
    UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* HPRegenText = nullptr;        // HP per second
    UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
    UTextBlock* ManaRegenText = nullptr;      // Mana per second

    UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
    UImage* ImgHealFlash = nullptr;

    UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly)
    UImage* ImgDamageFlash = nullptr;

    // ----- Tuning knobs -----
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatusBar|Smoothing")
    float ChipHoldTime = 0.12f;         // Delay before the ghost starts to move

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatusBar|Smoothing")
    float ChipLerpSpeedDown = 4.0f;     // Ghost catch-up speed (damage)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatusBar|Smoothing")
    float FillLerpSpeed_DmgDown = 12.0f;// Main bar down speed

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatusBar|Smoothing")
    float FillLerpSpeed_HealUp = 8.0f;  // Main bar up speed

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatusBar|FX")
    float FlashFadePerSec = 6.0f;       // Heal/Dmg flash fade speed

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatusBar|FX")
    float LowHPThreshold = 0.25f;       // Pulse when below this

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatusBar|FX")
    FLinearColor HealthColor_Normal = FLinearColor(0.15f, 0.9f, 0.3f, 1.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatusBar|FX")
    FLinearColor HealthColor_Low    = FLinearColor(0.95f, 0.15f, 0.1f, 1.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatusBar|Resource")
    EResourceVisibilityPolicy ResourceVisibility = EResourceVisibilityPolicy::Auto;

    /** If set, we'll also look for abilities with this tag to decide showing the resource bar (e.g. "Ability.Cost.Mana"). */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatusBar|Resource")
    FGameplayTag ManaAbilityTag;

    /** Minimal MaxResource value that counts as "has resource". */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|StatusBar|Resource")
    float MinResourceToShow = 0.5f;

    // ----- GAS Binding -----
    UFUNCTION(BlueprintCallable, Category="Aeyerji|StatusBar")
    void BindToAttributes(UAbilitySystemComponent* InASC,
                          FGameplayAttribute InHealth, FGameplayAttribute InMaxHealth,
                          FGameplayAttribute InMana,   FGameplayAttribute InMaxMana);

    /** Convenience overload: includes XP binding. */
    UFUNCTION(BlueprintCallable, Category="Aeyerji|StatusBar")
    void BindToAttributesWithXP(UAbilitySystemComponent* InASC,
                                FGameplayAttribute InHealth,   FGameplayAttribute InMaxHealth,
                                FGameplayAttribute InMana,     FGameplayAttribute InMaxMana,
                                FGameplayAttribute InXP,       FGameplayAttribute InXPMax);

    /** Convenience overload: includes XP and Level binding for text. */
    UFUNCTION(BlueprintCallable, Category="Aeyerji|StatusBar")
    void BindToAttributesWithXPAndLevel(UAbilitySystemComponent* InASC,
                                        FGameplayAttribute InHealth,   FGameplayAttribute InMaxHealth,
                                        FGameplayAttribute InMana,     FGameplayAttribute InMaxMana,
                                        FGameplayAttribute InXP,       FGameplayAttribute InXPMax,
                                        FGameplayAttribute InLevel);

    /** Optional: bind regen attributes (HPRegen/ManaRegen) for in-bar display. */
    UFUNCTION(BlueprintCallable, Category="Aeyerji|StatusBar")
    void BindRegenAttributes(UAbilitySystemComponent* InASC,
                             FGameplayAttribute InHPRegen,
                             FGameplayAttribute InManaRegen);

    /** Optional: let BP decide if this pawn uses a resource (OR-ed with auto and tag detection). */
    UFUNCTION(BlueprintNativeEvent, Category="Aeyerji|StatusBar|Resource")
    bool BP_ShouldShowResource(UAbilitySystemComponent* InASC);
    virtual bool BP_ShouldShowResource_Implementation(UAbilitySystemComponent* InASC);

protected:
    virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
    virtual void NativeDestruct() override;

private:
    // Cached ASC + attrs
    UPROPERTY(Transient) TWeakObjectPtr<UAbilitySystemComponent> ASC;
    FGameplayAttribute HealthAttr;
    FGameplayAttribute MaxHealthAttr;
    FGameplayAttribute ManaAttr;
    FGameplayAttribute MaxManaAttr;
    FGameplayAttribute XPAttr;
    FGameplayAttribute XPMaxAttr;
    FGameplayAttribute LevelAttr;
    FGameplayAttribute HPRegenAttr;
    FGameplayAttribute ManaRegenAttr;

    // Delegates
    FDelegateHandle HealthChangedHandle;
    FDelegateHandle MaxHealthChangedHandle;
    FDelegateHandle ManaChangedHandle;
    FDelegateHandle MaxManaChangedHandle;
    FDelegateHandle XPChangedHandle;
    FDelegateHandle MaxXPChangedHandle;
    FDelegateHandle LevelChangedHandle;
    FDelegateHandle HPRegenChangedHandle;
    FDelegateHandle ManaRegenChangedHandle;

    // Visual state
    float HealthTarget = 1.f;
    float HealthMain   = 1.f;
    float HealthGhost  = 1.f;
    float HealthGhostHold = 0.f;

    float ManaTarget = 1.f;
    float ManaMain   = 1.f;
    float ManaGhost  = 1.f;
    float ManaGhostHold = 0.f;

    float XPTarget = 1.f;
    float XPMain   = 1.f;
    float XPGhost  = 1.f;
    float XPGhostHold = 0.f;

    float HealFlash = 0.f;
    float DmgFlash  = 0.f;

    // Helpers
    void OnHealthChanged(const FOnAttributeChangeData& Data);
    void OnMaxHealthChanged(const FOnAttributeChangeData& Data);
    void OnManaChanged(const FOnAttributeChangeData& Data);
    void OnMaxManaChanged(const FOnAttributeChangeData& Data);
    void OnXPChanged(const FOnAttributeChangeData& Data);
    void OnMaxXPChanged(const FOnAttributeChangeData& Data);
    void OnLevelChanged(const FOnAttributeChangeData& Data);
    void OnHPRegenChanged(const FOnAttributeChangeData& Data);
    void OnManaRegenChanged(const FOnAttributeChangeData& Data);

    void RecalculateTargets(); // recompute normalized targets from ASC
    static float SafeDiv(float Numerator, float Denominator);

    void TickBar(float Dt, float Target, float& Main, float& Ghost, float& Hold,
                 UProgressBar* MainPB, UProgressBar* GhostPB,
                 bool bWasDamage);

    void ApplyFX(float Dt);
    void UpdateColors();
    void UpdateResourceVisibility();
    bool AutoDetectUsesResource() const;

    void UpdateXPLabel();
    void UpdateLevelLabel();
    void UpdateHPValueLabels();
    void UpdateManaValueLabels();
    void UpdateRegenLabels();
};
