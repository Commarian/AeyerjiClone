// Copyright (c) 2025 Aeyerji.

#include "GUI/AeyerjiUIStyleLibrary.h"

#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/RichTextBlock.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Engine/DataTable.h"
#include "Engine/Font.h"
#include "GUI/AeyerjiStringLibrary.h"
#include "Logging/AeyerjiLog.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Styling/SlateBrush.h"

namespace
{
	// Approved landing values, kept in linear RGB to match the editor scripts.
	const FLinearColor MenuNormalFill(0.025f, 0.012f, 0.045f, 1.f);
	const FLinearColor MenuNormalEdge(0.120f, 0.050f, 0.220f, 1.f);
	const FLinearColor MenuHoverFill(0.085f, 0.018f, 0.150f, 1.f);
	const FLinearColor MenuHoverEdge(0.380f, 0.160f, 0.660f, 1.f);
	const FLinearColor MenuPressedFill(0.045f, 0.012f, 0.085f, 1.f);
	const FLinearColor MenuPressedEdge(0.220f, 0.070f, 0.400f, 1.f);
	const FLinearColor ProfileValueColor(0.78f, 0.70f, 0.95f, 1.f);
	const FLinearColor ProfileGoldColor(0.95f, 0.68f, 0.27f, 1.f);
	const FLinearColor PrimaryViolet(0.48f, 0.16f, 0.96f, 1.f);
	const FLinearColor BrightLavender(0.90f, 0.84f, 1.f, 1.f);
	const FLinearColor CoolWhite(0.94f, 0.93f, 1.f, 1.f);
	const FLinearColor LivingCyan(0.20f, 0.82f, 1.f, 1.f);
	const FLinearColor WarningRose(1.00f, 0.34f, 0.54f, 1.f);
	const FLinearColor TransientSurface(0.025f, 0.012f, 0.045f, 0.96f);
	const FLinearColor XPFillColor(0.42f, 0.12f, 0.90f, 1.f);
	const FLinearColor XPTrackColor(0.05f, 0.025f, 0.09f, 1.f);

	constexpr float MaxFrontendXPDisplay = 1000000000000.f;

	TWeakObjectPtr<UMaterialInterface> GMenuHoverMaterial;
	TWeakObjectPtr<UMaterialInterface> GTitleAtmosphereMaterial;
	TWeakObjectPtr<UDataTable> GMenuTextStyleSet;
	TWeakObjectPtr<UFont> GExo2Font;
	TWeakObjectPtr<UFont> GBrunoAceFont;
	TWeakObjectPtr<UFont> GInterFont;

	template <typename T>
	T* LoadSharedAsset(TWeakObjectPtr<T>& Cache, const TCHAR* Path)
	{
		if (T* Cached = Cache.Get())
		{
			return Cached;
		}
		if (UObject* Loaded = StaticLoadObject(T::StaticClass(), nullptr, Path))
		{
			T* Casted = Cast<T>(Loaded);
			Cache = Casted;
			return Casted;
		}
		UE_LOG(LogAeyerji, Warning, TEXT("[UIStyle] Shared asset missing: %s"), Path);
		return nullptr;
	}

	void ApplyFlatButtonBrush(FSlateBrush& Brush, const FLinearColor& Fill, const FLinearColor& Edge)
	{
		// RoundedBox renders Tint as fill plus OutlineSettings as border.
		// Landing buttons already use this draw type; forcing it keeps fresh
		// designer buttons identical instead of inheriting the engine default.
		Brush.DrawAs = ESlateBrushDrawType::RoundedBox;
		Brush.TintColor = FSlateColor(Fill);
		Brush.OutlineSettings.Color = FSlateColor(Edge);
		if (Brush.OutlineSettings.Width <= 0.f)
		{
			Brush.OutlineSettings.Width = 1.f;
		}
	}

	void ApplyHoverMaterialBrush(FSlateBrush& Brush, UMaterialInterface* HoverMaterial)
	{
		// The hover shader outputs its own dark base plus edge lights, so the
		// tint stays white and the center remains readable for pale labels.
		Brush.DrawAs = ESlateBrushDrawType::Image;
		Brush.TintColor = FSlateColor(FLinearColor::White);
		Brush.SetResourceObject(HoverMaterial);
	}

