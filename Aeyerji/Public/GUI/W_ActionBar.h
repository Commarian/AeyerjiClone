// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GUI/AbilityTooltipData.h"
#include "W_AbilitySelectionNative.h"
#include "W_ActionSlotNative.h"
#include "Blueprint/UserWidget.h"
#include "../AeyerjiPlayerState.h"
#include "Components/HorizontalBox.h"
#include "W_ActionBar.generated.h"

class UHorizontalBox;
class UW_ActionSlotNative;
class UAbilitySystemComponent;
class APawn;
class UGameplayAbility;
class UWidget;
struct FAeyerjiAbilitySlot;

/** Widget class representing an action bar for abilities. */
UCLASS()
class AEYERJI_API UW_ActionBar : public UUserWidget
{
	GENERATED_BODY()	/** One slot per child widget in the designer. */

public:
	UW_ActionBar(const FObjectInitializer& ObjectInitializer);

	void InitWithPlayerState(AAeyerjiPlayerState* PS);

	UFUNCTION(BlueprintCallable, Category="Action Bar")
	bool ActivateSlotByIndex(int32 SlotIndex);

	UFUNCTION(BlueprintPure, Category="Action Bar")
	UW_ActionSlotNative* GetSlotWidget(int32 SlotIndex) const;

	/** Request/clear the tooltip from any slot widget. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|UI|Tooltip")
	void ShowAbilityTooltip(const FAeyerjiAbilitySlot& SlotData, FVector2D ScreenPosition, UWidget* SourceWidget);

	UFUNCTION(BlueprintCallable, Category="Aeyerji|UI|Tooltip")
	void HideAbilityTooltip(UWidget* SourceWidget);

	UFUNCTION(BlueprintPure, Category="Aeyerji|UI|Tooltip")
	const FAeyerjiAbilityTooltipData& GetLastAbilityTooltipData() const { return LastAbilityTooltipData; }

protected:
	UPROPERTY(meta = (BindWidget))
	UHorizontalBox* SlotsBox = nullptr;

	/** Auto-fill a dedicated potion slot when empty. */
	UPROPERTY(EditDefaultsOnly, Category="Action Bar|Potion")
	bool bAutoAssignDefaultPotionSlot = true;

	/** Widget name used to locate the potion slot in the UMG hierarchy. */
	UPROPERTY(EditDefaultsOnly, Category="Action Bar|Potion")
	FName PotionSlotWidgetName = TEXT("BP_PotionSlot_0");

	/** Base potion ability data to inject when the potion slot is empty. */
	UPROPERTY(EditDefaultsOnly, Category="Action Bar|Potion")
	FAeyerjiAbilitySlot DefaultPotionSlot;

	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/* --------- refresh called from delegate --------- */
	UFUNCTION()
	void Refresh(const TArray<FAeyerjiAbilitySlot>& NewBar);

	/* --------- slot right-click --------- */
	UFUNCTION()
	void HandleSlotRightClicked(int32 Index);

	UFUNCTION()
	void HandleSlotLeftClicked(UW_ActionSlotNative* MySlot);

	UFUNCTION()
	void HandleSwapBlocked(FText Reason, TSubclassOf<UGameplayAbility> AbilityClass);

	bool ExecuteAbilitySlot(const FAeyerjiAbilitySlot& SlotData);

	UPROPERTY(EditDefaultsOnly, Category="UI")
	TSubclassOf<UW_AbilitySelectionNative> PickerClass;

	UFUNCTION()
	void HandleAbilityPicked(int32 SlotIndex, FAeyerjiAbilitySlot Pick);

	/** Designers implement these to spawn/dismiss their ability tooltip widget. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|UI|Tooltip")
	void BP_ShowAbilityTooltip(const FAeyerjiAbilityTooltipData& TooltipData, FVector2D ScreenPosition, UWidget* SourceWidget);

	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|UI|Tooltip")
	void BP_HideAbilityTooltip(const FAeyerjiAbilityTooltipData& TooltipData, UWidget* SourceWidget);

	UPROPERTY()
	UW_AbilitySelectionNative* PickerInstance = nullptr;
	
	UPROPERTY()
	AAeyerjiPlayerState* CachedPS = nullptr;

private:
	void SetActiveAbilityTooltipSource(UWidget* SourceWidget);

	void UpdateCooldowns();
	bool TryUpdateSlotCooldown(UAbilitySystemComponent& AbilitySystem, UW_ActionSlotNative& SlotWidget) const;
	UAbilitySystemComponent* ResolveAbilitySystem();
	void ResetCachedAbilitySystem();
	/** Applies the configured default potion slot if the target slot is empty. */
	void EnsureDefaultPotionSlot(const TArray<FAeyerjiAbilitySlot>& NewBar);
	/** Validates that the default potion slot has required data. */
	bool IsDefaultPotionSlotConfigured() const;
	/** Returns true if a slot is effectively empty. */
	bool IsAbilitySlotEmpty(const FAeyerjiAbilitySlot& SlotData) const;
	/** Finds and caches the potion slot widget from the widget tree. */
	UW_ActionSlotNative* ResolvePotionSlotWidget();
	/** Resolves the action bar index for the potion slot widget. */
	int32 ResolvePotionSlotIndex();

	UPROPERTY(EditDefaultsOnly, Category="Action Bar|Cooldown")
	float CooldownTickInterval = 0.05f;

	float CooldownTickAccumulator = 0.f;

	TWeakObjectPtr<UAbilitySystemComponent> CachedAbilitySystem;
	TWeakObjectPtr<APawn> CachedPawn;
	TWeakObjectPtr<UW_ActionSlotNative> CachedPotionSlot;

	bool bApplyingDefaultPotionSlot = false;

	UPROPERTY()
	FAeyerjiAbilityTooltipData LastAbilityTooltipData;

	TWeakObjectPtr<UWidget> ActiveAbilityTooltipSource;
};
