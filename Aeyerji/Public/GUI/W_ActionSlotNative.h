#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Abilities/AeyerjiAbilitySlot.h"
#include "Components/Image.h"
#include "W_ActionSlotNative.generated.h"

class UProgressBar;

/* ───────── Delegate sent to ActionBar when a slot is right-clicked ───────── */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnActionSlotRightClicked,
    int32, SlotIndex
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnActionSlotLeftClicked,
    class UW_ActionSlotNative*, SlotWidget
);

/* ───────────────────────────── Widget class ─────────────────────────────── */
UCLASS()
class AEYERJI_API UW_ActionSlotNative : public UUserWidget
{
    GENERATED_BODY()

public:

    /* ---------- Exposed in UMG designer ---------- */
    UPROPERTY(meta = (BindWidget))
    UImage* Icon = nullptr;

    UPROPERTY(meta = (BindWidgetOptional))
    UProgressBar* CooldownProgress = nullptr;

    /* ---------- State cached by ActionBar ---------- */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, FieldNotify)
    int32               StoredSlotIndex = INDEX_NONE;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, FieldNotify)
    FAeyerjiAbilitySlot StoredSlotData;

    UPROPERTY(BlueprintReadOnly, EditAnywhere, FieldNotify, Category = "Aeyerji|Cooldown")
    float CooldownPercent = 0.f;

    UPROPERTY(BlueprintReadOnly, EditAnywhere, FieldNotify, Category = "Aeyerji|Cooldown")
    float CooldownTimeRemaining = 0.f;

    UPROPERTY(BlueprintReadOnly, EditAnywhere, FieldNotify, Category = "Aeyerji|Cooldown")
    float CooldownTotalTime = 0.f;

    UPROPERTY(BlueprintReadOnly, EditAnywhere, FieldNotify, Category = "Aeyerji|Cooldown")
    int32 CooldownDisplaySeconds = 0;

    UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Aeyerji|Cooldown")
    bool bIsCoolingDown = false;

    /* ---------- Blueprint can bind here ---------- */
    UPROPERTY(BlueprintAssignable, Category = "Aeyerji|Events")
    FOnActionSlotRightClicked OnSlotRightClicked;

    /* ---------- Blueprint can bind here ---------- */
    UPROPERTY(BlueprintAssignable, Category = "Aeyerji|Events")
    FOnActionSlotLeftClicked OnSlotLeftClicked;

    /* ---------- Constructor ---------- */
    UW_ActionSlotNative(const FObjectInitializer& ObjectInitializer);

    /* ---------- API ---------- */
    UFUNCTION(BlueprintCallable)
    void SetIcon(UTexture2D* InTex) const
    {
        if (!Icon)
        {
            UE_LOG(LogTemp, Warning,
                   TEXT("SetIcon: Image bind failed (Icon==nullptr). "
                        "Name your UMG Image 'Icon' and tick 'Is Variable'."));
            return;
        }

        UE_LOG(LogTemp, Log, TEXT("SetIcon: %s"), *InTex->GetName());
        Icon->SetBrushFromTexture(InTex, /*bMatchSize=*/false);
    }

    UFUNCTION(BlueprintCallable)
    void SetPlaceholderIcon() const
    {
        if (Icon && PlaceholderIcon)
        {
            Icon->SetBrushFromTexture(PlaceholderIcon, /*bMatchSize=*/false);
        }
    }

    UFUNCTION(BlueprintCallable, Category = "Aeyerji|Cooldown")
    void UpdateCooldownDisplay(float TimeRemaining, float TotalDuration);

    UFUNCTION(BlueprintCallable, Category = "Aeyerji|Cooldown")
    void ClearCooldownDisplay();

protected:

    virtual void NativeConstruct() override;

    virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

    virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;

    virtual FReply NativeOnMouseButtonDown(
        const FGeometry& InGeometry,
        const FPointerEvent& InMouseEvent) override;

private:

    /** Loaded once; used as fallback when no specific ability icon yet. */
    UPROPERTY()
    UTexture2D* PlaceholderIcon = nullptr;
};
