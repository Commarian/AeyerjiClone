#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "Systems/LootService.h"
#include "AeyerjiRiftTypes.generated.h"

class AAeyerjiRewardPresentationActor;

/** Distinguishes an open Standard Rift from a tier-capped Excursion. */
UENUM(BlueprintType)
enum class EAeyerjiRiftActivityType : uint8
{
	StandardRift UMETA(DisplayName="Standard Rift"),
	Excursion UMETA(DisplayName="Excursion")
};

/** Immutable server-authoritative activity facts frozen before the first encounter group can activate. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiRiftActivitySnapshot
{
	GENERATED_BODY()

	/** Activity rules used to resolve the launch-level policy. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rift Activity")
	EAeyerjiRiftActivityType ActivityType = EAeyerjiRiftActivityType::StandardRift;

	/** Highest participating character level at launch, capped only for Excursions. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rift Activity")
	int32 ActivityLevel = 1;

	/** Selected Excursion rank, or zero for a Standard Rift. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rift Activity")
	int32 ExcursionTier = 0;
};

/** Immutable server-derived monster power copied into a Greater Rift run. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiRiftMonsterPowerSnapshot
{
	GENERATED_BODY()

	/** Player-facing Rift rank used to evaluate the central monster-power curves. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rift Monster Power")
	int32 MonsterPowerIndex = 1;

	/** Frozen enemy health multiplier for this run. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rift Monster Power")
	float HealthMultiplier = 1.f;

	/** Frozen enemy damage multiplier for this run. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rift Monster Power")
	float DamageMultiplier = 1.f;

	/** Frozen enemy defense/control-resistance multiplier for this run. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rift Monster Power")
	float DefenseMultiplier = 1.f;

	/** Frozen loot-quality bias. Item level continues to come from the player. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rift Monster Power")
	float RewardQualityMultiplier = 1.f;
};

/** Stable failure codes returned to Blueprint when the server rejects a Rift Tier request. */
UENUM(BlueprintType)
enum class EAeyerjiRiftTierSelectionFailure : uint8
{
	None,
	NotAuthority,
	RunAlreadyActive,
	RunNotReady,
	RequesterNotLeader,
	ProfileNotReady,
	TierNotDefined,
	TierLockedForParty
};

