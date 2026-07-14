// Copyright (c) 2025 Aeyerji.
#pragma once

#include "CoreMinimal.h"
#include "AeyerjiObjectiveTypes.h"
#include "Director/AeyerjiEncounterDefinition.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameplayTagContainer.h"
#include "Inventory/AeyerjiInventoryBPFL.h"
#include "Systems/AeyerjiWorldStateTypes.h"
#include "Systems/LootService.h"
#include "Systems/AeyerjiRiftTypes.h"
#include "AeyerjiLevelDirector.generated.h"

class AAeyerjiSpawnerGroup;
class AAeyerjiEncounterDirector;
class AAeyerjiEndRunPortal;
class AAeyerjiLinkedTeleporter;
class AAeyerjiRewardPresentationActor;
class AAeyerjiPlayerController;
class AAeyerjiPlayerState;
class UAeyerjiEncounterDirectorDefinition;
class UAeyerjiAttributeSet;
class UAeyerjiLevelingComponent;
class UAbilitySystemComponent;
class UAeyerjiWorldSpawnProfile;
struct FOnAttributeChangeData;
struct FStreamableHandle;

UENUM(BlueprintType)
enum class EAeyerjiLevelSpawnMode : uint8
{
	Sequence UMETA(DisplayName="Sequence"),
	FixedWorldPopulation UMETA(DisplayName="Fixed World Population"),
	ProximityEncounterRegions UMETA(DisplayName="Proximity Encounter Regions"),
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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Loot|Advanced", meta=(DeprecatedProperty, DeprecationMessage="Ignored. Boss loot now uses the resolved character level exactly.", ToolTip="Deprecated: ignored. Boss loot now uses the resolved character level exactly."))
	int32 BossItemLevelJitterMin = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Loot|Advanced", meta=(DeprecatedProperty, DeprecationMessage="Ignored. Boss loot now uses the resolved character level exactly.", ToolTip="Deprecated: ignored. Boss loot now uses the resolved character level exactly."))
	int32 BossItemLevelJitterMax = 0;

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