	void ApplyTextStyle(UTextBlock* Label, UFont* FontAsset, const int32 Size, const FName Typeface, const FLinearColor& Color, const int32 OutlineSize = 0)
	{
		if (!Label)
		{
			return;
		}

		if (FontAsset)
		{
			FSlateFontInfo Font(FontAsset, Size, Typeface);
			Font.OutlineSettings.OutlineSize = OutlineSize;
			Font.OutlineSettings.OutlineColor = FLinearColor(0.025f, 0.01f, 0.05f, 0.90f);
			Label->SetFont(Font);
		}
		Label->SetColorAndOpacity(FSlateColor(Color));
	}
}

UMaterialInterface* UAeyerjiUIStyleLibrary::GetMenuHoverMaterial()
{
	return LoadSharedAsset(GMenuHoverMaterial, TEXT("/Game/GUI/Fonts/M_UI_MenuHoverAtmosphere.M_UI_MenuHoverAtmosphere"));
}

UMaterialInterface* UAeyerjiUIStyleLibrary::GetTitleAtmosphereMaterial()
{
	return LoadSharedAsset(GTitleAtmosphereMaterial, TEXT("/Game/GUI/Fonts/M_UI_LandingTitleAtmosphere.M_UI_LandingTitleAtmosphere"));
}

UDataTable* UAeyerjiUIStyleLibrary::GetMenuTextStyleSet()
{
	return LoadSharedAsset(GMenuTextStyleSet, TEXT("/Game/GUI/Fonts/RichTextStyleDT.RichTextStyleDT"));
}

UFont* UAeyerjiUIStyleLibrary::GetExo2Font()
{
	return LoadSharedAsset(GExo2Font, TEXT("/Game/GUI/Fonts/Exo2/FNT_Exo2.FNT_Exo2"));
}

UFont* UAeyerjiUIStyleLibrary::GetBrunoAceFont()
{
	return LoadSharedAsset(GBrunoAceFont, TEXT("/Game/GUI/Fonts/BrunoAce/FNT_BrunoAce.FNT_BrunoAce"));
}

UFont* UAeyerjiUIStyleLibrary::GetInterFont()
{
	return LoadSharedAsset(GInterFont, TEXT("/Game/GUI/Fonts/Inter/FNT_Inter.FNT_Inter"));
}

void UAeyerjiUIStyleLibrary::ApplyMenuButtonStyle(UButton* Button, bool bUseAnimatedHover, UMaterialInterface* HoverMaterialOverride)
{
	if (!Button)
	{
		return;
	}

	FButtonStyle Style = Button->GetStyle();
	ApplyFlatButtonBrush(Style.Normal, MenuNormalFill, MenuNormalEdge);
	ApplyFlatButtonBrush(Style.Pressed, MenuPressedFill, MenuPressedEdge);

	UMaterialInterface* HoverMaterial = HoverMaterialOverride ? HoverMaterialOverride : GetMenuHoverMaterial();
	if (bUseAnimatedHover && HoverMaterial)
	{
		ApplyHoverMaterialBrush(Style.Hovered, HoverMaterial);
	}
	else
	{
		// Static fallback keeps contrast when the effect material is disabled or missing.
		ApplyFlatButtonBrush(Style.Hovered, MenuHoverFill, MenuHoverEdge);
	}

	// Disabled reuses the resting fill at reduced opacity; no separate approved treatment exists.
	ApplyFlatButtonBrush(Style.Disabled, FLinearColor(MenuNormalFill.R, MenuNormalFill.G, MenuNormalFill.B, 0.4f), MenuNormalEdge);
	Button->SetStyle(Style);

	// Full-height rows need a centered label: a fill-stretched RichText label
	// renders its text at the top-left of the tall button instead of the middle.
	if (UButtonSlot* ContentSlot = Cast<UButtonSlot>(Button->GetContentSlot()))
	{
		ContentSlot->SetHorizontalAlignment(HAlign_Center);
		ContentSlot->SetVerticalAlignment(VAlign_Center);
		ContentSlot->SetPadding(FMargin(0.f));
	}
}

