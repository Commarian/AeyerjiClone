// Copyright (c) 2025 Aeyerji.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Controller.h"
#include "GameplayAbilitySpec.h"
#include "GameplayEffectTypes.h"
#include "GameplayTagContainer.h"
#include "Delegates/Delegate.h"
#include "UObject/ObjectKey.h"
#include "AeyerjiObjectiveTypes.h"
#include "Enemy/EnemyScalingTable.h"
#include "AeyerjiSpawnerGroup.generated.h"

class UBoxComponent;
class UPrimitiveComponent;
class AAeyerjiEncounterDirector;
class AAeyerjiLevelDirector;
class AAeyerjiSpawnRegion;
class UAeyerjiGameplayEventSubsystem;
class AController;
class APawn;
class UAeyerjiEncounterDefinition;
class UNiagaraComponent;
class UNiagaraSystem;
class UGameplayEffect;
class UGameplayAbility;
class UAbilitySystemComponent;
class APlayerStart;
class UDataTable;
struct FStreamableHandle;
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

	/** Weighted Greater Rift progress granted only when a registered enemy actually dies. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn|Rift", meta=(ClampMin="1"))
	int32 ProgressPoints = 1;

	/** Time between spawns of this set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(ClampMin="0.0"))
	float SpawnInterval = 0.2f;

	/** Optional flag used for presentation/VFX (outline tint, scale, etc.). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn")
	bool bIsElite = false;

	/** Skips numeric elite/affix stat and scale multipliers while retaining elite tags,
	 * affix gameplay, VFX, and XP; reserve for fully pre-tuned special classes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(EditCondition="bIsElite", EditConditionHides, AdvancedDisplay))
	bool bSkipEliteAutoScaling = false;

	/** Optional elite class pool used when this set is promoted to elite (or authored as elite). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(AdvancedDisplay))
	TArray<TSubclassOf<APawn>> EliteEnemyClassPoolOverride;

	/** Escalates elite tuning/FX; useful for rare mini bosses. (Note: only supported via RegisterExternalEnemy, not via wave data.) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(EditCondition="bIsElite", EditConditionHides))
	bool bIsMiniBoss = false;

	/** Marks this enemy set as a boss (stronger than mini boss); supported only for manual registration, not wave spawning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(EditCondition="bIsElite", EditConditionHides))
	bool bIsBoss = false;

	/** Optional signature abilities granted only to this mini boss set. Falls back to the spawner defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(EditCondition="bIsMiniBoss", EditConditionHides, AdvancedDisplay))
	TArray<TSubclassOf<UGameplayAbility>> MiniBossGrantedAbilities;

	/** Optional signature abilities granted only to this boss set. Falls back to the spawner defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(EditCondition="bIsBoss", EditConditionHides, AdvancedDisplay))
	TArray<TSubclassOf<UGameplayAbility>> BossGrantedAbilities;

	/** Force these affixes for this set (Diablo-style combo elites). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(EditCondition="bIsElite", EditConditionHides, AdvancedDisplay))
	TArray<FGameplayTag> ForcedEliteAffixes;

	/** If provided, limits random rolls to this pool; otherwise uses the spawner pool. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(EditCondition="bIsElite", EditConditionHides, AdvancedDisplay))
	TArray<FGameplayTag> EliteAffixPoolOverride;

	/** Minimum number of random affixes to roll (in addition to forced ones). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(ClampMin="0", EditCondition="bIsElite", EditConditionHides, AdvancedDisplay))
	int32 MinEliteAffixes = 0;

	/** Maximum number of random affixes to roll (in addition to forced ones). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(ClampMin="0", EditCondition="bIsElite", EditConditionHides, AdvancedDisplay))
	int32 MaxEliteAffixes = 0;

	/** Optional stat overrides per set; leave at 0 to use the spawner defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(ClampMin="0.0", EditCondition="bIsElite", EditConditionHides, AdvancedDisplay))
	float EliteHealthMultiplierOverride = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(ClampMin="0.0", EditCondition="bIsElite", EditConditionHides, AdvancedDisplay))
	float EliteDamageMultiplierOverride = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(ClampMin="0.0", EditCondition="bIsElite", EditConditionHides, AdvancedDisplay))
	float EliteRangeMultiplierOverride = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(ClampMin="0.0", EditCondition="bIsElite", EditConditionHides, AdvancedDisplay))
	float EliteScaleMultiplierOverride = 0.f;

	/** Optional per-set XP reward multiplier for elites. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(ClampMin="0.0", EditCondition="bIsElite", EditConditionHides, AdvancedDisplay))
	float EliteXPMultiplierOverride = 0.f;

	/** Optional per-set XP reward multiplier for mini bosses (applied on top of the elite mult). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(ClampMin="0.0", EditCondition="bIsElite", EditConditionHides, AdvancedDisplay))
	float MiniBossXPMultiplierOverride = 0.f;

	/** Archetype tag used to look up scaling data in the shared EnemyScaling table. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn")
	FGameplayTag EnemyArchetypeTag;
};

USTRUCT(BlueprintType)
struct AEYERJI_API FWaveDefinition
{
	GENERATED_BODY()

	/** Optional label to keep the wave list readable in the details panel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wave")
	FText WaveLabel;

	/** Multiple enemy sets compose a wave. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Wave", meta=(TitleProperty="EnemyClass"))
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

	/** Reissues focus/MoveTo commands while an encounter is active so survival enemies keep chasing their target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aggro", meta=(EditCondition="bEnableAggro"))
	bool bReissueAggroWhileActive = true;

	/** Seconds between repeated aggro commands. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aggro", meta=(ClampMin="0.1", EditCondition="bEnableAggro && bReissueAggroWhileActive", Units="s"))
	float ReissueAggroIntervalSeconds = 10.f;
};

USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiEnemyPoolSettings
{
	GENERATED_BODY()

	/** Enables spawner-owned pooling for enemies spawned or registered through this spawner. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pooling")
	bool bEnablePooling = false;

	/** Maximum inactive actors kept per class/archetype/elite key before overflow actors are destroyed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pooling", meta=(ClampMin="0"))
	int32 MaxInactivePerPoolKey = 12;

	/** Prewarms configured enemy sets while world-flow loading is active. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pooling")
	bool bPrewarmDuringWorldFlowLoading = true;

	/** Maximum number of fresh inactive actors created by one prewarm call. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pooling", meta=(ClampMin="1"))
	int32 PrewarmPerTick = 4;

	/** Relative location from this spawner where inactive pooled enemies are hidden and parked. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pooling")
	FVector PoolParkingOffset = FVector(0.f, 0.f, -5000.f);
};

USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiEnemyPoolKey
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Pooling")
	TSubclassOf<APawn> EnemyClass = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Pooling")
	FGameplayTag EnemyArchetypeTag;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Pooling")
	bool bIsElite = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Pooling")
	bool bIsMiniBoss = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Pooling")
	bool bIsBoss = false;

	bool operator==(const FAeyerjiEnemyPoolKey& Other) const
	{
		return EnemyClass == Other.EnemyClass
			&& EnemyArchetypeTag == Other.EnemyArchetypeTag
			&& bIsElite == Other.bIsElite
			&& bIsMiniBoss == Other.bIsMiniBoss
			&& bIsBoss == Other.bIsBoss;
	}
};

FORCEINLINE uint32 GetTypeHash(const FAeyerjiEnemyPoolKey& Key)
{
	uint32 Hash = GetTypeHash(Key.EnemyClass.Get());
	Hash = HashCombine(Hash, GetTypeHash(Key.EnemyArchetypeTag));
	Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Key.bIsElite)));
	Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Key.bIsMiniBoss)));
	Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Key.bIsBoss)));
	return Hash;
}

UENUM(BlueprintType)
enum class EAeyerjiSpawnPointMode : uint8
{
	Random UMETA(DisplayName="Random"),
	Sequential UMETA(DisplayName="Sequential"),
	Symmetrical UMETA(DisplayName="Symmetrical")
};

USTRUCT()
struct AEYERJI_API FCachedAeyerjiSpawnRegion
{
	GENERATED_BODY()

	/** Region actor sampled for survival/world spawn placement while this cache is valid. */
	UPROPERTY()
	TObjectPtr<AAeyerjiSpawnRegion> Region = nullptr;

	/** Cached world bounds so each spawn does not ask the region actor to recompute them. */
	FBox Bounds;

	/** Authored region weight; values <= 0 are excluded when the cache is built. */
	float Weight = 0.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSpawnerClearedSignature, class AAeyerjiSpawnerGroup*, Spawner);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSpawnerStartedSignature, class AAeyerjiSpawnerGroup*, Spawner);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSpawnerBossDefeatedSignature, class AAeyerjiSpawnerGroup*, Spawner, AActor*, BossEnemy);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSpawnerTrackedEnemiesRemovedSignature, class AAeyerjiSpawnerGroup*, Spawner, int32, RemovedCount);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FSpawnerWaveStartedSignature, class AAeyerjiSpawnerGroup*, Spawner, int32, WaveIndex);

