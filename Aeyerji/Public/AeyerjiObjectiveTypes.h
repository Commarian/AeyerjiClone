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

/** Designer-facing round category consumed by the mission HUD. */
UENUM(BlueprintType)
enum class EAeyerjiSurvivalRoundType : uint8
{
	Normal UMETA(DisplayName="Normal"),
	Elite  UMETA(DisplayName="Elite"),
	Flying UMETA(DisplayName="Flying"),
	Boss   UMETA(DisplayName="Boss")
};

/** One server-validated repair purchase shown by the defense objective repair menu. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiDefenseRepairOption
{
	GENERATED_BODY()

	FAeyerjiDefenseRepairOption() = default;

	FAeyerjiDefenseRepairOption(
		const FName InOptionId,
		const FName InDisplayKey,
		const int64 InGoldCost,
		const float InFlatHeal,
		const float InPercentHeal)
		: OptionId(InOptionId)
		, DisplayKey(InDisplayKey)
		, GoldCost(InGoldCost)
		, FlatHeal(InFlatHeal)
		, PercentHeal(InPercentHeal)
	{
	}

	/** Stable option id passed back by UI when the player chooses this repair. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Defense Repair")
	FName OptionId = NAME_None;

	/** String-table key used for the repair option label. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Defense Repair")
	FName DisplayKey = NAME_None;

	/** Gold cost charged before the objective is healed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Defense Repair", meta=(ClampMin="0"))
	int64 GoldCost = 0;

	/** Flat HP restored by this option before percent healing is added. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Defense Repair", meta=(ClampMin="0.0"))
	float FlatHeal = 0.f;

	/** Fraction of max HP restored by this option. 0.15 means 15 percent of current max HP. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Defense Repair", meta=(ClampMin="0.0"))
	float PercentHeal = 0.f;
};

/** Run-scoped survival upgrade effect type selected between rounds. */
UENUM(BlueprintType)
enum class EAeyerjiSurvivalUpgradeType : uint8
{
	TreeMaxHP          UMETA(DisplayName="Tree Max HP"),
	TreeReflectDamage UMETA(DisplayName="Tree Reflect Damage"),
	TreeRegen          UMETA(DisplayName="Tree Regen"),
	PlayerXP           UMETA(DisplayName="Player XP")
};

