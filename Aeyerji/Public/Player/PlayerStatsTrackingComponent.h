// PlayerStatsTrackingComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Player/PlayerLootStats.h"

#include "PlayerStatsTrackingComponent.generated.h"

class UItemDefinition;
struct FLootDropResult;

/**
 * Component that owns lifetime loot statistics for a player/controller/state.
 * All loot stat mutations should flow through this component.
 */
UCLASS(ClassGroup = (Aeyerji), meta = (BlueprintSpawnableComponent))
class AEYERJI_API UPlayerStatsTrackingComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UPlayerStatsTrackingComponent();

	/** Returns a read-only view of the current loot stats. */
	const FPlayerLootStats& GetLootStats() const { return LootStats; }

	/** Records a loot drop event for pity/history tracking. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Loot|Stats")
	void RecordItemDropped(const FLootDropResult& Result);

	/** Records a successful pickup event for loot stats. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Loot|Stats")
	void RecordItemPickedUp(const UItemDefinition* ItemDef, EItemRarity Rarity);

	/** Replaces current loot stats with loaded values (e.g., from a save). */
	void LoadLootStats(const FPlayerLootStats& InStats);

	/** Copies current loot stats into an external struct (e.g., for saving). */
	void ExtractLootStats(FPlayerLootStats& OutStats) const;

	/** True if the player has ever picked up the given item identifier. */
	UFUNCTION(BlueprintPure, Category = "Aeyerji|Loot|Stats")
	bool HasPickedUpItemId(FName ItemId) const;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Loot|Stats", meta = (AllowPrivateAccess = "true"))
	FPlayerLootStats LootStats;

	void TrackDropRarity(EItemRarity Rarity);
	void TrackPickupRarity(EItemRarity Rarity);
	void UpdateLegendaryRollingWindow(bool bLegendaryDrop);
};
