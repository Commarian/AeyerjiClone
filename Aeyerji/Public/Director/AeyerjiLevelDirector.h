// Copyright (c) 2025 Aeyerji.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "AeyerjiLevelDirector.generated.h"

class AAeyerjiSpawnerGroup;

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

	/** Returns the accumulated real-time seconds while the run has been active. */
	UFUNCTION(BlueprintPure, Category="Director")
	float GetRunTimeSeconds() const { return AccumulatedRunSeconds; }

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

	/** Broadcast when the shard total changes; ideal for UI counters or audio stingers. */
	UPROPERTY(BlueprintAssignable, Category="Director|Events")
	FShardsChangedSignature OnShardsChanged;

	/** Broadcast when the run starts or stops so UI can show timers or overlays accordingly. */
	UPROPERTY(BlueprintAssignable, Category="Director|Events")
	FRunStateChangedSignature OnRunStateChanged;

protected:
	void BindSpawner(AAeyerjiSpawnerGroup* Spawner);
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

	FTimerHandle RunTimerHandle;
};