/**
 * Mechanical spawn executor for one encounter room.
 * Run selection, boss plans, dynamic pacing, and zone ownership belong to the Level/Encounter directors and their definition assets.
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
	 * Arms this encounter with caller-provided runtime waves instead of the placed actor/asset waves.
	 * Used by mission directors that generate round content while keeping this actor as a spawn executor.
	 */
	UFUNCTION(BlueprintCallable, Category="Spawner")
	void ActivateEncounterWithRuntimeWaves(const TArray<FWaveDefinition>& RuntimeWaves, AActor* ActivationInstigator = nullptr, AController* ActivationController = nullptr);

	/**
	 * Returns the encounter to its idle setup state after a player wipe or level reset (authority only).
	 * Clears timers, re-opens combat doors, closes reward doors, and forgets wave progress.
	 */
	UFUNCTION(BlueprintCallable, Category="Spawner")
	void ResetEncounter();

	/**
	 * Registers an externally spawned pawn with this encounter so LiveEnemies and completion flow stay accurate.
	 * Useful when spawning bosses/mini-bosses via scripts or abilities instead of the built-in wave system.
	 * Optional flags control elite/boss application, aggro handoff, and whether to auto-activate (with a guard to avoid wave-based spawns).
	 * - bApplyEliteSettings: apply elite/mini/boss tags, abilities, stats, and XP bumps.
	 * - bApplyAggro: push cached instigator/controller to the pawn (focus/move if enabled).
	 * - bAutoActivate: arm the encounter so doors/events work.
	 * - bAutoActivateOnlyIfNoWaves: when true, auto-activate only if there is no wave data, preventing accidental double-spawns.
	 * - bSkipRandomEliteResolution: when true, skips random elite promotion and honors the template as-authored.
	 */
	UFUNCTION(BlueprintCallable, Category="Spawner")
	void RegisterExternalEnemy(APawn* SpawnedPawn,
	                           const FEnemySet& EnemyTemplate,
	                           bool bApplyEliteSettings = true,
	                           bool bApplyAggro = true,
	                           bool bAutoActivate = true,
	                           bool bAutoActivateOnlyIfNoWaves = true,
	                           AActor* ActivationInstigator = nullptr,
	                           AController* ActivationController = nullptr,
	                           bool bSkipRandomEliteResolution = false);

	/**
	 * Spawns or reuses an enemy actor from this spawner, then registers it for scaling, elite setup, aggro, and progress.
	 * This is the pooled path used by BPFL when a valid spawner is supplied.
	 */
	UFUNCTION(BlueprintCallable, Category="Spawner|Pooling", meta=(AutoCreateRefTerm="SpawnTransform"))
	APawn* SpawnRegisteredEnemyFromSet(const FEnemySet& EnemySet,
	                                    const FTransform& SpawnTransform,
	                                    AActor* SpawnOwner = nullptr,
	                                    APawn* InstigatorPawn = nullptr,
	                                    bool bApplyEliteSettings = true,
	                                    bool bApplyAggro = true,
	                                    bool bAutoActivate = true,
	                                    bool bAutoActivateOnlyIfNoWaves = true,
	                                    AActor* ActivationInstigator = nullptr,
	                                    AController* ActivationController = nullptr,
	                                    bool bSkipRandomEliteResolution = false);

	/** Returns a dead pooled enemy to its inactive hidden state; non-pooled enemies return false and should be destroyed. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Spawner|Pooling")
	bool ReturnEnemyToPool(APawn* EnemyPawn);

	/** Destroys inactive pooled actors and clears runtime bookkeeping. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Spawner|Pooling")
	void ReleaseEnemyPool(bool bDestroyInactiveEnemies = true);

	/** Returns the total number of inactive enemies currently retained by this spawner. */
	UFUNCTION(BlueprintPure, Category="Spawner|Pooling")
	int32 GetInactivePooledEnemyCount() const;

	/** Pre-creates inactive pooled enemies for the supplied sets, respecting MaxInactivePerPoolKey and PrewarmPerTick. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Spawner|Pooling")
	void PrewarmPoolForEnemySets(const TArray<FEnemySet>& EnemySets, int32 DesiredCountPerSet = 1);

	/**
	 * Pre-creates one exact planned enemy without elite re-resolution.
	 * EncounterDirector calls this incrementally during world-flow loading so the frozen
	 * Rift plan and the pool contain identical class/archetype/elite keys.
	 */
	bool PrewarmExactEnemy(const FEnemySet& ExactEnemySet);

	/** Begins a full-run prewarm and expands the per-key retention ceiling for the frozen plan. */
	void BeginExactPoolPrewarm(int32 PlannedPopulation);

	/** Ends temporary loading relevancy and marks later pool misses as emergency runtime construction. */
	void FinalizeExactPoolPrewarm();

	int32 GetFreshEnemyConstructionCount() const { return FreshEnemyConstructionCount; }
	int32 GetPooledCheckoutCount() const { return PooledCheckoutCount; }
	int32 GetPrewarmConstructionCount() const { return PrewarmConstructionCount; }
	int32 GetEmergencyRuntimeSpawnCount() const { return EmergencyRuntimeSpawnCount; }
	int32 GetExactPoolKeyCapacity(const FAeyerjiEnemyPoolKey& PoolKey) const
	{
		return ExactPoolKeyCapacities.FindRef(PoolKey);
	}

	/** True once all waves are complete and no tracked enemies remain. */
	UFUNCTION(BlueprintPure, Category="Spawner")
	bool IsCleared() const { return bCleared; }

	/** True while this encounter is currently active and has not yet cleared. */
	UFUNCTION(BlueprintPure, Category="Spawner")
	bool IsActive() const { return bActive; }

