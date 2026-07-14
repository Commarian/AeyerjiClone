// GUI/W_AbilityIconNative.h
#pragma once

#include "CoreMinimal.h"
#include "Abilities/AeyerjiAbilitySlot.h"
#include "Blueprint/UserWidget.h"
#include "GUI/W_AbilitySelectionNative.h"

#include "W_AbilityIconNative.generated.h"

/** Native parent for picker ability icons so C++ can pass table-backed slot data into BP visuals. */
UCLASS()
class AEYERJI_API UW_AbilityIconNative : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Store the ability data and owning picker before Blueprint updates the icon visuals. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Ability")
	void InitializeAbilityIcon(const FAeyerjiAbilitySlot& InSlotData, const FAeyerjiAbilityPickerEntryData& InEntryData, UW_AbilitySelectionNative* InParentSelectionWidget);

	/** Table-backed ability slot represented by this icon. */
	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|Ability")
	FAeyerjiAbilitySlot MyAeyerjiAbilitySlotData;

	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|Ability")
	FAeyerjiAbilityPickerEntryData PickerEntryData;

	/** Picker that owns this icon, used by Blueprint for click and tooltip callbacks. */
	UPROPERTY(BlueprintReadOnly, Category="Aeyerji|Ability")
	TWeakObjectPtr<UW_AbilitySelectionNative> ParentSelectionWidget;
};
