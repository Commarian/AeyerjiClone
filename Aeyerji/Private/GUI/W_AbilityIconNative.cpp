// W_AbilityIconNative.cpp

#include "GUI/W_AbilityIconNative.h"

#include "Components/Image.h"
#include "GUI/W_AbilitySelectionNative.h"

void UW_AbilityIconNative::InitializeAbilityIcon(const FAeyerjiAbilitySlot& InSlotData, const FAeyerjiAbilityPickerEntryData& InEntryData, UW_AbilitySelectionNative* InParentSelectionWidget)
{
	MyAeyerjiAbilitySlotData = InSlotData;
	if (!MyAeyerjiAbilitySlotData.Icon && !MyAeyerjiAbilitySlotData.SavedIcon.IsNull())
	{
		MyAeyerjiAbilitySlotData.Icon = MyAeyerjiAbilitySlotData.SavedIcon.LoadSynchronous();
	}

	PickerEntryData = InEntryData;
	PickerEntryData.Slot = MyAeyerjiAbilitySlotData;
	ParentSelectionWidget = InParentSelectionWidget;

	if (ImageLinkedToDataIcon)
	{
		// InitializeAbilityIcon runs after CreateWidget, so this is the earliest point
		// at which the picker entry has both its data and its designer-bound image.
		ImageLinkedToDataIcon->SetBrushFromTexture(MyAeyerjiAbilitySlotData.Icon, false);
	}
}

bool UW_AbilityIconNative::SelectAbility()
{
	if (UW_AbilitySelectionNative* SelectionWidget = ParentSelectionWidget.Get())
	{
		return SelectionWidget->SelectAbility(MyAeyerjiAbilitySlotData);
	}

	return false;
}
