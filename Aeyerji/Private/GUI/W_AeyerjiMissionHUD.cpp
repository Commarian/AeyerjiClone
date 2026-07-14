// Copyright (c) 2025 Aeyerji.

#include "GUI/W_AeyerjiMissionHUD.h"

#include "GUI/AeyerjiStringLibrary.h"
#include "../../AeyerjiPlayerController.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"

namespace
{
	FText GetRoundTypeDisplayText(const EAeyerjiSurvivalRoundType RoundType)
	{
		using namespace AeyerjiStringLibrary;

		switch (RoundType)
		{
		case EAeyerjiSurvivalRoundType::Elite:
			return GetGlobalStringTableText(TEXT("RoundTypeElite"));
		case EAeyerjiSurvivalRoundType::Flying:
			return GetGlobalStringTableText(TEXT("RoundTypeFlying"));
		case EAeyerjiSurvivalRoundType::Boss:
			return GetGlobalStringTableText(TEXT("RoundTypeBoss"));
		case EAeyerjiSurvivalRoundType::Normal:
		default:
			return GetGlobalStringTableText(TEXT("RoundTypeNormal"));
		}
	}

	FLinearColor GetRoundTypeColor(const EAeyerjiSurvivalRoundType RoundType)
	{
		switch (RoundType)
		{
		case EAeyerjiSurvivalRoundType::Elite:
			return FLinearColor(1.0f, 0.72f, 0.18f, 1.0f);
		case EAeyerjiSurvivalRoundType::Flying:
			return FLinearColor(0.25f, 0.85f, 1.0f, 1.0f);
		case EAeyerjiSurvivalRoundType::Boss:
			return FLinearColor(1.0f, 0.18f, 0.12f, 1.0f);
		case EAeyerjiSurvivalRoundType::Normal:
		default:
			return FLinearColor(0.72f, 0.86f, 1.0f, 1.0f);
		}
	}
}

void UW_AeyerjiMissionHUD::ApplyObjectiveState(const FAeyerjiObjectiveState& InObjectiveState)
{
	CachedObjectiveState = InObjectiveState;
	BP_HandleObjectiveStateApplied(CachedObjectiveState);
	ApplyObjectiveNativePresentation(CachedObjectiveState);
}

void UW_AeyerjiMissionHUD::ApplySurvivalRoundState(const FAeyerjiSurvivalRoundState& InSurvivalState)
{
	CachedSurvivalRoundState = InSurvivalState;
	const bool bShowSurvivalRoundHUD = ShouldShowSurvivalRoundHUD(CachedSurvivalRoundState);
	const bool bShouldHandleRoundMessage = ShouldHandleRoundMessage(CachedSurvivalRoundState);

	// Non-survival runs still receive a replicated empty survival snapshot. Do not let that inactive
	// state overwrite the shared mission objective widgets used by greater-rift objective display.
	if (bShowSurvivalRoundHUD)
	{
		UpdateRoundHeader(CachedSurvivalRoundState);
		UpdateRoundProgress(CachedSurvivalRoundState);
		UpdateRoundTypeVisual(CachedSurvivalRoundState);
	}
	UpdateDefenseObjectiveStatus(CachedSurvivalRoundState);
	if (bShouldHandleRoundMessage)
	{
		HandleRoundMessage(CachedSurvivalRoundState);
	}
	BP_HandleSurvivalRoundStateApplied(CachedSurvivalRoundState);
	FireSurvivalRoundAnimationCues(CachedSurvivalRoundState, bShouldHandleRoundMessage);

	PreviousSurvivalRoundState = CachedSurvivalRoundState;
	bHasPreviousSurvivalRoundState = true;
}