/** One independently eligible server-authored reward layer. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiRiftRewardLayerDefinition
{
	GENERATED_BODY()

	/** Source tag used by loot pools, pity, logs, and reward-result presentation. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift Reward")
	FGameplayTag SourceTag;

	/** Per-player rolls for this layer. Base rewards normally use 4 +/- 1, timed 3, and flawless 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift Reward")
	FLootMultiDropConfig MultiDropConfig;

	/** Optional pity bucket applied independently for this reward layer. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift Reward")
	FGameplayTag PityGroup;

	/** Layer-wide rarity floor applied before the individual bucket floor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift Reward")
	EItemRarity MinimumRarity = EItemRarity::Common;
};

/**
 * One merge-friendly Greater Rift tier row. The row name is the tier identity:
 * Tier_1, Tier_2, and so on. C++ freezes the resolved row at run start.
 */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiRiftTierRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Minimum participating character level required to launch this Excursion tier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift Tier|Activity", meta=(ClampMin="1"))
	int32 MinimumCharacterLevel = 1;

	/** Caps the frozen Activity Level for this Excursion without changing its player-facing tier rank. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift Tier|Activity", meta=(ClampMin="1"))
	int32 MaxActivityLevel = 50;

	/** Legion must die before this many seconds for timed completion. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift Tier", meta=(ClampMin="1.0", Units="s"))
	float TimeLimitSeconds = 900.f;

	/** Weighted progress points required to begin the boss phase. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift Tier", meta=(ClampMin="1"))
	int32 ProgressTargetPoints = 100;

	/** Total enemies allocated across automatically discovered SpawnRegions for this run. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift Tier|Population", meta=(ClampMin="1"))
	int32 EnemyBudget = 120;

	/** Distance from a SpawnRegion's box bounds at which a living participant consumes it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift Tier|Population", meta=(ClampMin="0.0", Units="cm"))
	float RegionActivationDistance = 2500.f;

	/** Zero generates a server seed per run; non-zero makes encounter planning reproducible. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift Tier|Population")
	int32 FixedRunSeed = 0;

	/** Scales the finite tier enemy budget before encounter-group reservations are created. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift Tier|Encounter", meta=(ClampMin="0.1"))
	float DensityMultiplier = 1.f;

	/** Multiplies authored elite chances after regional elite bonuses are applied. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift Tier|Encounter", meta=(ClampMin="0.0"))
	float EliteRateMultiplier = 1.f;

	/** Multiplies each authored encounter group's pack size while preserving the finite run budget. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift Tier|Encounter", meta=(ClampMin="0.1"))
	float EncounterSizeMultiplier = 1.f;

	/** Multiplies authored ordinary and elite progress values before the immutable death ledger registers them. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift Tier|Encounter", meta=(ClampMin="0.1"))
	float ProgressMultiplier = 1.f;

	/** Tier-specific enemy maximum-health and regeneration multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift Tier|Monster Power", meta=(ClampMin="0.0"))
	float HealthMultiplier = 1.f;

	/** Tier-specific enemy attack-damage, spell-power, and stagger-power multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift Tier|Monster Power", meta=(ClampMin="0.0"))
	float DamageMultiplier = 1.f;

	/** Tier-specific enemy armor, poise, and stagger-resistance multiplier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift Tier|Monster Power", meta=(ClampMin="0.0"))
	float DefenseMultiplier = 1.f;

	/** Loot-rarity bias for this tier. Item level still comes from the receiving player. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift Tier|Monster Power", meta=(ClampMin="0.0"))
	float RewardQualityMultiplier = 1.f;

	/** Always-eligible private pickup count. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift Tier|Rewards|Base", meta=(ClampMin="0"))
	int32 BaseRewardDrops = 4;

	/** Plus/minus random variance applied to BaseRewardDrops. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift Tier|Rewards|Base", meta=(ClampMin="0"))
	int32 BaseRewardVariance = 1;

	/** Minimum rarity for every base-layer roll. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift Tier|Rewards|Base")
	EItemRarity BaseMinimumRarity = EItemRarity::Common;

	/** Timed-cache private roll count. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift Tier|Rewards|Timed", meta=(ClampMin="0"))
	int32 TimedRewardDrops = 3;

	/** Plus/minus random variance applied to TimedRewardDrops. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift Tier|Rewards|Timed", meta=(ClampMin="0"))
	int32 TimedRewardVariance = 0;

	/** Minimum rarity for every timed-layer roll. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift Tier|Rewards|Timed")
	EItemRarity TimedMinimumRarity = EItemRarity::Common;

	/** Flawless-cache private roll count. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift Tier|Rewards|Flawless", meta=(ClampMin="0"))
	int32 FlawlessRewardDrops = 1;

	/** Plus/minus random variance applied to FlawlessRewardDrops. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift Tier|Rewards|Flawless", meta=(ClampMin="0"))
	int32 FlawlessRewardVariance = 0;

	/** Minimum rarity for every flawless-layer roll. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift Tier|Rewards|Flawless")
	EItemRarity FlawlessMinimumRarity = EItemRarity::Common;

	/** Optional shared visual cache class; empty uses the native presentation actor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift Tier|Rewards")
	TSoftClassPtr<AAeyerjiRewardPresentationActor> BonusRewardPresentationClass;
};

/** Shared server-authored run facts replicated through AAeyerjiGameState. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiRiftRunState
{
	GENERATED_BODY()

	/** Monotonic authority-issued run identifier used by every one-shot guard. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rift Run")
	int32 RunSerial = 0;

	/** Seed used to reproduce encounter planning and diagnose a run. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rift Run")
	int32 RunSeed = 0;

	/** Shared Rift Tier frozen for this run. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rift Run")
	int32 SelectedRiftTier = 1;

	/** Replicated authority snapshot used for every Rift enemy-level decision in this run. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rift Run")
	FAeyerjiRiftActivitySnapshot Activity;

	/** Central tier-derived combat/reward power frozen at run startup. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rift Run")
	FAeyerjiRiftMonsterPowerSnapshot MonsterPower;

	/** Replicated server-world timestamp captured when the run becomes active. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rift Run")
	float StartServerTimeSeconds = 0.f;

	/** Timed-success limit frozen from the tier profile. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rift Run")
	float TimeLimitSeconds = 900.f;

	/** True once weighted progress irreversibly opens the boss phase. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rift Run")
	bool bBossPhaseStarted = false;

	/** True once the time limit has elapsed; the run remains completable. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rift Run")
	bool bOvertime = false;

	/** True once Legion's authoritative death has been accepted. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rift Run")
	bool bBossDefeated = false;

	/** Final timed result, evaluated exactly once when Legion dies. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rift Run")
	bool bCompletedInTime = false;

	/** Shared flawless-loss fact set by the first boss-phase player death. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rift Run")
	bool bBossPhaseDeathOccurred = false;

	/** True after every participant has an immutable reward ledger. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rift Run")
	bool bRewardsFinalized = false;

	/** Next tier earned by this run, or zero when no tier advancement was earned. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rift Run")
	int32 EarnedNextRiftTier = 0;

	/** Monotonic revision used by event-driven Blueprint presentation. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Rift Run")
	int32 Revision = 0;
};