	/** PlayerStart tag used for every player death while the undefeated Rift boss phase is active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Respawn")
	FName BossArenaRespawnPlayerStartTag = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Persistence", meta=(TitleProperty="StateTag"))
	TArray<FAeyerjiPersistentFactWrite> BossDefeatedFacts;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Boss|Persistence", meta=(TitleProperty="StateTag"))
	TArray<FAeyerjiPersistentFactWrite> UnlockFacts;
};

/** Loot reward emitted after a survival round clears. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiSurvivalRoundRewardDefinition
{
	GENERATED_BODY()

	/** Enables this reward entry. Disabled entries are ignored, allowing per-round overrides to suppress the mission default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reward")
	bool bEnabled = false;

	/** Loot source used to select pools in the global AeyerjiLootTable. This wins over LootContext.SourceTag when set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reward")
	FGameplayTag SourceTag;

	/** Optional context overrides. Player, levels, world tier, and difficulty are filled from the current run when missing. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reward")
	FLootContext LootContext;

	/** Controls how many drops are rolled and any bucket rules such as guaranteed rarities. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reward")
	FLootMultiDropConfig MultiDropConfig;

	/** First round allowed to emit this reward. Use 5 with RewardEveryNRounds=5 for 5/10/15 milestones. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reward|Condition", meta=(ClampMin="1"))
	int32 FirstEligibleRound = 1;

	/** Cadence for this reward after FirstEligibleRound. 1 means every round, 5 means every fifth eligible round. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reward|Condition", meta=(ClampMin="1"))
	int32 RewardEveryNRounds = 1;

	/** Whether the pickup is for the killer/instigator only or distributed to every player. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reward")
	EItemDropDistributionMode DropMode = EItemDropDistributionMode::DropOnlyForInstigator;

	/** Distance in front of the player where the reward pickup burst appears. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reward", meta=(ClampMin="0.0", Units="cm"))
	float SpawnDistanceFromPlayer = 180.f;

	/** Height offset applied to the player location before spawning rewards. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reward", meta=(Units="cm"))
	float SpawnHeightOffset = 40.f;

	/** Optional replicated actor/chest that owns rolled loot until Blueprint or interaction releases it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reward|Presentation")
	TSubclassOf<AAeyerjiRewardPresentationActor> PresentationActorClass;

	/** Local offset from the presentation actor used when it releases stored loot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reward|Presentation")
	FVector LootReleaseOffset = FVector::ZeroVector;

	/** Lifespan applied to the presentation actor after release. Zero leaves cleanup to Blueprint/gameplay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Reward|Presentation", meta=(ClampMin="0.0", Units="s"))
	float PresentationLifeSpanAfterRelease = 10.f;
};

/** Designer-authored survival round. LevelDirector resolves these into runtime spawner waves. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiSurvivalRoundDefinition
{
	GENERATED_BODY()

	/** Optional label for editor/debugging and Blueprint UI. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival")
	FText DisplayLabel;

	/** HUD-facing category for this authored round pattern. Boss cadence rounds override this to Boss. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival")
	EAeyerjiSurvivalRoundType RoundType = EAeyerjiSurvivalRoundType::Normal;

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

	/** When true, this round uses RoundClearReward instead of the mission default. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Reward")
	bool bOverrideRoundClearReward = false;

	/** Optional loot reward emitted after this round clears. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Reward", meta=(EditCondition="bOverrideRoundClearReward", EditConditionHides))
	FAeyerjiSurvivalRoundRewardDefinition RoundClearReward;
};

/** Flat import row for survival rounds. One table row represents one enemy set inside one wave of one round. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiSurvivalRoundTableRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Round", meta=(ClampMin="1"))
	int32 RoundNumber = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Round")
	FText RoundDisplayLabel;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Round")
	EAeyerjiSurvivalRoundType RoundType = EAeyerjiSurvivalRoundType::Normal;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Round", meta=(ClampMin="0.0"))
	float EnemyCountMultiplier = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Round")
	FName RoundStartMessageKey = FName(TEXT("RoundStart"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Round")
	FName RoundClearMessageKey = FName(TEXT("RoundClear"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Round")
	FName BossIncomingMessageKey = FName(TEXT("BossIncoming"));

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Reward")
	bool bOverrideRoundClearReward = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Reward", meta=(EditCondition="bOverrideRoundClearReward", EditConditionHides))
	FAeyerjiSurvivalRoundRewardDefinition RoundClearReward;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Wave", meta=(ClampMin="1"))
	int32 WaveNumber = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Wave")
	FText WaveLabel;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Wave", meta=(ClampMin="0.0"))
	float PostSpawnDelay = 0.5f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Enemy", meta=(AllowedClasses="/Script/Engine.Pawn"))
	TSoftClassPtr<APawn> EnemyClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Enemy", meta=(ClampMin="0"))
	int32 Count = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Enemy", meta=(ClampMin="0.0"))
	float SpawnInterval = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Enemy")
	bool bIsElite = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Enemy")
	bool bIsMiniBoss = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Enemy")
	bool bIsBoss = false;
};

/** Optional defendable objective for survival rounds. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiSurvivalDefenseObjectiveDefinition
{
	GENERATED_BODY()

	/** Enables the second survival objective: enemies attack this actor until players pull threat. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Defense")
	bool bEnabled = false;

	/** Actor tag used to find the placed objective actor in the streamed level. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Defense", meta=(EditCondition="bEnabled"))
	FName ObjectiveActorTag = NAME_None;

	/** Ends the run as a failure when the objective reaches zero HP. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Defense", meta=(EditCondition="bEnabled"))
	bool bFailRunWhenDestroyed = true;

	/** UI message key published when the objective is first resolved for the active run. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Defense", meta=(EditCondition="bEnabled"))
	FName ObjectiveActiveMessageKey = FName(TEXT("DefenseObjectiveActive"));

	/** UI message key published when the objective is destroyed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Defense", meta=(EditCondition="bEnabled"))
	FName ObjectiveDestroyedMessageKey = FName(TEXT("DefenseObjectiveDestroyed"));

	/** Enables one-shot UI warning messages when objective HP crosses the configured health thresholds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Defense|Messages", meta=(EditCondition="bEnabled"))
	bool bEnableHealthWarningMessages = true;

	/** Normalized HP thresholds for warning messages, authored from high to low such as 0.75, 0.50, and 0.25. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Defense|Messages", meta=(EditCondition="bEnabled && bEnableHealthWarningMessages", ClampMin="0.0", ClampMax="1.0"))
	TArray<float> HealthWarningThresholds = { 0.75f, 0.5f, 0.25f };

	/** Message keys paired by index with HealthWarningThresholds; missing keys fall back to DefenseObjectiveHealthXX, while None suppresses that threshold. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Defense|Messages", meta=(EditCondition="bEnabled && bEnableHealthWarningMessages"))
	TArray<FName> HealthWarningMessageKeys = {
		FName(TEXT("DefenseObjectiveHealth75")),
		FName(TEXT("DefenseObjectiveHealth50")),
		FName(TEXT("DefenseObjectiveHealth25"))
	};

	/** Controls player-vs-objective retargeting for spawned enemies and StateTree conditions. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Defense", meta=(EditCondition="bEnabled"))
	FAeyerjiDefenseTargetingSettings TargetingSettings;

	/** Repair menu options validated on the server when a player interacts with the defense objective. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Defense|Repair", meta=(EditCondition="bEnabled", TitleProperty="OptionId"))
	TArray<FAeyerjiDefenseRepairOption> RepairOptions = {
		FAeyerjiDefenseRepairOption(FName(TEXT("Small")), FName(TEXT("DefenseRepairSmall")), 25, 0.f, 0.15f),
		FAeyerjiDefenseRepairOption(FName(TEXT("Medium")), FName(TEXT("DefenseRepairMedium")), 60, 0.f, 0.40f),
		FAeyerjiDefenseRepairOption(FName(TEXT("Full")), FName(TEXT("DefenseRepairFull")), 120, 0.f, 1.00f)
	};
};

/**
 * Designer-owned survival mission. A zone can reference this asset to run endless round loops without configuring placed actors.
 */
UCLASS(BlueprintType)
class AEYERJI_API UAeyerjiSurvivalMissionDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Primary authored round source. Create this from FAeyerjiSurvivalRoundTableRow and import CSV/JSON rows. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Import", meta=(RequiredAssetDataTags="RowStructure=/Script/Aeyerji.AeyerjiSurvivalRoundTableRow"))
	TObjectPtr<UDataTable> RoundTable = nullptr;

