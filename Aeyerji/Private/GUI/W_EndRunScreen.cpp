#include "GUI/W_EndRunScreen.h"

#include "Aeyerji/AeyerjiGameInstance.h"
#include "Aeyerji/AeyerjiPlayerState.h"
#include "Components/Button.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

#define LOCTEXT_NAMESPACE "W_EndRunScreen"

namespace
{
	FText FormatSeconds(const float TotalSeconds)
	{
		const int32 ClampedSeconds = FMath::Max(0, FMath::RoundToInt(TotalSeconds));
		const int32 Minutes = ClampedSeconds / 60;
		const int32 Seconds = ClampedSeconds % 60;
		return FText::FromString(FString::Printf(TEXT("%02d:%02d"), Minutes, Seconds));
	}

	FText FormatPercentValue(const float WholePercent)
	{
		FNumberFormattingOptions NumberOptions;
		NumberOptions.SetMinimumFractionalDigits(0);
		NumberOptions.SetMaximumFractionalDigits(0);
		return FText::AsPercent(WholePercent / 100.f, &NumberOptions);
	}

	FString MakeFriendlyZoneNameString(const FName ZoneId)
	{
		if (ZoneId.IsNone())
		{
			return FString();
		}

		FString ZoneString = ZoneId.ToString();
		int32 SeparatorIndex = INDEX_NONE;

		if (ZoneString.FindLastChar(TEXT('.'), SeparatorIndex))
		{
			ZoneString.RightChopInline(SeparatorIndex + 1, EAllowShrinking::No);
		}

		if (ZoneString.FindLastChar(TEXT('/'), SeparatorIndex))
		{
			ZoneString.RightChopInline(SeparatorIndex + 1, EAllowShrinking::No);
		}

		ZoneString.ReplaceInline(TEXT("_"), TEXT(" "));
		ZoneString.ReplaceInline(TEXT("-"), TEXT(" "));

		FString FriendlyString;
		FriendlyString.Reserve(ZoneString.Len() + 4);

		for (int32 CharacterIndex = 0; CharacterIndex < ZoneString.Len(); ++CharacterIndex)
		{
			const TCHAR CurrentChar = ZoneString[CharacterIndex];
			if (CharacterIndex > 0
				&& FChar::IsUpper(CurrentChar)
				&& (FChar::IsLower(ZoneString[CharacterIndex - 1]) || FChar::IsDigit(ZoneString[CharacterIndex - 1]))
				&& !FriendlyString.EndsWith(TEXT(" ")))
			{
				FriendlyString.AppendChar(TEXT(' '));
			}

			FriendlyString.AppendChar(CurrentChar);
		}

		return FriendlyString.TrimStartAndEnd();
	}

	FText FormatZoneDisplayName(const FName ZoneId)
	{
		if (ZoneId.IsNone())
		{
			return FText::GetEmpty();
		}

		const FString ZoneKey = ZoneId.ToString();
		if (ZoneKey.Equals(TEXT("Zone.Neon"), ESearchCase::IgnoreCase) || ZoneKey.Equals(TEXT("Neon"), ESearchCase::IgnoreCase))
		{
			return LOCTEXT("ZoneDisplayNeon", "Neon District");
		}

		if (ZoneKey.Equals(TEXT("Zone.Menu"), ESearchCase::IgnoreCase) || ZoneKey.Equals(TEXT("Menu"), ESearchCase::IgnoreCase))
		{
			return LOCTEXT("ZoneDisplayMenu", "Main Menu");
		}

		return FText::FromString(MakeFriendlyZoneNameString(ZoneId));
	}

	FText FormatResolutionTitle(const EAeyerjiRunResolution Resolution)
	{
		switch (Resolution)
		{
		case EAeyerjiRunResolution::Victory:
			return LOCTEXT("ResultTitleVictory", "Mission Complete");
		case EAeyerjiRunResolution::TimeExpired:
			return LOCTEXT("ResultTitleFailure", "Mission Failed");
		case EAeyerjiRunResolution::Abandoned:
			return LOCTEXT("ResultTitleAbandoned", "Run Abandoned");
		default:
			return LOCTEXT("ResultTitleSummary", "Run Summary");
		}
	}

