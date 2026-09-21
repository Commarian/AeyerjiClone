// Copyright (c) 2025 Aeyerji.

#include "GUI/W_PlayerStatusHUD.h"

#include "Blueprint/WidgetTree.h"
#include "AbilitySystemGlobals.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "GUI/AeyerjiStringLibrary.h"
#include "GUI/AeyerjiUIStyleLibrary.h"
#include "GUI/W_AeyerjiTransientMessage.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "AeyerjiPlayerStatusHUD"

void UW_PlayerStatusHUD::NativeConstruct()
{
	Super::NativeConstruct();
	if (!WidgetTree)
	{
		return;
	}

	UAeyerjiUIStyleLibrary::StyleXPBar(Cast<UProgressBar>(WidgetTree->FindWidget(TEXT("XPBar"))));
	if (UProgressBar* XPBarGhost = Cast<UProgressBar>(WidgetTree->FindWidget(TEXT("XPBar_Ghost"))))
	{
		UAeyerjiUIStyleLibrary::StyleXPBar(XPBarGhost, true);
		XPBarGhost->SetVisibility(ESlateVisibility::Collapsed);
	}
	UAeyerjiUIStyleLibrary::StyleHUDLevelText(Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("LevelText"))));
	UAeyerjiUIStyleLibrary::StyleHUDValueText(Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("XPText"))));
	UAeyerjiUIStyleLibrary::StyleHUDValueText(Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("HPValueText"))));
	UAeyerjiUIStyleLibrary::StyleHUDValueText(Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("ManaValueText"))));

	ToastMessageWidget = Cast<UW_AeyerjiTransientMessage>(WidgetTree->FindWidget(TEXT("WBP_ToastMessage")));
	if (ToastMessageWidget)
	{
		ToastMessageWidget->SetRenderOpacity(0.f);
		// Hidden reserves the XP layout space while excluding all toast child painting.
		ToastMessageWidget->SetVisibility(ESlateVisibility::Hidden);
	}

	TryBindToOwningAttributes();
}

void UW_PlayerStatusHUD::PresentPopupMessage(const FText& Message, const float Duration)
{
	if (!ToastMessageWidget && WidgetTree)
	{
		ToastMessageWidget = Cast<UW_AeyerjiTransientMessage>(WidgetTree->FindWidget(TEXT("WBP_ToastMessage")));
	}
	if (ToastMessageWidget)
	{
		// The presenter owns the complete sequence; do not also start the legacy
		// Blueprint hold/fade timer, which can hide a later queued message.
		ToastMessageWidget->EnqueueMessage(Message, Duration);
	}
}

