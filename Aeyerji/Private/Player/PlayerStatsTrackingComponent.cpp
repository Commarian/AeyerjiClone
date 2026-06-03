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

	FName DefinitionKey = Result.ItemDefinitionKey;
	if (DefinitionKey.IsNone() && Result.ItemDefinition)
	{
		DefinitionKey = Result.ItemDefinition->GetDefinitionKey();
	}

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

	if (Result.PityGroup.IsValid())
	{
		LootStats.RecordPityAttempt(Result.PityGroup, Result.bCountsAsPitySuccess, DefinitionKey, Result.SourceTag);
	}
}

void UPlayerStatsTrackingComponent::RecordItemPickedUp(const UItemDefinition* ItemDef, EItemRarity Rarity)
{
	TrackPickupRarity(Rarity);

	if (FPlayerLootStats::IsLegendaryRarity(Rarity))
	{
		++LootStats.TotalLegendariesPickedUp;
	}

	if (ItemDef)
	{
		const FName DefinitionKey = ItemDef->GetDefinitionKey();
		if (!DefinitionKey.IsNone())
		{
			int32& Count = LootStats.ItemsPickedUpById.FindOrAdd(DefinitionKey);
			++Count;
		}
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