	FText FormatFailureReason(const EAeyerjiRunResolution Resolution)
	{
		switch (Resolution)
		{
		case EAeyerjiRunResolution::TimeExpired:
			return LOCTEXT("FailureReasonTimeExpired", "Time expired");
		case EAeyerjiRunResolution::Abandoned:
			return LOCTEXT("FailureReasonAbandoned", "Run abandoned");
		default:
			return FText::GetEmpty();
		}
	}

	FText FormatResultDetail(const FAeyerjiRunResults& Results)
	{
		const FText ZoneDisplayName = FormatZoneDisplayName(Results.CompletedZoneId);
		const bool bHasZoneDisplayName = !ZoneDisplayName.IsEmpty();

		switch (Results.Resolution)
		{
		case EAeyerjiRunResolution::Victory:
			return bHasZoneDisplayName
				? FText::Format(LOCTEXT("ResultDetailVictoryZone", "Extracted from {0}"), ZoneDisplayName)
				: LOCTEXT("ResultDetailVictory", "Extraction complete");
		case EAeyerjiRunResolution::TimeExpired:
			return bHasZoneDisplayName
				? FText::Format(LOCTEXT("ResultDetailFailureZone", "Failed in {0}"), ZoneDisplayName)
				: LOCTEXT("ResultDetailFailure", "The time limit expired");
		case EAeyerjiRunResolution::Abandoned:
			return bHasZoneDisplayName
				? FText::Format(LOCTEXT("ResultDetailAbandonedZone", "Run abandoned in {0}"), ZoneDisplayName)
				: LOCTEXT("ResultDetailAbandoned", "Run abandoned");
		default:
			return bHasZoneDisplayName
				? FText::Format(LOCTEXT("ResultDetailSummaryZone", "Summary for {0}"), ZoneDisplayName)
				: LOCTEXT("ResultDetailSummary", "Run summary");
		}
	}

	void SetWidgetVisible(UWidget* Widget, const bool bVisible)
	{
		if (Widget)
		{
			Widget->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		}
	}

	void SetRowVisible(UWidget* RowWidget, UWidget* LabelWidget, UWidget* ValueWidget, const bool bVisible)
	{
		if (RowWidget)
		{
			RowWidget->SetVisibility(bVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
			return;
		}

		SetWidgetVisible(LabelWidget, bVisible);
		SetWidgetVisible(ValueWidget, bVisible);
	}
}

void UW_EndRunScreen::ApplyRunResults(const FAeyerjiRunResults& InResults)
{
	CachedResults = InResults;
	RefreshDisplayedValues();
	RefreshDifficultyControls();
}

void UW_EndRunScreen::NativeConstruct()
{
	Super::NativeConstruct();

	if (RetryDifficultySlider)
	{
		RetryDifficultySlider->OnValueChanged.RemoveDynamic(this, &UW_EndRunScreen::HandleRetryDifficultyChanged);
		RetryDifficultySlider->OnValueChanged.AddDynamic(this, &UW_EndRunScreen::HandleRetryDifficultyChanged);
	}

	if (RetryButton)
	{
		RetryButton->OnClicked.RemoveDynamic(this, &UW_EndRunScreen::HandleRetryClicked);
		RetryButton->OnClicked.AddDynamic(this, &UW_EndRunScreen::HandleRetryClicked);
	}

	if (ReturnToMenuButton)
	{
		ReturnToMenuButton->OnClicked.RemoveDynamic(this, &UW_EndRunScreen::HandleReturnToMenuClicked);
		ReturnToMenuButton->OnClicked.AddDynamic(this, &UW_EndRunScreen::HandleReturnToMenuClicked);
	}

	RefreshDisplayedValues();
	RefreshDifficultyControls();
}

void UW_EndRunScreen::NativeDestruct()
{
	if (RetryDifficultySlider)
	{
		RetryDifficultySlider->OnValueChanged.RemoveDynamic(this, &UW_EndRunScreen::HandleRetryDifficultyChanged);
	}

	if (RetryButton)
	{
		RetryButton->OnClicked.RemoveDynamic(this, &UW_EndRunScreen::HandleRetryClicked);
	}

	if (ReturnToMenuButton)
	{
		ReturnToMenuButton->OnClicked.RemoveDynamic(this, &UW_EndRunScreen::HandleReturnToMenuClicked);
	}

	Super::NativeDestruct();
}

void UW_EndRunScreen::HandleRetryDifficultyChanged(const float NewValue)
{
	if (bUpdatingDifficultyControls)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		if (UAeyerjiGameInstance* GameInstance = Cast<UAeyerjiGameInstance>(World->GetGameInstance()))
		{
			GameInstance->SetDifficultySlider(FMath::Clamp(NewValue, 0.f, 1.f) * 1000.f);
			RefreshDifficultyControls();
		}
	}
}