UMaterialInstanceDynamic* UAeyerjiUIStyleLibrary::ApplyTitleAtmosphere(UImage* Atmosphere, UObject* OuterForMID, float Intensity, float MotionSpeed, UMaterialInterface* AtmosphereMaterialOverride)
{
	if (!Atmosphere)
	{
		return nullptr;
	}

	UMaterialInterface* BaseMaterial = AtmosphereMaterialOverride ? AtmosphereMaterialOverride : GetTitleAtmosphereMaterial();
	if (!BaseMaterial)
	{
		return nullptr;
	}

	// A per-widget MID allows submenus to tune glow without forking the shared material.
	UMaterialInstanceDynamic* DynamicMaterial = UMaterialInstanceDynamic::Create(BaseMaterial, OuterForMID ? OuterForMID : static_cast<UObject*>(Atmosphere));
	DynamicMaterial->SetScalarParameterValue(TEXT("Intensity"), Intensity);
	DynamicMaterial->SetScalarParameterValue(TEXT("MotionSpeed"), MotionSpeed);

	FSlateBrush Brush = Atmosphere->GetBrush();
	Brush.DrawAs = ESlateBrushDrawType::Image;
	Brush.TintColor = FSlateColor(FLinearColor::White);
	Brush.SetResourceObject(DynamicMaterial);
	// Zero desired size lets the title text drive layout; the overlay stretches this layer.
	Brush.SetImageSize(FVector2D::ZeroVector);
	Atmosphere->SetBrush(Brush);
	Atmosphere->SetVisibility(ESlateVisibility::HitTestInvisible);
	Atmosphere->SetColorAndOpacity(FLinearColor::White);
	return DynamicMaterial;
}

void UAeyerjiUIStyleLibrary::StyleProfileLabel(UTextBlock* Label, bool bIsGold)
{
	if (!Label)
	{
		return;
	}
	if (UFont* Exo2 = GetExo2Font())
	{
		Label->SetFont(FSlateFontInfo(Exo2, 20, TEXT("Medium")));
	}
	Label->SetColorAndOpacity(FSlateColor(bIsGold ? ProfileGoldColor : ProfileValueColor));
}

void UAeyerjiUIStyleLibrary::StyleStatusLabel(UTextBlock* Label)
{
	if (!Label)
	{
		return;
	}
	if (UFont* Exo2 = GetExo2Font())
	{
		Label->SetFont(FSlateFontInfo(Exo2, 16, TEXT("Medium")));
	}
	Label->SetColorAndOpacity(FSlateColor(ProfileValueColor));
}

