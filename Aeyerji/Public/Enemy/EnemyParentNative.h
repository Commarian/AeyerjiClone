// EnemyParentNative.h
#pragma once

#include "CoreMinimal.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "GameFramework/Character.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "AbilitySystemInterface.h"
#include "GenericTeamAgentInterface.h"
#include "Abilities/GameplayAbility.h"
#include "AbilitySystemComponent.h"
#include "AeyerjiCharacter.h"
#include "GameplayTagAssetInterface.h"
#include "GameplayTagContainer.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Components/OutlineHighlightComponent.h"
#include "Inventory/AeyerjiInventoryBPFL.h"
#include "Systems/LootService.h"

#include "EnemyParentNative.generated.h"

class UPrimitiveComponent;

class AAeyerjiSpawnerGroup;
class AAeyerjiGoldPickup;
class UGameplayAbility;
class UAeyerjiEnemyArchetypeComponent;
class UAeyerjiEnemyArchetypeData;
class UAeyerjiEnemyTraitComponent;
class UAeyerjiLevelingComponent;
class UAeyerjiRewardConfigComponent;
struct FPropertyChangedEvent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FEnemyDiedSignature, AActor*, Enemy);

/** Normal enemy death gold drop settings, independent from item loot rolls. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiGoldDropConfig
{
	GENERATED_BODY()

	/** Enables gold pickup spawning from this enemy's normal death path. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Enemy|Gold")
	bool bEnabled = false;

	/** Chance to spawn gold after this enemy dies. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Enemy|Gold", meta=(ClampMin="0.0", ClampMax="1.0"))
	float DropChance = 1.f;

	/** Base gold amount before variance, level scaling, and archetype multipliers. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Enemy|Gold", meta=(ClampMin="0"))
	int64 BaseAmount = 5;

	/** Inclusive random variance added around BaseAmount. A value of 3 rolls BaseAmount plus [-3, +3]. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Enemy|Gold", meta=(ClampMin="0"))
	int64 Variance = 3;

	/** Additional gold per scaled enemy level. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Enemy|Gold", meta=(ClampMin="0.0"))
	float PerLevelScalar = 1.f;

	/** Multiplier used when this enemy's cached source tag indicates an elite source. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Enemy|Gold", meta=(ClampMin="0.0"))
	float EliteMultiplier = 1.f;

	/** Multiplier reserved for mini-boss enemy subclasses or source tags. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Enemy|Gold", meta=(ClampMin="0.0"))
	float MiniBossMultiplier = 1.f;

	/** Multiplier used when this enemy's cached source tag indicates a boss source. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Enemy|Gold", meta=(ClampMin="0.0"))
	float BossMultiplier = 1.f;

	/** Optional gold pickup Blueprint class for mesh, beam, label, and pickup FX authoring. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Enemy|Gold")
	TSubclassOf<AAeyerjiGoldPickup> PickupClass;

	/** Controls whether gold is for the credited player or mirrored per player. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Enemy|Gold")
	EItemDropDistributionMode DropOwnershipMode = EItemDropDistributionMode::DropOnlyForInstigator;
};

/**
 * Native base class for AI-controlled "creep" / enemy pawns.
 * Blueprint children should inherit from this (NOT from ACharacter directly).
 */
UCLASS()
class AEYERJI_API AEnemyParentNative : public AAeyerjiCharacter, public IGenericTeamAgentInterface, public IGameplayTagAssetInterface
{
	GENERATED_BODY()

public:
	
	/* IGenericTeamAgentInterface */
	virtual FGenericTeamId GetGenericTeamId() const override { return FGenericTeamId(TeamId); }
	virtual void SetGenericTeamId(const FGenericTeamId& NewID) override;


	/* IAbilitySystemInterface */
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override
	{
		return AbilitySystemAeyerji;
	}

	/* ====== LIFECYCLE ====== */

	AEnemyParentNative();

protected:
	/* --- AActor overrides --- */
	virtual void PostLoad() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void NotifyActorBeginCursorOver() override;
	virtual void NotifyActorEndCursorOver() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/* Initialise ASC owner/avatar pointers */
	void InitAbilityActorInfo();

	/* Grant abilities & effects defined below (runs once, server only) */
	void GiveStartupAbilitiesAndEffects();

	/* --------- Death hook (Blueprint-extendable) --------- */
	virtual void OnDeath_Implementation() override;
	virtual FAeyerjiDeathStateOptions BuildDeathStateOptionsForOutOfHealth() const override;

	/* ====== DESIGN-TIME LISTS ====== */

