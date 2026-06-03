// Copyright (c) 2025 Aeyerji.
#pragma once

#include "CoreMinimal.h"
#include "AeyerjiObjectiveTypes.h"
#include "Director/AeyerjiEncounterDefinition.h"
#include "Engine/DataAsset.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameplayTagContainer.h"
#include "Inventory/AeyerjiInventoryBPFL.h"
#include "Systems/AeyerjiWorldStateTypes.h"
#include "Systems/LootService.h"
#include "AeyerjiLevelDirector.generated.h"

class AAeyerjiSpawnerGroup;
class AAeyerjiEncounterDirector;
class AAeyerjiEndRunPortal;
class AAeyerjiLinkedTeleporter;
class UAeyerjiEncounterDirectorDefinition;
class UAeyerjiLevelingComponent;
class UAeyerjiWorldSpawnProfile;

UENUM(BlueprintType)
enum class EAeyerjiLevelSpawnMode : uint8
{
	Sequence UMETA(DisplayName="Sequence"),
	FixedWorldPopulation UMETA(DisplayName="Fixed World Population"),
	SurvivalRounds UMETA(DisplayName="Survival Rounds")
};

UENUM(BlueprintType)
enum class EAeyerjiRunWinCondition : uint8
{
	BossCleared UMETA(DisplayName="BossCleared"),
	KillTarget  UMETA(DisplayName="KillTarget"),
	KillTargetThenBoss UMETA(DisplayName="KillTargetThenBoss")
};

UENUM(BlueprintType)
enum class EAeyerjiPersistentFactWriteTrigger : uint8
{
	BossDefeated UMETA(DisplayName="Boss Defeated"),
	ZoneCompleted UMETA(DisplayName="Zone Completed"),
	Unlock UMETA(DisplayName="Unlock")
};

USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiPersistentFactWrite
{
	GENERATED_BODY()

	/** Gameplay tag naming the persistent fact to write. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="World State")
	FGameplayTag StateTag;

	/** Optional fact instance id. Leave empty for one global fact per tag. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="World State")
	FName InstanceId = NAME_None;

	/** Uses GameState.ActiveZoneId as InstanceId when present. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="World State")
	bool bUseActiveZoneAsInstanceId = false;

	/** Optional owner id for character-scoped facts. Leave empty for global facts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="World State")
	FName OwnerId = NAME_None;

	/** Boolean value written for this milestone fact. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="World State")
	bool bValue = true;

	/** Persistent facts default to server-only; expose publicly only when clients need UI access. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="World State")
	EAeyerjiWorldStateReplication Replication = EAeyerjiWorldStateReplication::ServerOnly;

	/** Global facts describe world/run progress; character facts must set OwnerId. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="World State")
	EAeyerjiWorldStateScope Scope = EAeyerjiWorldStateScope::Global;
};

/**
 * Designer-owned boss definition. LevelDirector consumes this, while SpawnerGroup only executes registration/spawn tracking.
 */
UCLASS(BlueprintType)
class AEYERJI_API UAeyerjiBossDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss", meta=(DisplayName="Boss Enemy Class", ToolTip="Replaces the old LevelDirector Boss Enemy Class field. This is the pawn class used for this boss definition."))
	TSubclassOf<APawn> BossPawnClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss", meta=(ToolTip="When true, LevelDirector uses its native boss spawn fallback. Leave false when Blueprint owns the final spawn flow."))
	bool bEnableNativeBossSpawn = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Loot", meta=(ToolTip="Primary source tag used when this boss rolls loot. Usually Loot.Source.Boss. This should line up with a pool Source Tag in BP_AeyerjiLootTable."))
	FGameplayTag LootSourceTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Loot", meta=(ToolTip="When true, this boss should use BossMultiDropConfig for death/reward drops. When false, callers can do a single RollLoot from MakeBossLootContext."))
	bool bUseBossMultiDrop = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Loot", meta=(ToolTip="Optional pity bucket for this boss, for example Loot.Pity.BossUnique. Leave empty when this boss should use only generic pity."))
	FGameplayTag BossPityGroup;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Loot", meta=(ToolTip="Optional forced item for scripted rewards. Leave unset for normal table-driven boss loot."))
	TObjectPtr<UItemDefinition> BossForcedItemDefinition = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Loot", meta=(ClampMin="0.0", ClampMax="1.0", ToolTip="Base legendary chance before pity and rarity tables. Usually 0 when the central rarity table should drive this."))
	float BossBaseLegendaryChance = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Loot", meta=(ToolTip="Minimum rarity for this boss's rolls. Multi-drop buckets can raise this per bucket."))
	EItemRarity BossMinimumRarity = EItemRarity::Common;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Loot", meta=(ToolTip="Minimum rarity that counts as pity success for BossPityGroup."))
	EItemRarity BossPitySuccessRarity = EItemRarity::Legendary;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Loot|Advanced", meta=(ToolTip="Optional rarity weights used only when the central loot table does not provide weights."))
	TMap<EItemRarity, float> BossRarityWeights;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Loot|Advanced", meta=(ToolTip="Minimum item-level jitter around the runtime player level."))
	int32 BossItemLevelJitterMin = -2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Loot|Advanced", meta=(ToolTip="Maximum item-level jitter around the runtime player level."))
	int32 BossItemLevelJitterMax = 2;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Loot|Advanced", meta=(ToolTip="Override for named pity soft start. -1 uses LootService defaults."))
	int32 BossPitySoftStartOverride = -1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Loot|Advanced", meta=(ToolTip="Override for named pity chance added per miss. Negative uses LootService defaults."))
	float BossPitySoftSlopeOverride = -1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Loot|Advanced", meta=(ToolTip="Override for named hard pity. -1 uses LootService defaults."))
	int32 BossPityHardAttemptsOverride = -1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Loot|Advanced", meta=(ClampMin="-1.0", ClampMax="1.0", ToolTip="Override for named pity chance cap. Negative uses LootService defaults."))
	float BossPityMaxChanceOverride = -1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Loot", meta=(EditCondition="bUseBossMultiDrop", ToolTip="Replaces the old boss row MultiDropConfig from AllEnemyLootTable. Controls how many rolls happen, rarity buckets, uniqueness, and debug behavior."))
	FLootMultiDropConfig BossMultiDropConfig;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Loot", meta=(ToolTip="Controls whether the spawned loot is only for the killer/instigator or distributed to every player."))
	EItemDropDistributionMode BossDropMode = EItemDropDistributionMode::DropOnlyForInstigator;

	/** Builds the runtime loot context for this boss from designer-owned loot knobs plus live player/enemy/run values. */
	UFUNCTION(BlueprintPure, Category="Boss|Loot")
	FLootContext MakeBossLootContext(AActor* PlayerActor, int32 EnemyLevel, int32 PlayerLevel, int32 WorldTier, float DifficultyScale) const;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Actors", meta=(ToolTip="Actor tag for the spawner group that executes the boss spawn."))
	FName BossSpawnerActorTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Actors", meta=(ToolTip="Actor tag for the blocking door/gate actor opened when the boss should become available."))
	FName BossGateActorTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Actors", meta=(ToolTip="Actor tag for the optional marker where the boss pawn should spawn."))
	FName BossSpawnMarkerActorTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Actors", meta=(DisplayName="Boss Trigger Actor Tag", ToolTip="Replaces the old Blueprint Boss Spawn Instigator reference. Add this Actor tag to BP_BossTrigger if Blueprint needs to resolve or debug the trigger from the boss definition."))
	FName BossTriggerActorTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Teleporter")
	TSubclassOf<AAeyerjiLinkedTeleporter> BossLinkedTeleporterClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Teleporter")
	FName BossTeleporterEndpointATag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Teleporter")
	FName BossTeleporterEndpointBTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Teleporter")
	bool bUseBossTeleporterEndpointATransform = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Teleporter", meta=(EditCondition="bUseBossTeleporterEndpointATransform"))
	FTransform BossTeleporterEndpointATransform = FTransform::Identity;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Teleporter")
	bool bUseBossTeleporterEndpointBTransform = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Teleporter", meta=(EditCondition="bUseBossTeleporterEndpointBTransform"))
	FTransform BossTeleporterEndpointBTransform = FTransform(FRotator::ZeroRotator, FVector(600.f, 0.f, 0.f), FVector::OneVector);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Persistence", meta=(TitleProperty="StateTag"))
	TArray<FAeyerjiPersistentFactWrite> BossDefeatedFacts;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Persistence", meta=(TitleProperty="StateTag"))
	TArray<FAeyerjiPersistentFactWrite> UnlockFacts;
};