void UW_AeyerjiMissionHUD::ApplyObjectiveNativePresentation(const FAeyerjiObjectiveState& ObjectiveState)
{
	const bool bHasObjectiveWidgets = MapMissionDescript || MapProgressKills || MapProgressBar;
	if (!bHasObjectiveWidgets)
	{
		return;
	}

	if (!ObjectiveState.bObjectiveReady)
	{
		if (MapProgressKills)
		{
			MapProgressKills->SetVisibility(ESlateVisibility::Collapsed);
		}
		return;
	}

	const auto ShowKillProgress = [this](const FText& DescriptionText, const FText& ProgressText, const float Progress01)
	{
		if (MapMissionDescript)
		{
			MapMissionDescript->SetText(DescriptionText);
			MapMissionDescript->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		if (MapProgressKills)
		{
			MapProgressKills->SetText(ProgressText);
			MapProgressKills->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		if (MapProgressBar)
		{
			MapProgressBar->SetPercent(FMath::Clamp(Progress01, 0.f, 1.f));
			MapProgressBar->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
	};

	const auto HideKillProgress = [this]()
	{
		if (MapProgressKills)
		{
			MapProgressKills->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (MapProgressBar)
		{
			MapProgressBar->SetVisibility(ESlateVisibility::Collapsed);
		}
	};

	switch (ObjectiveState.ObjectiveKind)
	{
	case EAeyerjiObjectiveKind::KillCount:
	case EAeyerjiObjectiveKind::KillCountThenBoss:
	{
		if (ObjectiveState.TotalToKill <= 0)
		{
			HideKillProgress();
			break;
		}

		FFormatNamedArguments KillTargetArgs;
		KillTargetArgs.Add(TEXT("amountOfEnemies"), FText::AsNumber(ObjectiveState.TotalToKill));
		ShowKillProgress(
			FText::Format(
				AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("KillXAmountOfEnemies")),
				KillTargetArgs),
			FText::Format(
				AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("RoundProgressKills")),
				FText::AsNumber(ObjectiveState.KilledCount),
				FText::AsNumber(ObjectiveState.TotalToKill)),
			ObjectiveState.Progress01);
		break;
	}
	case EAeyerjiObjectiveKind::KillNamedBoss:
		if (MapMissionDescript)
		{
			FFormatNamedArguments BossNameArgs;
			BossNameArgs.Add(TEXT("EnemyName"), FText::FromName(ObjectiveState.BossId));
			const FText BossText = ObjectiveState.bBossSpawned
				? FText::Format(
					AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("KillEnemyName")),
					BossNameArgs)
				: AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("BossIncoming"));
			MapMissionDescript->SetText(BossText);
			MapMissionDescript->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		HideKillProgress();
		break;
	case EAeyerjiObjectiveKind::BossCleared:
		if (MapMissionDescript)
		{
			MapMissionDescript->SetText(AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("BossDefeated")));
			MapMissionDescript->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		HideKillProgress();
		break;
	case EAeyerjiObjectiveKind::None:
	default:
		HideKillProgress();
		break;
	}
}

void UW_AeyerjiMissionHUD::NativeDestruct()
{
	StopGoldPresentationTimers();
	Super::NativeDestruct();
}

void UW_AeyerjiMissionHUD::ApplyGoldState(const int64 Gold, const int64 Delta)
{
	const int64 ClampedGold = FMath::Max<int64>(0, Gold);

	if (!bUseNativeGoldPresentationState)
	{
		BP_ApplyGoldState(ClampedGold, Delta);
		return;
	}

	if (!bNativeGoldStateInitialized)
	{
		const int64 InitialDisplayedGold = Delta > 0
			? FMath::Max<int64>(0, ClampedGold - Delta)
			: ClampedGold;
		ApplyImmediateGoldTotal(InitialDisplayedGold);
		if (Delta == 0)
		{
			return;
		}
	}

	NativeTargetGold = ClampedGold;

	if (Delta > 0)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(GoldCountTimerHandle);
		}

		NativePendingGoldDelta = FMath::Max<int64>(0, NativeTargetGold - NativeDisplayedGold);
		if (NativePendingGoldDelta <= 0)
		{
			ApplyImmediateGoldTotal(ClampedGold);
			return;
		}

		ApplyGoldDeltaPresentation(NativePendingGoldDelta > 0);
		BP_PlayGoldPickupPop(NativePendingGoldDelta);
		RestartGoldMergeTimer();
		return;
	}

	if (Delta < 0)
	{
		const int64 SpentGold = FMath::Max<int64>(0, -Delta);
		ApplyImmediateGoldTotal(ClampedGold);
		BP_PlayGoldSpent(SpentGold, BuildGoldSpentText(SpentGold));
		return;
	}

	ApplyImmediateGoldTotal(ClampedGold);
}

void UW_AeyerjiMissionHUD::ResetGoldPresentation(const int64 Gold)
{
	ApplyImmediateGoldTotal(FMath::Max<int64>(0, Gold));
}

