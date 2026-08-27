// GUI/W_AbilityIconNative.h
#pragma once

#include "CoreMinimal.h"
#include "Abilities/AeyerjiAbilitySlot.h"
#include "Blueprint/UserWidget.h"
#include "GUI/W_AbilitySelectionNative.h"

#include "W_AbilityIconNative.generated.h"

class UImage;

/** Native parent for picker ability icons so C++ can pass table-backed slot data into BP visuals. */
UCLASS()
class AEYERJI_API UW_AbilityIconNative : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Store the ability data and owning picker before Blueprint updates the icon visuals. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Ability")
	void InitializeAbilityIcon(const FAeyerjiAbilitySlot& InSlotData, const FAeyerjiAbilityPickerEntryData& InEntryData, UW_AbilitySelectionNative* InParentSelectionWidget);

	/** Selects this icon through its native picker; Blueprint click handlers need only call this function. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Ability")
	bool SelectAbility();

	/** Table-backed ability slot represented by this icon. */
	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|Ability")
	FAeyerjiAbilitySlot MyAeyerjiAbilitySlotData;

	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|Ability")
	FAeyerjiAbilityPickerEntryData PickerEntryData;

	/** Picker that owns this icon, used by Blueprint for click and tooltip callbacks. */
	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|Ability")
	TWeakObjectPtr<UW_AbilitySelectionNative> ParentSelectionWidget;

protected:
	/** Required UMG image named ImageLinkedToDataIcon. Native setup assigns the slot icon after CreateWidget binds this widget. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> ImageLinkedToDataIcon = nullptr;
};