void UAeyerjiUIStyleLibrary::StyleXPBar(UProgressBar* Bar, const bool bGhost)
{
	if (!Bar)
	{
		return;
	}
	Bar->SetFillColorAndOpacity(bGhost ? FLinearColor(LivingCyan.R, LivingCyan.G, LivingCyan.B, 0.30f) : XPFillColor);
	FProgressBarStyle Style = Bar->GetWidgetStyle();
	Style.BackgroundImage.DrawAs = ESlateBrushDrawType::RoundedBox;
	Style.BackgroundImage.TintColor = FSlateColor(XPTrackColor);
	Style.BackgroundImage.OutlineSettings.CornerRadii = FVector4(8.f, 8.f, 8.f, 8.f);
	Style.FillImage.DrawAs = ESlateBrushDrawType::RoundedBox;
	Style.FillImage.TintColor = FSlateColor(FLinearColor::White);
	Style.FillImage.OutlineSettings.CornerRadii = FVector4(8.f, 8.f, 8.f, 8.f);
	Bar->SetWidgetStyle(Style);
	Bar->SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UAeyerjiUIStyleLibrary::StyleInventoryLaneHeading(URichTextBlock* Heading)
{
	if (!Heading)
	{
		return;
	}

	FTextBlockStyle Style;
	Style.SetFont(FSlateFontInfo(GetBrunoAceFont(), 28, TEXT("Regular")));
	Style.SetColorAndOpacity(FSlateColor(PrimaryViolet));
	Style.SetShadowOffset(FVector2D(0.f, 1.f));
	Style.SetShadowColorAndOpacity(FLinearColor(LivingCyan.R, LivingCyan.G, LivingCyan.B, 0.25f));
	Style.Font.OutlineSettings.OutlineSize = 1;
	Style.Font.OutlineSettings.OutlineColor = FLinearColor(0.025f, 0.01f, 0.05f, 0.90f);
	Heading->SetDefaultTextStyle(Style);
}

void UAeyerjiUIStyleLibrary::StyleHUDLevelText(UTextBlock* Label)
{
	ApplyTextStyle(Label, GetBrunoAceFont(), 18, TEXT("Regular"), PrimaryViolet, 1);
}

float UAeyerjiUIStyleLibrary::EasePresentation(const float NormalizedTime)
{
	const float T = FMath::Clamp(NormalizedTime, 0.f, 1.f);
	return T * T * (3.f - 2.f * T);
}

void UAeyerjiUIStyleLibrary::ApplyLevelUpAccent(UTextBlock* LevelLabel, UProgressBar* ExperienceBar, const float Strength)
{
	const float Alpha = FMath::Clamp(Strength, 0.f, 1.f);
	if (LevelLabel)
	{
		LevelLabel->SetColorAndOpacity(FSlateColor(FMath::Lerp(PrimaryViolet, CoolWhite, Alpha)));
		LevelLabel->SetShadowOffset(FVector2D(0.f, 2.f * Alpha));
		LevelLabel->SetShadowColorAndOpacity(FLinearColor(LivingCyan.R, LivingCyan.G, LivingCyan.B, 0.8f * Alpha));
	}
	if (ExperienceBar)
	{
		ExperienceBar->SetFillColorAndOpacity(FMath::Lerp(XPFillColor, LivingCyan, Alpha));
	}
}

void UAeyerjiUIStyleLibrary::StyleHUDValueText(UTextBlock* Label)
{
	ApplyTextStyle(Label, GetExo2Font(), 15, TEXT("SemiBold"), CoolWhite, 1);
}

void UAeyerjiUIStyleLibrary::StyleMissionHeading(UTextBlock* Label)
{
	ApplyTextStyle(Label, GetBrunoAceFont(), 18, TEXT("Regular"), PrimaryViolet, 1);
}

void UAeyerjiUIStyleLibrary::StyleMissionDifficulty(UTextBlock* Label)
{
	ApplyTextStyle(Label, GetExo2Font(), 18, TEXT("SemiBold"), CoolWhite, 1);
	if (!Label) return;
	FSlateFontInfo Font = Label->GetFont();
	Font.OutlineSettings.OutlineColor = FLinearColor::Black;
	Label->SetFont(Font);
	Label->SetShadowOffset(FVector2D(1.f, 1.f));
	Label->SetShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.35f));
}

void UAeyerjiUIStyleLibrary::StyleMissionValue(UTextBlock* Label, const bool bWarning)
{
	ApplyTextStyle(Label, GetExo2Font(), 22, TEXT("SemiBold"), bWarning ? WarningRose : CoolWhite, 1);
}

void UAeyerjiUIStyleLibrary::StyleMissionGold(UTextBlock* Label, const bool bDelta)
{
	ApplyTextStyle(Label, GetExo2Font(), bDelta ? 16 : 18, TEXT("SemiBold"), bDelta ? LivingCyan : ProfileGoldColor, 1);
}

void UAeyerjiUIStyleLibrary::StyleMissionProgressBar(UProgressBar* Bar, const bool bDefenseObjective)
{
	if (!Bar)
	{
		return;
	}

	FProgressBarStyle Style = Bar->GetWidgetStyle();
	Style.BackgroundImage.DrawAs = ESlateBrushDrawType::RoundedBox;
	Style.BackgroundImage.TintColor = FSlateColor(XPTrackColor);
	Style.BackgroundImage.OutlineSettings.Color = FSlateColor(MenuNormalEdge);
	Style.BackgroundImage.OutlineSettings.Width = 1.f;
	Style.BackgroundImage.OutlineSettings.CornerRadii = FVector4(6.f, 6.f, 6.f, 6.f);
	Style.FillImage.DrawAs = ESlateBrushDrawType::RoundedBox;
	Style.FillImage.TintColor = FSlateColor(FLinearColor::White);
	Style.FillImage.OutlineSettings.CornerRadii = FVector4(6.f, 6.f, 6.f, 6.f);
	Bar->SetWidgetStyle(Style);
	Bar->SetFillColorAndOpacity(bDefenseObjective ? LivingCyan : XPFillColor);
}