#if WITH_DEV_AUTOMATION_TESTS
	/** Number of accepted inactive-to-active transitions; used to prove simultaneous overlaps dedupe. */
	int32 GetAutomationActivationCount() const { return AutomationActivationCount; }

#endif

	/** Configures this common executor for server-owned Rift pooling without global forced pursuit. */
	void ConfigureAsRiftPopulationExecutor(AAeyerjiLevelDirector* InLevelDirector);

	/** Assigns the defendable survival target pushed to spawned AI controllers. */
	UFUNCTION(BlueprintCallable, Category="Spawner|Aggro")
	void ConfigureDefenseObjectiveTarget(AActor* ObjectiveActor, const FAeyerjiDefenseTargetingSettings& TargetingSettings);

	/** Clears the defendable survival target from this spawner and tracked enemies. */
	UFUNCTION(BlueprintCallable, Category="Spawner|Aggro")
	void ClearDefenseObjectiveTarget();

	/** Returns the current defendable survival target, if one is assigned. */
	UFUNCTION(BlueprintPure, Category="Spawner|Aggro")
	AActor* GetDefenseObjectiveTarget() const { return DefenseObjectiveTargetActor.Get(); }

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
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Activation", meta=(AdvancedDisplay))
	FGameplayTag ActivationEventTag;

	/** When true, overlap activation is disabled and the volume is kept non-colliding. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Activation", meta=(AdvancedDisplay))
	bool bDisableActivationVolume = false;

	/** When true, the activation gameplay event will be ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Activation", meta=(AdvancedDisplay))
	bool bDisableActivationEvent = false;

	/**
	 * Allows ActivateEncounter to start even when no Waves/EncounterDefinition are provided; manual spawns must be registered via RegisterExternalEnemy.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Activation", meta=(AdvancedDisplay))
	bool bAllowManualActivationWithoutWaves = true;

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

	/** Reject spawn candidates that cannot build a nav path to the current aggro/player location. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Spawn|Reachability")
	bool bRequireSpawnReachableFromTarget = true;

	/** If no aggro/player actor exists yet, use the first PlayerStart as the reachability reference. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Spawn|Reachability", meta=(EditCondition="bRequireSpawnReachableFromTarget"))
	bool bFallbackReachabilityTargetToPlayerStart = true;

	/** Closest allowed spawn distance from the reachability target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Spawn|Reachability", meta=(ClampMin="0.0", Units="cm"))
	float SpawnMinDistanceFromTarget = 1200.f;

	/** Farthest allowed spawn distance from the reachability target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Spawn|Reachability", meta=(ClampMin="0.0", Units="cm"))
	float SpawnMaxDistanceFromTarget = 10000.f;

	/** Number of region samples tried before falling back to a random reachable point near the target. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Spawn|Reachability", meta=(ClampMin="1"))
	int32 SpawnRegionSearchAttempts = 48;

	/** Nav projection extent used for region-based survival spawn points. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Spawn|Reachability", meta=(ClampMin="0.0", Units="cm"))
	FVector SpawnNavProjectionExtent = FVector(1200.f, 1200.f, 2000.f);

	/** Height above a spawn region used when tracing down to find real ground. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Spawn|Reachability", meta=(ClampMin="0.0", Units="cm"))
	float SpawnGroundTraceUpOffset = 500.f;

	/** Distance below a spawn region searched by the ground trace. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Spawn|Reachability", meta=(ClampMin="0.0", Units="cm"))
	float SpawnGroundTraceDownDistance = 5000.f;

	/** Extra vertical clearance added on top of the spawned pawn capsule half-height. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Spawn|Reachability", meta=(ClampMin="0.0", Units="cm"))
	float SpawnGroundClearance = 5.f;

	/** Adds the configured cull-ignore actor tag to spawned wave enemies. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Spawn|Reachability")
	bool bTagSpawnedEnemiesAsCullIgnored = true;

	/** Actor tag used by the view culling component to leave combat spawns alone. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Spawn|Reachability", meta=(EditCondition="bTagSpawnedEnemiesAsCullIgnored"))
	FName SpawnedEnemyCullIgnoreActorTag = FName(TEXT("ViewCull.Ignore"));

	/** Optional reusable encounter asset. If set, this overrides the inline Waves at activation time. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Waves", meta=(EditCondition="bPreferEncounterAsset", DisplayName="Encounter Definition Asset"))
	TObjectPtr<UAeyerjiEncounterDefinition> EncounterDefinition = nullptr;

	/** When true, prefer data from EncounterDefinition; otherwise fall back to inline Waves unless they are empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Waves", meta=(DisplayName="Use Encounter Definition Asset"))
	bool bPreferEncounterAsset = true;

	/**
	 * Designer-authored wave data defining which enemy sets appear and in what order.
	 * The runtime system keeps an internal copy so editing in PIE does not mutate these values.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Waves", meta=(EditCondition="!bPreferEncounterAsset", EditConditionHides, TitleProperty="WaveLabel"))
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

	/** When true, door open/close operations are skipped (useful for global spawn managers). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Lockdown", meta=(AdvancedDisplay))
	bool bSuppressDoorControl = false;

	/** Fired the moment ActivateEncounter transitions the room into combat (useful for audio or VFX). */
	UPROPERTY(BlueprintAssignable, Category="Spawner|Events")
	FSpawnerStartedSignature OnEncounterStarted;

	/** Fired once all waves are spawned and every tracked enemy is destroyed. */
	UPROPERTY(BlueprintAssignable, Category="Spawner|Events")
	FSpawnerClearedSignature OnEncounterCleared;

	/** Fired as soon as a tracked boss enemy dies, even if the spawner still owns other enemies. */
	UPROPERTY(BlueprintAssignable, Category="Spawner|Events")
	FSpawnerBossDefeatedSignature OnBossDefeated;

	/** Fired whenever one or more tracked enemies leave the live set because they died or were destroyed. */
	UPROPERTY(BlueprintAssignable, Category="Spawner|Events")
	FSpawnerTrackedEnemiesRemovedSignature OnTrackedEnemiesRemoved;

	/** Fired when the active wave changes. WaveIndex is zero-based. */
	UPROPERTY(BlueprintAssignable, Category="Spawner|Events")
	FSpawnerWaveStartedSignature OnWaveStarted;

	/** Returns the current number of alive enemies spawned by this encounter. */
	UFUNCTION(BlueprintPure, Category="Spawner")
	int32 GetLiveEnemyCount() const { return LiveEnemies; }

	/** Returns the zero-based active wave index, or INDEX_NONE when no runtime wave is active. */
	UFUNCTION(BlueprintPure, Category="Spawner")
	int32 GetCurrentWaveIndex() const { return CurrentWaveIndex; }

	/** Returns the number of runtime waves currently owned by this spawner. */
	UFUNCTION(BlueprintPure, Category="Spawner")
	int32 GetWaveCount() const { return EncounterWavesRuntime.Num(); }

	/** Returns the authored enemy count for a runtime wave after mission scaling has already been applied. */
	UFUNCTION(BlueprintPure, Category="Spawner")
	int32 GetWaveEnemyTotal(int32 WaveIndex) const;

	/** Returns the authored enemy count for the active runtime wave after mission scaling has already been applied. */
	UFUNCTION(BlueprintPure, Category="Spawner")
	int32 GetCurrentWaveEnemyTotal() const { return GetWaveEnemyTotal(CurrentWaveIndex); }

	/** Returns the designer-authored label for a runtime wave. */
	UFUNCTION(BlueprintPure, Category="Spawner")
	FText GetWaveDisplayLabel(int32 WaveIndex) const;

	/** Returns the designer-authored label for the active runtime wave. */
	UFUNCTION(BlueprintPure, Category="Spawner")
	FText GetCurrentWaveDisplayLabel() const { return GetWaveDisplayLabel(CurrentWaveIndex); }

	/** Returns true when any set in the runtime wave is authored as a boss. */
	UFUNCTION(BlueprintPure, Category="Spawner")
	bool DoesWaveContainBoss(int32 WaveIndex) const;

	/** Aggro behavior applied to freshly spawned enemies. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Aggro", meta=(ShowOnlyInnerProperties, AdvancedDisplay))
	FSpawnerAggroSettings AggroSettings;

	/** Runtime flag set by the Rift plan: enemies continuously pursue the nearest living participant. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Spawner|Rift")
	bool bPermanentRiftPursuit = false;

	/** Controls whether this spawner parks dead enemies for reuse instead of destroying them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Pooling", meta=(ShowOnlyInnerProperties))
	FAeyerjiEnemyPoolSettings PoolSettings;

	/** When true, non-elite spawns have a chance to be promoted to elites. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites")
	bool bAllowRandomElites = false;

	/** Chance (0..1) for a non-elite spawn to become an elite. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(ClampMin="0.0", ClampMax="1.0", UIMin="0.0", UIMax="1.0", EditCondition="bAllowRandomElites"))
	float RandomEliteChance = 0.1f;

	/** Fallback elite class pool used when a set does not provide EliteEnemyClassPoolOverride. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(EditCondition="bAllowRandomElites", AdvancedDisplay))
	TArray<TSubclassOf<APawn>> EliteEnemyClassPool;

	/** Requires an elite class pool to resolve before non-elite enemies can be promoted. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(EditCondition="bAllowRandomElites", AdvancedDisplay))
	bool bRequireEliteClassPoolForRandomPromotion = true;

	/** Canonical health promotion applied once to enemy sets flagged as elite, before documented per-set exceptions and affixes. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Spawner|Elites", meta=(ClampMin="0.1"))
	float EliteHealthMultiplier = 4.0f;

	/** Canonical damage promotion applied once to enemy sets flagged as elite. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Spawner|Elites", meta=(ClampMin="0.1"))
	float EliteDamageMultiplier = 1.35f;

	/** Canonical attack-range promotion applied once to enemy sets flagged as elite. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Spawner|Elites", meta=(ClampMin="0.1"))
	float EliteRangeMultiplier = 1.5f;

	/** Scale multiplier and FX applied to any enemy sets flagged as elite. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(ClampMin="0.1"))
	float EliteScaleMultiplier = 1.5f;

	/** Looping Niagara system used to visually distinguish elites (attach a glow, fire, etc.). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites")
	TObjectPtr<UNiagaraSystem> EliteVFXSystem = nullptr;

	/** Socket to attach the elite FX to (leave None to use the root). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(AdvancedDisplay))
	FName EliteVFXSocket = NAME_None;

	/** Local offset for elite FX when attached. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(AdvancedDisplay))
	FVector EliteVFXOffset = FVector::ZeroVector;

	/** Replicate the elite FX component so a dedicated server still shows the aura to clients. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites")
	bool bReplicateEliteVFX = true;

	/** Default min/max random affixes when a set asks for random rolls. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(ClampMin="0", AdvancedDisplay))
	int32 DefaultEliteAffixMin = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(ClampMin="0", AdvancedDisplay))
	int32 DefaultEliteAffixMax = 3;

	/** Global affix pool to pull Diablo-style modifiers from. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(TitleProperty="AffixTag", AdvancedDisplay))
	TArray<FEliteAffixDefinition> EliteAffixPool;

	/** Extra bumps applied when an elite is flagged as a mini boss. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(ClampMin="0.1", AdvancedDisplay))
	float MiniBossScaleMultiplier = 2.25f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(ClampMin="0.1", AdvancedDisplay))
	float MiniBossHealthMultiplier = 4.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(ClampMin="0.1", AdvancedDisplay))
	float MiniBossDamageMultiplier = 3.0f;

	/** Signature abilities auto-granted to any mini boss when no per-set overrides are provided. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(AdvancedDisplay))
	TArray<TSubclassOf<UGameplayAbility>> DefaultMiniBossAbilities;

	/** Extra bumps applied when an elite is flagged as a boss. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(ClampMin="0.1", AdvancedDisplay))
	float BossScaleMultiplier = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(ClampMin="0.1", AdvancedDisplay))
	float BossHealthMultiplier = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(ClampMin="0.1", AdvancedDisplay))
	float BossDamageMultiplier = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(ClampMin="0.1", AdvancedDisplay))
	float BossRangeMultiplier = 2.0f;

	/** Optional per-boss ability set when no per-set overrides are provided. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(AdvancedDisplay))
	TArray<TSubclassOf<UGameplayAbility>> DefaultBossAbilities;

	/** XP reward multipliers for elites/mini bosses. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(ClampMin="0.0", AdvancedDisplay))
	float EliteXPMultiplier = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(ClampMin="0.0", AdvancedDisplay))
	float MiniBossXPMultiplier = 2.0f;

	/** XP reward multiplier for bosses (applied on top of elite mult). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(ClampMin="0.0", AdvancedDisplay))
	float BossXPMultiplier = 3.0f;

	/** Tags to mark elites/mini-bosses on the ASC if present. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(AdvancedDisplay))
	FGameplayTag EliteGameplayTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(AdvancedDisplay))
	FGameplayTag MiniBossGameplayTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(AdvancedDisplay))
	FGameplayTag BossGameplayTag;

	/** Actor tag added to spawned elites; configurable for StateTree queries. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(AdvancedDisplay))
	FName EliteActorTag = TEXT("Elite");

	/** Actor tag added to spawned mini bosses; configurable for StateTree queries. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(AdvancedDisplay))
	FName MiniBossActorTag = TEXT("MiniBoss");

	/** Actor tag added to spawned bosses; configurable for StateTree queries. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Elites", meta=(AdvancedDisplay))
	FName BossActorTag = TEXT("Boss");

	/** Optional pointer back to the level director to read difficulty and player level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawner|Difficulty")
	TObjectPtr<AAeyerjiLevelDirector> LevelDirector = nullptr;

	/** Shared DataTable (JSON/CSV) that drives enemy attribute scaling by archetype tag. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Spawner|Difficulty")
	TSoftObjectPtr<UDataTable> EnemyScalingTable;

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
	bool ChooseSpawnTransform(TSubclassOf<APawn> EnemyClass, FTransform& OutTransform);

	/** Spawns one pawn from the provided wave/set definition and begins tracking it. */
	bool SpawnOneFromSet(int32 WaveIndex, int32 SetIndex);

	/** Builds the class/archetype/role key used to bucket inactive pooled enemies. */
	FAeyerjiEnemyPoolKey MakePoolKey(const FEnemySet& EnemySet) const;

	/** Creates a fresh pawn actor without registering it, shared by normal spawn and prewarm paths. */
	APawn* SpawnRawEnemyActor(const FEnemySet& EnemySet, const FTransform& SpawnTransform, AActor* SpawnOwner, APawn* InstigatorPawn, bool bValidateNav);

	/** Finds and removes an inactive pawn from the matching pool bucket, if one is ready for checkout. */
	APawn* AcquireInactivePooledEnemy(const FAeyerjiEnemyPoolKey& PoolKey);

	/** Captures original actor scale and unmodified attributes before spawner scaling/elite packages run. */
	void CapturePooledEnemyBaseline(APawn* EnemyPawn, const FEnemySet& ResolvedEnemySet);

	/** Restores attributes, scale, gameplay tags, effects, abilities, and VFX that this spawner applied previously. */
	void RestorePooledEnemyForCheckout(APawn* EnemyPawn, const FEnemySet& ResolvedEnemySet, const FTransform& SpawnTransform);

	/** Removes the current spawner-applied package from a pooled enemy without touching blueprint/base startup grants. */
	void CleanupSpawnerAppliedRuntimeState(APawn* EnemyPawn);

	void TrackPooledActorTag(APawn* EnemyPawn, FName ActorTag);
	void TrackPooledLooseTag(APawn* EnemyPawn, FGameplayTag GameplayTag);
	void TrackPooledAbility(APawn* EnemyPawn, FGameplayAbilitySpecHandle AbilityHandle);
	void TrackPooledEffect(APawn* EnemyPawn, FActiveGameplayEffectHandle EffectHandle);
	void TrackPooledNiagara(APawn* EnemyPawn, UNiagaraComponent* NiagaraComponent);
	FVector ResolvePooledOriginalScale(APawn* EnemyPawn) const;
	FVector GetPoolParkingLocation() const;
	void SetPooledEnemyInactiveState(APawn* EnemyPawn, const FVector& ParkingLocation);
	void SetPooledEnemyActiveState(APawn* EnemyPawn, const FTransform& SpawnTransform);
	void DestroySpawnerAppliedVFX(APawn* EnemyPawn);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastSetPooledEnemyInactive(APawn* EnemyPawn, FVector ParkingLocation);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastSetPooledEnemyActive(APawn* EnemyPawn, FTransform SpawnTransform);

	/** Resolves the player/PlayerStart location used for survival reachability checks. */
	bool ResolveSpawnReferenceLocation(FVector& OutLocation) const;

	/** Returns true if a candidate is distance-valid and path-reachable from the target reference. */
	bool IsSpawnCandidateReachable(const FVector& CandidateLocation, const FVector& ReferenceLocation) const;

	/** Utilities for toggling door actors on activation/reset. */
	void SetDoorArrayEnabled(const TArray<TObjectPtr<AActor>>& Targets, bool bEnabled);

	/** Called whenever a tracked enemy is destroyed, keeping live counts accurate. */
	UFUNCTION()
	void OnEnemyDestroyed(AActor* DestroyedEnemy);

	/** Called as soon as a tracked enemy reports death so completion does not wait on corpse cleanup. */
	UFUNCTION()
	void OnEnemyDied(AActor* DeadEnemy);

	/** Removes a tracked enemy exactly once, regardless of whether death or destroy fires first. */
	void HandleTrackedEnemyRemoved(AActor* EnemyActor);

	/** Unhooks delegate bindings for a tracked enemy before it leaves the live set. */
	void UnbindTrackedEnemy(AActor* EnemyActor);

	/** Clears the tracked enemy set and detaches any per-enemy delegates. */
	void ResetTrackedEnemies();

	/** Registers this enemy with the encounter director for progress tracking if needed. */
	void RegisterProgressEnemy(APawn* SpawnedPawn, int32 ProgressPoints);

	/** Applies elite presentation (scale/VFX) when the enemy set is flagged as elite. */
	void ApplyElitePresentation(APawn* SpawnedPawn, float ScaleMultiplier, const TArray<const FEliteAffixDefinition*>& Affixes, bool bApplyScale = true);

	/** Returns a copy of the enemy set with elite promotion/class resolution applied, if enabled. */
	FEnemySet ResolveEliteSpawnSet(const FEnemySet& EnemySet) const;

	/** Multicast cosmetic-only RPC so dedicated servers can still show elite FX on clients. */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastApplyElitePresentation(APawn* SpawnedPawn, float ScaleMultiplier, const TArray<FGameplayTag>& AffixTags, FVector BaseScale);

	/** Applies elite stat bumps to the pawn's attribute set (server only). */
	void ApplyEliteStats(APawn* SpawnedPawn, float HealthMultiplier, float DamageMultiplier, float RangeMultiplier, bool bPreserveHealthRatio = false);
	/** Pushes gameplay tags, abilities, and affix-driven effects. */
	void ApplyEliteGameplay(APawn* SpawnedPawn, const FEnemySet& EnemySet, const TArray<const FEliteAffixDefinition*>& Affixes, bool bApplyXPMultipliers = true);
	/** Rolls affixes for a given elite set (forced + random). */
	TArray<const FEliteAffixDefinition*> BuildEliteAffixLoadout(const FEnemySet& EnemySet) const;
	const FEliteAffixDefinition* FindAffixDefinition(const FGameplayTag& Tag) const;
	float ComputeEliteScale(const FEnemySet& EnemySet, const TArray<const FEliteAffixDefinition*>& Affixes) const;
	void ApplyAffixVFX(APawn* SpawnedPawn, const FEliteAffixDefinition& Affix);
	void ApplyElitePackage(APawn* SpawnedPawn, const FEnemySet& EnemySet);

