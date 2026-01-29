// Copyright (c) 2025 Aeyerji.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "AeyerjiLevelDirector.generated.h"

class AAeyerjiSpawnerGroup;
class AAeyerjiEncounterDirector;
class UAeyerjiLevelingComponent;
class UAeyerjiWorldSpawnProfile;

UENUM(BlueprintType)
enum class EAeyerjiLevelSpawnMode : uint8
{
	Sequence UMETA(DisplayName="Sequence"),
	FixedWorldPopulation UMETA(DisplayName="Fixed World Population")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FShardsChangedSignature, int32, NewCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FRunStateChangedSignature, bool, bIsRunning);

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

	/** Returns the current shard total for the run. */
	UFUNCTION(BlueprintPure, Category="Director")
	int32 GetShardCount() const { return ShardCount; }

	/** Difficulty slider the UI drives (0..1000). */
	UFUNCTION(BlueprintPure, Category="Director|Difficulty")
	float GetDifficultySlider() const { return DifficultySlider; }

	/** Normalized difficulty scale (0..1). */
	UFUNCTION(BlueprintPure, Category="Director|Difficulty")
	float GetDifficultyScale() const;

	/** Curved difficulty using pow(scale, DifficultyExponent). */
	UFUNCTION(BlueprintPure, Category="Director|Difficulty")
	float GetCurvedDifficulty() const;

	/** Snapshot the current player level (reads Level attribute from player 0 if available). */
	UFUNCTION(BlueprintPure, Category="Director|Difficulty")
	int32 GetCurrentPlayerLevel() const;

	/** When true, enemies are forced to match the current player level during scaling. */
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

	/** Responds to player level changes by resyncing enemy levels when enabled. */
	UFUNCTION()
	void HandlePlayerLevelUp(int32 OldLevel, int32 NewLevel);

public:
	/**
	 * Ordered list of encounter rooms for this director to manage.
	 * The director auto-advances through this array, triggering each spawner when the previous one clears.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Director")
	TArray<TObjectPtr<AAeyerjiSpawnerGroup>> SpawnerSequence;

	/**
	 * Optional reference to the boss encounter spawner.
	 * When set, the director defers activation until shards are collected or manually forced.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Director")
	TObjectPtr<AAeyerjiSpawnerGroup> BossSpawner = nullptr;

	/**
	 * Blocking volume, door mesh, or other actor that keeps the boss room sealed.
	 * Collision is re-enabled at the start of each run and disabled when the boss gate opens.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Director")
	TObjectPtr<AActor> BossGateActor = nullptr;

	/** Optional marker to dictate where the boss pawn should appear. Falls back to BossSpawner transform. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Director|Boss", meta=(EditCondition="bEnableNativeBossSpawn", EditConditionHides))
	TObjectPtr<AActor> BossSpawnMarker = nullptr;

	/** Pawn class to spawn for the boss encounter (must be a Pawn/AEnemyParentNative subclass). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Director|Boss", meta=(EditCondition="bEnableNativeBossSpawn", EditConditionHides))
	TSubclassOf<APawn> BossPawnClass;

	/** Enables the native SpawnBossEncounter body; leave false to drive boss spawning entirely from Blueprint. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Director|Boss", meta=(DisplayName="Use Native Boss Spawn"))
	bool bEnableNativeBossSpawn = false;

	/**
	 * Number of shards the player must collect before the boss gate unlocks.
	 * Shards are typically awarded by encounters via HandleSpawnerCleared or scripted rewards.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Director", meta=(ClampMin="1"))
	int32 ShardsNeeded = 3;

	/**
	 * When true, StartRun immediately activates the first spawner in the sequence that is not already cleared.
	 * Disable if you want to drive the first encounter via level scripting instead.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Director")
	bool bAutoStartFirstRoom = true;

	/** Selects between the classic sequential spawner flow and fixed world population mode. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Director|Spawning")
	EAeyerjiLevelSpawnMode SpawnMode = EAeyerjiLevelSpawnMode::Sequence;

	/** Spawn profile used when SpawnMode is FixedWorldPopulation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Director|Spawning", meta=(EditCondition="SpawnMode==EAeyerjiLevelSpawnMode::FixedWorldPopulation"))
	TObjectPtr<UAeyerjiWorldSpawnProfile> WorldSpawnProfile = nullptr;

	/** Optional spawner group used as the global spawn manager for fixed populations. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Director|Spawning", meta=(EditCondition="SpawnMode==EAeyerjiLevelSpawnMode::FixedWorldPopulation"))
	TObjectPtr<AAeyerjiSpawnerGroup> WorldPopulationSpawner = nullptr;

	/** When true, the boss gate opens when the fixed population is fully cleared. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Director|Spawning", meta=(EditCondition="SpawnMode==EAeyerjiLevelSpawnMode::FixedWorldPopulation"))
	bool bOpenBossGateOnFixedPopulationCleared = true;

	/** Designer-driven slider (0..1000) used to derive DifficultyScale. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Director|Difficulty", meta=(ClampMin="0.0", ClampMax="1000.0", UIMin="0.0", UIMax="1000.0"))
	float DifficultySlider = 0.f;

	/** Exponent for pow(DifficultyScale, DifficultyExponent); >1 backloads difficulty. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Director|Difficulty", meta=(ClampMin="0.1", AdvancedDisplay))
	float DifficultyExponent = 1.25f;

	/** When true, all spawned enemies are forced to the current player level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Director|Difficulty")
	bool bForceEnemyLevelToPlayerLevel = true;

	/** When true, existing enemies are resynced to the player level when a run starts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Director|Difficulty", meta=(EditCondition="bForceEnemyLevelToPlayerLevel"))
	bool bResyncEnemyLevelsOnRunStart = true;

	/** When true, enemy levels are updated whenever the player levels up. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Director|Difficulty", meta=(EditCondition="bForceEnemyLevelToPlayerLevel"))
	bool bResyncEnemyLevelsOnPlayerLevelUp = false;

	/** Broadcast when the shard total changes; ideal for UI counters or audio stingers. */
	UPROPERTY(BlueprintAssignable, Category="Director|Events")
	FShardsChangedSignature OnShardsChanged;

	/** Broadcast when the run starts or stops so UI can show timers or overlays accordingly. */
	UPROPERTY(BlueprintAssignable, Category="Director|Events")
	FRunStateChangedSignature OnRunStateChanged;

protected:
	void BindSpawner(AAeyerjiSpawnerGroup* Spawner);
	void BindEncounterDirector(AAeyerjiEncounterDirector* Director);
	/** Binds the player's leveling component so enemy level sync can react to level-ups. */
	void BindPlayerLevelingComponent();
	void TickRunTimer();

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

	UPROPERTY(VisibleAnywhere, Category="Director|State")
	int32 FixedPopulationClustersCleared = 0;

	TWeakObjectPtr<AAeyerjiEncounterDirector> CachedEncounterDirector;
	TWeakObjectPtr<UAeyerjiLevelingComponent> CachedPlayerLeveling;

	FTimerHandle RunTimerHandle;
};
