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
	Abandoned   UMETA(DisplayName="Abandoned")
};

USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiRunResults
{
	GENERATED_BODY()

	/** Monotonic counter used to safely gate local "results ready" broadcasts across replication order differences. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Run")
	int32 ResultsVersion = 0;

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
};

USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiCompletedRunRecord
{
	GENERATED_BODY()

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
};
