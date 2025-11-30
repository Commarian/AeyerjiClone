// Copyright (c) 2025 Aeyerji.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameplayTagContainer.h"
#include "Delegates/Delegate.h"
#include "AeyerjiSpawnerGroup.generated.h"

class UBoxComponent;
class UPrimitiveComponent;
class UAeyerjiGameplayEventSubsystem;
class AController;
class UAeyerjiEncounterDefinition;
class UNiagaraSystem;
class UGameplayEffect;
class UGameplayAbility;
struct FGameplayEventData;

USTRUCT(BlueprintType)
struct AEYERJI_API FEliteAffixDefinition
{
	GENERATED_BODY()

	/** Identifier/tag used at runtime; also pushed onto the ASC as a loose tag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elites")
	FGameplayTag AffixTag;

	/** Optional label for designers/UI. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elites")
	FText DisplayName;

	/** Multipliers applied on top of the base elite stats. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elites", meta=(ClampMin="0.1"))
	float HealthMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elites", meta=(ClampMin="0.1"))
	float DamageMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elites", meta=(ClampMin="0.1"))
	float RangeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elites", meta=(ClampMin="0.1"))
	float ScaleMultiplier = 1.0f;

	/** Optional GE to apply on spawn (server only). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elites")
	TSubclassOf<UGameplayEffect> GameplayEffect = nullptr;

	/** Optional abilities to grant to the spawned elite. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elites")
	TArray<TSubclassOf<UGameplayAbility>> GrantedAbilities;

	/** Optional VFX to visually communicate the affix. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elites")
	TObjectPtr<UNiagaraSystem> VFXSystem = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elites")
	FName VFXSocket = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Elites")
	FVector VFXOffset = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct AEYERJI_API FEnemySet
{
	GENERATED_BODY()

	/** Pawn/Character to spawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn")
	TSubclassOf<APawn> EnemyClass = nullptr;

	/** How many of this enemy to spawn in this set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(ClampMin="0"))
	int32 Count = 0;

	/** Time between spawns of this set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(ClampMin="0.0"))
	float SpawnInterval = 0.2f;

	/** Optional flag used for presentation/VFX (outline tint, scale, etc.). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn")
	bool bIsElite = false;

	/** Escalates elite tuning/FX; useful for rare mini bosses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(EditCondition="bIsElite"))
	bool bIsMiniBoss = false;

	/** Optional signature abilities granted only to this mini boss set. Falls back to the spawner defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(EditCondition="bIsMiniBoss"))
	TArray<TSubclassOf<UGameplayAbility>> MiniBossGrantedAbilities;

	/** Force these affixes for this set (Diablo-style combo elites). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(EditCondition="bIsElite"))
	TArray<FGameplayTag> ForcedEliteAffixes;

	/** If provided, limits random rolls to this pool; otherwise uses the spawner pool. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(EditCondition="bIsElite"))
	TArray<FGameplayTag> EliteAffixPoolOverride;

	/** Minimum number of random affixes to roll (in addition to forced ones). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(ClampMin="0", EditCondition="bIsElite"))
	int32 MinEliteAffixes = 0;

	/** Maximum number of random affixes to roll (in addition to forced ones). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(ClampMin="0", EditCondition="bIsElite"))
	int32 MaxEliteAffixes = 0;

	/** Optional stat overrides per set; leave at 0 to use the spawner defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(ClampMin="0.0", EditCondition="bIsElite"))
	float EliteHealthMultiplierOverride = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(ClampMin="0.0", EditCondition="bIsElite"))
	float EliteDamageMultiplierOverride = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(ClampMin="0.0", EditCondition="bIsElite"))
	float EliteRangeMultiplierOverride = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(ClampMin="0.0", EditCondition="bIsElite"))
	float EliteScaleMultiplierOverride = 0.f;

	/** Optional per-set XP reward multiplier for elites. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(ClampMin="0.0", EditCondition="bIsElite"))
	float EliteXPMultiplierOverride = 0.f;

	/** Optional per-set XP reward multiplier for mini bosses (applied on top of the elite mult). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(ClampMin="0.0", EditCondition="bIsElite"))
	float MiniBossXPMultiplierOverride = 0.f;
};

USTRUCT(BlueprintType)
struct AEYERJI_API FWaveDefinition
{
	GENERATED_BODY()

	/** Multiple enemy sets compose a wave. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wave")
	TArray<FEnemySet> EnemySets;

	/** Delay (seconds) after the wave is fully spawned before the next can begin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wave", meta=(ClampMin="0.0"))
	float PostSpawnDelay = 0.5f;
};

USTRUCT(BlueprintType)
struct AEYERJI_API FSpawnerAggroSettings
{
	GENERATED_BODY()

	/** Enables the aggro handoff; when false, spawned enemies behave as before. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aggro")
	bool bEnableAggro = true;

	/** Ensure each spawned pawn has a controller before issuing aggro commands. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aggro")
	bool bEnsureController = true;

	/** Sets focus on the instigating actor so perception/aim aligns immediately. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aggro")
	bool bSetFocusOnInstigator = true;

	/** Issues a MoveTo command toward the instigating actor after spawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aggro")
	bool bIssueMoveCommand = true;

	/** Acceptance radius for the MoveTo command when issuing MoveTo commands. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aggro", meta=(ClampMin="0.0", EditCondition="bIssueMoveCommand"))
	float MoveAcceptanceRadius = 150.f;
};

UENUM(BlueprintType)
enum class EAeyerjiSpawnPointMode : uint8
{
	Random UMETA(DisplayName="Random"),
	Sequential UMETA(DisplayName="Sequential"),
	Symmetrical UMETA(DisplayName="Symmetrical")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSpawnerClearedSignature, class AAeyerjiSpawnerGroup*, Spawner);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSpawnerStartedSignature, class AAeyerjiSpawnerGroup*, Spawner);

/**
 * Handles spawning and sequencing of a single encounter room.
 */
UCLASS(Blueprintable)
class AEYERJI_API AAeyerjiSpawnerGroup : public AActor
{
	GENERATED_BODY()

public:
	AAeyerjiSpawnerGroup();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * Arms this encounter and begins spawning the first wave (authority only).
	 * Optional instigator/controller arguments are cached so newly spawned enemies can immediately aggro that source.
	 * Safe for designers to call from level sequences, triggers, or the Level Director.
	 * Does nothing if the encounter has already been cleared or is currently active.
	 */
	UFUNCTION(BlueprintCallable, Category="Spawner")
	void ActivateEncounter(AActor* ActivationInstigator = nullptr, AController* ActivationController = nullptr);

