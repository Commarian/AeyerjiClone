#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Abilities/AeyerjiAbilitySlot.h"
#include "Components/Image.h"
#include "W_ActionSlotNative.generated.h"

class UProgressBar;

/** Delegate sent to the action bar when a slot is right-clicked. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnActionSlotRightClicked,
    int32, SlotIndex
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
    FOnActionSlotLeftClicked,
    class UW_ActionSlotNative*, SlotWidget
);

/** Native action-bar slot presentation and mouse interaction bridge. */
UCLASS()
class AEYERJI_API UW_ActionSlotNative : public UUserWidget
{
    GENERATED_BODY()

public:

    /** Required UMG image named Icon that displays the assigned action icon. */
    UPROPERTY(meta = (BindWidget))
    UImage* Icon = nullptr;

    /** Optional UMG progress bar used to display the active cooldown fraction. */
    UPROPERTY(meta = (BindWidgetOptional))
    UProgressBar* CooldownProgress = nullptr;

    /** Zero-based action-bar slot assigned by the owning action-bar widget. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, FieldNotify)
    int32               StoredSlotIndex = INDEX_NONE;

    /** Ability-slot definition currently presented by this widget. */
    UPROPERTY(BlueprintReadWrite, EditAnywhere, FieldNotify)
    FAeyerjiAbilitySlot StoredSlotData;

    /** True when this widget represents the fixed final potion slot. */
    UPROPERTY(BlueprintReadOnly, EditAnywhere, Category = "Aeyerji|Slot")
    bool bIsPotionSlot = false;

    /** Normalized 0..1 cooldown fraction used by the progress-bar presentation. */
    UPROPERTY(BlueprintReadOnly, EditAnywhere, FieldNotify, Category = "Aeyerji|Cooldown")
    float CooldownPercent = 0.f;

    /** Remaining finite cooldown duration in seconds. */
    UPROPERTY(BlueprintReadOnly, EditAnywhere, FieldNotify, Category = "Aeyerji|Cooldown")
    float CooldownTimeRemaining = 0.f;

    /** Total finite cooldown duration in seconds. */
    UPROPERTY(BlueprintReadOnly, EditAnywhere, FieldNotify, Category = "Aeyerji|Cooldown")
    float CooldownTotalTime = 0.f;

    /** Whole seconds shown by Blueprint cooldown text. */
    UPROPERTY(BlueprintReadOnly, EditAnywhere, FieldNotify, Category = "Aeyerji|Cooldown")
    int32 CooldownDisplaySeconds = 0;

    /** True while a valid positive cooldown is being presented. */
    UPROPERTY(BlueprintReadOnly, EditAnywhere, FieldNotify, Category = "Aeyerji|Cooldown")
    bool bIsCoolingDown = false;

    /** Broadcasts a local right-click so the action bar can clear or edit the slot. */
    UPROPERTY(BlueprintAssignable, Category = "Aeyerji|Events")
    FOnActionSlotRightClicked OnSlotRightClicked;

    /** Broadcasts a local left-click so the action bar can activate the slot. */
    UPROPERTY(BlueprintAssignable, Category = "Aeyerji|Events")
    FOnActionSlotLeftClicked OnSlotLeftClicked;

    UW_ActionSlotNative(const FObjectInitializer& ObjectInitializer);

    /** Applies an icon or restores the placeholder when the texture is invalid. */
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

		if (!IsValid(InTex))
		{
			SetPlaceholderIcon();
			return;
		}

		Icon->SetBrushFromTexture(InTex, /*bMatchSize=*/false);
    }

    /** Restores the cached placeholder texture when the bound image is available. */
    UFUNCTION(BlueprintCallable)
    void SetPlaceholderIcon() const
    {
        if (Icon && PlaceholderIcon)
        {
            Icon->SetBrushFromTexture(PlaceholderIcon, /*bMatchSize=*/false);
        }
    }

    /** Sanitizes and presents a cooldown snapshot supplied by the owning action bar. */
    UFUNCTION(BlueprintCallable, Category = "Aeyerji|Cooldown")
    void UpdateCooldownDisplay(float TimeRemaining, float TotalDuration);

    /** Clears all native cooldown presentation state. */
    UFUNCTION(BlueprintCallable, Category = "Aeyerji|Cooldown")
    void ClearCooldownDisplay();

protected:

    virtual void NativeConstruct() override;

    virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

    virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;

    virtual FReply NativeOnMouseButtonDown(
        const FGeometry& InGeometry,
        const FPointerEvent& InMouseEvent) override;

    virtual FReply NativeOnMouseButtonDoubleClick(
        const FGeometry& InGeometry,
        const FPointerEvent& InMouseEvent) override;

    virtual FReply NativeOnMouseButtonUp(
        const FGeometry& InGeometry,
        const FPointerEvent& InMouseEvent) override;

private:

    /** Loaded once; used as fallback when no specific ability icon yet. */
    UPROPERTY()
    UTexture2D* PlaceholderIcon = nullptr;
};
