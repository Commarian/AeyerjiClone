// GUI/W_AbilitySelectionNative.h
#pragma once

#include "Abilities/AeyerjiAbilitySlot.h"
#include "Blueprint/UserWidget.h"
#include "GUI/AbilityTooltipData.h"

#include "W_AbilitySelectionNative.generated.h"

class UAbilitySystemComponent;
class UWidget;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnAbilityPicked,    /* delegate name           */
	int32,               SlotIndex,                /* which slot to fill   */
	FAeyerjiAbilitySlot, PickedData                /* the chosen ability   */
);

UCLASS()
class AEYERJI_API UW_AbilitySelectionNative : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Set by ActionBar right before AddToViewport(). */
	UPROPERTY(BlueprintReadOnly)
	int32 EditingSlotIndex = INDEX_NONE;

	/** Optional ability system used to evaluate costs/cooldowns for tooltip display. */
	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|UI|Tooltip")
	TWeakObjectPtr<UAbilitySystemComponent> AbilitySystemForTooltip;

	/** Supplies an ASC for cost scaling in tooltips (optional). */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|UI|Tooltip")
	void SetAbilitySystemForTooltip(UAbilitySystemComponent* InAbilitySystem);

	/** Blueprint fires this when user clicks an icon. */
	UPROPERTY(BlueprintAssignable, Category="Events", BlueprintCallable)
	FOnAbilityPicked OnAbilityPicked;

	/** Request/clear the tooltip from any child widget in the picker. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|UI|Tooltip")
	void ShowAbilityTooltip(const FAeyerjiAbilitySlot& SlotData, FVector2D ScreenPosition, UWidget* SourceWidget);

	UFUNCTION(BlueprintCallable, Category="Aeyerji|UI|Tooltip")
	void HideAbilityTooltip(UWidget* SourceWidget);

	UFUNCTION(BlueprintPure, Category="Aeyerji|UI|Tooltip")
	const FAeyerjiAbilityTooltipData& GetLastAbilityTooltipData() const { return LastTooltipData; }

	/** Helper BP call: the widget finished, close itself. */
	UFUNCTION(BlueprintCallable)
	void Close()
	{
		RemoveFromParent();
	}

	/** Designers implement these to spawn/dismiss their ability tooltip widget. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|UI|Tooltip")
	void BP_ShowAbilityTooltip(const FAeyerjiAbilityTooltipData& TooltipData, FVector2D ScreenPosition, UWidget* SourceWidget);

	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|UI|Tooltip")
	void BP_HideAbilityTooltip(const FAeyerjiAbilityTooltipData& TooltipData, UWidget* SourceWidget);

private:
	void SetActiveTooltipSource(UWidget* SourceWidget);

	UPROPERTY()
	FAeyerjiAbilityTooltipData LastTooltipData;

	TWeakObjectPtr<UWidget> ActiveTooltipSource;
};

