#include "GUI/W_EndRunScreen.h"

#include "Aeyerji/AeyerjiGameInstance.h"
#include "Aeyerji/AeyerjiPlayerState.h"
#include "Components/Button.h"
#include "Components/Slider.h"
#include "Components/TextBlock.h"
#include "Components/Widget.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GUI/AeyerjiStringLibrary.h"
#include "Systems/AeyerjiDifficultyTuning.h"

namespace
{
	FText FormatSeconds(const float TotalSeconds)
	{
		constexpr double MaxDisplayedSeconds = 3155760000.0;
		const double SafeSeconds = FMath::Clamp(
			FMath::IsFinite(TotalSeconds) ? static_cast<double>(TotalSeconds) : 0.0,
			0.0,
			MaxDisplayedSeconds);
		const int64 ClampedSeconds = FMath::RoundToInt64(SafeSeconds);
		const int64 Minutes = ClampedSeconds / 60;
		const int64 Seconds = ClampedSeconds % 60;
		return FText::FromString(FString::Printf(TEXT("%02lld:%02lld"), Minutes, Seconds));
	}

	FText FormatPercentValue(const float WholePercent)
	{
		FNumberFormattingOptions NumberOptions;
		NumberOptions.SetMinimumFractionalDigits(0);
		NumberOptions.SetMaximumFractionalDigits(0);
		const float SafePercent = FMath::Clamp(
			FMath::IsFinite(WholePercent) ? WholePercent : 0.f, 0.f, 1000000.f);
		return FText::AsPercent(SafePercent / 100.f, &NumberOptions);
	}