void UW_AeyerjiMissionHUD::RestartGoldMergeTimer()
{
	if (GoldMergeWindow <= UE_SMALL_NUMBER)
	{
		CommitPendingGold();
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		CommitPendingGold();
		return;
	}

	World->GetTimerManager().ClearTimer(GoldMergeTimerHandle);
	World->GetTimerManager().SetTimer(
		GoldMergeTimerHandle,
		this,
		&UW_AeyerjiMissionHUD::CommitPendingGold,
		GoldMergeWindow,
		false);
}

void UW_AeyerjiMissionHUD::CommitPendingGold()
{
	if (NativePendingGoldDelta <= 0 || NativeTargetGold <= NativeDisplayedGold)
	{
		NativePendingGoldDelta = 0;
		ApplyGoldDeltaPresentation(false);
		BP_ClearGoldDelta();
		return;
	}

	NativeCountStartGold = NativeDisplayedGold;
	NativeCountTargetGold = NativeTargetGold;
	NativeGoldCountElapsed = 0.f;

	BP_PlayGoldMerge(NativePendingGoldDelta, NativeCountStartGold, NativeCountTargetGold, GoldCountDuration);

	UWorld* World = GetWorld();
	if (!World)
	{
		ApplyImmediateGoldTotal(NativeCountTargetGold);
		return;
	}

	World->GetTimerManager().ClearTimer(GoldCountTimerHandle);
	World->GetTimerManager().SetTimer(
		GoldCountTimerHandle,
		this,
		&UW_AeyerjiMissionHUD::TickGoldCountUp,
		FMath::Max(0.001f, GoldCountTickInterval),
		true);
}

void UW_AeyerjiMissionHUD::TickGoldCountUp()
{
	const float TickInterval = FMath::Max(0.001f, GoldCountTickInterval);
	const float Duration = FMath::Max(0.01f, GoldCountDuration);
	NativeGoldCountElapsed += TickInterval;

	const float Alpha = FMath::Clamp(NativeGoldCountElapsed / Duration, 0.f, 1.f);
	const float SmoothAlpha = EaseOutCubic(Alpha);
	const double InterpolatedGold = FMath::Lerp(
		static_cast<double>(NativeCountStartGold),
		static_cast<double>(NativeCountTargetGold),
		static_cast<double>(SmoothAlpha));

	NativeDisplayedGold = FMath::Max<int64>(0, FMath::RoundToInt64(InterpolatedGold));
	ApplyGoldTotalPresentation();
	BP_HandleGoldCountProgress(NativeDisplayedGold, NativeTargetGold, Alpha);

	if (Alpha < 1.f - UE_SMALL_NUMBER)
	{
		return;
	}

	NativeDisplayedGold = NativeCountTargetGold;
	NativeTargetGold = NativeCountTargetGold;
	NativePendingGoldDelta = 0;
	NativeGoldCountElapsed = 0.f;

	ApplyGoldTotalPresentation();
	BP_HandleGoldCountProgress(NativeDisplayedGold, NativeTargetGold, 1.f);
	ApplyGoldDeltaPresentation(false);
	BP_ClearGoldDelta();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GoldCountTimerHandle);
	}
}

void UW_AeyerjiMissionHUD::StopGoldPresentationTimers()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(GoldMergeTimerHandle);
		World->GetTimerManager().ClearTimer(GoldCountTimerHandle);
	}
}

void UW_AeyerjiMissionHUD::ApplyImmediateGoldTotal(const int64 Gold)
{
	StopGoldPresentationTimers();

	NativeDisplayedGold = FMath::Max<int64>(0, Gold);
	NativeTargetGold = NativeDisplayedGold;
	NativePendingGoldDelta = 0;
	NativeCountStartGold = NativeDisplayedGold;
	NativeCountTargetGold = NativeDisplayedGold;
	NativeGoldCountElapsed = 0.f;
	bNativeGoldStateInitialized = true;

	ApplyGoldTotalPresentation();
	ApplyGoldDeltaPresentation(false);
	BP_ClearGoldDelta();
}

void UW_AeyerjiMissionHUD::ApplyGoldTotalPresentation()
{
	BP_ApplyGoldTotalState(NativeDisplayedGold, NativeTargetGold);
	BP_ApplyGoldTotalPresentation(
		NativeDisplayedGold,
		NativeTargetGold,
		BuildGoldTotalText(NativeDisplayedGold));
}