	/** Abilities given on spawn (level 1) */
	UPROPERTY(EditDefaultsOnly, Category="GAS|Startup")
	TArray<TSubclassOf<UGameplayAbility>> StartupAbilities;

	/** Gameplay Effects applied on spawn (e.g. passive buffs) */
	UPROPERTY(EditDefaultsOnly, Category="GAS|Startup")
	TArray<TSubclassOf<UGameplayEffect>> StartupEffects;

	/** Deprecated: prefer ArchetypeComponent.ArchetypeData. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Enemy|Archetype", meta=(DeprecatedProperty, DeprecationMessage="Use ArchetypeComponent.ArchetypeData instead."))
	TObjectPtr<UAeyerjiEnemyArchetypeData> ArchetypeData;

	/** Deprecated: prefer ArchetypeComponent auto-apply setting. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Enemy|Archetype", meta=(DeprecatedProperty, DeprecationMessage="Use ArchetypeComponent auto-apply setting instead."))
	bool bApplyArchetypeOnBeginPlay = true;

	/** Internal guard so we don't double-grant after seamless travel */
	bool bStartupGiven = false;

	UPROPERTY(EditDefaultsOnly, Category="AI")
	uint8 TeamId = 1;                            // 0 = Players by convention

	/** Gameplay tag that marks this pawn as an enemy by default. */
	UPROPERTY(EditDefaultsOnly, Category="Enemy|Team")
	FGameplayTag DefaultTeamTag;

	/** Active team gameplay tag replicated for client-side queries. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, ReplicatedUsing=OnRep_ActiveTeamTag, Category="Enemy|Team")
	FGameplayTag ActiveTeamTag;

	UPROPERTY(Transient)
	FGameplayTag LastAppliedTeamTag;

public:
	/** Fired when this pawn dies so encounter directors can react immediately. */
	UPROPERTY(BlueprintAssignable, Category="Enemy|Events")
	FEnemyDiedSignature OnEnemyDied;

	/** Apply archetype tags, traits, abilities, and effects (server only). */
	UFUNCTION(BlueprintCallable, Category="Enemy|Archetype")
	void ApplyArchetypeData();

	/** Returns true if Candidate is hostile to this enemy's AI controller and does not carry InvalidTag (GAS/ASC). */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Enemy|AI")
	bool IsAliveAndHostile(const AActor* Candidate, FGameplayTag InvalidTag = FGameplayTag()) const;

	/** Returns true if tag is valid and NOT found on the EnemyParentNative object*/
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Enemy|AI")
	bool IsAlive(FGameplayTag DeathTag = FGameplayTag()) const;

	/** Alerts nearby allies about a freshly acquired hostile target (server only). */
	void NotifyNearbyAlliesOfTarget(AActor* Target);

	/** Applies an ally-generated alert without re-broadcasting it to a second ring. */
	void ReceiveAllyAlert(AActor* Target, const AEnemyParentNative* Notifier);

	/** Updates the active team tag and pushes it to the ASC (server only). */
	void SetActiveTeamTag(const FGameplayTag& NewTag);

	/** Assigns a new archetype asset and optionally applies it immediately (server only). */
	UFUNCTION(BlueprintCallable, Category="Enemy|Archetype")
	void SetArchetypeAndApply(UAeyerjiEnemyArchetypeData* NewArchetypeData, bool bApplyImmediately = true);

	// Applies archetype stat multipliers to a scaling value if configured.
	bool ApplyArchetypeStatMultipliers(const FName& AttributeName, float& InOutValue) const;

	// Returns the default team tag used when no archetype override is provided.
	FGameplayTag GetDefaultTeamTag() const { return DefaultTeamTag; }

	/** IGameplayTagAssetInterface */
	virtual void GetOwnedGameplayTags(FGameplayTagContainer& TagContainer) const override;

	/* ====== OUTLINE HIGHLIGHT ====== */
public:
	UFUNCTION(BlueprintCallable, Category="Enemy|Highlight")
	void SetEnemyHighlighted(bool bInHighlighted);

	/** Cached scaling info applied on spawn (for loot/stat reads). */
	UFUNCTION(BlueprintPure, Category="Enemy|Scaling")
	int32 GetScaledLevel() const { return CachedScaledLevel; }

	UFUNCTION(BlueprintPure, Category="Enemy|Scaling")
	float GetScaledDifficulty() const { return CachedDifficultyScale; }

	/** Frozen reward-quality bias supplied by the run that spawned this enemy. */
	UFUNCTION(BlueprintPure, Category="Enemy|Scaling")
	float GetRewardQualityMultiplier() const { return CachedRewardQualityMultiplier; }