	int32 DifficultyDisplayValue(const float Slider)
	{
		const float SafeSlider = FMath::IsFinite(Slider)
			? Slider
			: UAeyerjiDifficultySettings::WorldTierToDifficultySlider(
				UAeyerjiDifficultySettings::GetNormalWorldTier());
		return FMath::RoundToInt(FMath::Clamp(
			SafeSlider, 0.f, UAeyerjiDifficultySettings::DifficultySliderMax));
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
			return AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("EndRunNeonDistrict"));
		}

		if (ZoneKey.Equals(TEXT("Zone.Menu"), ESearchCase::IgnoreCase) || ZoneKey.Equals(TEXT("Menu"), ESearchCase::IgnoreCase))
		{
			return AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("EndRunMainMenu"));
		}

		return FText::FromString(MakeFriendlyZoneNameString(ZoneId));
	}

	FText FormatResolutionTitle(const EAeyerjiRunResolution Resolution)
	{
		using namespace AeyerjiStringLibrary;
		switch (Resolution)
		{
		case EAeyerjiRunResolution::Victory:
			return GetGlobalStringTableText(TEXT("EndRunMissionComplete"));
		case EAeyerjiRunResolution::TimeExpired:
			return GetGlobalStringTableText(TEXT("EndRunMissionFailed"));
		case EAeyerjiRunResolution::DefenseObjectiveDestroyed:
			return GetGlobalStringTableText(TEXT("EndRunMissionFailed"));
		case EAeyerjiRunResolution::Abandoned:
			return GetGlobalStringTableText(TEXT("EndRunRunAbandoned"));
		default:
			return GetGlobalStringTableText(TEXT("EndRunRunSummary"));
		}
	}

	FText FormatFailureReason(const EAeyerjiRunResolution Resolution)
	{
		using namespace AeyerjiStringLibrary;
		switch (Resolution)
		{
		case EAeyerjiRunResolution::TimeExpired:
			return GetGlobalStringTableText(TEXT("EndRunTimeExpired"));
		case EAeyerjiRunResolution::DefenseObjectiveDestroyed:
			return GetGlobalStringTableText(TEXT("EndRunDefenseObjectiveDestroyedReason"));
		case EAeyerjiRunResolution::Abandoned:
			return GetGlobalStringTableText(TEXT("EndRunRunAbandonedReason"));
		default:
			return FText::GetEmpty();
		}
	}

	FText FormatResultDetail(const FAeyerjiRunResults& Results)
	{
		const FText ZoneDisplayName = FormatZoneDisplayName(Results.CompletedZoneId);
		const bool bHasZoneDisplayName = !ZoneDisplayName.IsEmpty();

		using namespace AeyerjiStringLibrary;
		switch (Results.Resolution)
		{
		case EAeyerjiRunResolution::Victory:
			return bHasZoneDisplayName
				? FText::Format(GetGlobalStringTableText(TEXT("EndRunExtractedFromZone")), ZoneDisplayName)
				: GetGlobalStringTableText(TEXT("EndRunExtractionComplete"));
		case EAeyerjiRunResolution::TimeExpired:
			return bHasZoneDisplayName
				? FText::Format(GetGlobalStringTableText(TEXT("EndRunFailedInZone")), ZoneDisplayName)
				: GetGlobalStringTableText(TEXT("EndRunTimeLimitExpired"));
		case EAeyerjiRunResolution::DefenseObjectiveDestroyed:
			return bHasZoneDisplayName
				? FText::Format(GetGlobalStringTableText(TEXT("EndRunDefenseDestroyedInZone")), ZoneDisplayName)
				: GetGlobalStringTableText(TEXT("EndRunDefenseDestroyed"));
		case EAeyerjiRunResolution::Abandoned:
			return bHasZoneDisplayName
				? FText::Format(GetGlobalStringTableText(TEXT("EndRunAbandonedInZone")), ZoneDisplayName)
				: GetGlobalStringTableText(TEXT("EndRunAbandoned"));
		default:
			return bHasZoneDisplayName
				? FText::Format(GetGlobalStringTableText(TEXT("EndRunSummaryForZone")), ZoneDisplayName)
				: GetGlobalStringTableText(TEXT("EndRunRunSummaryDetail"));
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
			const float SafeValue = FMath::IsFinite(NewValue) ? FMath::Clamp(NewValue, 0.f, 1.f) : 0.f;
			GameInstance->SetDifficultySlider(SafeValue * UAeyerjiDifficultySettings::DifficultySliderMax);
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
	const bool bShowTimedRows = FMath::IsFinite(CachedResults.TimeLimitSeconds) && CachedResults.TimeLimitSeconds > 0.f;
	const bool bShowBestTime = FMath::IsFinite(CachedResults.BestTimeForDifficultySeconds)
		&& CachedResults.BestTimeForDifficultySeconds > 0.f;
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
		UnitsKilledLabelText->SetText(AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("EndRunEnemiesDefeated")));
	}

	if (UnitsKilledValueText)
	{
		UnitsKilledValueText->SetText(FText::AsNumber(FMath::Max(0, CachedResults.UnitsKilled)));
	}

	if (UnitsGoalLabelText)
	{
		UnitsGoalLabelText->SetText(AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("EndRunObjectiveTarget")));
	}

	if (UnitsGoalValueText)
	{
		UnitsGoalValueText->SetText(FText::AsNumber(FMath::Max(0, CachedResults.UnitsKillTarget)));
	}

	if (TimeElapsedLabelText)
	{
		TimeElapsedLabelText->SetText(AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("EndRunClearTime")));
	}

	if (TimeElapsedValueText)
	{
		TimeElapsedValueText->SetText(FormatSeconds(CachedResults.RunTimeSeconds));
	}

	if (TimeRemainingLabelText)
	{
		TimeRemainingLabelText->SetText(AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("EndRunTimeRemaining")));
	}

	if (TimeRemainingValueText)
	{
		TimeRemainingValueText->SetText(FormatSeconds(CachedResults.TimeRemainingSeconds));
	}

	if (SpeedBonusLabelText)
	{
		SpeedBonusLabelText->SetText(AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("EndRunSpeedBonus")));
	}

	if (SpeedBonusValueText)
	{
		SpeedBonusValueText->SetText(FormatPercentValue(CachedResults.SpeedBonusPercent));
	}

	if (BestTimeLabelText)
	{
		BestTimeLabelText->SetText(AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("EndRunBestTime")));
	}

	if (BestTimeValueText)
	{
		BestTimeValueText->SetText(bShowBestTime
			? FormatSeconds(CachedResults.BestTimeForDifficultySeconds)
			: AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("EndRunBestTimeUnavailable")));
	}

	if (DifficultyLabelText)
	{
		DifficultyLabelText->SetText(AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("EndRunDifficulty")));
	}

	if (DifficultyValueText)
	{
		DifficultyValueText->SetText(FText::AsNumber(DifficultyDisplayValue(CachedResults.DifficultySlider)));
	}

	if (FailureReasonLabelText)
	{
		FailureReasonLabelText->SetText(AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("EndRunFailureReasonLabel")));
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
		RetryDifficultySlider->SetValue(static_cast<float>(DifficultyDisplayValue(GameInstance->GetDifficultySlider()))
			/ UAeyerjiDifficultySettings::DifficultySliderMax);
		bUpdatingDifficultyControls = false;
	}

	if (DifficultyValueText)
	{
		DifficultyValueText->SetText(FText::AsNumber(DifficultyDisplayValue(GameInstance->GetDifficultySlider())));
	}
}