void UW_AeyerjiMissionHUD::ApplyGoldDeltaPresentation(const bool bShowDelta)
{
	const bool bHasDelta = bShowDelta && NativePendingGoldDelta > 0;
	BP_ApplyGoldDeltaState(NativePendingGoldDelta, bHasDelta);
	BP_ApplyGoldDeltaPresentation(
		NativePendingGoldDelta,
		bHasDelta,
		bHasDelta ? BuildGoldDeltaText(NativePendingGoldDelta) : FText::GetEmpty(),
		bHasDelta ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden,
		bHasDelta ? 1.f : 0.f);
}

FText UW_AeyerjiMissionHUD::BuildGoldTotalText(const int64 Gold) const
{
	return FText::AsNumber(FMath::Max<int64>(0, Gold));
}

FText UW_AeyerjiMissionHUD::BuildGoldDeltaText(const int64 Delta) const
{
	// NOTE: Keys come from GlobalStringTable.csv. Reimport the string table asset after CSV edits.
	FText DeltaTemplate = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("GoldPickupDelta"));
	if (DeltaTemplate.IsEmpty())
	{
		// Fallback only for missing key during dev (should not ship).
		DeltaTemplate = FText::FromString(TEXT("+{0} Gold"));
	}

	return FText::Format(
		DeltaTemplate,
		FText::AsNumber(FMath::Max<int64>(0, Delta)));
}

FText UW_AeyerjiMissionHUD::BuildGoldSpentText(const int64 Delta) const
{
	// NOTE: Keys come from GlobalStringTable.csv. Reimport the string table asset after CSV edits.
	FText DeltaTemplate = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("GoldSpentDelta"));
	if (DeltaTemplate.IsEmpty())
	{
		// Fallback only for missing key during dev (should not ship).
		DeltaTemplate = FText::FromString(TEXT("-{0} Gold"));
	}

	return FText::Format(
		DeltaTemplate,
		FText::AsNumber(FMath::Max<int64>(0, Delta)));
}

void UW_AeyerjiMissionHUD::FireSurvivalRoundAnimationCues(const FAeyerjiSurvivalRoundState& NewState, const bool bRoundMessageHandled)
{
	const bool bNewSurvivalHUDVisible = ShouldShowSurvivalRoundHUD(NewState);
	const bool bPreviousSurvivalHUDVisible = bHasPreviousSurvivalRoundState
		&& ShouldShowSurvivalRoundHUD(PreviousSurvivalRoundState);

	if (bNewSurvivalHUDVisible && !bPreviousSurvivalHUDVisible)
	{
		BP_PlaySurvivalHUDShown(NewState);
	}
	else if (!bNewSurvivalHUDVisible && bPreviousSurvivalHUDVisible)
	{
		BP_PlaySurvivalHUDHidden(NewState);
	}

	if (bHasPreviousSurvivalRoundState && PreviousSurvivalRoundState.Phase != NewState.Phase)
	{
		BP_PlayRoundPhaseChanged(PreviousSurvivalRoundState.Phase, NewState.Phase, NewState);
	}

	if (bRoundMessageHandled)
	{
		FireRoundMessageAnimationCue(NewState);
	}

	const bool bPreviousDefenseObjectiveActive = bHasPreviousSurvivalRoundState
		&& PreviousSurvivalRoundState.bDefenseObjectiveActive;
	if (NewState.bDefenseObjectiveActive && !bPreviousDefenseObjectiveActive)
	{
		BP_PlayDefenseObjectiveShown(NewState);
	}
	else if (!NewState.bDefenseObjectiveActive && bPreviousDefenseObjectiveActive)
	{
		BP_PlayDefenseObjectiveHidden(NewState);
	}

	if (bHasPreviousSurvivalRoundState
		&& NewState.bDefenseObjectiveActive
		&& PreviousSurvivalRoundState.bDefenseObjectiveActive
		&& !NewState.bDefenseObjectiveDestroyed
		&& !PreviousSurvivalRoundState.bDefenseObjectiveDestroyed)
	{
		const float HealthDelta = NewState.DefenseObjectiveHealth - PreviousSurvivalRoundState.DefenseObjectiveHealth;
		if (HealthDelta < -0.5f)
		{
			BP_PlayDefenseObjectiveDamaged(NewState, PreviousSurvivalRoundState.DefenseObjectiveHealth, NewState.DefenseObjectiveHealth);
		}
		else if (HealthDelta > 0.5f)
		{
			BP_PlayDefenseObjectiveHealed(NewState, PreviousSurvivalRoundState.DefenseObjectiveHealth, NewState.DefenseObjectiveHealth);
		}
	}

	if (bHasPreviousSurvivalRoundState
		&& !PreviousSurvivalRoundState.bDefenseObjectiveDestroyed
		&& NewState.bDefenseObjectiveDestroyed)
	{
		BP_PlayDefenseObjectiveDestroyed(NewState);
	}
}