	/**
	 * Returns the encounter to its idle setup state after a player wipe or level reset (authority only).
	 * Clears timers, re-opens combat doors, closes reward doors, and forgets wave progress.
	 */
	UFUNCTION(BlueprintCallable, Category="Spawner")
	void ResetEncounter();

	/** True once all waves are complete and no tracked enemies remain. */
	UFUNCTION(BlueprintPure, Category="Spawner")
	bool IsCleared() const { return bCleared; }

	/**
	 * When assigned, the encounter auto-starts when the player pawn overlaps this box.
	 * Leave null to start encounters manually from scripts or other world actors.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Activation")
	TObjectPtr<UBoxComponent> ActivationVolume;

	/**
	 * Optional gameplay event tag that will activate this encounter when broadcast through the gameplay event subsystem.
	 * Useful for hooking pickups, scripted moments, or Level Director triggers directly to this spawner.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Activation")
	FGameplayTag ActivationEventTag;

	/** Optional delay before the first wave begins spawning once activated. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Activation", meta=(ClampMin="0.0"))
	float InitialSpawnDelay = 0.f;

	/**
	 * Points used as spawn anchors for enemies in this encounter.
	 * If left empty, enemies materialize from the spawner actor's own transform.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Spawn")
	TArray<TObjectPtr<AActor>> SpawnPoints;

	/** Controls how SpawnPoints are iterated when emitting enemies. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Spawn")
	EAeyerjiSpawnPointMode SpawnPointMode = EAeyerjiSpawnPointMode::Random;

	/**
	 * Designer-authored wave data defining which enemy sets appear and in what order.
	 * The runtime system keeps an internal copy so editing in PIE does not mutate these values.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Waves")
	TArray<FWaveDefinition> Waves;

	/**
	 * Actors (blocking volumes, doors, etc.) that should lock the player inside while waves are active.
	 * They are enabled as soon as the encounter activates and disabled again during ResetEncounter.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Lockdown")
	TArray<TObjectPtr<AActor>> DoorsToClose;

	/**
	 * Doors, treasures, or exit blockers that should open once the encounter is cleared.
	 * These remain closed during idle and combat, then activate after the final enemy falls.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Lockdown")
	TArray<TObjectPtr<AActor>> DoorsToOpenOnClear;

	/** Fired the moment ActivateEncounter transitions the room into combat (useful for audio or VFX). */
	UPROPERTY(BlueprintAssignable, Category="Spawner|Events")
	FSpawnerStartedSignature OnEncounterStarted;