void UW_EndRunScreen::HandleRetryClicked()
{
	UE_LOG(LogTemp, Display, TEXT("W_EndRunScreen: Retry clicked Widget=%s"), *GetNameSafe(this));

	if (APlayerController* PlayerController = GetOwningPlayer())
	{
		if (AAeyerjiPlayerState* PlayerState = PlayerController->GetPlayerState<AAeyerjiPlayerState>())
		{
			PlayerState->RequestRetryRun();
		}
	}
}

void UW_EndRunScreen::HandleReturnToMenuClicked()
{
	UE_LOG(LogTemp, Display, TEXT("W_EndRunScreen: ReturnToMenu clicked Widget=%s"), *GetNameSafe(this));

	if (APlayerController* PlayerController = GetOwningPlayer())
	{
		if (AAeyerjiPlayerState* PlayerState = PlayerController->GetPlayerState<AAeyerjiPlayerState>())
		{
			PlayerState->RequestReturnToMenu();
		}
	}
}

void UW_EndRunScreen::RefreshDisplayedValues()
{
	const FText FailureReason = FormatFailureReason(CachedResults.Resolution);
	const bool bShowUnitsGoal = CachedResults.UnitsKillTarget > 0;
	const bool bShowTimedRows = CachedResults.TimeLimitSeconds > 0.f;
	const bool bShowBestTime = CachedResults.BestTimeForDifficultySeconds > 0.f;
	const bool bShowFailureReason = !FailureReason.IsEmpty();

	if (ResultTitleText)
	{
		ResultTitleText->SetText(FormatResolutionTitle(CachedResults.Resolution));
	}

	if (ResultDetailText)
	{
		const FText ResultDetail = FormatResultDetail(CachedResults);
		ResultDetailText->SetText(ResultDetail);
		ResultDetailText->SetVisibility(ResultDetail.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::Visible);
	}

	if (UnitsKilledLabelText)
	{
		UnitsKilledLabelText->SetText(LOCTEXT("UnitsKilledLabel", "Enemies Defeated"));
	}

	if (UnitsKilledValueText)
	{
		UnitsKilledValueText->SetText(FText::AsNumber(CachedResults.UnitsKilled));
	}

	if (UnitsGoalLabelText)
	{
		UnitsGoalLabelText->SetText(LOCTEXT("UnitsGoalLabel", "Objective Target"));
	}

	if (UnitsGoalValueText)
	{
		UnitsGoalValueText->SetText(FText::AsNumber(CachedResults.UnitsKillTarget));
	}

	if (TimeElapsedLabelText)
	{
		TimeElapsedLabelText->SetText(LOCTEXT("TimeElapsedLabel", "Clear Time"));
	}

	if (TimeElapsedValueText)
	{
		TimeElapsedValueText->SetText(FormatSeconds(CachedResults.RunTimeSeconds));
	}

	if (TimeRemainingLabelText)
	{
		TimeRemainingLabelText->SetText(LOCTEXT("TimeRemainingLabel", "Time Remaining"));
	}

	if (TimeRemainingValueText)
	{
		TimeRemainingValueText->SetText(FormatSeconds(CachedResults.TimeRemainingSeconds));
	}

	if (SpeedBonusLabelText)
	{
		SpeedBonusLabelText->SetText(LOCTEXT("SpeedBonusLabel", "Speed Bonus"));
	}

	if (SpeedBonusValueText)
	{
		SpeedBonusValueText->SetText(FormatPercentValue(CachedResults.SpeedBonusPercent));
	}

	if (BestTimeLabelText)
	{
		BestTimeLabelText->SetText(LOCTEXT("BestTimeLabel", "Best Time"));
	}

	if (BestTimeValueText)
	{
		BestTimeValueText->SetText(CachedResults.BestTimeForDifficultySeconds > 0.f
			? FormatSeconds(CachedResults.BestTimeForDifficultySeconds)
			: LOCTEXT("BestTimeUnavailable", "--"));
	}

	if (DifficultyLabelText)
	{
		DifficultyLabelText->SetText(LOCTEXT("DifficultyLabel", "Difficulty"));
	}

	if (DifficultyValueText)
	{
		DifficultyValueText->SetText(FText::AsNumber(FMath::RoundToInt(CachedResults.DifficultySlider)));
	}

	if (FailureReasonLabelText)
	{
		FailureReasonLabelText->SetText(LOCTEXT("FailureReasonLabel", "Failure Reason"));
	}

	if (FailureReasonText)
	{
		FailureReasonText->SetText(FailureReason);
	}

	if (RetryDifficultySlider)
	{
		RetryDifficultySlider->SetVisibility(ESlateVisibility::Visible);
	}

	if (RetryButton)
	{
		RetryButton->SetVisibility(ESlateVisibility::Visible);
	}

	if (ReturnToMenuButton)
	{
		ReturnToMenuButton->SetVisibility(ESlateVisibility::Visible);
	}

	SetRowVisible(UnitsKilledRow, UnitsKilledLabelText, UnitsKilledValueText, /*bVisible=*/true);
	SetRowVisible(UnitsGoalRow, UnitsGoalLabelText, UnitsGoalValueText, bShowUnitsGoal);
	SetRowVisible(TimeElapsedRow, TimeElapsedLabelText, TimeElapsedValueText, /*bVisible=*/true);
	SetRowVisible(TimeRemainingRow, TimeRemainingLabelText, TimeRemainingValueText, bShowTimedRows);
	SetRowVisible(SpeedBonusRow, SpeedBonusLabelText, SpeedBonusValueText, bShowTimedRows);
	SetRowVisible(BestTimeRow, BestTimeLabelText, BestTimeValueText, bShowBestTime);
	SetRowVisible(DifficultyRow, DifficultyLabelText, DifficultyValueText, /*bVisible=*/true);
	SetRowVisible(FailureReasonRow, FailureReasonLabelText, FailureReasonText, bShowFailureReason);
}

void UW_EndRunScreen::RefreshDifficultyControls()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const UAeyerjiGameInstance* GameInstance = Cast<UAeyerjiGameInstance>(World->GetGameInstance());
	if (!GameInstance)
	{
		return;
	}

	if (RetryDifficultySlider)
	{
		bUpdatingDifficultyControls = true;
		RetryDifficultySlider->SetValue(FMath::Clamp(GameInstance->GetDifficultySlider() / 1000.f, 0.f, 1.f));
		bUpdatingDifficultyControls = false;
	}

	if (DifficultyValueText)
	{
		DifficultyValueText->SetText(FText::AsNumber(FMath::RoundToInt(GameInstance->GetDifficultySlider())));
	}
}

#undef LOCTEXT_NAMESPACE