/** One weighted option that can appear in the between-round survival upgrade offer. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiSurvivalUpgradeOption
{
	GENERATED_BODY()

	FAeyerjiSurvivalUpgradeOption() = default;

	FAeyerjiSurvivalUpgradeOption(
		const FName InOptionId,
		const EAeyerjiSurvivalUpgradeType InUpgradeType,
		const FName InDisplayKey,
		const FName InDescriptionKey,
		const float InBaseMagnitude,
		const float InWeight)
		: OptionId(InOptionId)
		, UpgradeType(InUpgradeType)
		, DisplayKey(InDisplayKey)
		, DescriptionKey(InDescriptionKey)
		, BaseMagnitude(InBaseMagnitude)
		, Weight(InWeight)
	{
	}

	/** Stable option id passed back by UI when the player chooses this upgrade. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival Upgrade")
	FName OptionId = NAME_None;

	/** Native upgrade behavior applied on the server. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival Upgrade")
	EAeyerjiSurvivalUpgradeType UpgradeType = EAeyerjiSurvivalUpgradeType::TreeMaxHP;

	/** 
	 * String-table key used for the option title (looked up via GlobalStringTable.csv / FText::FromStringTable).
	 * All keys must have corresponding rows in Source/Aeyerji/Data/Strings/GlobalStringTable.csv for localization.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival Upgrade")
	FName DisplayKey = NAME_None;

	/** 
	 * String-table key used for the option description (looked up via GlobalStringTable.csv).
	 * Must be present in Source/Aeyerji/Data/Strings/GlobalStringTable.csv.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival Upgrade")
	FName DescriptionKey = NAME_None;

	/** Single-player magnitude before multiplayer division by active player count. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival Upgrade")
	float BaseMagnitude = 0.f;

	/** Weighted random selection weight. Zero or less excludes this option from generated offers. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival Upgrade", meta=(ClampMin="0.0"))
	float Weight = 1.f;
};

/** Replicated between-round upgrade offer shown to every eligible local player.
 * Option display/description text is resolved on the client via DisplayKey/DescriptionKey
 * in GlobalStringTable.csv (see Data/Strings/GlobalStringTable.csv and FText::FromStringTable).
 * Title ("SurvivalUpgradeOfferTitle") and timeout template ("SurvivalUpgradeTimeout") are typically
 * looked up by fixed key on the Blueprint presentation side.
 */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiSurvivalUpgradeOfferState
{
	GENERATED_BODY()

	/** True while the server is waiting for upgrade selections. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival Upgrade")
	bool bActive = false;

	/** Survival round that produced this offer. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival Upgrade")
	int32 RoundNumber = 0;

	/** Monotonic version used by UI and server RPC validation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival Upgrade")
	int32 Revision = 0;

	/** Shared option list for every eligible player. Selection ownership is tracked server-side. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival Upgrade")
	TArray<FAeyerjiSurvivalUpgradeOption> Options;

	/** Seconds each player has to choose before missing selections default to option zero. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival Upgrade")
	float TimeoutSeconds = 0.f;

	/** Server world time when this offer expires. Clients use this for countdown presentation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival Upgrade")
	float OfferEndServerTimeSeconds = 0.f;

	/** Number of players that have already selected an option. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival Upgrade")
	int32 SelectedCount = 0;

	/** Number of eligible active players required before the round can continue immediately. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival Upgrade")
	int32 RequiredSelectionCount = 0;
};

/** Runtime targeting knobs used by survival defense objectives and AI StateTree conditions. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiDefenseTargetingSettings
{
	GENERATED_BODY()

	/** Applies this objective target handoff to cadence boss rounds as well as normal survival rounds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Defense Targeting")
	bool bApplyToBossRounds = true;

	/** Enemy-to-player distance that makes the enemy prefer a live player over the defense objective. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Defense Targeting", meta=(ClampMin="0.0", Units="cm"))
	float PlayerThreatAcquireRadius = 900.f;

	/** Larger distance used to avoid rapid target flipping once the enemy is already attacking a player. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Defense Targeting", meta=(ClampMin="0.0", Units="cm"))
	float PlayerThreatReleaseRadius = 1200.f;

	/** Player-to-objective distance that allows enemies to peel from the defense objective to a player. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Defense Targeting", meta=(ClampMin="0.0", Units="cm"))
	float PlayerThreatObjectiveAcquireRadius = 1600.f;

	/** Larger player-to-objective distance used before enemies give up chasing a player and return to the objective. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Defense Targeting", meta=(ClampMin="0.0", Units="cm"))
	float PlayerThreatObjectiveReleaseRadius = 2200.f;

	/** When true, enemies only peel to a player if that player is meaningfully closer than the objective. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Defense Targeting")
	bool bRequirePlayerCloserThanObjective = true;

	/** Required player distance advantage over the objective when bRequirePlayerCloserThanObjective is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Defense Targeting", meta=(ClampMin="0.0", Units="cm"))
	float PlayerDistanceBias = 100.f;
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

	/** Actual number of enemies defeated, independent from weighted progress value. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Objective")
	int32 EnemiesDefeated = 0;

	/** Current server-authoritative weighted Greater Rift progress. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Objective")
	int32 ProgressPoints = 0;

	/** Weighted points required to begin the boss phase. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Objective")
	int32 ProgressPointTarget = 0;

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

	/** Current zero-based completed loop count. HUDs usually want CycleDisplayNumber instead. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival")
	int32 CycleNumber = 0;

	/** Current one-based loop display number. Round 1 starts at cycle 1. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival")
	int32 CycleDisplayNumber = 1;

	/** One-based index within the authored round pattern/cadence. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival")
	int32 RoundPatternNumber = 0;

	/** Number of rounds in one cycle, including the boss cadence round. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival")
	int32 RoundPatternCount = 0;

	/** Boss cadence for this mission, usually 5. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival")
	int32 BossEveryNRounds = 0;

	/** Max round for finite missions. Zero means endless/infinite. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival")
	int32 MaxRoundNumber = 0;

	/** True when the mission loops after the boss instead of ending. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival")
	bool bEndless = false;

	/** Current server-authored survival phase. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival")
	EAeyerjiSurvivalRoundPhase Phase = EAeyerjiSurvivalRoundPhase::Inactive;

	/** Designer-authored category for HUD labels/icons. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival")
	EAeyerjiSurvivalRoundType RoundType = EAeyerjiSurvivalRoundType::Normal;

	/** Designer-authored round label for HUD text. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival")
	FText RoundDisplayLabel;

	/** One-based active wave number inside the current round. Zero means no wave is active yet. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival")
	int32 WaveNumber = 0;

	/** Number of waves in the current round. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival")
	int32 WaveCount = 0;

	/** Designer-authored label for the active wave. HUDs can use this instead of the generic round type text. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival")
	FText WaveDisplayLabel;

	/** Enemies killed in the active wave. This is the primary value for survival HUD progress. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival")
	int32 WaveEnemiesKilled = 0;

	/** Enemies required to clear the active wave. This is the primary value for survival HUD progress. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival")
	int32 WaveEnemiesRequired = 0;

	/** Enemies killed in the current non-boss round. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival")
	int32 EnemiesKilled = 0;

	/** Enemies required to clear the current non-boss round. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival")
	int32 EnemiesRequired = 0;

	/** True when this state describes a boss round. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival")
	bool bBossRound = false;

	/** True when a survival defense objective is configured and active for the run. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival|Defense")
	bool bDefenseObjectiveActive = false;

	/** True once the configured defense objective has been destroyed. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival|Defense")
	bool bDefenseObjectiveDestroyed = false;

	/** Current objective health for HUD presentation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival|Defense")
	float DefenseObjectiveHealth = 0.f;

	/** Max objective health for HUD presentation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival|Defense")
	float DefenseObjectiveHealthMax = 0.f;

	/** Normalized defense objective health in the range [0..1]. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival|Defense")
	float DefenseObjectiveProgress01 = 0.f;

	/** Actor tag used to resolve the current defense objective, useful for UI/debug lookup. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Survival|Defense")
	FName DefenseObjectiveActorTag = NAME_None;

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
