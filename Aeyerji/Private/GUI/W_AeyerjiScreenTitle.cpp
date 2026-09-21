// Copyright (c) 2025 Aeyerji.

#include "GUI/W_AeyerjiScreenTitle.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/RichTextBlock.h"
#include "GUI/AeyerjiStringLibrary.h"
#include "GUI/AeyerjiUIStyleLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

UW_AeyerjiScreenTitle::UW_AeyerjiScreenTitle(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}

void UW_AeyerjiScreenTitle::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	EnsureWidgetTree();
	ApplyTitle();
	ApplyEffect();
}

void UW_AeyerjiScreenTitle::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	// Designer edits (key, intensity, speed, scale) preview live without PIE.
	EnsureWidgetTree();
	ApplyTitle();
	ApplyEffect();
}

void UW_AeyerjiScreenTitle::SetTitleKey(FName InKey)
{
	TitleStringTableKey = InKey;
	bHasDirectText = false;
	DirectText = FText::GetEmpty();
	EnsureWidgetTree();
	ApplyTitle();
}

void UW_AeyerjiScreenTitle::SetTitleText(const FText& InText)
{
	DirectText = InText;
	bHasDirectText = true;
	EnsureWidgetTree();
	ApplyTitle();
}

void UW_AeyerjiScreenTitle::SetEffectParams(float InIntensity, float InMotionSpeed)
{
	Intensity = InIntensity;
	MotionSpeed = InMotionSpeed;
	EnsureWidgetTree();
	ApplyEffect();
}

void UW_AeyerjiScreenTitle::EnsureWidgetTree()
{
	if (RootOverlay && AtmosphereImage && TitleText)
	{
		return;
	}

	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"));
	}

	RootOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("TitlePresentation"));
	AtmosphereImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("TitleAtmosphere"));
	TitleText = WidgetTree->ConstructWidget<URichTextBlock>(URichTextBlock::StaticClass(), TEXT("TitleLabel"));

	check(RootOverlay && AtmosphereImage && TitleText);

	RootOverlay->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	// Effect renders first (behind text) and stretches across the overlay slot.
	if (UOverlaySlot* EffectSlot = RootOverlay->AddChildToOverlay(AtmosphereImage))
	{
		EffectSlot->SetHorizontalAlignment(HAlign_Fill);
		EffectSlot->SetVerticalAlignment(VAlign_Fill);
	}
	if (UOverlaySlot* TextSlot = RootOverlay->AddChildToOverlay(TitleText))
	{
		TextSlot->SetHorizontalAlignment(HAlign_Center);
		TextSlot->SetVerticalAlignment(VAlign_Center);
	}

	WidgetTree->RootWidget = RootOverlay;
}

void UW_AeyerjiScreenTitle::ApplyTitle()
{
	if (!TitleText)
	{
		return;
	}
	if (UDataTable* StyleSet = UAeyerjiUIStyleLibrary::GetMenuTextStyleSet())
	{
		TitleText->SetTextStyleSet(StyleSet);
	}
	const FText Resolved = bHasDirectText
		? DirectText
		: AeyerjiStringLibrary::GetGlobalStringTableText(TitleStringTableKey);
	TitleText->SetText(Resolved);
	TitleText->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UW_AeyerjiScreenTitle::ApplyEffect()
{
	if (!AtmosphereImage)
	{
		return;
	}

	UMaterialInterface* Base = AtmosphereMaterialOverride.IsNull()
		? UAeyerjiUIStyleLibrary::GetTitleAtmosphereMaterial()
		: AtmosphereMaterialOverride.LoadSynchronous();
	if (!Base)
	{
		return;
	}

	if (AtmosphereMID && LastAtmosphereBase == Base)
	{
		// Steady state: retune the existing instance instead of reallocating.
		AtmosphereMID->SetScalarParameterValue(TEXT("Intensity"), Intensity);
		AtmosphereMID->SetScalarParameterValue(TEXT("MotionSpeed"), MotionSpeed);
	}
	else
	{
		AtmosphereMID = UAeyerjiUIStyleLibrary::ApplyTitleAtmosphere(AtmosphereImage, this, Intensity, MotionSpeed, Base);
		LastAtmosphereBase = Base;
	}

	FWidgetTransform Transform = AtmosphereImage->GetRenderTransform();
	Transform.Scale = AtmosphereScale;
	AtmosphereImage->SetRenderTransform(Transform);
}