	UFUNCTION(BlueprintPure, Category="Enemy|Scaling")
	FGameplayTag GetScalingSourceTag() const { return CachedScalingSourceTag; }

	void SetScalingSnapshot(int32 InLevel, float InDifficultyScale, const FGameplayTag& InSourceTag, float InRewardQualityMultiplier = 1.f);

	/** Server-only authoritative normal enemy death reward hook. Returns false when disabled or no loot was spawned. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Enemy|Loot")
	bool TrySpawnEnemyDeathRewards(AActor* RewardInstigator = nullptr);

	/** Server-only authoritative normal enemy death gold hook. Returns false when disabled or no gold was spawned. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Enemy|Gold")
	bool TrySpawnEnemyGoldDrop(AActor* RewardInstigator = nullptr);

	/** Assigns the server spawner that owns this enemy while it participates in object pooling. */
	void SetOwningSpawnerPool(AAeyerjiSpawnerGroup* InSpawner, bool bInPoolManaged);

	/** Returns this enemy to its owning spawner pool after death presentation finishes. */
	bool TryReturnToOwningSpawnerPool();

	bool IsPoolManagedBySpawner() const { return bPoolManagedBySpawner; }

	/** Resets enemy-only runtime state immediately before a pooled checkout is made live. */
	void PrepareForPooledActivation();

	/** Clears enemy-only runtime state immediately before a pooled enemy is hidden and parked. */
	void PrepareForPooledDeactivation();

	/** Blueprint hook for restoring enemy visuals/components after native pooled activation reset. */
	UFUNCTION(BlueprintImplementableEvent, Category="Enemy|Pooling")
	void BP_OnPooledEnemyActivated();

	/** Blueprint hook for shutting down enemy visuals/components before the actor is hidden in the pool. */
	UFUNCTION(BlueprintImplementableEvent, Category="Enemy|Pooling")
	void BP_OnPooledEnemyDeactivated();
	
	bool IsHoverTargetComponent(const UPrimitiveComponent* Component) const;

protected:
	/** Applies optional crowd-focused animation/rendering throttles for performance testing. */
	void ApplyCrowdPerformanceSettings();

	void RefreshEnemyHighlightTargets();
	void UpdateEnemyHighlightState();
	void ConfigureEnemyOutlineComponent();
	void GrantAbilityList(const TArray<TSubclassOf<UGameplayAbility>>& Abilities, int32 AbilityLevel);
	void ApplyEffectList(const TArray<TSubclassOf<UGameplayEffect>>& Effects, float EffectLevel);
	void AddTraitComponents(const TArray<TSubclassOf<UAeyerjiEnemyTraitComponent>>& TraitComponents);
	void ApplyDefaultTeamTags();
	void ApplyActiveTeamTagToASC(const FGameplayTag& OldTag);

	UFUNCTION()
	void OnRep_ActiveTeamTag();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enemy|Highlight")
	TObjectPtr<UOutlineHighlightComponent> OutlineHighlight;

	/** Nearby allies inside this radius are alerted when this enemy directly acquires a hostile target. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Enemy|AI|Alert", meta=(ClampMin="0.0", Units="cm"))
	float AllyAlertRadius = 800.f;

	/** When true, allies only wake if navigation can reach them from this enemy. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Enemy|AI|Alert")
	bool bRequireNavigableAllyAlertPath = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enemy|Archetype")
	TObjectPtr<UAeyerjiEnemyArchetypeComponent> ArchetypeComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enemy|Progression")
	TObjectPtr<UAeyerjiLevelingComponent> LevelingComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enemy|Progression")
	TObjectPtr<UAeyerjiRewardConfigComponent> RewardConfigComponent;

	/** Enables normal enemy loot on this pawn. Bosses/special encounters should leave this off and use StateTree reward tasks. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Enemy|Loot")
	bool bSpawnNormalDeathLoot = false;

	/** When true, DeathLootMultiDropConfig is used; otherwise a single ULootService roll is spawned. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Enemy|Loot", meta=(EditCondition="bSpawnNormalDeathLoot"))
	bool bUseDeathLootMultiDrop = false;

	/** Base context for normal enemy death loot. Level, difficulty, source tag, and player actor are filled from runtime when unset. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Enemy|Loot", meta=(EditCondition="bSpawnNormalDeathLoot"))
	FLootContext DeathLootContext;

	/** Optional multi-drop config for normal enemy death loot. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Enemy|Loot", meta=(EditCondition="bSpawnNormalDeathLoot && bUseDeathLootMultiDrop"))
	FLootMultiDropConfig DeathLootMultiDropConfig;

	/** Controls whether the death drop is only for the credited player or distributed to all players. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Enemy|Loot", meta=(EditCondition="bSpawnNormalDeathLoot"))
	EItemDropDistributionMode DeathLootDropMode = EItemDropDistributionMode::DropOnlyForInstigator;

	/** Optional profile-currency drop rolled independently from normal item loot. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Enemy|Gold")
	FAeyerjiGoldDropConfig DeathGoldDropConfig;

	/** Runtime guard against duplicate loot rolls if death is reported by multiple systems. */
	UPROPERTY(Transient)
	bool bDeathRewardsRolled = false;