void UAeyerjiUIStyleLibrary::StyleCompactStatusBar(UProgressBar* Bar)
{
	if (!Bar)
	{
		return;
	}

	FProgressBarStyle Style = Bar->GetWidgetStyle();
	Style.BackgroundImage.DrawAs = ESlateBrushDrawType::RoundedBox;
	Style.BackgroundImage.TintColor = FSlateColor(XPTrackColor);
	Style.BackgroundImage.OutlineSettings.Color = FSlateColor(MenuNormalEdge);
	Style.BackgroundImage.OutlineSettings.Width = 1.f;
	Style.BackgroundImage.OutlineSettings.CornerRadii = FVector4(3.f, 3.f, 3.f, 3.f);
	Style.FillImage.DrawAs = ESlateBrushDrawType::RoundedBox;
	Style.FillImage.TintColor = FSlateColor(FLinearColor::White);
	Style.FillImage.OutlineSettings.CornerRadii = FVector4(3.f, 3.f, 3.f, 3.f);
	Bar->SetWidgetStyle(Style);
}

void UAeyerjiUIStyleLibrary::StyleFloatingStatusText(UTextBlock* Label)
{
	ApplyTextStyle(Label, GetExo2Font(), 13, TEXT("SemiBold"), CoolWhite, 1);
	if (Label)
	{
		Label->SetShadowOffset(FVector2D(0.f, 1.f));
		Label->SetShadowColorAndOpacity(FLinearColor(0.01f, 0.005f, 0.02f, 0.8f));
	}
}

void UAeyerjiUIStyleLibrary::StyleCombatText(UTextBlock* Label)
{
	if (!Label)
	{
		return;
	}

	const FSlateColor ExistingColor = Label->GetColorAndOpacity();
	ApplyTextStyle(Label, GetExo2Font(), 22, TEXT("SemiBold"), FLinearColor::White, 1);
	Label->SetColorAndOpacity(ExistingColor);
	Label->SetShadowOffset(FVector2D(0.f, 1.f));
	Label->SetShadowColorAndOpacity(FLinearColor(0.01f, 0.005f, 0.02f, 0.9f));
}

void UAeyerjiUIStyleLibrary::StyleTransientMessage(UBorder* Surface, UTextBlock* Message, const bool bErrorPresentation)
{
	if (Surface)
	{
		FSlateBrush Brush;
		ApplyFlatButtonBrush(Brush, TransientSurface, bErrorPresentation ? WarningRose : LivingCyan);
		Brush.OutlineSettings.Width = 1.f;
		if (!bErrorPresentation)
		{
			Brush.OutlineSettings.RoundingType = ESlateBrushRoundingType::FixedRadius;
			Brush.OutlineSettings.CornerRadii = FVector4(5.f, 5.f, 5.f, 5.f);
			Brush.OutlineSettings.Color = FSlateColor(FLinearColor(0.20f, 0.45f, 0.65f, 0.65f));
			Surface->SetPadding(FMargin(10.f, 6.f));
			Surface->SetHorizontalAlignment(HAlign_Fill);
		}
		Surface->SetBrush(Brush);
		Surface->SetBrushColor(FLinearColor::White);
	}

	ApplyTextStyle(
		Message,
		GetInterFont(),
		bErrorPresentation ? 22 : 14,
		TEXT("Regular"),
		bErrorPresentation ? CoolWhite : BrightLavender,
		bErrorPresentation ? 1 : 0);
	if (Message && !bErrorPresentation)
	{
		Message->SetAutoWrapText(true);
		Message->SetWrappingPolicy(ETextWrappingPolicy::AllowPerCharacterWrapping);
		Message->SetJustification(ETextJustify::Center);
		Message->SetShadowOffset(FVector2D::ZeroVector);
	}
}

void UAeyerjiUIStyleLibrary::StyleScreenHeading(UTextBlock* Label)
{
	ApplyTextStyle(Label, GetBrunoAceFont(), 40, TEXT("Regular"), PrimaryViolet, 1);
}

void UAeyerjiUIStyleLibrary::StyleGameplayWarning(UTextBlock* Label)
{
	ApplyTextStyle(Label, GetInterFont(), 16, TEXT("Regular"), FLinearColor(1.f, 0.12f, 0.10f, 1.f), 1);
	if (!Label) return;
	FSlateFontInfo Font = Label->GetFont();
	Font.OutlineSettings.OutlineColor = FLinearColor::Black;
	Label->SetFont(Font);
	Label->SetShadowOffset(FVector2D::ZeroVector);
	Label->SetJustification(ETextJustify::Left);
	Label->SetAutoWrapText(true);
}