	/** When a valid RoundTable is assigned, runtime uses imported rows instead of BaseRounds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Import")
	bool bPreferRoundTable = true;

	/** Legacy fallback loaded from older assets when no valid imported RoundTable rows exist. Not designer-facing. */
	UPROPERTY()
	TArray<FAeyerjiSurvivalRoundDefinition> BaseRounds;

	/** Actor tag for the placed spawner group used to execute survival rounds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival")
	FName RoundSpawnerActorTag = NAME_None;

	/** Optional separate boss-round cadence. Zero means bosses are authored directly in survival waves. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival", meta=(ClampMin="0"))
	int32 BossEveryNRounds = 0;

	/** Starts the next cycle after a boss kill instead of completing the run. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival")
	bool bLoopAfterBoss = true;

	/** Delay between normal rounds and after looped boss kills. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival", meta=(ClampMin="0.0", Units="s"))
	float InterRoundDelaySeconds = 3.f;

	/** Multiplier applied to enemy counts every completed cycle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival", meta=(ClampMin="0.0"))
	float EnemyCountScalePerCycle = 1.25f;

	/** Blends previous authored round enemy sets into the current round so enemy rosters fade in/out instead of switching abruptly. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Blending")
	bool bBlendPreviousRoundEnemySets = true;

	/** Blends previous waves inside the same authored round. Use this when one BaseRound contains Wave 1, Wave 2, Wave 3, etc. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Blending")
	bool bBlendPreviousWaveEnemySets = true;

	/** Previous-round carry weight. 0.8 makes authored round 2 roughly 80% round 1 and 20% round 2. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Blending", meta=(ClampMin="0.0", ClampMax="1.0", EditCondition="bBlendPreviousRoundEnemySets"))
	float PreviousRoundCarryWeight = 0.8f;

	/** Maximum number of earlier authored rounds allowed to bleed into the current round. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Blending", meta=(ClampMin="1", EditCondition="bBlendPreviousRoundEnemySets"))
	int32 RoundBlendLookback = 4;

	/** Prevents boss-authored enemy sets from carrying into later non-boss rounds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Blending", meta=(EditCondition="bBlendPreviousRoundEnemySets"))
	bool bExcludeBossSetsFromRoundBlend = true;

	/** Ensures a newly introduced current-round enemy set gets at least one spawn when blending would otherwise round it to zero. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Blending", meta=(EditCondition="bBlendPreviousRoundEnemySets"))
	bool bGuaranteeCurrentRoundBlendEntries = true;

	/** Added to the player level used by enemy scaling for every completed cycle. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival", meta=(ClampMin="0"))
	int32 EnemyLevelBonusPerCycle = 1;

	/** Added to the player level used by enemy scaling for every survival round after round 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Scaling", meta=(ClampMin="0"))
	int32 EnemyLevelBonusPerRound = 0;

	/** Final HP/HPMax multiplier applied once per round step. 1.10 means round 2 has 10% more health than round 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Scaling", meta=(ClampMin="0.0"))
	float EnemyHealthMultiplierPerRound = 1.f;

	/** Final AttackDamage/SpellPower multiplier applied once per round step. 1.10 means round 2 has 10% more damage than round 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Scaling", meta=(ClampMin="0.0"))
	float EnemyDamageMultiplierPerRound = 1.f;

	/** Reissues aggro commands to all live survival enemies so they keep chasing the active player. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Aggro")
	bool bReissueAggroWhileActive = true;

	/** Seconds between repeated survival aggro commands. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Aggro", meta=(ClampMin="0.1", EditCondition="bReissueAggroWhileActive", Units="s"))
	float ReissueAggroIntervalSeconds = 10.f;

	/** Optional defendable static-mesh/GAS actor that survival enemies should attack unless players pull threat. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Defense")
	FAeyerjiSurvivalDefenseObjectiveDefinition DefenseObjective;

	/** Overrides GA_Death's player respawn delay during this survival mission. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Respawn")
	bool bOverridePlayerRespawnDelay = false;

	/** Seconds to wait before restarting a player-controlled pawn during survival death. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Respawn", meta=(ClampMin="0.0", EditCondition="bOverridePlayerRespawnDelay", Units="s"))
	float PlayerRespawnDelaySeconds = 5.f;

	/** Optional boss override. If unset, ZoneRunDefinition.BossDefinition is used. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival")
	TObjectPtr<UAeyerjiBossDefinition> BossDefinitionOverride = nullptr;

	/** Default loot reward emitted after every cleared survival round unless a round overrides it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Reward")
	FAeyerjiSurvivalRoundRewardDefinition DefaultRoundClearReward;

	/** Enables per-player between-round upgrade choices after a survival round clears. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Upgrades")
	bool bEnableRoundUpgradeChoices = true;

	/** Number of weighted options offered after each cleared round. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Upgrades", meta=(ClampMin="1", EditCondition="bEnableRoundUpgradeChoices"))
	int32 UpgradeChoicesPerOffer = 3;

	/** Seconds players have to choose before missing selections use the first offered option. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Upgrades", meta=(ClampMin="0.0", EditCondition="bEnableRoundUpgradeChoices", Units="s"))
	float UpgradeChoiceTimeoutSeconds = 20.f;

	/** 
	 * Weighted pool used to generate between-round survival upgrade offers.
	 * IMPORTANT: DisplayKey and DescriptionKey must reference rows in 
	 * Source/Aeyerji/Data/Strings/GlobalStringTable.csv so they can be localized.
	 * Current starting set (TreeMaxHP, TreeRegen, TreeReflectDamage, PlayerXP) matches the plan.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Survival|Upgrades", meta=(EditCondition="bEnableRoundUpgradeChoices", TitleProperty="OptionId"))
	TArray<FAeyerjiSurvivalUpgradeOption> RoundUpgradeOptions = {
		// Keys resolve via GlobalStringTable.csv (see Data/Strings/)
		FAeyerjiSurvivalUpgradeOption(FName(TEXT("TreeMaxHP")), EAeyerjiSurvivalUpgradeType::TreeMaxHP, FName(TEXT("SurvivalUpgradeTreeMaxHP")), FName(TEXT("SurvivalUpgradeTreeMaxHPDesc")), 100.f, 1.f),
		FAeyerjiSurvivalUpgradeOption(FName(TEXT("TreeReflectDamage")), EAeyerjiSurvivalUpgradeType::TreeReflectDamage, FName(TEXT("SurvivalUpgradeTreeReflectDamage")), FName(TEXT("SurvivalUpgradeTreeReflectDamageDesc")), 0.10f, 1.f),
		FAeyerjiSurvivalUpgradeOption(FName(TEXT("TreeRegen")), EAeyerjiSurvivalUpgradeType::TreeRegen, FName(TEXT("SurvivalUpgradeTreeRegen")), FName(TEXT("SurvivalUpgradeTreeRegenDesc")), 5.f, 1.f),
		FAeyerjiSurvivalUpgradeOption(FName(TEXT("PlayerXP")), EAeyerjiSurvivalUpgradeType::PlayerXP, FName(TEXT("SurvivalUpgradePlayerXP")), FName(TEXT("SurvivalUpgradePlayerXPDesc")), 50.f, 1.f)
	};
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

	/** Selects whether this zone launches an uncapped Standard Rift or a tier-capped Excursion. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run|Rift")
	EAeyerjiRiftActivityType RiftActivityType = EAeyerjiRiftActivityType::StandardRift;

	/** Finite ordinary-enemy budget for Standard Rifts. Excursions use their selected DataTable row instead. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run|Rift", meta=(ClampMin="1"))
	int32 StandardRiftEnemyBudget = 120;

	/** Weighted objective target for Standard Rifts. Excursions use their selected DataTable row instead. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run|Rift", meta=(ClampMin="1"))
	int32 StandardRiftProgressTargetPoints = 100;

	/** Maximum anchor distance for Standard Rift proximity and pressure activation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Run|Rift", meta=(ClampMin="0.0", Units="cm"))
	float StandardRiftActivationDistance = 2500.f;

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
	 * Called whenever a bound spawner removes tracked enemies.
	 * Survival rounds use this to publish live kill progress to replicated HUD state.
	 */
	UFUNCTION()
	void HandleSpawnerTrackedEnemiesRemoved(AAeyerjiSpawnerGroup* Spawner, int32 RemovedCount);

