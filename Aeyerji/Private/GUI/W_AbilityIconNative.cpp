// W_AbilityIconNative.cpp

#include "GUI/W_AbilityIconNative.h"

#include "GUI/W_AbilitySelectionNative.h"

void UW_AbilityIconNative::InitializeAbilityIcon(const FAeyerjiAbilitySlot& InSlotData, UW_AbilitySelectionNative* InParentSelectionWidget)
{
	MyAeyerjiAbilitySlotData = InSlotData;
	ParentSelectionWidget = InParentSelectionWidget;
}