void UW_AeyerjiMissionHUD::FireRoundMessageAnimationCue(const FAeyerjiSurvivalRoundState& SurvivalState)
{
	const FName MessageKey = SurvivalState.MessageKey;
	if (MessageKey == FName(TEXT("RoundStart")))
	{
		BP_PlayRoundStarted(SurvivalState);
	}
	else if (MessageKey == FName(TEXT("RoundClear")))
	{
		BP_PlayRoundCleared(SurvivalState);
	}
	else if (MessageKey == FName(TEXT("BossIncoming")))
	{
		BP_PlayBossIncoming(SurvivalState);
	}
	else if (MessageKey == FName(TEXT("CycleStart")))
	{
		BP_PlayCycleStarted(SurvivalState);
	}

	if (IsDefenseObjectiveWarningMessage(MessageKey))
	{
		BP_PlayDefenseObjectiveWarning(MessageKey, SurvivalState);
	}
}

void UW_AeyerjiMissionHUD::FireSurvivalUpgradeOfferAnimationCues(const FAeyerjiSurvivalUpgradeOfferState& OfferState)
{
	if (!OfferState.bActive)
	{
		FireSurvivalUpgradeOfferHiddenCue();
		return;
	}

	const bool bPreviousOfferActive = bHasPreviousSurvivalUpgradeOfferState
		&& PreviousSurvivalUpgradeOfferState.bActive;
	if (!bPreviousOfferActive || PreviousSurvivalUpgradeOfferState.Revision != OfferState.Revision)
	{
		BP_PlaySurvivalUpgradeOfferShown(OfferState);
		return;
	}

	if (PreviousSurvivalUpgradeOfferState.SelectedCount != OfferState.SelectedCount)
	{
		BP_PlaySurvivalUpgradeOfferUpdated(OfferState);
	}
}

void UW_AeyerjiMissionHUD::FireSurvivalUpgradeOfferHiddenCue()
{
	if (bHasPreviousSurvivalUpgradeOfferState && PreviousSurvivalUpgradeOfferState.bActive)
	{
		BP_PlaySurvivalUpgradeOfferHidden(PreviousSurvivalUpgradeOfferState);
	}
}

bool UW_AeyerjiMissionHUD::ShouldHandleRoundMessage(const FAeyerjiSurvivalRoundState& SurvivalState) const
{
	return !SurvivalState.MessageKey.IsNone()
		&& (SurvivalState.MessageKey != LastHandledRoundMessageKey
			|| SurvivalState.Revision != LastHandledRoundMessageRevision);
}

void UW_AeyerjiMissionHUD::MarkRoundMessageHandled(const FAeyerjiSurvivalRoundState& SurvivalState)
{
	LastHandledRoundMessageKey = SurvivalState.MessageKey;
	LastHandledRoundMessageRevision = SurvivalState.Revision;
}

bool UW_AeyerjiMissionHUD::IsDefenseObjectiveWarningMessage(const FName MessageKey)
{
	return MessageKey == FName(TEXT("DefenseObjectiveHealth75"))
		|| MessageKey == FName(TEXT("DefenseObjectiveHealth50"))
		|| MessageKey == FName(TEXT("DefenseObjectiveHealth25"));
}

float UW_AeyerjiMissionHUD::EaseOutCubic(const float Alpha)
{
	const float ClampedAlpha = FMath::Clamp(Alpha, 0.f, 1.f);
	const float InverseAlpha = 1.f - ClampedAlpha;
	return 1.f - (InverseAlpha * InverseAlpha * InverseAlpha);
}

void UW_AeyerjiMissionHUD::ShowDefenseObjectiveRepairMenu(
	AActor* ObjectiveActor,
	const TArray<FAeyerjiDefenseRepairOption>& RepairOptions,
	const int64 CurrentGold,
	const float CurrentHealth,
	const float MaxHealth)
{
	BP_ShowDefenseObjectiveRepairMenu(ObjectiveActor, RepairOptions, CurrentGold, CurrentHealth, MaxHealth);
}