void UAeyerjiUIStyleLibrary::StyleBodyText(UTextBlock* Label)
{
	ApplyTextStyle(Label, GetInterFont(), 18, TEXT("Regular"), BrightLavender);
}

void UAeyerjiUIStyleLibrary::StyleMetricLabel(UTextBlock* Label)
{
	ApplyTextStyle(Label, GetExo2Font(), 18, TEXT("Medium"), ProfileValueColor);
}

void UAeyerjiUIStyleLibrary::StyleMetricValue(UTextBlock* Label, const bool bWarning)
{
	ApplyTextStyle(Label, GetExo2Font(), 20, TEXT("SemiBold"), bWarning ? WarningRose : CoolWhite, 1);
}

void UAeyerjiUIStyleLibrary::StyleMenuButtonLabel(UTextBlock* Label)
{
	ApplyTextStyle(Label, GetExo2Font(), 20, TEXT("SemiBold"), BrightLavender, 1);
}

void UAeyerjiUIStyleLibrary::StyleSlider(USlider* Slider)
{
	if (!Slider)
	{
		return;
	}

	FSliderStyle Style = Slider->GetWidgetStyle();
	Style.NormalBarImage.DrawAs = ESlateBrushDrawType::RoundedBox;
	Style.NormalBarImage.TintColor = FSlateColor(MenuNormalEdge);
	Style.HoveredBarImage.DrawAs = ESlateBrushDrawType::RoundedBox;
	Style.HoveredBarImage.TintColor = FSlateColor(MenuHoverEdge);
	Style.DisabledBarImage.DrawAs = ESlateBrushDrawType::RoundedBox;
	Style.DisabledBarImage.TintColor = FSlateColor(MenuNormalFill);
	Style.NormalThumbImage.DrawAs = ESlateBrushDrawType::RoundedBox;
	Style.NormalThumbImage.TintColor = FSlateColor(PrimaryViolet);
	Style.HoveredThumbImage.DrawAs = ESlateBrushDrawType::RoundedBox;
	Style.HoveredThumbImage.TintColor = FSlateColor(LivingCyan);
	Style.DisabledThumbImage.DrawAs = ESlateBrushDrawType::RoundedBox;
	Style.DisabledThumbImage.TintColor = FSlateColor(MenuNormalEdge);
	Style.BarThickness = 4.f;
	Slider->SetWidgetStyle(Style);
}

FText UAeyerjiUIStyleLibrary::FormatFrontendLevel(int32 CharacterLevel)
{
	return FText::Format(
		AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("Frontend_LevelFormat")),
		FText::AsNumber(CharacterLevel));
}

FText UAeyerjiUIStyleLibrary::FormatFrontendGold(int64 Gold)
{
	return FText::Format(
		AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("Frontend_GoldFormat")),
		FText::AsNumber(Gold));
}

FText UAeyerjiUIStyleLibrary::FormatFrontendXP(float CurrentXP, float RequiredXP)
{
	return FText::Format(
		AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("Frontend_XPFormat")),
		FText::AsNumber(FrontendXPInteger(CurrentXP)),
		FText::AsNumber(FrontendXPInteger(RequiredXP)));
}

float UAeyerjiUIStyleLibrary::ComputeFrontendXPPercent(float CurrentXP, float RequiredXP)
{
	const float SafeCurrent = FMath::Clamp(FMath::IsFinite(CurrentXP) ? CurrentXP : 0.f, 0.f, MaxFrontendXPDisplay);
	const float SafeRequired = FMath::Clamp(FMath::IsFinite(RequiredXP) ? RequiredXP : 0.f, 0.f, MaxFrontendXPDisplay);
	return SafeRequired > 0.f ? FMath::Clamp(SafeCurrent / SafeRequired, 0.f, 1.f) : 0.f;
}

int64 UAeyerjiUIStyleLibrary::FrontendXPInteger(float Value)
{
	return FMath::RoundToInt64(FMath::Clamp(
		FMath::IsFinite(Value) ? static_cast<double>(Value) : 0.0,
		0.0,
		static_cast<double>(MaxFrontendXPDisplay)));
}
