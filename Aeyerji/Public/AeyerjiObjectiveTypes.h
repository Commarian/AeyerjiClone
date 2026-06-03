// Copyright (c) 2025 Aeyerji.
#pragma once

#include "CoreMinimal.h"
#include "AeyerjiObjectiveTypes.generated.h"

/** Replicated objective mode consumed by the player HUD. */
UENUM(BlueprintType)
enum class EAeyerjiObjectiveKind : uint8
{
	None             UMETA(DisplayName="None"),
	KillCount        UMETA(DisplayName="KillCount"),
	KillNamedBoss    UMETA(DisplayName="KillNamedBoss"),
	KillCountThenBoss UMETA(DisplayName="KillCountThenBoss"),
	BossCleared      UMETA(DisplayName="BossCleared")
};

/** Replicated phase for survival-round missions. */
UENUM(BlueprintType)
enum class EAeyerjiSurvivalRoundPhase : uint8
{
	Inactive      UMETA(DisplayName="Inactive"),
	Preparing     UMETA(DisplayName="Preparing"),
	Spawning      UMETA(DisplayName="Spawning"),
	Clearing      UMETA(DisplayName="Clearing"),
	Boss          UMETA(DisplayName="Boss"),
	RoundComplete UMETA(DisplayName="RoundComplete")
};

/** Coherent snapshot of the current run objective for event-driven HUD updates. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiObjectiveState
{
	GENERATED_BODY()

	/** Objective presentation mode the HUD should render. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Objective")
	EAeyerjiObjectiveKind ObjectiveKind = EAeyerjiObjectiveKind::None;

	/** Current kill count for kill-target objectives. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Objective")
	int32 KilledCount = 0;

	/** Total kills required for kill-target objectives. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Objective")
	int32 TotalToKill = 0;

	/** Normalized progress in the range [0..1] for the active objective phase. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Objective")
	float Progress01 = 0.f;

	/** True once the primary kill-target phase is complete in combined objectives. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Objective")
	bool bPrimaryObjectiveComplete = false;

	/** True once the main run objective has been completed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Objective")
	bool bObjectiveComplete = false;

	/** True once the boss actor has actually spawned for the current encounter. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Objective")
	bool bBossSpawned = false;

	/** True only when every server-side field needed to render the objective coherently is valid. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Objective")
	bool bObjectiveReady = false;

	/** Monotonic revision used to force deterministic HUD refreshes across replication boundaries. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Objective")
	int32 ObjectiveRevision = 0;

	/** Stable boss identifier for client-side display-name lookup. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Objective")
	FName BossId = NAME_None;

	/** Optional text-formatting key so Blueprint can select the correct localized template. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Objective")
	FName ObjectiveTextKey = NAME_None;
};

/** Coherent snapshot of a survival-round mission for replicated HUD updates. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiSurvivalRoundState
{
	GENERATED_BODY()

	/** Current absolute round number, starting at 1. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival")
	int32 RoundNumber = 0;

	/** Current loop number, starting at 0 for rounds 1..BossEveryNRounds. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival")
	int32 CycleNumber = 0;

	/** Current server-authored survival phase. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival")
	EAeyerjiSurvivalRoundPhase Phase = EAeyerjiSurvivalRoundPhase::Inactive;

	/** Enemies killed in the current non-boss round. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival")
	int32 EnemiesKilled = 0;

	/** Enemies required to clear the current non-boss round. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival")
	int32 EnemiesRequired = 0;

	/** True when this state describes a boss round. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival")
	bool bBossRound = false;

	/** Optional UI key for transient round messages. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival")
	FName MessageKey = NAME_None;

	/** Monotonic revision used to force deterministic HUD refreshes across replication boundaries. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival")
	int32 Revision = 0;

	/** True only when survival data is active and safe for UI consumption. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival")
	bool bActive = false;
};
