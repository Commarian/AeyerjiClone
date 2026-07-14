// GUI/W_AbilitySelectionNative.h
#pragma once

#include "Abilities/AeyerjiAbilityProgression.h"
#include "Abilities/AeyerjiAbilitySlot.h"
#include "Blueprint/UserWidget.h"
#include "GUI/AbilityTooltipData.h"

#include "W_AbilitySelectionNative.generated.h"

class UAbilitySystemComponent;
class UUniformGridPanel;
class UWidget;
class AAeyerjiPlayerState;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnAbilityPicked,    /* delegate name           */
	int32,               SlotIndex,                /* which slot to fill   */
	FAeyerjiAbilitySlot, PickedData                /* the chosen ability   */
);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnAbilityUpgradeRequested, FGameplayTag, AbilityTag);

UENUM(BlueprintType)
enum class EAeyerjiAbilityPickerMode : uint8
{
	Assign,
	Upgrade
};

USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiAbilityPickerEntryData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|UI|Abilities")
	FAeyerjiAbilitySlot Slot;

	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|UI|Abilities")
	FGameplayTag AbilityTag;

	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|UI|Abilities")
	int32 CurrentRank = 0;

	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|UI|Abilities")
	int32 MaxRank = 0;

	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|UI|Abilities")
	bool bBaseUnlocked = false;

	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|UI|Abilities")
	bool bCanUpgrade = false;

	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|UI|Abilities")
	int32 PointCost = 0;

	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|UI|Abilities")
	int32 RequiredPlayerLevel = 1;

	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|UI|Abilities")
	int32 RemainingAbilityPoints = 0;
};

UCLASS()
class AEYERJI_API UW_AbilitySelectionNative : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Rebuild runtime layout guards after the widget tree is available. */
	virtual void NativeConstruct() override;

	/** Re-apply square sizing when the picker's layout changes at runtime. */
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual void NativeDestruct() override;

	/** Set by ActionBar right before AddToViewport(). */
	UPROPERTY(BlueprintReadOnly)
	int32 EditingSlotIndex = INDEX_NONE;

	/** True when the picker is editing the fixed final potion slot. */
	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|UI|Abilities")
	bool bEditingPotionSlot = false;

	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|UI|Abilities")
	EAeyerjiAbilityPickerMode PickerMode = EAeyerjiAbilityPickerMode::Assign;

	/** Sets whether this picker should show potion abilities or normal abilities. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|UI|Abilities")
	void SetPotionSlotContext(bool bInEditingPotionSlot);

	/** Optional ability system used to evaluate costs/cooldowns for tooltip display. */
	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|UI|Tooltip")
	TWeakObjectPtr<UAbilitySystemComponent> AbilitySystemForTooltip;

	/** Supplies an ASC for cost scaling in tooltips (optional). */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|UI|Tooltip")
	void SetAbilitySystemForTooltip(UAbilitySystemComponent* InAbilitySystem);

	/** Blueprint fires this when user clicks an icon. */
	UPROPERTY(BlueprintAssignable, Category="Events", BlueprintCallable)
	FOnAbilityPicked OnAbilityPicked;

	UPROPERTY(BlueprintAssignable, Category="Events", BlueprintCallable)
	FOnAbilityUpgradeRequested OnAbilityUpgradeRequested;

	/** Request/clear the tooltip from any child widget in the picker. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|UI|Tooltip")
	void ShowAbilityTooltip(const FAeyerjiAbilitySlot& SlotData, FVector2D ScreenPosition, UWidget* SourceWidget);

	UFUNCTION(BlueprintCallable, Category="Aeyerji|UI|Tooltip")
	void HideAbilityTooltip(UWidget* SourceWidget);

	UFUNCTION(BlueprintPure, Category="Aeyerji|UI|Tooltip")
	const FAeyerjiAbilityTooltipData& GetLastAbilityTooltipData() const { return LastTooltipData; }

	UFUNCTION(BlueprintCallable, Category="Aeyerji|UI|Abilities")
	void SetPickerMode(EAeyerjiAbilityPickerMode InPickerMode);

	UFUNCTION(BlueprintPure, Category="Aeyerji|UI|Abilities")
	EAeyerjiAbilityPickerMode GetPickerMode() const { return PickerMode; }

	UFUNCTION(BlueprintCallable, Category="Aeyerji|UI|Abilities")
	void RequestUpgradeAbility(const FAeyerjiAbilitySlot& SlotData);

	UFUNCTION(BlueprintPure, Category="Aeyerji|UI|Abilities")
	bool BuildPickerEntryData(const FAeyerjiAbilitySlot& SlotData, FAeyerjiAbilityPickerEntryData& OutEntryData) const;

	/** Rebuild the picker grid from the global ability tuning table. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|UI|Abilities")
	void RebuildAbilityGrid();

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

protected:
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional), Category="Aeyerji|UI|Abilities")
	TObjectPtr<UUniformGridPanel> UniformGridPanel_Abilities = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|UI|Abilities")
	TSubclassOf<UUserWidget> AbilityIconWidgetClass;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|UI|Abilities", meta=(ClampMin="1"))
	int32 AbilityGridColumns = 4;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|UI|Abilities")
	bool bPopulateAbilitiesOnConstruct = true;

	/** Caches UniformGrid panels that need their entries clamped to square tiles. */
	void RefreshManagedUniformGrids();

	/** Wraps grid children in size boxes so their runtime size can be constrained safely. */
	void NormalizeUniformGridChildren(UUniformGridPanel& GridPanel);

	/** Applies square width/height overrides based on the grid's current geometry. */
	void UpdateUniformGridSizing(UUniformGridPanel& GridPanel);

	/** Reads the occupied row/column span from the current grid children. */
	bool GetUniformGridDimensions(const UUniformGridPanel& GridPanel, int32& OutRows, int32& OutColumns) const;

private:
	void SetActiveTooltipSource(UWidget* SourceWidget);
	AAeyerjiPlayerState* ResolveOwningPlayerState() const;

	UFUNCTION()
	void HandleAbilityProgressionChanged(const TArray<FAeyerjiAbilityProgressEntry>& ProgressEntries, int32 RemainingPoints, int32 TotalPointSpends);

	TArray<TWeakObjectPtr<UUniformGridPanel>> ManagedUniformGrids;

	UPROPERTY()
	FAeyerjiAbilityTooltipData LastTooltipData;

	TWeakObjectPtr<UWidget> ActiveTooltipSource;
	TWeakObjectPtr<AAeyerjiPlayerState> BoundPlayerState;
};
