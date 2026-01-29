// LootTable.cpp

#include "Systems/LootTable.h"

#include "Logging/AeyerjiLog.h"

namespace
{
	FName NormalizeAttributeName(const FName& Name)
	{
		FString NameString = Name.ToString();
		int32 DotIndex = INDEX_NONE;
		if (NameString.FindChar('.', DotIndex))
		{
			NameString = NameString.Mid(DotIndex + 1);
		}

		return FName(*NameString);
	}
}

UAeyerjiLootTable::UAeyerjiLootTable()
{
	// Provide a sensible default pool designers can tweak.
	FLootTablePool DefaultPool;
	DefaultPool.MinWorldTier = 0;
	DefaultPool.MaxWorldTier = 0; // 0 means unbounded in this scheme.

	Pools.Add(DefaultPool);

	AutoloadNote = TEXT("Auto-loaded by LootService via DefaultEngine.ini LootTableAsset -> /Game/Loot/BP_AeyerjiLootTable.BP_AeyerjiLootTable");
}

const FItemRarityNameFormat* UAeyerjiLootTable::FindNameFormat(EItemRarity Rarity) const
{
	for (const FItemRarityNameFormat& Entry : NameFormats)
	{
		if (Entry.Rarity == Rarity)
		{
			return &Entry;
		}
	}
	return nullptr;
}

const FItemStatScalingRow* UAeyerjiLootTable::FindScalingForAttribute(const FGameplayAttribute& Attribute) const
{
	if (!Attribute.IsValid() || StatScalingTable.IsNull())
	{
		return nullptr;
	}

	if (UDataTable* Table = StatScalingTable.LoadSynchronous())
	{
		const FString AttributeNameString = Attribute.GetName();
		const FName AttributeName(*AttributeNameString);
		const FName NormalizedAttributeName = NormalizeAttributeName(AttributeName);

		if (const FItemStatScalingRow* DirectRow = Table->FindRow<FItemStatScalingRow>(AttributeName, TEXT("LootTable Stat Scaling")))
		{
			return DirectRow;
		}

		if (NormalizedAttributeName != AttributeName)
		{
			if (const FItemStatScalingRow* DirectRow = Table->FindRow<FItemStatScalingRow>(NormalizedAttributeName, TEXT("LootTable Stat Scaling")))
			{
				return DirectRow;
			}
		}

		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			const FItemStatScalingRow* Row = reinterpret_cast<const FItemStatScalingRow*>(Pair.Value);
			if (!Row)
			{
				continue;
			}

			const FName RowAttributeName = Row->AttributeName.IsNone() ? Pair.Key : Row->AttributeName;
			const FName NormalizedRowAttributeName = NormalizeAttributeName(RowAttributeName);

			if (RowAttributeName == AttributeName
				|| RowAttributeName == NormalizedAttributeName
				|| NormalizedRowAttributeName == AttributeName
				|| NormalizedRowAttributeName == NormalizedAttributeName)
			{
				return Row;
			}
		}

		UE_LOG(LogAeyerji, Error, TEXT("LootTable stat scaling missing for attribute %s (normalized=%s) in %s."),
			*AttributeName.ToString(), *NormalizedAttributeName.ToString(), *GetNameSafe(Table));
	}

	return nullptr;
}

const FRarityScalingRow* UAeyerjiLootTable::FindRarityScaling(EItemRarity Rarity) const
{
	if (RarityScalingTable.IsNull())
	{
		return nullptr;
	}

	if (UDataTable* Table = RarityScalingTable.LoadSynchronous())
	{
		const FString RowName = StaticEnum<EItemRarity>()->GetNameStringByValue(static_cast<int64>(Rarity));
		return Table->FindRow<FRarityScalingRow>(FName(*RowName), TEXT("LootTable Rarity Scaling"));
	}

	return nullptr;
}

const FRarityWeightRow* UAeyerjiLootTable::FindRarityWeightRow(const FName& RowName) const
{
	if (RowName.IsNone() || RarityWeightsTable.IsNull())
	{
		return nullptr;
	}

	if (UDataTable* Table = RarityWeightsTable.LoadSynchronous())
	{
		return Table->FindRow<FRarityWeightRow>(RowName, TEXT("LootTable Rarity Weights"));
	}

	return nullptr;
}

void UAeyerjiLootTable::BuildRarityWeights(int32 CharacterLevel, float DifficultyScale, TMap<EItemRarity, float>& OutWeights) const
{
	OutWeights.Reset();

	if (RarityWeightsTable.IsNull())
	{
		return;
	}

	const float Difficulty = FMath::Max(0.f, DifficultyScale);

	if (UDataTable* Table = RarityWeightsTable.LoadSynchronous())
	{
		for (const auto& Pair : Table->GetRowMap())
		{
			if (const FRarityWeightRow* Row = reinterpret_cast<const FRarityWeightRow*>(Pair.Value))
			{
				if (Row->Rarity == EItemRarity::Legendary)
				{
					continue; // keep legendary path separate via pity logic
				}

				if (Row->MinLevel > 0 && CharacterLevel < Row->MinLevel)
				{
					continue;
				}

				if (Row->MaxLevel > 0 && CharacterLevel > Row->MaxLevel)
				{
					continue;
				}

				const int32 LevelDelta = (Row->MinLevel > 0) ? FMath::Max(0, CharacterLevel - Row->MinLevel) : CharacterLevel;
				float Weight = Row->BaseWeight + Row->WeightPerLevel * LevelDelta;
				Weight *= FMath::Max(0.f, Row->DifficultyMultiplier) * (Difficulty > 0.f ? Difficulty : 1.f);

				if (Weight > 0.f)
				{
					OutWeights.FindOrAdd(Row->Rarity) += Weight;
				}
			}
		}
	}
}