	/** Fired once all waves are spawned and every tracked enemy is destroyed. */
	UPROPERTY(BlueprintAssignable, Category="Spawner|Events")
	FSpawnerClearedSignature OnEncounterCleared;

	/** Returns the current number of alive enemies spawned by this encounter. */
	UFUNCTION(BlueprintPure, Category="Spawner")
	int32 GetLiveEnemyCount() const { return LiveEnemies; }

	/** Aggro behavior applied to freshly spawned enemies. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Aggro", meta=(ShowOnlyInnerProperties))
	FSpawnerAggroSettings AggroSettings;

	/** Optional reusable encounter asset. If set, this overrides the inline Waves at activation time. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Waves")
	TObjectPtr<UAeyerjiEncounterDefinition> EncounterDefinition = nullptr;

	/** When true, prefer data from EncounterDefinition; otherwise fall back to inline Waves unless they are empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Waves")
	bool bPreferEncounterAsset = true;

	/** Base stat bumps applied to any enemy sets flagged as elite (before affixes). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(ClampMin="0.1"))
	float EliteHealthMultiplier = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(ClampMin="0.1"))
	float EliteDamageMultiplier = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(ClampMin="0.1"))
	float EliteRangeMultiplier = 1.5f;

	/** Scale multiplier and FX applied to any enemy sets flagged as elite. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(ClampMin="0.1"))
	float EliteScaleMultiplier = 1.5f;

	/** Looping Niagara system used to visually distinguish elites (attach a glow, fire, etc.). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites")
	TObjectPtr<UNiagaraSystem> EliteVFXSystem = nullptr;

	/** Socket to attach the elite FX to (leave None to use the root). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites")
	FName EliteVFXSocket = NAME_None;

	/** Local offset for elite FX when attached. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites")
	FVector EliteVFXOffset = FVector::ZeroVector;

	/** Replicate the elite FX component so a dedicated server still shows the aura to clients. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites")
	bool bReplicateEliteVFX = true;

	/** Default min/max random affixes when a set asks for random rolls. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(ClampMin="0"))
	int32 DefaultEliteAffixMin = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(ClampMin="0"))
	int32 DefaultEliteAffixMax = 3;

	/** Global affix pool to pull Diablo-style modifiers from. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites")
	TArray<FEliteAffixDefinition> EliteAffixPool;

	/** Extra bumps applied when an elite is flagged as a mini boss. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(ClampMin="0.1"))
	float MiniBossScaleMultiplier = 2.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(ClampMin="0.1"))
	float MiniBossHealthMultiplier = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(ClampMin="0.1"))
	float MiniBossDamageMultiplier = 3.0f;

	/** Signature abilities auto-granted to any mini boss when no per-set overrides are provided. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites")
	TArray<TSubclassOf<UGameplayAbility>> DefaultMiniBossAbilities;

	/** XP reward multipliers for elites/mini bosses. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(ClampMin="0.0"))
	float EliteXPMultiplier = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(ClampMin="0.0"))
	float MiniBossXPMultiplier = 2.0f;

	/** Tags to mark elites/mini-bosses on the ASC if present. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites")
	FGameplayTag EliteGameplayTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites")
	FGameplayTag MiniBossGameplayTag;

	/** Actor tag added to spawned elites; configurable for StateTree queries. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites")
	FName EliteActorTag = TEXT("Elite");

	/** Actor tag added to spawned mini bosses; configurable for StateTree queries. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites")
	FName MiniBossActorTag = TEXT("MiniBoss");

protected:
	UFUNCTION()
	void HandleActivationOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	                             UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
	                             const FHitResult& SweepResult);

	/** Begins emitting spawns for the specified wave index. */
	void StartWave(int32 WaveIndex);

	/** Schedules the next pawn spawn for a given wave/set combination. */
	void ScheduleNextSpawn(int32 WaveIndex, int32 SetIndex, float DelaySeconds);

	/** Timer callback used to spawn one pawn from a specific set. */
	void HandleSpawnTimer(int32 WaveIndex, int32 SetIndex);

	/** Returns true when all sets in the specified wave have emitted every spawn request. */
	bool HaveAllSpawnsEmitted(int32 WaveIndex) const;

	/** Checks if the active wave can advance or finish the encounter entirely. */
	void CheckWaveCompletion();