public:
	/** Applies shared enemy scaling from the cached spawn template. Reuses cached baselines on later refreshes to avoid compounding. */
	void ApplyEnemyScaling(APawn* SpawnedPawn, const FEnemySet& EnemySet);
	/** Recomputes scaling for every tracked live enemy owned by this spawner. */
	void RefreshTrackedEnemyScaling(TSet<TWeakObjectPtr<AActor>>& OutHandledEnemies);
	const FEnemyScalingRow* FindScalingRow(const FGameplayTag& ArchetypeTag) const;
	int32 ResolvePlayerLevelForScaling() const;
	FGameplayAttribute ResolveAttribute(const FName& AttributeName) const;

protected:
	/** Returns the current survival-round multiplier for attributes the mission owns, otherwise 1. */
	float ResolveSurvivalRoundAttributeMultiplier(const FGameplayAttribute& Attribute) const;
	/** Captures an unscaled attribute value the first time we see it so later refreshes can rebuild from that baseline. */
	void CaptureTrackedBaseValueIfNeeded(const TWeakObjectPtr<AActor>& EnemyKey, UAbilitySystemComponent* ASC, const FGameplayAttribute& Attribute, const FName& AttributeName);
	/** Preserves the current resource percentage during a live rescale instead of hard-healing to full. */
	static float ResolvePreservedResourceValue(float OldCurrent, float OldMax, float NewMax, bool bPreserveRatio);
	/** Callback for gameplay events used to trigger encounter activation. */
	void HandleActivationEvent(const FGameplayTag& EventTag, const FGameplayEventData& Payload);
	void CacheActivationStimulus(AActor* InstigatorActor, AController* InstigatorController);
	AActor* ResolveAggroTargetActor() const;
	APawn* ResolveAggroTargetPawn() const;
	AController* ResolveAggroController() const;
	void ApplyAggroToSpawnedPawn(APawn* SpawnedPawn);
	void StartAggroReissueTimer();
	void StopAggroReissueTimer();
	void ReissueAggroToTrackedEnemies();
	APawn* ResolveNearestLivePlayer(const FVector& FromLocation) const;
	void ClearAggroCache();
	void RebuildSpawnPointOrder();
	int32 GetNextSpawnPointIndex();
	void ResetSpawnPointCycle();
	/** Discovers spawn regions and the fallback PlayerStart once for the active encounter window. */
	void RebuildSpawnDiscoveryCache();
	/** Starts async loading and retention for the shared enemy scaling table. */
	void BeginEnemyScalingTablePreload();
	/** Retains the scaling table pointer and clears row caches after async loading completes. */
	void HandleEnemyScalingTableLoaded();
	/** Returns the cached capsule half-height for a pawn class, resolving the CDO once if needed. */
	float GetCachedSpawnHalfHeight(TSubclassOf<APawn> EnemyClass);

	UPROPERTY(VisibleAnywhere, Category="Spawner|State")
	bool bActive = false;

