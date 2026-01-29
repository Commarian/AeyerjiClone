// W_AbilitySelectionNative.cpp

#include "GUI/W_AbilitySelectionNative.h"

#include "AbilitySystemComponent.h"

void UW_AbilitySelectionNative::SetAbilitySystemForTooltip(UAbilitySystemComponent* InAbilitySystem)
{
	AbilitySystemForTooltip = InAbilitySystem;
}

void UW_AbilitySelectionNative::ShowAbilityTooltip(const FAeyerjiAbilitySlot& SlotData, FVector2D ScreenPosition, UWidget* SourceWidget)
{
	if (SlotData.Tag.IsEmpty())
	{
		return;
	}

	LastTooltipData = FAeyerjiAbilityTooltipData::FromSlot(
		AbilitySystemForTooltip.Get(),
		SlotData,
		EAbilityTooltipSource::AbilityPicker);

	SetActiveTooltipSource(SourceWidget);
	BP_ShowAbilityTooltip(LastTooltipData, ScreenPosition, SourceWidget);
}

void UW_AbilitySelectionNative::HideAbilityTooltip(UWidget* SourceWidget)
{
	if (ActiveTooltipSource.IsValid() && SourceWidget && ActiveTooltipSource.Get() != SourceWidget)
	{
		return;
	}

	BP_HideAbilityTooltip(LastTooltipData, SourceWidget);
	ActiveTooltipSource.Reset();
	LastTooltipData = FAeyerjiAbilityTooltipData();
}

void UW_AbilitySelectionNative::SetActiveTooltipSource(UWidget* SourceWidget)
{
	if (!SourceWidget)
	{
		ActiveTooltipSource.Reset();
		return;
	}

	ActiveTooltipSource = SourceWidget;
}