void UW_AeyerjiMissionHUD::RequestDefenseObjectiveRepair(AActor* ObjectiveActor, const FName OptionId)
{
	if (AAeyerjiPlayerController* AeyerjiPC = GetOwningPlayer<AAeyerjiPlayerController>())
	{
		AeyerjiPC->Server_RequestDefenseObjectiveRepair(ObjectiveActor, OptionId);
	}
}

void UW_AeyerjiMissionHUD::ApplySurvivalUpgradeOffer(const FAeyerjiSurvivalUpgradeOfferState& OfferState)
{
	if (!OfferState.bActive)
	{
		ClearSurvivalUpgradeOffer();
		return;
	}

	BP_ApplySurvivalUpgradeOffer(OfferState);
	FireSurvivalUpgradeOfferAnimationCues(OfferState);

	PreviousSurvivalUpgradeOfferState = OfferState;
	bHasPreviousSurvivalUpgradeOfferState = true;
}

void UW_AeyerjiMissionHUD::ClearSurvivalUpgradeOffer()
{
	BP_ClearSurvivalUpgradeOffer();
	FireSurvivalUpgradeOfferHiddenCue();

	PreviousSurvivalUpgradeOfferState = FAeyerjiSurvivalUpgradeOfferState();
	bHasPreviousSurvivalUpgradeOfferState = true;
}

void UW_AeyerjiMissionHUD::RequestSurvivalUpgradeChoice(const FName OptionId, const int32 OfferRevision)
{
	if (AAeyerjiPlayerController* AeyerjiPC = GetOwningPlayer<AAeyerjiPlayerController>())
	{
		AeyerjiPC->Server_SelectSurvivalUpgrade(OptionId, OfferRevision);
	}
}

void UW_AeyerjiMissionHUD::ShowMissionMessageKey(const FName MessageKey, const float DisplaySeconds)
{
	if (MessageKey.IsNone())
	{
		return;
	}

	const FText MessageText = AeyerjiStringLibrary::GetGlobalStringTableText(MessageKey);
	BP_HandleRoundMessage(MessageKey, MessageText.IsEmpty() ? FText::FromString(MessageKey.ToString()) : MessageText, FMath::Max(0.f, DisplaySeconds));
}

void UW_AeyerjiMissionHUD::UpdateRoundHeader(const FAeyerjiSurvivalRoundState& SurvivalState)
{
	const bool bShowSurvivalRoundHUD = ShouldShowSurvivalRoundHUD(SurvivalState);

	// NOTE: All display strings resolved from GlobalStringTable.csv via AeyerjiStringLibrary.
	// Reimport /Game/Localization/GlobalStringTable after modifying the CSV.
	FText HeaderText = SurvivalState.RoundDisplayLabel;
	if (HeaderText.IsEmpty())
	{
		HeaderText = SurvivalState.RoundType == EAeyerjiSurvivalRoundType::Boss
			? AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("BossRoundHeader"))
			: AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("RoundHeader"));
	}

	const FText RoundText = SurvivalState.bEndless || SurvivalState.MaxRoundNumber <= 0
		? FText::Format(AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("RoundNumberEndless")), FText::AsNumber(SurvivalState.RoundNumber))
		: FText::Format(AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("RoundNumberFinite")), FText::AsNumber(SurvivalState.RoundNumber), FText::AsNumber(SurvivalState.MaxRoundNumber));

	const bool bShowCycleText = SurvivalState.bEndless && SurvivalState.CycleDisplayNumber > 1;
	const FText CycleText = bShowCycleText
		? FText::Format(AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("CycleNumber")), FText::AsNumber(SurvivalState.CycleDisplayNumber))
		: FText::GetEmpty();

	BP_ApplyRoundHeader(HeaderText, RoundText, CycleText, bShowCycleText, bShowSurvivalRoundHUD);
}

bool UW_AeyerjiMissionHUD::ShouldShowSurvivalRoundHUD(const FAeyerjiSurvivalRoundState& SurvivalState) const
{
	return SurvivalState.RoundNumber > 0
		&& SurvivalState.Phase != EAeyerjiSurvivalRoundPhase::Inactive;
}