	/** Runtime guard against duplicate gold rolls if death is reported by multiple systems. */
	UPROPERTY(Transient)
	bool bDeathGoldRolled = false;

	/** Server spawner responsible for delayed pool return after death finalization. */
	UPROPERTY(Transient)
	TWeakObjectPtr<AAeyerjiSpawnerGroup> OwningSpawnerPool;

	/** True when normal non-player death cleanup should return this pawn to a spawner pool. */
	UPROPERTY(Transient)
	bool bPoolManagedBySpawner = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy|Highlight")
	TArray<TObjectPtr<UPrimitiveComponent>> AdditionalHighlightPrimitives;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy|Highlight", meta=(ClampMin="0", ClampMax="255", DisplayName="Highlight Channel"))
	int32 HighlightChannel = 20;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy|Highlight")
	bool bHighlightOnSpawn = false;

	UPROPERTY(BlueprintReadOnly, Category="Enemy|Highlight")
	bool bEnemyHighlighted = false;

	UPROPERTY(Transient, BlueprintReadOnly, Category="Enemy|Highlight")
	int32 HoverHighlightRefCount = 0;

	/** Enables crowd performance overrides on this enemy (opt-in). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy|Performance")
	bool bEnableCrowdPerformanceSettings = false;

	/** Skip crowd performance overrides on this enemy (bosses, hero units, etc.). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy|Performance", meta=(EditCondition="bEnableCrowdPerformanceSettings"))
	bool bIgnoreCrowdPerformanceSettings = false;

	/** Enable skeletal mesh update rate optimizations. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy|Performance", meta=(EditCondition="bEnableCrowdPerformanceSettings"))
	bool bEnableUpdateRateOptimizations = true;

	/** Only tick animation pose when the mesh is rendered. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy|Performance", meta=(EditCondition="bEnableCrowdPerformanceSettings"))
	bool bOnlyTickPoseWhenRendered = true;

	/** Minimum LOD to render when crowd settings are enabled (0 = no override). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy|Performance", meta=(EditCondition="bEnableCrowdPerformanceSettings", ClampMin="0"))
	int32 CrowdMinLOD = 0;

	/** Force a specific LOD when crowd settings are enabled (0 = no override). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy|Performance", meta=(EditCondition="bEnableCrowdPerformanceSettings", ClampMin="0"))
	int32 CrowdForcedLOD = 0;

	/** Max draw distance for the enemy mesh when crowd settings are enabled (0 = no override). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy|Performance", meta=(EditCondition="bEnableCrowdPerformanceSettings", ClampMin="0.0", Units="cm"))
	float CrowdMaxDrawDistance = 0.f;

	/** Disable dynamic shadows for the enemy mesh when crowd settings are enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Enemy|Performance", meta=(EditCondition="bEnableCrowdPerformanceSettings"))
	bool bDisableDynamicShadows = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enemy|Scaling")
	int32 CachedScaledLevel = 1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enemy|Scaling")
	float CachedDifficultyScale = 0.f;

	/** Immutable loot-quality bias captured on spawn; item level remains player-derived. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enemy|Scaling")
	float CachedRewardQualityMultiplier = 1.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enemy|Scaling")
	FGameplayTag CachedScalingSourceTag;

	/** Recent target used to suppress duplicate ally-alert broadcasts from repeated perception updates. */
	UPROPERTY(Transient)
	TWeakObjectPtr<AActor> LastAlertedTarget;

	/** World time of the last ally-alert broadcast for duplicate suppression. */
	UPROPERTY(Transient)
	double LastAlertBroadcastTime = -1.0;

	UFUNCTION()
	void HandleMeshBeginCursorOver(UPrimitiveComponent* TouchedComponent);

	UFUNCTION()
	void HandleMeshEndCursorOver(UPrimitiveComponent* TouchedComponent);

	UFUNCTION()
	void HandleEnemyDamageTaken(AActor* VictimActor, AActor* InstigatorActor, float DamageTaken, FGameplayTag DamageType);

private:
	bool HasNavigableAlertPathTo(const AEnemyParentNative* OtherEnemy) const;
};
