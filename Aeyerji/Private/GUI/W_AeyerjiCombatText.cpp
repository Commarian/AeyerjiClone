#include "GUI/W_AeyerjiCombatText.h"

#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"

void UW_AeyerjiCombatText::ApplyCombatText(
	const FText& InDisplayText,
	const FLinearColor InTextColor,
	const float InTextScale,
	const EAeyerjiCombatTextResultType InResultType,
	const float InMagnitude)
{
	DisplayText = InDisplayText;
	TextColor = InTextColor;
	TextScale = FMath::Max(0.01f, InTextScale);
	ResultType = InResultType;
	Magnitude = InMagnitude;

	EnsureNativeTextBlock();
	RefreshNativeTextBlock();
	BP_OnCombatTextAssigned();
}

void UW_AeyerjiCombatText::NotifyCombatTextTick(const float NormalizedAge)
{
	BP_OnCombatTextTick(FMath::Clamp(NormalizedAge, 0.f, 1.f));
}

void UW_AeyerjiCombatText::NativePreConstruct()
{
	Super::NativePreConstruct();

	EnsureNativeTextBlock();
	RefreshNativeTextBlock();
}

void UW_AeyerjiCombatText::EnsureNativeTextBlock()
{
	if (!WidgetTree)
	{
		return;
	}

	if (NativeTextBlock)
	{
		return;
	}

	if (UWidget* RootWidget = WidgetTree->RootWidget)
	{
		NativeTextBlock = Cast<UTextBlock>(RootWidget);
		return;
	}

	NativeTextBlock = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("NativeCombatText"));
	if (!NativeTextBlock)
	{
		return;
	}

	NativeTextBlock->SetJustification(ETextJustify::Center);
	NativeTextBlock->SetShadowOffset(FVector2D(1.f, 1.f));
	NativeTextBlock->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f));

	FSlateFontInfo FontInfo = NativeTextBlock->GetFont();
	FontInfo.Size = 22;
	NativeTextBlock->SetFont(FontInfo);

	WidgetTree->RootWidget = NativeTextBlock;
}

void UW_AeyerjiCombatText::RefreshNativeTextBlock()
{
	if (!NativeTextBlock)
	{
		return;
	}

	NativeTextBlock->SetText(DisplayText);
	NativeTextBlock->SetColorAndOpacity(FSlateColor(TextColor));
	SetRenderScale(FVector2D(TextScale, TextScale));
}