void UW_AeyerjiMissionHUD::UpdateRoundProgress(const FAeyerjiSurvivalRoundState& SurvivalState)
{
	FText ProgressText = FText::GetEmpty();
	float Progress01 = 0.f;

	switch (SurvivalState.Phase)
	{
	case EAeyerjiSurvivalRoundPhase::Preparing:
		ProgressText = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("RoundProgressPrepare"));
		break;
	case EAeyerjiSurvivalRoundPhase::Spawning:
	case EAeyerjiSurvivalRoundPhase::Clearing:
	{
		const bool bUseWaveProgress = SurvivalState.WaveEnemiesRequired > 0;
		const int32 Killed = bUseWaveProgress ? SurvivalState.WaveEnemiesKilled : SurvivalState.EnemiesKilled;
		const int32 Required = bUseWaveProgress ? SurvivalState.WaveEnemiesRequired : SurvivalState.EnemiesRequired;

		if (bUseWaveProgress && SurvivalState.WaveCount > 1 && !SurvivalState.WaveDisplayLabel.IsEmpty())
		{
			ProgressText = FText::Format(
				AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("RoundProgressNamedWaveKills")),
				SurvivalState.WaveDisplayLabel,
				FText::AsNumber(SurvivalState.WaveNumber),
				FText::AsNumber(SurvivalState.WaveCount),
				FText::AsNumber(Killed),
				FText::AsNumber(Required));
		}
		else if (bUseWaveProgress && SurvivalState.WaveCount > 1)
		{
			ProgressText = FText::Format(
				AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("RoundProgressWaveKills")),
				FText::AsNumber(SurvivalState.WaveNumber),
				FText::AsNumber(SurvivalState.WaveCount),
				FText::AsNumber(Killed),
				FText::AsNumber(Required));
		}
		else if (bUseWaveProgress && !SurvivalState.WaveDisplayLabel.IsEmpty())
		{
			ProgressText = FText::Format(
				AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("RoundProgressSingleNamedWaveKills")),
				SurvivalState.WaveDisplayLabel,
				FText::AsNumber(Killed),
				FText::AsNumber(Required));
		}
		else
		{
			ProgressText = FText::Format(
				AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("RoundProgressKills")),
				FText::AsNumber(Killed),
				FText::AsNumber(Required));
		}

		Progress01 = Required > 0
			? FMath::Clamp(static_cast<float>(Killed) / static_cast<float>(Required), 0.f, 1.f)
			: 0.f;
		break;
	}
	case EAeyerjiSurvivalRoundPhase::Boss:
		ProgressText = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("RoundProgressBoss"));
		break;
	case EAeyerjiSurvivalRoundPhase::RoundComplete:
		ProgressText = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("RoundProgressComplete"));
		Progress01 = 1.f;
		break;
	case EAeyerjiSurvivalRoundPhase::Inactive:
	default:
		break;
	}

	BP_ApplyRoundProgress(ProgressText, Progress01);
}

void UW_AeyerjiMissionHUD::UpdateRoundTypeVisual(const FAeyerjiSurvivalRoundState& SurvivalState)
{
	const FText TypeText = !SurvivalState.WaveDisplayLabel.IsEmpty()
		? SurvivalState.WaveDisplayLabel
		: GetRoundTypeDisplayText(SurvivalState.RoundType);

	BP_ApplyRoundTypeVisual(
		SurvivalState.RoundType,
		TypeText,
		GetRoundTypeColor(SurvivalState.RoundType));
}

void UW_AeyerjiMissionHUD::UpdateDefenseObjectiveStatus(const FAeyerjiSurvivalRoundState& SurvivalState)
{
	const bool bShowDefenseObjectiveHUD = SurvivalState.bDefenseObjectiveActive;
	const bool bObjectiveDestroyed = SurvivalState.bDefenseObjectiveDestroyed;
	const float Progress01 = bShowDefenseObjectiveHUD && !bObjectiveDestroyed
		? FMath::Clamp(SurvivalState.DefenseObjectiveProgress01, 0.f, 1.f)
		: 0.f;

	FText HealthText = FText::GetEmpty();
	if (bShowDefenseObjectiveHUD)
	{
		if (bObjectiveDestroyed)
		{
			HealthText = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("DefenseObjectiveHealthDestroyed"));
		}
		else
		{
			const int32 CurrentHealth = FMath::Max(0, FMath::CeilToInt(SurvivalState.DefenseObjectiveHealth));
			const int32 MaxHealth = FMath::Max(0, FMath::CeilToInt(SurvivalState.DefenseObjectiveHealthMax));
			HealthText = FText::Format(
				AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("DefenseObjectiveHealthFormat")),
				FText::AsNumber(CurrentHealth),
				FText::AsNumber(MaxHealth));
		}
	}

	BP_ApplyDefenseObjectiveStatus(
		bShowDefenseObjectiveHUD,
		bObjectiveDestroyed,
		AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("DefenseObjectiveTreeLabel")),
		HealthText,
		Progress01,
		bShowDefenseObjectiveHUD ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);

	const ESlateVisibility DefenseVisibility = bShowDefenseObjectiveHUD
		? ESlateVisibility::HitTestInvisible
		: ESlateVisibility::Collapsed;
	if (ObjectiveHealthText)
	{
		ObjectiveHealthText->SetVisibility(DefenseVisibility);
	}
	if (ObjectiveHPBar)
	{
		ObjectiveHPBar->SetVisibility(DefenseVisibility);
	}
	if (ObjectiveLabel)
	{
		ObjectiveLabel->SetVisibility(DefenseVisibility);
	}
}