/** Designer-authored survival round. LevelDirector resolves these into runtime spawner waves. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiSurvivalRoundDefinition
{
	GENERATED_BODY()

	/** Optional label for editor/debugging and Blueprint UI. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival")
	FText DisplayLabel;

	/** Waves emitted by this round. Uses the same authoring rows as reusable encounter definitions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival", meta=(TitleProperty="WaveLabel"))
	TArray<FWaveDefData> Waves;

	/** Optional UI message key published when this round starts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival")
	FName RoundStartMessageKey = FName(TEXT("RoundStart"));

	/** Optional UI message key published when this round clears. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival")
	FName RoundClearMessageKey = FName(TEXT("RoundClear"));

	/** Optional UI message key published when this round is a boss round. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival")
	FName BossIncomingMessageKey = FName(TEXT("BossIncoming"));

	/** Extra per-round multiplier applied before cycle scaling. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival", meta=(ClampMin="0.0"))
	float EnemyCountMultiplier = 1.f;
};

/**
 * Designer-owned survival mission. A zone can reference this asset to run endless round loops without configuring placed actors.
 */
UCLASS(BlueprintType)
class AEYERJI_API UAeyerjiSurvivalMissionDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Authored round pattern. Usually five rounds, with the boss spawned after the fifth. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival", meta=(TitleProperty="DisplayLabel"))
	TArray<FAeyerjiSurvivalRoundDefinition> BaseRounds;

	/** Actor tag for the placed spawner group used to execute survival rounds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival")
	FName RoundSpawnerActorTag = NAME_None;

	/** Boss cadence for endless loops. Default means every fifth round is a boss round. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival", meta=(ClampMin="1"))
	int32 BossEveryNRounds = 5;

	/** Starts the next cycle after a boss kill instead of completing the run. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival")
	bool bLoopAfterBoss = true;

	/** Delay between normal rounds and after looped boss kills. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival", meta=(ClampMin="0.0", Units="s"))
	float InterRoundDelaySeconds = 3.f;

	/** Multiplier applied to enemy counts every completed cycle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival", meta=(ClampMin="0.0"))
	float EnemyCountScalePerCycle = 1.25f;

	/** Added to the player level used by enemy scaling for every completed cycle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival", meta=(ClampMin="0"))
	int32 EnemyLevelBonusPerCycle = 1;

	/** Optional boss override. If unset, ZoneRunDefinition.BossDefinition is used. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival")
	TObjectPtr<UAeyerjiBossDefinition> BossDefinitionOverride = nullptr;
};

/**
 * Designer-owned zone/run setup. Actor references are resolved by tags so streamed maps can keep data in assets.
 */
UCLASS(BlueprintType)
class AEYERJI_API UAeyerjiZoneRunDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Zone")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Zone")
	FName ZoneId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run")
	EAeyerjiLevelSpawnMode SpawnMode = EAeyerjiLevelSpawnMode::Sequence;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run")
	EAeyerjiRunWinCondition RunWinCondition = EAeyerjiRunWinCondition::BossCleared;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run", meta=(ClampMin="1"))
	int32 ShardsNeeded = 3;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run")
	bool bAutoStartFirstRoom = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run", meta=(ClampMin="0"))
	int32 ObjectiveKillTargetOverride = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run", meta=(ClampMin="0.0", Units="s"))
	float RunTimeLimitSeconds = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Difficulty")
	bool bResyncEnemyLevelsOnRunStart = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Difficulty")
	bool bResyncEnemyLevelsOnPlayerLevelUp = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning", meta=(ToolTip="Fixed-population profile for normal world enemies in this zone. This replaces the old LevelDirector World Spawn Profile field."))
	TObjectPtr<UAeyerjiWorldSpawnProfile> WorldSpawnProfile = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning", meta=(ToolTip="Pacing/dynamic encounter config consumed by the placed EncounterDirector. This replaces configuring EncounterDirector directly on the level actor."))
	TObjectPtr<UAeyerjiEncounterDirectorDefinition> EncounterDirectorDefinition = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning", meta=(ToolTip="Actor tag for the spawner group used as the fixed world population executor. Add this tag to the placed AeyerjiSpawnerGroup actor."))
	FName WorldPopulationSpawnerActorTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning", meta=(ToolTip="Actor tags for ordered room/sequence spawner groups. Empty is valid for Fixed World Population mode."))
	TArray<FName> SpawnerSequenceActorTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawning")
	bool bOpenBossGateOnFixedPopulationCleared = true;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival", meta=(ToolTip="Optional round-based mission owned by this zone. Set Spawn Mode to Survival Rounds to use it."))
	TObjectPtr<UAeyerjiSurvivalMissionDefinition> SurvivalMissionDefinition = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss", meta=(ToolTip="Boss content and boss-specific actor tags for this zone. Boss class lives inside this asset."))
	TObjectPtr<UAeyerjiBossDefinition> BossDefinition = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss", meta=(ToolTip="Optional zone override for BossDefinition.BossSpawnerActorTag. Prefer leaving this empty unless one boss asset is reused in multiple maps."))
	FName BossSpawnerActorTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss", meta=(ToolTip="Optional zone override for BossDefinition.BossGateActorTag. Prefer leaving this empty unless one boss asset is reused in multiple maps."))
	FName BossGateActorTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss", meta=(ToolTip="Optional zone override for BossDefinition.BossSpawnMarkerActorTag. Prefer leaving this empty unless one boss asset is reused in multiple maps."))
	FName BossSpawnMarkerActorTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss", meta=(ToolTip="Optional zone override for BossDefinition.BossTriggerActorTag. Prefer leaving this empty unless one boss asset is reused in multiple maps."))
	FName BossTriggerActorTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Extraction")
	TSubclassOf<AAeyerjiEndRunPortal> EndRunPortalClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Extraction")
	FName EndRunPortalSpawnPointTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Persistence", meta=(TitleProperty="StateTag"))
	TArray<FAeyerjiPersistentFactWrite> ZoneCompletedFacts;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Persistence", meta=(TitleProperty="StateTag"))
	TArray<FAeyerjiPersistentFactWrite> UnlockFacts;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FShardsChangedSignature, int32, NewCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRunStateChangedSignature, bool, bIsRunning);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FRunTimerExpiredSignature);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPrimaryObjectiveStateChangedSignature, bool, bIsComplete);

