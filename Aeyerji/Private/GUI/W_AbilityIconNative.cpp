// W_AbilityIconNative.cpp

#include "GUI/W_AbilityIconNative.h"

#include "GUI/W_AbilitySelectionNative.h"

void UW_AbilityIconNative::InitializeAbilityIcon(const FAeyerjiAbilitySlot& InSlotData, const FAeyerjiAbilityPickerEntryData& InEntryData, UW_AbilitySelectionNative* InParentSelectionWidget)
{
	MyAeyerjiAbilitySlotData = InSlotData;
	PickerEntryData = InEntryData;
	ParentSelectionWidget = InParentSelectionWidget;
}