	/** Handles the transition from active combat back to the cleared state. */
	void FinishEncounter();

	/** Starts the first wave after any configured initial spawn delay. */
	void KickoffFirstWave();

	/** Picks a location/orientation for the next enemy to appear. */
	FTransform ChooseSpawnTransform();

	/** Spawns one pawn from the provided wave/set definition and begins tracking it. */
	void SpawnOneFromSet(int32 WaveIndex, int32 SetIndex);

	/** Utilities for toggling door actors on activation/reset. */
	void SetDoorArrayEnabled(const TArray<TObjectPtr<AActor>>& Targets, bool bEnabled);

	/** Called whenever a tracked enemy is destroyed, keeping live counts accurate. */
	UFUNCTION()
	void OnEnemyDestroyed(AActor* DestroyedEnemy);

	/** Applies elite presentation (scale/VFX) when the enemy set is flagged as elite. */
	void ApplyElitePresentation(APawn* SpawnedPawn, float ScaleMultiplier, const TArray<const FEliteAffixDefinition*>& Affixes, bool bApplyScale = true);

	/** Multicast cosmetic-only RPC so dedicated servers can still show elite FX on clients. */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastApplyElitePresentation(APawn* SpawnedPawn, float ScaleMultiplier, const TArray<FGameplayTag>& AffixTags);

	/** Applies elite stat bumps to the pawn's attribute set (server only). */
	void ApplyEliteStats(APawn* SpawnedPawn, float HealthMultiplier, float DamageMultiplier, float RangeMultiplier);
	/** Pushes gameplay tags, abilities, and affix-driven effects. */
	void ApplyEliteGameplay(APawn* SpawnedPawn, const FEnemySet& EnemySet, const TArray<const FEliteAffixDefinition*>& Affixes);
	/** Rolls affixes for a given elite set (forced + random). */
	TArray<const FEliteAffixDefinition*> BuildEliteAffixLoadout(const FEnemySet& EnemySet) const;
	const FEliteAffixDefinition* FindAffixDefinition(const FGameplayTag& Tag) const;
	float ComputeEliteScale(const FEnemySet& EnemySet, const TArray<const FEliteAffixDefinition*>& Affixes) const;
	void ApplyAffixVFX(APawn* SpawnedPawn, const FEliteAffixDefinition& Affix);

protected:
	/** Callback for gameplay events used to trigger encounter activation. */
	void HandleActivationEvent(const FGameplayTag& EventTag, const FGameplayEventData& Payload);
	void CacheActivationStimulus(AActor* InstigatorActor, AController* InstigatorController);
	AActor* ResolveAggroTargetActor() const;
	APawn* ResolveAggroTargetPawn() const;
	AController* ResolveAggroController() const;
	void ApplyAggroToSpawnedPawn(APawn* SpawnedPawn);
	void ClearAggroCache();
	void RebuildSpawnPointOrder();
	int32 GetNextSpawnPointIndex();
	void ResetSpawnPointCycle();

	UPROPERTY(VisibleAnywhere, Category="Spawner|State")
	bool bActive = false;

	UPROPERTY(VisibleAnywhere, Category="Spawner|State")
	bool bCleared = false;

	UPROPERTY(VisibleAnywhere, Category="Spawner|State")
	int32 CurrentWaveIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, Category="Spawner|State")
	int32 LiveEnemies = 0;

	/** Remaining spawn counts for each wave/set (runtime state). */
	TArray<TArray<int32>> PendingSpawnCounts;

	/** Timer handles for in-flight spawn timers per wave/set. */
	TArray<TArray<FTimerHandle>> SpawnTimerHandles;

	/** Delay timer between waves. */
	FTimerHandle WaveDelayHandle;

	/** Handle stored so we can unregister the gameplay event listener when the spawner ends play. */
	FDelegateHandle ActivationEventHandle;

	/** Timer handle controlling any configured initial spawn delay. */
	FTimerHandle InitialSpawnDelayHandle;

	/** Cached activation information used to drive aggro behavior. */
	TWeakObjectPtr<AActor> CachedAggroActor;
	TWeakObjectPtr<AController> CachedAggroController;

	/** Runtime copy of waves (from inline authoring or encounter definition). */
	TArray<FWaveDefinition> EncounterWavesRuntime;

	/** Spawn point iteration cache for sequential/symmetrical patterns. */
	TArray<int32> SpawnPointOrder;
	int32 SpawnPointCursor = 0;
};