/**
 * Orchestrates encounter sequencing, shard tracking, and boss gate control for a level run.
 */
UCLASS(Blueprintable)
class AEYERJI_API AAeyerjiLevelDirector : public AActor
{
	GENERATED_BODY()

public:
	AAeyerjiLevelDirector();

	virtual void BeginPlay() override;

	/**
	 * Arms the level run: resets shard count, locks the boss gate, and starts the encounter timer.
	 * Optionally auto-activates the first uncleared spawner in the sequence for designer convenience.
	 */
	UFUNCTION(BlueprintCallable, Category="Director")
	void StartRun();

	/**
	 * Ends the active run and stops the timer.
	 * Call when the player leaves the level, dies to the boss, or the encounter flow should fully reset.
	 */
	UFUNCTION(BlueprintCallable, Category="Director")
	void EndRun();

	/**
	 * Grants one or more shards to the run progression.
	 * Broadcasts the shard change event and automatically opens the boss gate once the requirement is met.
	 */
	UFUNCTION(BlueprintCallable, Category="Director")
	void AddShard(int32 Amount = 1);

	/**
	 * Instantly teleports the player pawn back to the most recent checkpoint transform.
	 * Use after player death or when resetting the arena between attempts.
	 */
	UFUNCTION(BlueprintCallable, Category="Director")
	void RespawnAtCheckpoint();

	/** Forwarded from spawners when combat starts; primarily for audio/UI hooks. */
	UFUNCTION()
	void HandleSpawnerStarted(AAeyerjiSpawnerGroup* Spawner);

	/**
	 * Called whenever a bound spawner finishes its encounter.
	 * Advances the sequence, updates checkpoints, awards shards, and opens the boss when appropriate.
	 */
	UFUNCTION()
	void HandleSpawnerCleared(AAeyerjiSpawnerGroup* Spawner);

	/**
	 * Overwrites the respawn checkpoint with the provided transform.
	 * Designers can call this from level scripting to create mid-run respawn anchors.
	 */
	UFUNCTION(BlueprintCallable, Category="Director")
	void UpdateCheckpoint(const FTransform& NewCheckpoint);

	/**
	 * Unlocks the boss gate by disabling collision/visibility and optionally starts the boss encounter.
	 * Automatically called once the shard requirement is satisfied, but also callable manually for scripted events.
	 */
	UFUNCTION(BlueprintCallable, Category="Director")
	void OpenBossGate();

	/** Handles fixed population cluster clears when using fixed world spawn mode. */
	UFUNCTION()
	void HandleFixedClusterCleared(int32 ClusterId, float DensityAlpha, bool bDenseCluster);

	/** Handles fixed population completion when using fixed world spawn mode. */
	UFUNCTION()
	void HandleFixedPopulationCleared();

	/** Returns the accumulated real-time seconds while the run has been active. */
	UFUNCTION(BlueprintPure, Category="Director")
	float GetRunTimeSeconds() const { return AccumulatedRunSeconds; }

