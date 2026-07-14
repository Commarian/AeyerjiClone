// Copyright (c) 2025 Aeyerji.
#pragma once

#include "CoreMinimal.h"
#include "AeyerjiObjectiveTypes.h"
#include "Blueprint/UserWidget.h"
#include "Components/SlateWrapperTypes.h"
#include "TimerManager.h"
#include "W_AeyerjiMissionHUD.generated.h"

class AActor;
class UProgressBar;
class UTextBlock;

/**
 * Controller-owned mission HUD contract for objective, survival-round, and run message presentation.
 * Blueprint subclasses should render only from the replicated snapshots passed through ApplyObjectiveState()
 * and ApplySurvivalRoundState().
 */
UCLASS()
class AEYERJI_API UW_AeyerjiMissionHUD : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Caches and forwards the latest replicated objective snapshot to Blueprint presentation logic. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|HUD|Objective")
	void ApplyObjectiveState(const FAeyerjiObjectiveState& InObjectiveState);

	/** Caches and forwards the latest replicated survival-round snapshot to Blueprint presentation logic. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|HUD|Survival")
	void ApplySurvivalRoundState(const FAeyerjiSurvivalRoundState& InSurvivalState);

	/** Forwards the latest profile-persistent gold amount and local delta to Blueprint presentation logic. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|HUD|Currency")
	void ApplyGoldState(int64 Gold, int64 Delta);

	/** Opens the Blueprint repair menu for the active defense objective. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|HUD|Survival|Defense")
	void ShowDefenseObjectiveRepairMenu(AActor* ObjectiveActor, const TArray<FAeyerjiDefenseRepairOption>& RepairOptions, int64 CurrentGold, float CurrentHealth, float MaxHealth);

	/** Blueprint calls this after the player chooses a repair option. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|HUD|Survival|Defense")
	void RequestDefenseObjectiveRepair(AActor* ObjectiveActor, FName OptionId);

	/** Applies or refreshes the replicated between-round upgrade offer. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|HUD|Survival|Upgrades")
	void ApplySurvivalUpgradeOffer(const FAeyerjiSurvivalUpgradeOfferState& OfferState);

	/** Clears the between-round upgrade UI when no offer is active. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|HUD|Survival|Upgrades")
	void ClearSurvivalUpgradeOffer();

	/** Blueprint calls this after the player chooses a survival upgrade option. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|HUD|Survival|Upgrades")
	void RequestSurvivalUpgradeChoice(FName OptionId, int32 OfferRevision);

	/** Shows a localized one-shot mission message from an arbitrary string-table key. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|HUD|Messages")
	void ShowMissionMessageKey(FName MessageKey, float DisplaySeconds = 2.f);

	/** Immediately resets the native gold presentation state to the supplied authoritative amount. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|HUD|Currency")
	void ResetGoldPresentation(int64 Gold);

	/** Returns the last objective snapshot applied to this widget. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|HUD|Objective")
	const FAeyerjiObjectiveState& GetCachedObjectiveState() const { return CachedObjectiveState; }

	/** Returns the last survival-round snapshot applied to this widget. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|HUD|Survival")
	const FAeyerjiSurvivalRoundState& GetCachedSurvivalRoundState() const { return CachedSurvivalRoundState; }

	/** Builds the round header strings and forwards them to Blueprint presentation. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|HUD|Survival")
	void UpdateRoundHeader(const FAeyerjiSurvivalRoundState& SurvivalState);

	/** Builds the round progress text/fill and forwards them to Blueprint presentation. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|HUD|Survival")
	void UpdateRoundProgress(const FAeyerjiSurvivalRoundState& SurvivalState);

	/** Converts the replicated round type into a display label/tint and forwards it to Blueprint presentation. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|HUD|Survival")
	void UpdateRoundTypeVisual(const FAeyerjiSurvivalRoundState& SurvivalState);

	/** Builds defense objective health text/fill and forwards it to Blueprint presentation. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|HUD|Survival|Defense")
	void UpdateDefenseObjectiveStatus(const FAeyerjiSurvivalRoundState& SurvivalState);

	/** Handles one-shot message keys such as RoundStart, RoundClear, BossIncoming, and CycleStart. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|HUD|Survival")
	void HandleRoundMessage(const FAeyerjiSurvivalRoundState& SurvivalState);

	/** Returns true when the survival round panel should be visible for this state. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|HUD|Survival")
	bool ShouldShowSurvivalRoundHUD(const FAeyerjiSurvivalRoundState& SurvivalState) const;

protected:
	virtual void NativeDestruct() override;

	/** Blueprint presentation hook fired whenever ApplyObjectiveState() receives a new snapshot. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Objective", meta=(DisplayName="Handle Objective State Applied"))
	void BP_HandleObjectiveStateApplied(const FAeyerjiObjectiveState& InObjectiveState);

	/** Blueprint presentation hook fired whenever ApplySurvivalRoundState() receives a new snapshot. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Survival", meta=(DisplayName="Handle Survival Round State Applied"))
	void BP_HandleSurvivalRoundStateApplied(const FAeyerjiSurvivalRoundState& InSurvivalState);

	/** Blueprint applies header text to widgets. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Survival", meta=(DisplayName="Apply Round Header"))
	void BP_ApplyRoundHeader(const FText& HeaderText, const FText& RoundText, const FText& CycleText, bool bShowCycleText, bool bShowSurvivalRoundHUD);

	/** Blueprint applies progress text and normalized progress to widgets. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Survival", meta=(DisplayName="Apply Round Progress"))
	void BP_ApplyRoundProgress(const FText& ProgressText, float Progress01);

	/** Blueprint applies the round type label and color/icon styling. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Survival", meta=(DisplayName="Apply Round Type Visual"))
	void BP_ApplyRoundTypeVisual(EAeyerjiSurvivalRoundType RoundType, const FText& TypeText, const FLinearColor& TypeColor);

	/** Blueprint applies the defense objective health bar, text, and panel visibility. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Survival|Defense", meta=(DisplayName="Apply Defense Objective Status"))
	void BP_ApplyDefenseObjectiveStatus(
		bool bShowDefenseObjectiveHUD,
		bool bObjectiveDestroyed,
		const FText& LabelText,
		const FText& HealthText,
		float Progress01,
		ESlateVisibility SuggestedVisibility);

	/** Blueprint plays/shows a one-shot round message. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Survival", meta=(DisplayName="Handle Round Message"))
	void BP_HandleRoundMessage(FName MessageKey, const FText& MessageText, float DisplaySeconds);

	/** Legacy raw gold hook. Disable Use Native Gold Presentation State to drive gold entirely from this event. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Currency", meta=(DisplayName="Apply Gold State"))
	void BP_ApplyGoldState(int64 Gold, int64 Delta);

	/** Blueprint applies the permanent gold total text whenever the native state changes DisplayedGold. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Currency", meta=(DisplayName="Apply Gold Total State"))
	void BP_ApplyGoldTotalState(int64 InDisplayedGold, int64 InTargetGold);

	/** Blueprint applies already formatted total-gold text; use this for simple widgets with no Blueprint formatting logic. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Currency", meta=(DisplayName="Apply Gold Total Presentation"))
	void BP_ApplyGoldTotalPresentation(int64 InDisplayedGold, int64 InTargetGold, const FText& InDisplayedGoldText);

	/** Blueprint applies the temporary pending pickup text, for example +150 gold. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Currency", meta=(DisplayName="Apply Gold Delta State"))
	void BP_ApplyGoldDeltaState(int64 InPendingGoldDelta, bool bShowDelta);

	/** Blueprint applies already formatted +gold text plus default visibility/opacity for the delta widget. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Currency", meta=(DisplayName="Apply Gold Delta Presentation"))
	void BP_ApplyGoldDeltaPresentation(
		int64 InPendingGoldDelta,
		bool bShowDelta,
		const FText& InDeltaText,
		ESlateVisibility SuggestedVisibility,
		float SuggestedRenderOpacity);

	/** Blueprint should play the short pickup pop animation on GoldDeltaText from the beginning. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Currency", meta=(DisplayName="Play Gold Pickup Pop"))
	void BP_PlayGoldPickupPop(int64 InPendingGoldDelta);

	/** Blueprint should play the merge/fade animation that visually absorbs the delta into the total. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Currency", meta=(DisplayName="Play Gold Merge"))
	void BP_PlayGoldMerge(int64 InPendingGoldDelta, int64 InCountStartGold, int64 InCountTargetGold, float InCountDuration);

	/** Blueprint receives count-up progress for optional effects beyond setting total text. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Currency", meta=(DisplayName="Handle Gold Count Progress"))
	void BP_HandleGoldCountProgress(int64 InDisplayedGold, int64 InTargetGold, float Alpha);

	/** Blueprint should hide/reset GoldDeltaText after the pending amount has merged. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Currency", meta=(DisplayName="Clear Gold Delta"))
	void BP_ClearGoldDelta();

	/** Blueprint opens and populates the defense objective repair menu. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Survival|Defense", meta=(DisplayName="Show Defense Objective Repair Menu"))
	void BP_ShowDefenseObjectiveRepairMenu(AActor* ObjectiveActor, const TArray<FAeyerjiDefenseRepairOption>& RepairOptions, int64 CurrentGold, float CurrentHealth, float MaxHealth);

	/** Blueprint applies the between-round upgrade offer. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Survival|Upgrades", meta=(DisplayName="Apply Survival Upgrade Offer"))
	void BP_ApplySurvivalUpgradeOffer(const FAeyerjiSurvivalUpgradeOfferState& OfferState);

	/** Blueprint hides the between-round upgrade offer. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Survival|Upgrades", meta=(DisplayName="Clear Survival Upgrade Offer"))
	void BP_ClearSurvivalUpgradeOffer();

	/** Blueprint plays the survival HUD enter animation when round UI becomes visible. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Survival|Animation", meta=(DisplayName="Play Survival HUD Shown"))
	void BP_PlaySurvivalHUDShown(const FAeyerjiSurvivalRoundState& InSurvivalState);

	/** Blueprint plays the survival HUD exit animation when round UI becomes inactive. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Survival|Animation", meta=(DisplayName="Play Survival HUD Hidden"))
	void BP_PlaySurvivalHUDHidden(const FAeyerjiSurvivalRoundState& InSurvivalState);

	/** Blueprint plays a phase transition animation when the server-authored survival phase changes. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Survival|Animation", meta=(DisplayName="Play Round Phase Changed"))
	void BP_PlayRoundPhaseChanged(EAeyerjiSurvivalRoundPhase PreviousPhase, EAeyerjiSurvivalRoundPhase NewPhase, const FAeyerjiSurvivalRoundState& InSurvivalState);

	/** Blueprint plays the round-start animation for a deduped RoundStart message. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Survival|Animation", meta=(DisplayName="Play Round Started"))
	void BP_PlayRoundStarted(const FAeyerjiSurvivalRoundState& InSurvivalState);

	/** Blueprint plays the round-clear animation for a deduped RoundClear message. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Survival|Animation", meta=(DisplayName="Play Round Cleared"))
	void BP_PlayRoundCleared(const FAeyerjiSurvivalRoundState& InSurvivalState);

	/** Blueprint plays the boss warning animation for a deduped BossIncoming message. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Survival|Animation", meta=(DisplayName="Play Boss Incoming"))
	void BP_PlayBossIncoming(const FAeyerjiSurvivalRoundState& InSurvivalState);

	/** Blueprint plays the cycle-start animation for a deduped CycleStart message. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Survival|Animation", meta=(DisplayName="Play Cycle Started"))
	void BP_PlayCycleStarted(const FAeyerjiSurvivalRoundState& InSurvivalState);

	/** Blueprint plays the defense objective panel enter animation when the tree HUD becomes active. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Survival|Defense|Animation", meta=(DisplayName="Play Defense Objective Shown"))
	void BP_PlayDefenseObjectiveShown(const FAeyerjiSurvivalRoundState& InSurvivalState);

	/** Blueprint plays the defense objective panel exit animation when the tree HUD becomes inactive. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Survival|Defense|Animation", meta=(DisplayName="Play Defense Objective Hidden"))
	void BP_PlayDefenseObjectiveHidden(const FAeyerjiSurvivalRoundState& InSurvivalState);

	/** Blueprint plays a tree damage pulse when replicated objective HP drops. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Survival|Defense|Animation", meta=(DisplayName="Play Defense Objective Damaged"))
	void BP_PlayDefenseObjectiveDamaged(const FAeyerjiSurvivalRoundState& InSurvivalState, float PreviousHealth, float NewHealth);

	/** Blueprint plays a tree heal pulse when replicated objective HP rises. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Survival|Defense|Animation", meta=(DisplayName="Play Defense Objective Healed"))
	void BP_PlayDefenseObjectiveHealed(const FAeyerjiSurvivalRoundState& InSurvivalState, float PreviousHealth, float NewHealth);

	/** Blueprint plays a warning animation for threshold messages such as DefenseObjectiveHealth25. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Survival|Defense|Animation", meta=(DisplayName="Play Defense Objective Warning"))
	void BP_PlayDefenseObjectiveWarning(FName WarningMessageKey, const FAeyerjiSurvivalRoundState& InSurvivalState);

	/** Blueprint plays the tree destroyed animation once when the replicated destroyed flag changes to true. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Survival|Defense|Animation", meta=(DisplayName="Play Defense Objective Destroyed"))
	void BP_PlayDefenseObjectiveDestroyed(const FAeyerjiSurvivalRoundState& InSurvivalState);

	/** Blueprint plays the between-round upgrade offer enter animation. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Survival|Upgrades|Animation", meta=(DisplayName="Play Survival Upgrade Offer Shown"))
	void BP_PlaySurvivalUpgradeOfferShown(const FAeyerjiSurvivalUpgradeOfferState& InOfferState);

	/** Blueprint plays an update animation when an active upgrade offer changes selected count. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Survival|Upgrades|Animation", meta=(DisplayName="Play Survival Upgrade Offer Updated"))
	void BP_PlaySurvivalUpgradeOfferUpdated(const FAeyerjiSurvivalUpgradeOfferState& InOfferState);

	/** Blueprint plays the between-round upgrade offer exit animation. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Survival|Upgrades|Animation", meta=(DisplayName="Play Survival Upgrade Offer Hidden"))
	void BP_PlaySurvivalUpgradeOfferHidden(const FAeyerjiSurvivalUpgradeOfferState& InPreviousOfferState);

	/** Blueprint plays a spend pulse when profile gold is spent through a server-authoritative action. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Currency", meta=(DisplayName="Play Gold Spent"))
	void BP_PlayGoldSpent(int64 InSpentGold, const FText& InSpentGoldText);

private:
	void RestartGoldMergeTimer();
	void CommitPendingGold();
	void TickGoldCountUp();
	void StopGoldPresentationTimers();
	void ApplyImmediateGoldTotal(int64 Gold);
	void ApplyGoldTotalPresentation();
	void ApplyGoldDeltaPresentation(bool bShowDelta);
	void FireSurvivalRoundAnimationCues(const FAeyerjiSurvivalRoundState& NewState, bool bRoundMessageHandled);
	void FireRoundMessageAnimationCue(const FAeyerjiSurvivalRoundState& SurvivalState);
	void FireSurvivalUpgradeOfferAnimationCues(const FAeyerjiSurvivalUpgradeOfferState& OfferState);
	void FireSurvivalUpgradeOfferHiddenCue();
	void ApplyObjectiveNativePresentation(const FAeyerjiObjectiveState& ObjectiveState);
	bool ShouldHandleRoundMessage(const FAeyerjiSurvivalRoundState& SurvivalState) const;
	void MarkRoundMessageHandled(const FAeyerjiSurvivalRoundState& SurvivalState);
	FText BuildGoldTotalText(int64 Gold) const;
	FText BuildGoldDeltaText(int64 Delta) const;
	FText BuildGoldSpentText(int64 Delta) const;
	static bool IsDefenseObjectiveWarningMessage(FName MessageKey);
	static float EaseOutCubic(float Alpha);

	/** Cached objective snapshot for immediate refreshes after widget reattachment. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|HUD|Objective", meta=(AllowPrivateAccess="true"))
	FAeyerjiObjectiveState CachedObjectiveState;

	/** Cached survival-round snapshot for immediate refreshes after widget reattachment. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|HUD|Survival", meta=(AllowPrivateAccess="true"))
	FAeyerjiSurvivalRoundState CachedSurvivalRoundState;

	/** Enables the native batched +gold/count-up state machine. Disable to use only the legacy Apply Gold State Blueprint event. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|HUD|Currency", meta=(AllowPrivateAccess="true"))
	bool bUseNativeGoldPresentationState = true;

	/** Time window that batches repeated gold pickups before starting the merge/count-up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|HUD|Currency", meta=(AllowPrivateAccess="true", ClampMin="0.0", Units="s"))
	float GoldMergeWindow = 0.75f;

	/** Duration of the total-gold count-up after pending pickup gold commits. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|HUD|Currency", meta=(AllowPrivateAccess="true", ClampMin="0.01", Units="s"))
	float GoldCountDuration = 0.35f;

	/** Native count-up timer interval. 0.033 is roughly 30 UI updates per second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|HUD|Currency", meta=(AllowPrivateAccess="true", ClampMin="0.001", Units="s"))
	float GoldCountTickInterval = 0.033f;

	// Internal native presentation state. Kept unreflected so Blueprint widgets can create their own variables
	// with clean names such as DisplayedGold, TargetGold, and PendingGoldDelta.
	int64 NativeDisplayedGold = 0;
	int64 NativeTargetGold = 0;
	int64 NativePendingGoldDelta = 0;
	int64 NativeCountStartGold = 0;
	int64 NativeCountTargetGold = 0;
	float NativeGoldCountElapsed = 0.f;
	bool bNativeGoldStateInitialized = false;

	// Previous replicated snapshots are unreflected because they only drive native animation cue comparisons.
	FAeyerjiSurvivalRoundState PreviousSurvivalRoundState;
	FAeyerjiSurvivalUpgradeOfferState PreviousSurvivalUpgradeOfferState;
	bool bHasPreviousSurvivalRoundState = false;
	bool bHasPreviousSurvivalUpgradeOfferState = false;
	FName LastHandledRoundMessageKey = NAME_None;
	int32 LastHandledRoundMessageRevision = INDEX_NONE;

	/** Optional Blueprint widget used by native greater-rift objective presentation for the current objective label. */
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional, AllowPrivateAccess="true"))
	TObjectPtr<UTextBlock> MapMissionDescript;

	/** Optional Blueprint widget used by native greater-rift objective presentation for kill-count text. */
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional, AllowPrivateAccess="true"))
	TObjectPtr<UTextBlock> MapProgressKills;

	/** Optional Blueprint widget used by native greater-rift objective presentation for kill-progress fill. */
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional, AllowPrivateAccess="true"))
	TObjectPtr<UProgressBar> MapProgressBar;

	/** Optional Blueprint widget used by native defense-objective cleanup to hide inactive survival health text. */
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional, AllowPrivateAccess="true"))
	TObjectPtr<UTextBlock> ObjectiveHealthText;

	/** Optional Blueprint widget used by native defense-objective cleanup to hide inactive survival health fill. */
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional, AllowPrivateAccess="true"))
	TObjectPtr<UProgressBar> ObjectiveHPBar;

	/** Optional Blueprint widget used by native defense-objective cleanup to hide inactive survival labels. */
	UPROPERTY(BlueprintReadOnly, meta=(BindWidgetOptional, AllowPrivateAccess="true"))
	TObjectPtr<UTextBlock> ObjectiveLabel;

	FTimerHandle GoldMergeTimerHandle;
	FTimerHandle GoldCountTimerHandle;
};