	/** Handles boss death from either the legacy boss spawner or a boss authored inside a survival wave. */
	UFUNCTION()
	void HandleSpawnerBossDefeated(AAeyerjiSpawnerGroup* Spawner, AActor* BossEnemy);

	/**
	 * Called when a runtime spawner advances into a new wave.
	 * Survival rounds use this for player-facing wave progress.
	 */
	UFUNCTION()
	void HandleSpawnerWaveStarted(AAeyerjiSpawnerGroup* Spawner, int32 WaveIndex);

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

	/** Final health multiplier currently applied to survival round enemies. Normal modes return 1. */
	UFUNCTION(BlueprintPure, Category="Director|Survival")
	float GetSurvivalEnemyHealthMultiplier() const { return SurvivalEnemyHealthMultiplier; }

	/** Final damage multiplier currently applied to survival round enemies. Normal modes return 1. */
	UFUNCTION(BlueprintPure, Category="Director|Survival")
	float GetSurvivalEnemyDamageMultiplier() const { return SurvivalEnemyDamageMultiplier; }

	/** True while this director owns an active survival-round run. */
	UFUNCTION(BlueprintPure, Category="Director|Survival")
	bool IsActiveSurvivalRun() const;

	/** Returns GA_Death's default delay or the survival mission override for player-controlled pawns. */
	UFUNCTION(BlueprintPure, Category="Director|Survival")
	float ResolvePlayerRespawnDelaySeconds(float DefaultDelay) const;