	/** Returns the configured run time still remaining (or 0 when there is no active time limit). */
	UFUNCTION(BlueprintPure, Category="Director")
	float GetRemainingRunTimeSeconds() const;

	/** Returns true when this level run is time-limited. */
	UFUNCTION(BlueprintPure, Category="Director")
	bool HasRunTimeLimit() const { return RunTimeLimitSeconds > 0.f; }

	/** Returns the current shard total for the run. */
	UFUNCTION(BlueprintPure, Category="Director")
	int32 GetShardCount() const { return ShardCount; }

	/** Difficulty slider the UI drives (0..1000). */
	UFUNCTION(BlueprintPure, Category="Director|Difficulty")
	float GetDifficultySlider() const { return DifficultySlider; }

	/** Legacy 0..1 difficulty alpha derived from the authoritative WorldTier curve. */
	UFUNCTION(BlueprintPure, Category="Director|Difficulty")
	float GetDifficultyScale() const;

	/** Deprecated wrapper kept for legacy systems that still ask for the old curved difficulty. */
	UFUNCTION(BlueprintPure, Category="Director|Difficulty")
	float GetCurvedDifficulty() const;

	/** Current win condition used by the GameState objective-complete flow. */
	UFUNCTION(BlueprintPure, Category="Director|Run")
	EAeyerjiRunWinCondition GetRunWinCondition() const { return RunWinCondition; }

	/** Returns true once the kill-target phase has been completed in combined run modes. */
	UFUNCTION(BlueprintPure, Category="Director|Run")
	bool IsPrimaryObjectiveComplete() const { return bPrimaryObjectiveComplete; }

	/** Returns true once boss progression has already been triggered for the current run. */
	UFUNCTION(BlueprintPure, Category="Director|Run")
	bool HasBossEncounterBeenTriggered() const;

	/** Returns the raw effective kill target used by gameplay code. */
	int32 GetEffectiveObjectiveKillTargetRaw() const;

	/** Returns the effective kill target used by kill-target run modes. Blueprint-facing version never returns 0 to avoid divide-by-zero UI paths. */
	UFUNCTION(BlueprintPure, Category="Director|Run")
	int32 GetEffectiveObjectiveKillTarget() const;

	/** Marks the primary kill-target phase complete and unlocks boss progression when required. */
	UFUNCTION(BlueprintCallable, Category="Director|Run")
	void MarkPrimaryObjectiveComplete();

	/** Clears the primary objective completion state for a fresh run. */
	UFUNCTION(BlueprintCallable, Category="Director|Run")
	void ResetPrimaryObjective();

	/** Snapshot the current player level (reads Level attribute from player 0 if available). */
	UFUNCTION(BlueprintPure, Category="Director|Difficulty")
	int32 GetCurrentPlayerLevel() const;

	/** Player level plus temporary survival cycle bonus used only by spawned enemy scaling. */
	UFUNCTION(BlueprintPure, Category="Director|Difficulty")
	int32 GetEnemyScalingPlayerLevel() const;

	/** Returns the authoritative world tier currently applied to this run. */
	UFUNCTION(BlueprintPure, Category="Director|Difficulty")
	int32 GetEffectiveWorldTier() const { return WorldTier; }

	/** Evaluates the globally tuned enemy level for the supplied player level. */
	UFUNCTION(BlueprintPure, Category="Director|Difficulty")
	int32 GetEffectiveEnemyLevelForPlayerLevel(int32 PlayerLevel) const;

	/** Evaluates the globally tuned enemy level for the current player level. */
	UFUNCTION(BlueprintPure, Category="Director|Difficulty")
	int32 GetEffectiveEnemyLevel() const;

	/** Returns the globally tuned stat-budget multiplier for the active world tier. */
	UFUNCTION(BlueprintPure, Category="Director|Difficulty")
	float GetGlobalStatBudgetMultiplier() const;

	/** Returns the globally tuned legacy alpha for systems that still consume 0..1 difficulty. */
	UFUNCTION(BlueprintPure, Category="Director|Difficulty")
	float GetDerivedDifficultyAlpha() const;

	/** Deprecated compatibility toggle retained so placed actors do not lose serialized data. */
	UFUNCTION(BlueprintPure, Category="Director|Difficulty")
	bool ShouldForceEnemyLevelToPlayerLevel() const { return bForceEnemyLevelToPlayerLevel; }

	/** Updates all enemies in the world to the current player level. */
	UFUNCTION(BlueprintCallable, Category="Director|Difficulty")
	void RefreshEnemyLevelsToCurrentPlayer();