void UW_AeyerjiMissionHUD::HandleRoundMessage(const FAeyerjiSurvivalRoundState& SurvivalState)
{
	if (!ShouldHandleRoundMessage(SurvivalState))
	{
		return;
	}

	FText MessageText;
	float DisplaySeconds = 2.f;

	if (SurvivalState.MessageKey == FName(TEXT("RoundStart")))
	{
		MessageText = FText::Format(AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("RoundStart")), FText::AsNumber(SurvivalState.RoundNumber));
	}
	else if (SurvivalState.MessageKey == FName(TEXT("RoundClear")))
	{
		MessageText = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("RoundClear"));
	}
	else if (SurvivalState.MessageKey == FName(TEXT("BossIncoming")))
	{
		MessageText = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("BossIncoming"));
		DisplaySeconds = 3.f;
	}
	else if (SurvivalState.MessageKey == FName(TEXT("BossDefeated")))
	{
		MessageText = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("BossDefeated"));
		DisplaySeconds = 3.f;
	}
	else if (SurvivalState.MessageKey == FName(TEXT("CycleStart")))
	{
		MessageText = FText::Format(AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("CycleStart")), FText::AsNumber(SurvivalState.CycleDisplayNumber));
	}
	else if (SurvivalState.MessageKey == FName(TEXT("DefenseObjectiveActive")))
	{
		MessageText = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("DefenseObjectiveActive"));
		DisplaySeconds = 3.f;
	}
	else if (SurvivalState.MessageKey == FName(TEXT("DefenseObjectiveHealth75")))
	{
		MessageText = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("DefenseObjectiveHealth75"));
		DisplaySeconds = 2.5f;
	}
	else if (SurvivalState.MessageKey == FName(TEXT("DefenseObjectiveHealth50")))
	{
		MessageText = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("DefenseObjectiveHealth50"));
		DisplaySeconds = 3.f;
	}
	else if (SurvivalState.MessageKey == FName(TEXT("DefenseObjectiveHealth25")))
	{
		MessageText = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("DefenseObjectiveHealth25"));
		DisplaySeconds = 3.5f;
	}
	else if (SurvivalState.MessageKey == FName(TEXT("DefenseObjectiveDestroyed")))
	{
		MessageText = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("DefenseObjectiveDestroyed"));
		DisplaySeconds = 3.5f;
	}
	else if (SurvivalState.MessageKey == FName(TEXT("DefenseObjectiveRepaired")))
	{
		MessageText = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("DefenseObjectiveRepaired"));
		DisplaySeconds = 2.f;
	}
	else if (SurvivalState.MessageKey == FName(TEXT("RepairEssenceCollected")))
	{
		MessageText = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("RepairEssenceCollected"));
		DisplaySeconds = 2.f;
	}
	else if (SurvivalState.MessageKey == FName(TEXT("RepairEssenceReady")))
	{
		MessageText = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("RepairEssenceReady"));
		DisplaySeconds = 2.5f;
	}
	else
	{
		MessageText = AeyerjiStringLibrary::GetGlobalStringTableText(SurvivalState.MessageKey);
		if (MessageText.IsEmpty())
		{
			MessageText = FText::FromString(SurvivalState.MessageKey.ToString());
		}
	}

	BP_HandleRoundMessage(SurvivalState.MessageKey, MessageText, DisplaySeconds);
	MarkRoundMessageHandled(SurvivalState);
}
