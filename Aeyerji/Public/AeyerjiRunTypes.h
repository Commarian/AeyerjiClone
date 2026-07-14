// Copyright (c) 2025 Aeyerji.
#pragma once

#include "CoreMinimal.h"
#include "AeyerjiRunTypes.generated.h"

UENUM(BlueprintType)
enum class EAeyerjiRunResolution : uint8
{
	None        UMETA(DisplayName="None"),
	Victory     UMETA(DisplayName="Victory"),
	TimeExpired UMETA(DisplayName="TimeExpired"),
	Abandoned   UMETA(DisplayName="Abandoned"),
	DefenseObjectiveDestroyed UMETA(DisplayName="DefenseObjectiveDestroyed")
};

USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiRunResults
{
	GENERATED_BODY()

	/** Monotonic counter used to safely gate local "results ready" broadcasts across replication order differences. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run")
	int32 ResultsVersion = 0;

	/** Authority-issued run identifier used to deduplicate persistence and rewards. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run")
	int32 RunSerial = 0;

	/** True when the boss objective was defeated before the results snapshot was frozen. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run")
	bool bBossDefeated = false;

	/** How the run was resolved once it ended. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run")
	EAeyerjiRunResolution Resolution = EAeyerjiRunResolution::None;

	/** Total run time captured from the LevelDirector timer. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run")
	float RunTimeSeconds = 0.f;

	/** Total shards collected during the run (if a LevelDirector is present). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run")
	int32 ShardsCollected = 0;

	/** Total units killed toward the encounter objective. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run")
	int32 UnitsKilled = 0;

	/** Final encounter objective target when the run ended. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run")
	int32 UnitsKillTarget = 0;

	/** Configured time limit in seconds for this run. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run")
	float TimeLimitSeconds = 0.f;

	/** Time still remaining when the run was completed or failed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run")
	float TimeRemainingSeconds = 0.f;

	/** Presentation-only bonus derived from how quickly the run was completed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run")
	float SpeedBonusPercent = 0.f;

	/** Best completed time recorded for the active difficulty after persistence. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run")
	float BestTimeForDifficultySeconds = 0.f;

	/** Difficulty slider snapshot (0..1000) used for this run. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run")
	float DifficultySlider = 0.f;

	/** Zone id that was active when the run results were frozen. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run")
	FName CompletedZoneId = NAME_None;

	/** Greater Rift Tier selected for this run. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run|Rift")
	int32 SelectedRiftTier = 1;

	/** Actual enemies defeated during the run. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run|Rift")
	int32 EnemiesDefeated = 0;

	/** Weighted progress earned before the boss phase. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run|Rift")
	int32 ProgressPoints = 0;

	/** Weighted progress target for the selected tier. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run|Rift")
	int32 ProgressPointTarget = 0;

	/** True only when Legion died strictly before the configured time limit. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run|Rift")
	bool bCompletedInTime = false;

	/** True when the time limit elapsed before Legion died. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run|Rift")
	bool bOvertime = false;

	/** True when at least one player died after the boss phase began. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run|Rift")
	bool bBossPhaseDeathOccurred = false;

	/** True when the timed and no-boss-death conditions earned the premium roll. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run|Rift")
	bool bFlawlessRewardEarned = false;

	/** Actual base-layer rolls generated for this local profile. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run|Rift|Rewards")
	int32 BaseRewardRolls = 0;

	/** Actual timed-cache rolls generated for this local profile. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run|Rift|Rewards")
	int32 TimedRewardRolls = 0;

	/** Actual flawless-layer rolls generated for this local profile. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run|Rift|Rewards")
	int32 FlawlessRewardRolls = 0;

	/** Tier earned by the shared run, or zero when no advancement was earned. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run|Rift")
	int32 EarnedNextRiftTier = 0;

	/** Highest tier stored for the receiving profile after this run. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run|Rift")
	int32 HighestUnlockedRiftTier = 1;

	/** True only for a profile whose highest tier increased from this result. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run|Rift")
	bool bNewRiftTierUnlockedForProfile = false;
};

USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiCompletedRunRecord
{
	GENERATED_BODY()

	/** Authority-issued run identifier used to prevent duplicate history entries. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Run")
	int32 RunSerial = 0;

	/** UTC timestamp captured when the run finished. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Run")
	FDateTime CompletedAtUtc;

	/** Persisted run outcome. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Run")
	EAeyerjiRunResolution Resolution = EAeyerjiRunResolution::None;

	/** Total elapsed time when the run ended. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Run")
	float RunTimeSeconds = 0.f;

	/** Total units killed when the run ended. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Run")
	int32 UnitsKilled = 0;

	/** Final objective target when the run ended. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Run")
	int32 UnitsKillTarget = 0;

	/** Difficulty slider used for the run. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Run")
	float DifficultySlider = 0.f;

	/** Speed bonus captured for post-run summaries. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Run")
	float SpeedBonusPercent = 0.f;

	/** Gameplay zone the player completed or failed in. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Run")
	FName CompletedZoneId = NAME_None;

	/** Greater Rift Tier used by this historical run. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Run")
	int32 SelectedRiftTier = 1;

	/** Weighted progress earned when the run completed. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Run")
	int32 ProgressPoints = 0;

	/** Weighted progress target for the run. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Run")
	int32 ProgressPointTarget = 0;

	/** Whether Legion died before the tier time limit. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Run")
	bool bCompletedInTime = false;

	/** Whether a boss-phase player death removed flawless eligibility. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Run")
	bool bBossPhaseDeathOccurred = false;
};