	/**
	 * Entry point for triggering a boss encounter. Blueprint override is expected to own the flow; native body can be toggled via bEnableNativeBossSpawn.
	 * Returns the spawned pawn so Blueprint can customize/possess it if desired. When bEnableNativeBossSpawn is false (default) the native body does nothing.
	 */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category="Director|Boss")
	APawn* SpawnBossEncounter(AAeyerjiEncounterDirector* EncounterDirector = nullptr);
	virtual APawn* SpawnBossEncounter_Implementation(AAeyerjiEncounterDirector* EncounterDirector = nullptr);

	/** Assigns/overrides the boss spawn marker at runtime (Blueprint-friendly). Useful when a level sequence or trigger chooses the spawn location dynamically. */
	UFUNCTION(BlueprintCallable, Category="Director|Boss")
	void SetBossSpawnMarker(AActor* NewMarker);

	/** Spawns the configured linked teleporter for the boss encounter using the director's endpoint markers. */
	UFUNCTION(BlueprintCallable, Category="Director|Boss|Teleporter")
	AAeyerjiLinkedTeleporter* SpawnBossLinkedTeleporter();

	/** Removes the active boss linked teleporter, if one was spawned for this run. */
	UFUNCTION(BlueprintCallable, Category="Director|Boss|Teleporter")
	void ClearBossLinkedTeleporter();

	/** Responds to player level changes by resyncing enemy levels when enabled. */
	UFUNCTION()
	void HandlePlayerLevelUp(int32 OldLevel, int32 NewLevel);

	/** Returns the cached encounter director or finds/binds one in the world. */
	UFUNCTION(BlueprintCallable, Category="Director")
	AAeyerjiEncounterDirector* GetEncounterDirector();

	/** Re-applies player-dependent runtime bindings after a streamed gameplay zone becomes active. */
	UFUNCTION(BlueprintCallable, Category="Director")
	void HandleGameplayZoneActivated();

	/** Applies designer-owned run setup from ZoneRunDefinition onto this runtime director instance. */
	UFUNCTION(BlueprintCallable, Category="Director|Definition")
	void ApplyZoneRunDefinition();

	/** Writes configured persistent milestone facts for boss kills, completed zones, and unlocks. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Director|Persistence")
	void WritePersistentFactsForTrigger(EAeyerjiPersistentFactWriteTrigger Trigger);

	/** Compact ownership/debug string for console tools and temporary widgets. */
	UFUNCTION(BlueprintPure, Category="Director|Debug")
	FString GetRunDefinitionDebugString() const;

	/** Consumes a boss-defeated signal when survival rounds should loop instead of ending the run. */
	bool HandleSurvivalBossDefeated();