#if WITH_DEV_AUTOMATION_TESTS
	int32 AutomationActivationCount = 0;
#endif

	UPROPERTY(VisibleAnywhere, Category="Spawner|State")
	bool bCleared = false;

	UPROPERTY(VisibleAnywhere, Category="Spawner|State")
	int32 CurrentWaveIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, Category="Spawner|State")
	bool bAwaitingManualSpawns = false;

	UPROPERTY(VisibleAnywhere, Category="Spawner|State")
	int32 LiveEnemies = 0;

	/** Live enemy set used to make death/destroy callbacks idempotent. */
	TSet<TWeakObjectPtr<AActor>> TrackedLiveEnemies;

	/** Boss subset used to signal boss defeat without requiring the whole spawner to clear. */
	TSet<TWeakObjectPtr<AActor>> TrackedBossEnemies;

	struct FTrackedEnemyScalingState
	{
		FEnemySet ResolvedTemplate;
		TMap<FName, float> BaseAttributeValues;
		float EliteHealthMultiplier = 1.f;
		float EliteDamageMultiplier = 1.f;
		float EliteRangeMultiplier = 1.f;
		bool bHasEliteAutoScaling = false;
	};

	/** Runtime scaling cache so resyncs can recompute from the original baseline instead of compounding. */
	TMap<TWeakObjectPtr<AActor>, FTrackedEnemyScalingState> TrackedEnemyScalingStates;

	enum class EAeyerjiPooledEnemyState : uint8
	{
		Active,
		PendingReturn,
		Inactive
	};

	struct FPooledEnemyRuntimeState
	{
		FAeyerjiEnemyPoolKey PoolKey;
		FVector OriginalScale = FVector::OneVector;
		TMap<FName, float> BaselineAttributeValues;
		TSet<FName> AppliedActorTags;
		TArray<FGameplayTag> AppliedLooseTags;
		TArray<FGameplayAbilitySpecHandle> GrantedAbilityHandles;
		TArray<FActiveGameplayEffectHandle> AppliedEffectHandles;
		TArray<TWeakObjectPtr<UNiagaraComponent>> SpawnedNiagaraComponents;
		EAeyerjiPooledEnemyState State = EAeyerjiPooledEnemyState::Active;
		bool bBaselineCaptured = false;
		bool bAlwaysRelevant = false;
	};

	/** Inactive reusable enemies bucketed by class/archetype/elite role. */
	TMap<FAeyerjiEnemyPoolKey, TArray<TWeakObjectPtr<APawn>>> InactiveEnemyPools;

	/** Runtime cleanup package owned by this spawner for every pool-managed enemy. */
	TMap<TWeakObjectPtr<APawn>, FPooledEnemyRuntimeState> PooledEnemyStates;

	/** Profiling counters distinguish loading construction, pooled checkout, and runtime fallback. */
	UPROPERTY(VisibleAnywhere, Category="Spawner|Pooling|Diagnostics")
	int32 FreshEnemyConstructionCount = 0;

	UPROPERTY(VisibleAnywhere, Category="Spawner|Pooling|Diagnostics")
	int32 PooledCheckoutCount = 0;

	UPROPERTY(VisibleAnywhere, Category="Spawner|Pooling|Diagnostics")
	int32 PrewarmConstructionCount = 0;

	UPROPERTY(VisibleAnywhere, Category="Spawner|Pooling|Diagnostics")
	int32 EmergencyRuntimeSpawnCount = 0;

	bool bExactPoolPrewarmInProgress = false;
	bool bExactPoolPrewarmFinalized = false;

	/** Exact planned actor count per class/archetype/elite key for the current frozen run. */
	TMap<FAeyerjiEnemyPoolKey, int32> ExactPoolKeyCapacities;

	/** Remaining spawn counts for each wave/set (runtime state). */
	TArray<TArray<int32>> PendingSpawnCounts;

	/** Timer handles for in-flight spawn timers per wave/set. */
	TArray<TArray<FTimerHandle>> SpawnTimerHandles;

	/** Consecutive spawn failures per wave/set, used to prevent invalid authoring from retrying forever. */
	TArray<TArray<int32>> SpawnFailureCounts;

	/** Delay timer between waves. */
	FTimerHandle WaveDelayHandle;

	/** Handle stored so we can unregister the gameplay event listener when the spawner ends play. */
	FDelegateHandle ActivationEventHandle;

	/** Timer handle controlling any configured initial spawn delay. */
	FTimerHandle InitialSpawnDelayHandle;

	/** Timer handle for repeatedly reasserting chase/focus on live tracked enemies. */
	FTimerHandle AggroReissueTimerHandle;

	/** Cached activation information used to drive aggro behavior. */
	TWeakObjectPtr<AActor> CachedAggroActor;
	TWeakObjectPtr<AController> CachedAggroController;

	/** Optional defendable survival objective handed to enemy AI controllers. */
	TWeakObjectPtr<AActor> DefenseObjectiveTargetActor;
	FAeyerjiDefenseTargetingSettings DefenseTargetingSettings;

	/** Runtime copy of waves (from inline authoring or encounter definition). */
	TArray<FWaveDefinition> EncounterWavesRuntime;

	/** Spawn point iteration cache for sequential/symmetrical patterns. */
	TArray<int32> SpawnPointOrder;
	int32 SpawnPointCursor = 0;

	UPROPERTY(Transient)
	TObjectPtr<UDataTable> CachedEnemyScalingTable = nullptr;

	TSharedPtr<FStreamableHandle> EnemyScalingTableHandle;
	mutable TMap<FGameplayTag, const FEnemyScalingRow*> CachedScalingRows;
	mutable TSet<FGameplayTag> CachedMissingScalingRows;

	UPROPERTY(Transient)
	TArray<FCachedAeyerjiSpawnRegion> CachedSpawnRegions;

	UPROPERTY(Transient)
	TObjectPtr<APlayerStart> CachedFallbackPlayerStart = nullptr;

	TMap<TObjectKey<UClass>, float> CachedSpawnHalfHeights;
};
