// PlayerStatsTrackingComponent.cpp

#include "Player/PlayerStatsTrackingComponent.h"

#include "Items/ItemDefinition.h"
#include "Items/LootTypes.h"

UPlayerStatsTrackingComponent::UPlayerStatsTrackingComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UPlayerStatsTrackingComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UPlayerStatsTrackingComponent::LoadLootStats(const FPlayerLootStats& InStats)
{
	LootStats = InStats;
}

void UPlayerStatsTrackingComponent::ExtractLootStats(FPlayerLootStats& OutStats) const
{
	OutStats = LootStats;
}

bool UPlayerStatsTrackingComponent::HasPickedUpItemId(FName ItemId) const
{
	if (ItemId.IsNone())
	{
		return false;
	}

	const int32* Count = LootStats.ItemsPickedUpById.Find(ItemId);
	return Count && *Count > 0;
}

void UPlayerStatsTrackingComponent::RecordItemDropped(const FLootDropResult& Result)
{
	TrackDropRarity(Result.Rarity);

	if (FPlayerLootStats::IsLegendaryRarity(Result.Rarity))
	{
		++LootStats.TotalLegendariesDropped;
		LootStats.DropsSinceLastLegendary = 0;
		UpdateLegendaryRollingWindow(true);
	}
	else
	{
		++LootStats.DropsSinceLastLegendary;
		UpdateLegendaryRollingWindow(false);
	}
}

void UPlayerStatsTrackingComponent::RecordItemPickedUp(const UItemDefinition* ItemDef, EItemRarity Rarity)
{
	TrackPickupRarity(Rarity);

	if (FPlayerLootStats::IsLegendaryRarity(Rarity))
	{
		++LootStats.TotalLegendariesPickedUp;
	}

	if (ItemDef && ItemDef->ItemId != NAME_None)
	{
		int32& Count = LootStats.ItemsPickedUpById.FindOrAdd(ItemDef->ItemId);
		++Count;
	}
}

void UPlayerStatsTrackingComponent::TrackDropRarity(EItemRarity Rarity)
{
	const int32 Index = FPlayerLootStats::RarityToIndex(Rarity);
	if (Index != INDEX_NONE)
	{
		++LootStats.TotalItemsDroppedByRarity[Index];
	}
}

void UPlayerStatsTrackingComponent::TrackPickupRarity(EItemRarity Rarity)
{
	const int32 Index = FPlayerLootStats::RarityToIndex(Rarity);
	if (Index != INDEX_NONE)
	{
		++LootStats.TotalItemsPickedUpByRarity[Index];
	}
}

void UPlayerStatsTrackingComponent::UpdateLegendaryRollingWindow(bool bLegendaryDrop)
{
	LootStats.AppendRollingEntry(bLegendaryDrop);
}
