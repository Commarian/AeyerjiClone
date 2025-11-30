// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
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

protected:
	UPROPERTY(meta = (BindWidget))
	UHorizontalBox* SlotsBox = nullptr;

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

	UPROPERTY()
	UW_AbilitySelectionNative* PickerInstance = nullptr;
	
	UPROPERTY()
	AAeyerjiPlayerState* CachedPS = nullptr;

private:
	void UpdateCooldowns();
	bool TryUpdateSlotCooldown(UAbilitySystemComponent& AbilitySystem, UW_ActionSlotNative& SlotWidget) const;
	UAbilitySystemComponent* ResolveAbilitySystem();
	void ResetCachedAbilitySystem();

	UPROPERTY(EditDefaultsOnly, Category="Action Bar|Cooldown")
	float CooldownTickInterval = 0.05f;

	float CooldownTickAccumulator = 0.f;

	TWeakObjectPtr<UAbilitySystemComponent> CachedAbilitySystem;
	TWeakObjectPtr<APawn> CachedPawn;
};



