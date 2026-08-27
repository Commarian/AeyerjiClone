// LootTable.cpp

#include "Systems/LootTable.h"

#include "Logging/AeyerjiLog.h"
#include "Systems/AeyerjiDifficultyTuning.h"

namespace
{
	constexpr int32 MaxLootDataTableRows = 4096;
	constexpr int32 MaxLootNameFormats = 256;
	constexpr float MaxLootTableWeight = 1000000000.f;
	constexpr float MaxLootTableDifficulty = 1000000.f;

	bool IsValidLootTableRarity(const EItemRarity Rarity)
	{
		const UEnum* Enum = StaticEnum<EItemRarity>();
		return Enum && Enum->IsValidEnumValue(static_cast<int64>(Rarity));
	}

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
	if (!IsValidLootTableRarity(Rarity))
	{
		return nullptr;
	}
	const int32 FormatCount = FMath::Min(NameFormats.Num(), MaxLootNameFormats);
	for (int32 FormatIndex = 0; FormatIndex < FormatCount; ++FormatIndex)
	{
		const FItemRarityNameFormat& Entry = NameFormats[FormatIndex];
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

	if (UDataTable* Table = StatScalingTable.LoadSynchronous();
		Table && Table->GetRowStruct() == FItemStatScalingRow::StaticStruct())
	{
		const FString AttributeNameString = Attribute.GetName();
		const FName AttributeName(*AttributeNameString);
		const FName NormalizedAttributeName = NormalizeAttributeName(AttributeName);

		int32 InspectedRowCount = 0;
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			if (++InspectedRowCount > MaxLootDataTableRows)
			{
				break;
			}
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

		static TSet<FName> LoggedMissingRows;
		if (LoggedMissingRows.Num() < MaxLootDataTableRows && !LoggedMissingRows.Contains(AttributeName))
		{
			LoggedMissingRows.Add(AttributeName);
			UE_LOG(LogAeyerji, Warning, TEXT("[LootReward] Optional stat scaling missing for attribute %s (normalized=%s) in %s; leaving modifier magnitude unscaled."),
				*AttributeName.ToString(), *NormalizedAttributeName.ToString(), *GetNameSafe(Table));
		}
	}

	return nullptr;
}

const FRarityScalingRow* UAeyerjiLootTable::FindRarityScaling(EItemRarity Rarity) const
{
	if (RarityScalingTable.IsNull() || !IsValidLootTableRarity(Rarity))
	{
		return nullptr;
	}

	if (UDataTable* Table = RarityScalingTable.LoadSynchronous();
		Table && Table->GetRowStruct() == FRarityScalingRow::StaticStruct())
	{
		const UEnum* RarityEnum = StaticEnum<EItemRarity>();
		const FString RowName = RarityEnum->GetNameStringByValue(static_cast<int64>(Rarity));
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

	if (UDataTable* Table = RarityWeightsTable.LoadSynchronous();
		Table && Table->GetRowStruct() == FRarityWeightRow::StaticStruct())
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

	const int32 SafeCharacterLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(CharacterLevel);
	const float Difficulty = FMath::Clamp(
		FMath::IsFinite(DifficultyScale) ? DifficultyScale : 1.f,
		0.f,
		MaxLootTableDifficulty);

	if (UDataTable* Table = RarityWeightsTable.LoadSynchronous();
		Table && Table->GetRowStruct() == FRarityWeightRow::StaticStruct())
	{
		int32 InspectedRowCount = 0;
		for (const auto& Pair : Table->GetRowMap())
		{
			if (++InspectedRowCount > MaxLootDataTableRows)
			{
				break;
			}
			if (const FRarityWeightRow* Row = reinterpret_cast<const FRarityWeightRow*>(Pair.Value))
			{
				if (!IsValidLootTableRarity(Row->Rarity) || Row->Rarity == EItemRarity::Legendary)
				{
					continue; // keep legendary path separate via pity logic
				}

				const int32 MinLevel = Row->MinLevel > 0
					? UAeyerjiDifficultySettings::ClampGameplayLevel(Row->MinLevel)
					: 0;
				const int32 MaxLevel = Row->MaxLevel > 0
					? UAeyerjiDifficultySettings::ClampGameplayLevel(Row->MaxLevel)
					: 0;
				if (MinLevel > 0 && SafeCharacterLevel < MinLevel)
				{
					continue;
				}

				if (MaxLevel > 0 && SafeCharacterLevel > MaxLevel)
				{
					continue;
				}

				const int32 LevelDelta = MinLevel > 0 ? FMath::Max(0, SafeCharacterLevel - MinLevel) : SafeCharacterLevel;
				const double BaseWeight = FMath::IsFinite(Row->BaseWeight) ? Row->BaseWeight : 0.f;
				const double WeightPerLevel = FMath::IsFinite(Row->WeightPerLevel) ? Row->WeightPerLevel : 0.f;
				const double DifficultyMultiplier = FMath::Clamp(
					FMath::IsFinite(Row->DifficultyMultiplier) ? static_cast<double>(Row->DifficultyMultiplier) : 1.0,
					0.0,
					static_cast<double>(MaxLootTableDifficulty));
				double Weight = BaseWeight + (WeightPerLevel * LevelDelta);
				Weight *= DifficultyMultiplier * (Difficulty > 0.f ? Difficulty : 1.f);

				if (FMath::IsFinite(Weight) && Weight > 0.0)
				{
					float& StoredWeight = OutWeights.FindOrAdd(Row->Rarity);
					StoredWeight = static_cast<float>(FMath::Clamp(
						static_cast<double>(StoredWeight) + Weight,
						0.0,
						static_cast<double>(MaxLootTableWeight)));
				}
			}
		}
	}
}