	/** Current defendable survival objective, if configured and resolved. */
	UFUNCTION(BlueprintPure, Category="Director|Survival")
	AActor* GetSurvivalDefenseObjectiveActor() const { return SurvivalDefenseObjectiveActor.Get(); }

	/** Returns true when survival defense objective behavior is configured for this run. */
	UFUNCTION(BlueprintPure, Category="Director|Survival")
	bool IsSurvivalDefenseObjectiveEnabled() const;

	/** Opens the client repair menu for the active defense objective after native interaction validation. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Director|Survival|Defense")
	void OpenSurvivalDefenseObjectiveRepairMenu(AAeyerjiPlayerController* Controller, AActor* ObjectiveActor) const;

	/** Validates and applies one gold-paid repair option to the active survival defense objective. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Director|Survival|Defense")
	bool TryRepairSurvivalDefenseObjective(AAeyerjiPlayerController* Controller, AActor* ObjectiveActor, FName OptionId);

	/** Records a player's between-round survival upgrade choice and applies its scaled effect. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Director|Survival|Upgrades")
	bool SubmitSurvivalUpgradeChoice(AAeyerjiPlayerState* PlayerState, FName OptionId, int32 OfferRevision);

	/** Returns the authoritative world tier currently applied to this run. */
	UFUNCTION(BlueprintPure, Category="Director|Difficulty")
	int32 GetEffectiveWorldTier() const { return WorldTier; }

	/** Finds Tier_N in the project-wide Greater Rift DataTable. */
	const FAeyerjiRiftTierRow* FindRiftTierRow(int32 RiftTier) const;

