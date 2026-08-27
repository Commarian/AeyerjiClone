#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "Inventory/AeyerjiInventoryBPFL.h"
#include "Items/ItemTypes.h"

class AAeyerjiRewardPresentationActor;
class UItemDefinition;

#include "AeyerjiTreasureTypes.generated.h"

/**
 * Designer-authored DataTable row used by a Rift treasure chest.
 *
 * This intentionally exposes only the choices that distinguish one Rift chest policy from another.
 * The LevelDirector supplies the live player, levels, world tier, difficulty, and Rift-quality state
 * when it builds the generic loot-service request on the authoritative server.
 */
USTRUCT(BlueprintType, meta=(DisplayName="Rift Treasure Loot Profile"))
struct AEYERJI_API FAeyerjiTreasureLootProfileRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Enables this row. Disabled rows are rejected before a Rift can select their points. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure|Loot")
	bool bEnabled = true;

	/** Loot source used to select a pool in the global AeyerjiLootTable. Leave empty only when a fixed item is set. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure|Loot")
	FGameplayTag SourceTag;

	/** Minimum rarity accepted for this chest's rolls. Common is the normal default; use a higher value for a deliberately better chest. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure|Loot")
	EItemRarity MinimumRarity = EItemRarity::Common;

	/** Number of loot-service rolls requested for this chest. The underlying source pool still controls which items are eligible. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure|Loot", meta=(ClampMin="1"))
	int32 DropsPerChest = 1;

	/** Optional random +/- adjustment to Drops Per Chest. Keep zero for a fixed count. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure|Loot", AdvancedDisplay, meta=(ClampMin="0"))
	int32 DropCountVariance = 0;

	/** Optional hand-authored item for a curated or test chest. It must be eligible for the current player level or the chest is skipped with a warning. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure|Loot", AdvancedDisplay, meta=(DisplayName="Fixed Item Definition"))
	TObjectPtr<UItemDefinition> ForcedItemDefinition = nullptr;

	/** Pickup ownership rule after release. The normal Rift setting is Drop Only For Instigator. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure|Loot", AdvancedDisplay)
	EItemDropDistributionMode DropMode = EItemDropDistributionMode::DropOnlyForInstigator;
};

/**
 * Rift-owned treasure placement and release policy.
 * This belongs on the ZoneRunDefinition so level designers can change a Rift without hardcoding map-specific values.
 */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiRiftTreasureSpawnConfig
{
	GENERATED_BODY()

	/** Enables runtime treasure selection for this Rift. Keep disabled until the zone has authored points and a default profile. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure")
	bool bEnabled = false;

	/** Minimum number of candidate points selected when this Rift starts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure", meta=(ClampMin="0"))
	int32 MinimumChests = 10;

	/** Maximum number of candidate points selected when this Rift starts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure", meta=(ClampMin="0"))
	int32 MaximumChests = 13;

	/** Optional actor tag for the placed Rift-start marker used for exclusion and reachability. When empty, the LevelDirector transform is used. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure|Navigation")
	FName RiftStartActorTag = NAME_None;

	/** Search extent used to project each designer-authored chest point onto nearby navigable space. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure|Navigation", meta=(Units="cm"))
	FVector NavigationProjectionExtent = FVector(250.f, 250.f, 500.f);

	/** Maximum 2D distance allowed between the visual chest transform and its projected navigation/interaction anchor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure|Navigation", meta=(ClampMin="0.0", Units="cm"))
	float MaximumNavigationAnchorDistance = 600.f;

	/** Tests a synchronous path from the Rift start navigation anchor before this candidate becomes eligible. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure|Navigation")
	bool bRequireReachableFromRiftStart = true;

	/** Excludes ordinary candidate points within this 2D distance of the Rift start anchor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure|Distribution", meta=(ClampMin="0.0", Units="cm"))
	float StartExclusionDistance = 1000.f;

	/** Candidate points closer than this 2D distance to a selected chest are never selected in the same run. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure|Distribution", meta=(ClampMin="0.0", Units="cm"))
	float HardMinimumChestSeparation = 450.f;

	/** Distance at which the soft spread weighting reaches its full value. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure|Distribution", meta=(ClampMin="0.0", Units="cm"))
	float PreferredChestSeparation = 2200.f;

	/** Strength of the soft separation bias. Zero leaves only authored weights; one strongly favors distant valid candidates. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure|Distribution", meta=(ClampMin="0.0", ClampMax="1.0"))
	float SpreadStrength = 0.75f;

	/** Reduces the selection weight of a point for each already-selected point in the same optional ZoneId. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure|Distribution", meta=(ClampMin="0.0"))
	float ZoneRepeatPenalty = 0.5f;

	/** Multiplier for a point whose optional ZoneId has not yet been represented this run. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure|Distribution", meta=(ClampMin="0.0"))
	float UnusedZoneWeightMultiplier = 1.5f;

	/** Existing replicated chest Blueprint/class used when a candidate has no class override. Assign BP_RewardChest or a derived chest. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure|Chest")
	TSubclassOf<AAeyerjiRewardPresentationActor> DefaultChestClass;

	/** Default typed DataTable row used when a candidate has no loot-profile-row override. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure|Chest", meta=(RowType="/Script/Aeyerji.AeyerjiTreasureLootProfileRow"))
	FDataTableRowHandle DefaultLootProfileRow;

	/** Enables chest proximity opening. The chest still invokes the same server-side release request used by manual interaction. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure|Auto Open")
	bool bEnableAutoOpen = false;

	/** Minimum character level required to auto-open a chest unless maximum-level mode is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure|Auto Open", meta=(ClampMin="1", EditCondition="bEnableAutoOpen"))
	int32 AutoOpenUnlockLevel = 1;

	/** Uses UAeyerjiDifficultySettings::GetMaxGameplayLevel instead of a hardcoded maximum-level requirement. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure|Auto Open", meta=(EditCondition="bEnableAutoOpen"))
	bool bRequireMaxCharacterLevelForAutoOpen = false;

	/** 2D radius in which an eligible player can trigger the chest's normal server-side release request. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure|Auto Open", meta=(ClampMin="0.0", Units="cm", EditCondition="bEnableAutoOpen"))
	float AutoOpenRadius = 260.f;

	/** Server polling interval for proximity auto-open. Zero is clamped to a safe short interval. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure|Auto Open", meta=(ClampMin="0.01", Units="s", EditCondition="bEnableAutoOpen"))
	float AutoOpenPollingInterval = 0.15f;

	/** Configures released pickups to use their existing authoritative auto-pickup path. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure|Auto Collect")
	bool bEnableAutoCollect = false;

	/** Radius passed to the existing pickup auto-collection volume after this chest releases loot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure|Auto Collect", meta=(ClampMin="1.0", Units="cm", EditCondition="bEnableAutoCollect"))
	float AutoCollectRadius = 140.f;

	/** Number of deterministic layouts sampled by the LevelDirector's editor-only simulation action. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Rift|Treasure|Debug", meta=(ClampMin="1", ClampMax="100000"))
	int32 DebugSimulationRunCount = 1000;
};

/** Pure candidate data passed from the validated map points to the seeded selector. */
struct AEYERJI_API FAeyerjiRiftTreasureSelectionCandidate
{
	FString StableId;
	FVector NavigationAnchor = FVector::ZeroVector;
	float SpawnWeight = 1.f;
	FName ZoneId = NAME_None;
};

/** Aggregated editor/runtime validation facts emitted in the Rift treasure log. */
struct AEYERJI_API FAeyerjiRiftTreasureValidationSummary
{
	int32 TotalPoints = 0;
	int32 DisabledPoints = 0;
	int32 OtherRiftPoints = 0;
	int32 MissingChestClass = 0;
	int32 MissingLootProfile = 0;
	int32 InvalidNavigation = 0;
	int32 AnchorTooFar = 0;
	int32 StartExcluded = 0;
	int32 Unreachable = 0;
	int32 ValidCandidates = 0;
};