public:
	/** Designer-facing run setup. When assigned, BeginPlay copies its settings and resolves actor tags. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Director|Definition")
	TObjectPtr<UAeyerjiZoneRunDefinition> ZoneRunDefinition = nullptr;

	/** Applies ZoneRunDefinition during BeginPlay and streamed zone activation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Director|Definition")
	bool bApplyZoneRunDefinitionOnBeginPlay = true;

	/**
	 * Ordered list of encounter rooms for this director to manage.
	 * The director auto-advances through this array, triggering each spawner when the previous one clears.
	 */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	TArray<TObjectPtr<AAeyerjiSpawnerGroup>> SpawnerSequence;

	/**
	 * Optional reference to the boss encounter spawner.
	 * When set, the director defers activation until shards are collected or manually forced.
	 */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	TObjectPtr<AAeyerjiSpawnerGroup> BossSpawner = nullptr;

	/**
	 * Blocking volume, door mesh, or other actor that keeps the boss room sealed.
	 * Collision is re-enabled at the start of each run and disabled when the boss gate opens.
	 */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	TObjectPtr<AActor> BossGateActor = nullptr;

	/** Optional marker to dictate where the boss pawn should appear. Falls back to BossSpawner transform. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	TObjectPtr<AActor> BossSpawnMarker = nullptr;

	/** Optional boss trigger/instigator actor resolved from the zone or boss definition for Blueprint and debug use. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	TObjectPtr<AActor> BossTriggerActor = nullptr;

	/** Pawn class to spawn for the boss encounter (must be a Pawn/AEnemyParentNative subclass). */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	TSubclassOf<APawn> BossPawnClass;

	/** Enables the native SpawnBossEncounter body; leave false to drive boss spawning entirely from Blueprint. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	bool bEnableNativeBossSpawn = false;

	/** Optional linked teleporter class spawned when boss progression starts. Leave unset to disable this behavior. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	TSubclassOf<AAeyerjiLinkedTeleporter> BossLinkedTeleporterClass;

	/** Optional placed actor marker for endpoint A. Any actor type is valid, including TargetPoint or an empty actor. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	TObjectPtr<AActor> BossTeleporterEndpointA = nullptr;

	/** Optional placed actor marker for endpoint B. Any actor type is valid, including TargetPoint or an empty actor. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	TObjectPtr<AActor> BossTeleporterEndpointB = nullptr;

	/** Optional actor tag fallback for endpoint A when direct level actor references are unreliable. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	FName BossTeleporterEndpointATag = NAME_None;

	/** Optional actor tag fallback for endpoint B when direct level actor references are unreliable. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	FName BossTeleporterEndpointBTag = NAME_None;

	/** Uses the endpoint A transform below instead of the endpoint A actor reference. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	bool bUseBossTeleporterEndpointATransform = false;

	/** World-space fallback transform for endpoint A when actor picking is inconvenient. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	FTransform BossTeleporterEndpointATransform = FTransform::Identity;

	/** Uses the endpoint B transform below instead of the endpoint B actor reference. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	bool bUseBossTeleporterEndpointBTransform = false;

	/** World-space fallback transform for endpoint B when actor picking is inconvenient. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	FTransform BossTeleporterEndpointBTransform = FTransform(FRotator::ZeroRotator, FVector(600.f, 0.f, 0.f), FVector::OneVector);

	/**
	 * Number of shards the player must collect before the boss gate unlocks.
	 * Shards are typically awarded by encounters via HandleSpawnerCleared or scripted rewards.
	 */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	int32 ShardsNeeded = 3;

	/**
	 * When true, StartRun immediately activates the first spawner in the sequence that is not already cleared.
	 * Disable if you want to drive the first encounter via level scripting instead.
	 */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	bool bAutoStartFirstRoom = true;

	/** Selects between the classic sequential spawner flow and fixed world population mode. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	EAeyerjiLevelSpawnMode SpawnMode = EAeyerjiLevelSpawnMode::Sequence;

	/** Controls which objective source should end the run in this level. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	EAeyerjiRunWinCondition RunWinCondition = EAeyerjiRunWinCondition::BossCleared;

	/** Optional override for kill-target run modes. Set to 0 to use EncounterDirector's TotalToKill. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	int32 ObjectiveKillTargetOverride = 0;

	/** Spawn profile used when SpawnMode is FixedWorldPopulation. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	TObjectPtr<UAeyerjiWorldSpawnProfile> WorldSpawnProfile = nullptr;

	/** Optional spawner group used as the global spawn manager for fixed populations. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	TObjectPtr<AAeyerjiSpawnerGroup> WorldPopulationSpawner = nullptr;

	/** Survival mission resolved from the zone definition. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	TObjectPtr<UAeyerjiSurvivalMissionDefinition> SurvivalMissionDefinition = nullptr;

	/** Spawner group used as the executor for survival round waves. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	TObjectPtr<AAeyerjiSpawnerGroup> SurvivalRoundSpawner = nullptr;

	/** When true, the boss gate opens when the fixed population is fully cleared. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	bool bOpenBossGateOnFixedPopulationCleared = true;

	/** Legacy slider alias derived from the authoritative WorldTier. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	float DifficultySlider = 0.f;

	/** Optional time limit in seconds. Zero disables failure-by-time in native code. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	float RunTimeLimitSeconds = 0.f;

	/** Deprecated no-op kept only for backward compatibility with placed actors. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	float DifficultyExponent = 1.25f;

	/** Deprecated no-op kept only for backward compatibility with placed actors. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	bool bForceEnemyLevelToPlayerLevel = true;

	/** When true, existing enemies are resynced from the global level curve when a run starts. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	bool bResyncEnemyLevelsOnRunStart = true;

	/** When true, enemy levels are refreshed from the global level curve whenever the player levels up. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	bool bResyncEnemyLevelsOnPlayerLevelUp = false;

	/** Broadcast when the shard total changes; ideal for UI counters or audio stingers. */
	UPROPERTY(BlueprintAssignable, Category="Director|Events")
	FShardsChangedSignature OnShardsChanged;

	/** Broadcast when the run starts or stops so UI can show timers or overlays accordingly. */
	UPROPERTY(BlueprintAssignable, Category="Director|Events")
	FRunStateChangedSignature OnRunStateChanged;

	/** Broadcast once when the configured time limit expires. */
	UPROPERTY(BlueprintAssignable, Category="Director|Events")
	FRunTimerExpiredSignature OnRunTimerExpired;

	/** Broadcast when the primary kill-target objective state changes. */
	UPROPERTY(BlueprintAssignable, Category="Director|Events")
	FPrimaryObjectiveStateChangedSignature OnPrimaryObjectiveStateChanged;

	/** Native portal actor class spawned after a successful objective completion. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	TSubclassOf<AAeyerjiEndRunPortal> EndRunPortalClass;

	/** Optional actor used as the extraction portal spawn point. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	TObjectPtr<AActor> EndRunPortalSpawnPoint = nullptr;

protected:
	void BindSpawner(AAeyerjiSpawnerGroup* Spawner);
	void BindEncounterDirector(AAeyerjiEncounterDirector* Director);
	/** Returns the cached encounter director or finds/binds one in the world. */
	AAeyerjiEncounterDirector* GetOrFindEncounterDirector();

	/** Returns true when boss progression is blocked until the primary objective is complete. */
	bool IsBossSpawnBlockedByPrimaryObjective() const;
	/** Finds the first loaded actor with the supplied tag. */
	AActor* FindActorByTag(FName ActorTag) const;
	/** Finds and validates a spawner group by actor tag. */
	AAeyerjiSpawnerGroup* FindSpawnerByTag(FName ActorTag) const;
	/** Writes one set of persistent world-state facts on the authority. */
	void ApplyPersistentFactWrites(const TArray<FAeyerjiPersistentFactWrite>& FactWrites);
	/** Resolves the world transform used to spawn endpoint A for the boss linked teleporter. */
	FTransform GetBossTeleporterEndpointATransform() const;
	/** Resolves the optional world transform used to position endpoint B. */
	bool GetBossTeleporterEndpointBTransform(FTransform& OutTransform) const;
	/** Binds the player's leveling component so enemy level sync can react to level-ups. */
	void BindPlayerLevelingComponent();
	void TickRunTimer();
	void StartSurvivalRound(int32 RoundNumber);
	void StartNextSurvivalRound();
	void StartSurvivalBossRound(int32 RoundNumber);
	void PublishSurvivalRoundState(EAeyerjiSurvivalRoundPhase Phase, FName MessageKey = NAME_None);
	bool BuildRuntimeSurvivalWaves(const FAeyerjiSurvivalRoundDefinition& RoundDefinition, int32 CycleNumber, TArray<FWaveDefinition>& OutWaves, int32& OutEnemyCount) const;
	int32 GetSurvivalCycleForRound(int32 RoundNumber) const;
	bool IsSurvivalBossRound(int32 RoundNumber) const;