	/** Freezes server-resolved activity and optional Excursion tier values before StartRun. Authority only. */
	bool ApplyRiftActivityForNextRun(const FAeyerjiRiftActivitySnapshot& Activity, const FAeyerjiRiftTierRow* TierRow);

	/** Returns the authored activity policy for the active zone. */
	EAeyerjiRiftActivityType GetRiftActivityType() const { return RiftActivityType; }

	/** Returns the immutable activity-level snapshot used by every Rift enemy in the active run. */
	UFUNCTION(BlueprintPure, Category="Director|Rift")
	FAeyerjiRiftActivitySnapshot GetActiveRiftActivity() const { return ActiveRiftActivity; }

	/** Returns the immutable tier-derived monster-power snapshot for the active Rift. */
	UFUNCTION(BlueprintPure, Category="Director|Rift")
	FAeyerjiRiftMonsterPowerSnapshot GetActiveRiftMonsterPower() const { return ActiveRiftMonsterPower; }

	/** Returns the selective Rift multiplier for an enemy attribute; utility attributes remain neutral. */
	float GetRiftAttributeMultiplier(const FGameplayAttribute& Attribute) const;

	/** Returns the frozen Rift loot-quality multiplier, or one outside a Rift. */
	UFUNCTION(BlueprintPure, Category="Director|Rift")
	float GetActiveRiftRewardQualityMultiplier() const;

	/** Tagged PlayerStart used for boss-phase player respawns. */
	UFUNCTION(BlueprintPure, Category="Director|Rift")
	FName GetBossArenaRespawnPlayerStartTag() const { return BossArenaRespawnPlayerStartTag; }

	/** Validates the director/encounter/boss references required before authority starts a run. */
	bool ValidateRunStartReadiness(FString& OutReason);

	/** Builds a deterministic one-shot SpawnRegion plan without spawning enemies. */
	bool PrepareRiftRegionEncounterPlan(int32 RunSerial, int32 RunSeed, int32 ProgressTarget, FString& OutReason);

	/** Stops selecting unused Rift regions; activated queues and living enemies remain valid. */
	void DisableUnopenedRiftEncounterRegions();

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

	/** PlayerStart tag resolved from the active boss definition for boss-phase respawns. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	FName BossArenaRespawnPlayerStartTag = NAME_None;

	/** Positive while a Greater Rift DataTable row is frozen for the active/next run. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|Resolved")
	int32 ActiveRiftTierNumber = 0;

	/** Server-frozen activity policy and level copied from GameState before enemy planning begins. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|Resolved")
	FAeyerjiRiftActivitySnapshot ActiveRiftActivity;

	/** True after GameState has accepted and frozen an activity snapshot for the pending/active Rift run. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|Resolved")
	bool bHasActiveRiftActivity = false;

	/** Total region-enemy budget copied from the active tier row. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|Resolved")
	int32 ActiveRiftEnemyBudget = 0;

	/** Distance from region bounds at which a live run participant can consume that region. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|Resolved")
	float ActiveRiftRegionActivationDistance = 2500.f;

	/** Tier-frozen population modifiers consumed by the encounter-anchor planner. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|Resolved")
	float ActiveRiftDensityMultiplier = 1.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|Resolved")
	float ActiveRiftEliteRateMultiplier = 1.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|Resolved")
	float ActiveRiftEncounterSizeMultiplier = 1.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|Resolved")
	float ActiveRiftProgressMultiplier = 1.f;

	/** Central monster-power values copied by value so later curve edits cannot rescale this run. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|Resolved")
	FAeyerjiRiftMonsterPowerSnapshot ActiveRiftMonsterPower;

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

	/** Activity policy copied from ZoneRunDefinition and frozen into GameState when a run begins. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	EAeyerjiRiftActivityType RiftActivityType = EAeyerjiRiftActivityType::StandardRift;

	/** Standard Rift planning values copied from ZoneRunDefinition. */
	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	int32 StandardRiftEnemyBudget = 120;

	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	int32 StandardRiftProgressTargetPoints = 100;

