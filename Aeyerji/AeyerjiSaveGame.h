// Copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/AeyerjiAbilityProgression.h"
#include "GameFramework/SaveGame.h"
#include "Abilities/AeyerjiAbilitySlot.h"
#include "Items/InventoryComponent.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "AeyerjiRunTypes.h"
#include "Player/PlayerLootStats.h"
#include "Systems/AeyerjiSaveTypes.h"
#include "Systems/AeyerjiWorldStateTypes.h"
#include "AeyerjiSaveGame.generated.h"

USTRUCT(BlueprintType)
struct FAttrSnapshot
{
	GENERATED_BODY()

	UPROPERTY(SaveGame) float XP = 0.f;
	UPROPERTY(SaveGame) int32 Level = 1;
	// add other scalar attributes as needed
};

/**
 * 
 */
UCLASS()
class AEYERJI_API UAeyerjiSaveGame : public USaveGame
{
	GENERATED_BODY()
public:
	/** Save schema version used by the manager to detect manager-era payloads. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Save")
	int32 SchemaVersion = 0;

	/** Monotonic revision used to resolve local/cloud conflicts. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Save")
	int64 Revision = 0;

	/** UTC timestamp of the last successful authoritative commit. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Save")
	FDateTime LastModifiedUtc = FDateTime::MinValue();

	/** Stable owner identity used for per-user save namespaces. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Save")
	FString OwnerKey;

	/** Artifact kind stamp used when reconciling local and cloud payloads. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Save")
	EAeyerjiSaveArtifactKind ArtifactKind = EAeyerjiSaveArtifactKind::Profile;

	/*
	 * So this is where we define what should be serialized. In the case of Aeyerji
	 * we want to serialize only the actionBar and the Attributes the character is holding
	 * at the moment of a save/load.
	 */
	
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite)
	TArray<FAeyerjiAbilitySlot> ActionBar;
	//TODO later use this new struct to save and load XP instead of using the entire attribute thing, it looks like it doesnt serialize well.
	UPROPERTY(SaveGame) FAttrSnapshot Attributes;

	UPROPERTY(SaveGame)
	FAeyerjiInventorySaveData Inventory;

	/** Currently selected passive choice (FName identifier) */
	UPROPERTY(SaveGame)
	FName SelectedPassiveId;

	UPROPERTY(SaveGame)
	TArray<FAeyerjiAbilityProgressEntry> AbilityProgressEntries;

	UPROPERTY(SaveGame)
	int32 UnspentAbilityPoints = 0;

	UPROPERTY(SaveGame)
	int32 TotalAbilityPointSpends = 0;

	/** Profile-persistent spendable currency used by survival repair and future vendors. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Currency")
	int64 Gold = 0;

	/** Lifetime loot stats (profile-level by default). */
	UPROPERTY(SaveGame)
	FPlayerLootStats LootStats;

	/** Legacy UI slider alias (0..1000) derived from the authoritative WorldTier. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Difficulty")
	float DifficultySlider = 0.f;

	/** True when a difficulty slider value was explicitly chosen. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Difficulty")
	bool bHasDifficultySelection = false;

	/** Authoritative world tier (0..999). DifficultySlider is rewritten from this value. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Difficulty")
	int32 WorldTier = 0;

	/** True when a world tier value was explicitly chosen. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Difficulty")
	bool bHasWorldTierSelection = false;

	/** Highest Greater Rift tier this profile may select. Tier 1 is the migration/default value. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Rift")
	int32 HighestUnlockedRiftTier = 1;

	/** Last Greater Rift tier selected by this profile for restart and results-screen consistency. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Rift")
	int32 LastSelectedRiftTier = 1;

	/** Best (lowest) completed run time per difficulty slider key (0..1000, rounded). */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Run")
	TMap<int32, float> BestRunTimeSecondsByDifficulty;

	/** Recent completed runs (newest first) used for local history and future leaderboards. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Run")
	TArray<FAeyerjiCompletedRunRecord> RecentRuns;

	/** Character/profile-scoped persistent world-state facts owned by this save slot. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|World State")
	TArray<FAeyerjiWorldStateEntry> WorldStateEntries;
};