void UW_PlayerStatusHUD::NativeTick(const FGeometry& MyGeometry, const float InDeltaTime)
{
	TryBindToOwningAttributes();
	Super::NativeTick(MyGeometry, InDeltaTime);
	TickLevelUpEffect(InDeltaTime);
	if (GameplayWarningText && GameplayWarningRemaining > 0.f)
	{
		GameplayWarningRemaining = FMath::Max(0.f, GameplayWarningRemaining - InDeltaTime);
		GameplayWarningText->SetRenderOpacity(FMath::Clamp(GameplayWarningRemaining / UAeyerjiUIStyleLibrary::MessageExitSeconds, 0.f, 1.f));
		if (GameplayWarningRemaining <= 0.f) GameplayWarningText->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UW_PlayerStatusHUD::PresentGameplayWarning(const FText& Message, const float Duration)
{
	if (!WidgetTree || Message.IsEmpty()) return;
	if (!GameplayWarningText)
	{
		UCanvasPanel* Canvas = Cast<UCanvasPanel>(WidgetTree->RootWidget);
		if (!Canvas) return;
		GameplayWarningText = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("GameplayWarningText"));
		UAeyerjiUIStyleLibrary::StyleGameplayWarning(GameplayWarningText);
		UCanvasPanelSlot* WarningSlot = Canvas->AddChildToCanvas(GameplayWarningText);
		WarningSlot->SetAnchors(FAnchors(0.02f, 0.12f, 0.34f, 0.12f));
		WarningSlot->SetOffsets(FMargin(0.f, 0.f, 0.f, 90.f));
		WarningSlot->SetZOrder(10);
	}
	// Each failure refreshes the lifetime and replaces the previous reason.
	GameplayWarningText->SetText(Message);
	GameplayWarningText->SetVisibility(ESlateVisibility::HitTestInvisible);
	GameplayWarningText->SetRenderOpacity(1.f);
	GameplayWarningRemaining = FMath::IsFinite(Duration) ? FMath::Clamp(Duration, 0.2f, 5.f) : 1.6f;
}

void UW_PlayerStatusHUD::NativeDestruct()
{
	if (LevelUpElapsed >= 0.f)
	{
		LevelUpElapsed = UAeyerjiUIStyleLibrary::LevelUpPulseSeconds;
		TickLevelUpEffect(0.f);
	}
	Super::NativeDestruct();
}

void UW_PlayerStatusHUD::TickLevelUpEffect(const float DeltaSeconds)
{
	if (LevelUpElapsed < 0.f) return;
	LevelUpElapsed += FMath::IsFinite(DeltaSeconds) ? FMath::Max(0.f, DeltaSeconds) : 0.f;
	const float T = FMath::Clamp(LevelUpElapsed / UAeyerjiUIStyleLibrary::LevelUpPulseSeconds, 0.f, 1.f);
	// A quick rise followed by a soft decay keeps the level readable throughout.
	const float Pulse = T < 0.15f
		? UAeyerjiUIStyleLibrary::EasePresentation(T / 0.15f)
		: 1.f - UAeyerjiUIStyleLibrary::EasePresentation((T - 0.15f) / 0.85f);
	UAeyerjiUIStyleLibrary::ApplyLevelUpAccent(LevelText, XPBar, Pulse);
	if (LevelText)
	{
		FWidgetTransform Transform = LevelRestTransform;
		Transform.Scale *= 1.f + 0.16f * Pulse;
		LevelText->SetRenderTransform(Transform);
		if (T >= 1.f)
		{
			LevelText->SetShadowOffset(LevelRestShadowOffset);
			LevelText->SetShadowColorAndOpacity(LevelRestShadowColor);
		}
	}
	if (T >= 1.f) LevelUpElapsed = -1.f;
}

void UW_PlayerStatusHUD::TryBindToOwningAttributes()
{
	if (APawn* OwningPawn = GetOwningPlayerPawn())
	{
		if (UAbilitySystemComponent* PlayerASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwningPawn))
		{
			if (BoundStatusASC.Get() == PlayerASC)
			{
				return;
			}

			BindToAttributesWithXPAndLevel(
				PlayerASC,
				UAeyerjiAttributeSet::GetHPAttribute(),
				UAeyerjiAttributeSet::GetHPMaxAttribute(),
				UAeyerjiAttributeSet::GetManaAttribute(),
				UAeyerjiAttributeSet::GetManaMaxAttribute(),
				UAeyerjiAttributeSet::GetXPAttribute(),
				UAeyerjiAttributeSet::GetXPMaxAttribute(),
				UAeyerjiAttributeSet::GetLevelAttribute());
			BoundStatusASC = PlayerASC;
		}
	}
}

void UW_PlayerStatusHUD::ApplyObjectiveState(const FAeyerjiObjectiveState& InObjectiveState)
{
	CachedObjectiveState = InObjectiveState;
	BP_HandleObjectiveStateApplied(CachedObjectiveState);
}

void UW_PlayerStatusHUD::ApplySurvivalRoundState(const FAeyerjiSurvivalRoundState& InSurvivalState)
{
	CachedSurvivalRoundState = InSurvivalState;
	BP_HandleSurvivalRoundStateApplied(CachedSurvivalRoundState);
}

void UW_PlayerStatusHUD::HandleLevelAdvanced(const int32 OldLevel, const int32 NewLevel)
{
	if (LevelUpElapsed < 0.f && LevelText)
	{
		LevelRestTransform = LevelText->GetRenderTransform();
		LevelRestShadowOffset = LevelText->GetShadowOffset();
		LevelRestShadowColor = LevelText->GetShadowColorAndOpacity();
	}
	LevelUpElapsed = 0.f;
	// Detection lives in UW_AeyerjiStatusBar::OnLevelChanged() on the replicated Level
	// attribute, never on the authority-only UAeyerjiLevelingComponent::OnLevelUp delegate,
	// so this runs on the owning client exactly when the displayed level advances.
	// Uses GlobalStringTable.csv key StatusLevelUpFormat. Reimport string table asset after CSV changes.
	const FText Template = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("StatusLevelUpFormat"));
	const FText LevelUpText = FText::Format(
		Template.IsEmpty() ? LOCTEXT("StatusLevelUpFormatFallback", "LEVEL {0}") : Template,
		FText::AsNumber(NewLevel));
	PresentPopupMessage(LevelUpText, 2.0f);
	BP_HandleLevelAdvanced(OldLevel, NewLevel, LevelUpText);
}

#undef LOCTEXT_NAMESPACE