	UPROPERTY(BlueprintReadOnly, Category="Director|Resolved")
	float StandardRiftActivationDistance = 2500.f;

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
	void ScheduleNextSurvivalRound();
	void BeginSurvivalRoundUpgradeOfferOrScheduleNextRound();
	void StartSurvivalUpgradeOffer();
	void FinishSurvivalUpgradeOffer(bool bApplyMissingSelections);
	void HandleSurvivalUpgradeOfferTimeout();
	bool BuildSurvivalUpgradeOfferOptions(TArray<FAeyerjiSurvivalUpgradeOption>& OutOptions) const;
	void CollectActiveSurvivalUpgradePlayers(TArray<AAeyerjiPlayerState*>& OutPlayers) const;
	bool IsPlayerEligibleForCurrentSurvivalUpgrade(AAeyerjiPlayerState* PlayerState) const;
	bool HasPlayerSelectedCurrentSurvivalUpgrade(AAeyerjiPlayerState* PlayerState) const;
	const FAeyerjiSurvivalUpgradeOption* FindCurrentSurvivalUpgradeOption(FName OptionId) const;
	void ApplySurvivalUpgradeOptionToPlayer(AAeyerjiPlayerState* PlayerState, const FAeyerjiSurvivalUpgradeOption& Option);
	void ApplySurvivalTreeMaxHealthUpgrade(float DeltaHP);
	void TickSurvivalDefenseObjectiveRegen();
	UFUNCTION()
	void HandleSurvivalDefenseObjectiveDamageTaken(AActor* VictimActor, AActor* InstigatorActor, float DamageTaken, FGameplayTag DamageType);
	void ApplySurvivalDefenseObjectiveReflect(AActor* Attacker, float DamageTaken, float ReflectFraction) const;
	void ClearSurvivalUpgradeOfferState();
	void PublishSurvivalRoundState(EAeyerjiSurvivalRoundPhase Phase, FName MessageKey = NAME_None);
	void PublishCurrentSurvivalRoundProgress();
	/** Finds, binds, and publishes the optional placed defense objective for survival runs. */
	void ResolveSurvivalDefenseObjective();
	/** Removes objective delegates and clears spawned-enemy objective handoff. */
	void ClearSurvivalDefenseObjective();
	/** Pushes the current defense objective and targeting settings to a spawner before activation. */
	void ApplySurvivalDefenseObjectiveToSpawner(AAeyerjiSpawnerGroup* Spawner, bool bBossRound) const;
	/** Reads current objective HP from its ASC for replicated HUD state. */
	float GetSurvivalDefenseObjectiveHealth() const;
	float GetSurvivalDefenseObjectiveMaxHealth() const;
	bool IsSurvivalDefenseObjectiveAlive() const;
	/** Clears one-shot defense objective warning bookkeeping when a new objective is resolved or removed. */
	void ResetSurvivalDefenseObjectiveHealthWarnings();
	/** Returns the warning message key to publish for the latest objective HP change, if any. */
	FName ConsumeSurvivalDefenseObjectiveHealthWarningMessage(const FOnAttributeChangeData& Data);
	/** Resolves the configured or fallback message key for a health-warning threshold. */
	FName ResolveSurvivalDefenseObjectiveHealthWarningMessageKey(int32 ThresholdIndex, float Threshold01) const;
	UFUNCTION()
	void HandleSurvivalDefenseObjectiveOutOfHealth(AActor* ObjectiveActor, AActor* InstigatorActor, float DamageTaken);
	void HandleSurvivalDefenseObjectiveHealthChanged(const FOnAttributeChangeData& Data);
	/** Spawns configured loot rewards for the just-cleared survival round. */
	void SpawnSurvivalRoundClearReward();
	/** Resolves the round override or mission default used for the current survival reward. */
	const FAeyerjiSurvivalRoundRewardDefinition* ResolveSurvivalRoundClearReward() const;
	/** Returns true when the current survival round satisfies the reward's cadence settings. */
	bool IsSurvivalRoundRewardEligible(const FAeyerjiSurvivalRoundRewardDefinition& Reward) const;
	/** Builds a server-authoritative loot context from reward overrides and current run state. */
	FLootContext BuildSurvivalRewardLootContext(const FAeyerjiSurvivalRoundRewardDefinition& Reward, AActor* PlayerActor) const;
	/** Chooses a nearby pickup location for round-clear rewards. */
	FVector GetSurvivalRewardSpawnLocation(const FAeyerjiSurvivalRoundRewardDefinition& Reward, const APawn* PlayerPawn) const;
	void BuildAuthoredSurvivalRounds(TArray<FAeyerjiSurvivalRoundDefinition>& OutRounds) const;
	/** Rebuilds the imported/base survival round cache from the current mission definition. */
	void RebuildAuthoredSurvivalRoundsCache() const;
	/** Clears cached survival round data and any in-flight preload when the zone definition changes. */
	void InvalidateSurvivalRuntimeCaches();
	/** Starts async loading for every soft enemy class referenced by survival rounds. */
	void BeginSurvivalAssetPreload();
	/** Retains hard class references after survival enemy class preloading completes. */
	void HandleSurvivalAssetPreloadComplete();
	/** Returns true once survival soft class references are loaded or no survival mission exists. */
	bool AreSurvivalAssetsReady() const;
	bool BuildRuntimeSurvivalWaves(const FAeyerjiSurvivalRoundDefinition& RoundDefinition, const TArray<FAeyerjiSurvivalRoundDefinition>& AuthoredRounds, int32 AuthoredRoundIndex, int32 CycleNumber, TArray<FWaveDefinition>& OutWaves, int32& OutEnemyCount) const;
	int32 CountRuntimeWaveEnemies(const FWaveDefinition& WaveDefinition) const;
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