protected:
	UPROPERTY(VisibleAnywhere, Category="Director|State")
	bool bRunActive = false;

	UPROPERTY(VisibleAnywhere, Category="Director|State")
	int32 ShardCount = 0;

	UPROPERTY(VisibleAnywhere, Category="Director|State")
	int32 CurrentIndex = 0;

	UPROPERTY(VisibleAnywhere, Category="Director|State")
	FTransform Checkpoint;

	UPROPERTY(VisibleAnywhere, Category="Director|State")
	float AccumulatedRunSeconds = 0.f;

	/** Authoritative world tier snapshot captured from the game instance for this run. */
	UPROPERTY(VisibleAnywhere, Category="Director|State")
	int32 WorldTier = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|State")
	bool bPrimaryObjectiveComplete = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|State")
	bool bNativeBossSpawnIssued = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|State")
	bool bBossEncounterTriggered = false;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Director|Boss|Teleporter")
	TObjectPtr<AAeyerjiLinkedTeleporter> ActiveBossLinkedTeleporter = nullptr;

	UPROPERTY(VisibleAnywhere, Category="Director|State")
	int32 FixedPopulationClustersCleared = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|State")
	int32 CurrentSurvivalRound = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|State")
	int32 CurrentSurvivalCycle = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|State")
	int32 CurrentSurvivalRoundEnemyTotal = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|State")
	int32 SurvivalEnemyLevelBonus = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|State")
	bool bSurvivalBossRoundActive = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|State")
	bool bSurvivalBossDefeatHandled = false;

	TWeakObjectPtr<AAeyerjiEncounterDirector> CachedEncounterDirector;
	TWeakObjectPtr<UAeyerjiLevelingComponent> CachedPlayerLeveling;

	FTimerHandle RunTimerHandle;
	FTimerHandle SurvivalRoundDelayHandle;
};