	UPROPERTY(Transient)
	int32 LastSurvivalRoundClearRewardSpawnedRound = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|State")
	int32 CurrentSurvivalCycle = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|State")
	int32 CurrentSurvivalRoundEnemyTotal = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|State")
	int32 CurrentSurvivalRoundEnemiesKilled = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|State")
	int32 CurrentSurvivalWaveIndex = INDEX_NONE;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|State")
	int32 CurrentSurvivalWaveCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|State")
	int32 CurrentSurvivalWaveEnemyTotal = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|State")
	FText CurrentSurvivalWaveDisplayLabel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|State")
	bool bCurrentSurvivalWaveContainsBoss = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|State")
	int32 CurrentSurvivalWaveEnemiesKilled = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|State")
	EAeyerjiSurvivalRoundPhase CurrentSurvivalRoundPhase = EAeyerjiSurvivalRoundPhase::Inactive;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|State")
	int32 SurvivalEnemyLevelBonus = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|State")
	float SurvivalEnemyHealthMultiplier = 1.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|State")
	float SurvivalEnemyDamageMultiplier = 1.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|State")
	bool bSurvivalBossRoundActive = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|State")
	bool bSurvivalBossDefeatHandled = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|State")
	TObjectPtr<AActor> SurvivalDefenseObjectiveActor = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Director|State")
	bool bSurvivalDefenseObjectiveDestroyed = false;

	TWeakObjectPtr<UAbilitySystemComponent> CachedSurvivalDefenseObjectiveASC;
	FDelegateHandle SurvivalDefenseObjectiveHealthChangedHandle;
	FDelegateHandle SurvivalDefenseObjectiveMaxHealthChangedHandle;
	TWeakObjectPtr<UAeyerjiAttributeSet> CachedSurvivalDefenseObjectiveAttributeSet;

	/** Fired health-warning threshold indices for the currently resolved survival defense objective. */
	TSet<int32> FiredSurvivalDefenseObjectiveHealthWarningIndices;

	TWeakObjectPtr<AAeyerjiEncounterDirector> CachedEncounterDirector;
	TWeakObjectPtr<UAeyerjiLevelingComponent> CachedPlayerLeveling;

	FTimerHandle RunTimerHandle;
	bool bRunTimerExpiredBroadcast = false;
	FTimerHandle SurvivalRoundDelayHandle;
	FTimerHandle SurvivalUpgradeOfferTimeoutHandle;
	FTimerHandle SurvivalDefenseObjectiveRegenHandle;

	FAeyerjiSurvivalUpgradeOfferState ActiveSurvivalUpgradeOffer;
	int32 SurvivalUpgradeOfferRevision = 0;
	TArray<TWeakObjectPtr<AAeyerjiPlayerState>> SurvivalUpgradeEligiblePlayers;
	TArray<TWeakObjectPtr<AAeyerjiPlayerState>> SurvivalUpgradeSelectedPlayers;
	float SurvivalDefenseObjectiveReflectFraction = 0.f;
	float SurvivalDefenseObjectiveRegenPerSecond = 0.f;

	TSharedPtr<FStreamableHandle> SurvivalPreloadHandle;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UClass>> PreloadedSurvivalEnemyClasses;

	mutable TArray<FAeyerjiSurvivalRoundDefinition> CachedAuthoredSurvivalRounds;
	mutable bool bAuthoredSurvivalRoundsCacheValid = false;
	bool bSurvivalAssetsReady = false;
	bool bSurvivalAssetPreloadInProgress = false;
};
