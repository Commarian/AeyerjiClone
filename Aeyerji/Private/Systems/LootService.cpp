// LootService.cpp

#include "Systems/LootService.h"

#include "Player/PlayerStatsTrackingComponent.h"
#include "Director/AeyerjiLevelDirector.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Items/ItemDefinition.h"
#include "Engine/AssetManager.h"
#include "Systems/LootTable.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"

static UItemDefinition* ChooseFallbackItemDefinition();
static bool SupportsRarity(const UItemDefinition& Definition, EItemRarity Rarity);
static void ChooseDefinitionForContext(const FLootContext& Context, EItemRarity Rarity, const FLootTablePool* Pool, TObjectPtr<UItemDefinition>& OutDefinition, FName& OutItemId, bool& bOutDropSuppressed);
static void CollectEntries(const FLootTablePool* Pool, TArray<const FLootTableEntry*>& OutEntries);
static int32 TriangularRollInt(int32 Min, int32 Mode, int32 Max);

namespace
{
	constexpr float DifficultyLootMaxScalar = 100.f;

	float ResolveRunDifficultyAlpha(const UObject* WorldContextObject)
	{
		if (!WorldContextObject)
		{
			return 0.f;
		}

		UWorld* World = WorldContextObject->GetWorld();
		if (!World)
		{
			return 0.f;
		}

		// Prefer the LevelDirector, since it owns the run's difficulty slider in-level.
		for (TActorIterator<AAeyerjiLevelDirector> It(World); It; ++It)
		{
			if (AAeyerjiLevelDirector* Director = *It)
			{
				return FMath::Clamp(Director->GetCurvedDifficulty(), 0.f, 1.f);
			}
		}

		return 0.f;
	}

	float ResolveLootDifficultyScalar(const FLootContext& Context, const UObject* WorldContextObject)
	{
		// Treat "1.0" as the blueprint default meaning "use the run difficulty" for the MVP.
		// Explicit overrides are still supported by setting DifficultyScale to something else.
		const float Provided = Context.DifficultyScale;
		if (Provided > 0.f && !FMath::IsNearlyEqual(Provided, 1.f))
		{
			return Provided;
		}

		const float Alpha = ResolveRunDifficultyAlpha(WorldContextObject);
		return FMath::Lerp(1.f, DifficultyLootMaxScalar, Alpha);
	}

	int32 RollCountWithVariance(int32 Base, int32 Variance)
	{
		const int32 SafeBase = FMath::Max(0, Base);
		const int32 SafeVariance = FMath::Max(0, Variance);
		if (SafeVariance <= 0)
		{
			return SafeBase;
		}

		const int32 Delta = FMath::RandRange(-SafeVariance, SafeVariance);
		return FMath::Max(0, SafeBase + Delta);
	}

	FName ResolveResultId(const FLootDropResult& Result)
	{
		if (!Result.ItemId.IsNone())
		{
			return Result.ItemId;
		}

		if (Result.ItemDefinition)
		{
			return Result.ItemDefinition->ItemId;
		}

		return NAME_None;
	}

	void DebugConfigMessage(const UObject* WorldContextObject, const FString& Message, const FColor& Color, float Duration)
	{
		UE_LOG(LogTemp, Warning, TEXT("%s"), *Message);

		if (!GEngine)
		{
			return;
		}

		UWorld* World = nullptr;
		if (const UObject* ContextObj = WorldContextObject)
		{
			World = ContextObj->GetWorld();
		}

		if (World)
		{
			GEngine->AddOnScreenDebugMessage(-1, Duration, Color, Message);
		}
	}
}

FLootDropResult ULootService::RollLoot(const FLootContext& Context)
{
	FLootDropResult Result;

	UPlayerStatsTrackingComponent* StatsComp = ResolvePlayerStats(Context);
	const FPlayerLootStats* Stats = StatsComp ? &StatsComp->GetLootStats() : nullptr;

	const UAeyerjiLootTable* LootTable = GetLootTable();
	const FLootTablePool* MatchingPool = LootTable ? FindMatchingPool(Context, *LootTable) : nullptr;
	if (!MatchingPool && LootTable && LootTable->Pools.Num() > 0)
	{
		// No match found: fall back to the first pool so tables take precedence over context gaps.
		MatchingPool = &LootTable->Pools[0];
	}
	TArray<const FLootTableEntry*> PoolEntries;
	CollectEntries(MatchingPool, PoolEntries);

	const float BaseLegendaryChance = FMath::Clamp(Context.BaseLegendaryChance, 0.f, 1.f);
	const float FinalLegendaryChance = Stats ? ComputeLegendaryChance(Context, *Stats) : BaseLegendaryChance;

	bool bDropSuppressed = false;

	FLootContext RarityContext = Context;
	const UObject* DifficultyWorldContext = Context.PlayerActor.IsValid() ? Context.PlayerActor.Get() : static_cast<const UObject*>(this);
	RarityContext.DifficultyScale = ResolveLootDifficultyScalar(Context, DifficultyWorldContext);
	if (LootTable && LootTable->RarityWeightsTable.IsValid())
	{
		LootTable->BuildRarityWeights(Context.EnemyLevel, RarityContext.DifficultyScale, RarityContext.RarityWeights);
	}
	else if (PoolEntries.Num() > 0)
	{
		TMap<EItemRarity, float> Aggregated;
		for (const FLootTableEntry* Entry : PoolEntries)
		{
			if (Entry && Entry->Weight > 0.f)
			{
				const float DropChance = FMath::Clamp(Entry->DropChance, 0.f, 1.f);
				const float EffectiveWeight = DropChance > 0.f ? (Entry->Weight * DropChance) : 0.f;
				if (EffectiveWeight > 0.f)
				{
					Aggregated.FindOrAdd(Entry->Rarity) += EffectiveWeight;
				}
			}
		}

		if (Aggregated.Num() > 0)
		{
			RarityContext.RarityWeights = Aggregated;
		}
	}
	else if (Context.RarityWeights.Num() > 0)
	{
		RarityContext.RarityWeights = Context.RarityWeights;
	}

	// Rarity rolls come from the pool weights when present; ensure your pool has entries with >0 weight for any rarity you allow here.
	Result.Rarity = ChooseRarity(RarityContext, FinalLegendaryChance, RarityContext.MinimumRarity);
	const int32 BaseItemLevel = (Context.PlayerLevel > 0) ? Context.PlayerLevel : Context.EnemyLevel;
	int32 ItemLevel = FMath::Max(1, BaseItemLevel);
	{
		int32 Low = ItemLevel + Context.ItemLevelJitterMin;
		int32 High = ItemLevel + Context.ItemLevelJitterMax;
		if (Low > High)
		{
			Swap(Low, High);
		}

		if (Low != High)
		{
			ItemLevel = FMath::Max(1, TriangularRollInt(Low, ItemLevel, High));
		}
	}
	Result.ItemLevel = ItemLevel;
	Result.Seed = FMath::Rand();

	// Propagate forced item selection when provided so downstream spawn has a definition/id.
	if (Context.ForcedItemDefinition)
	{
		Result.ItemDefinition = Context.ForcedItemDefinition;
		Result.ItemId = Context.ForcedItemDefinition->ItemId;
	}
	else if (Context.ForcedItemId != NAME_None)
	{
		Result.ItemId = Context.ForcedItemId;
	}

	// Pick ItemId/Definition from your actual loot tables based on rarity and context.
	if (!Result.ItemDefinition && Result.ItemId.IsNone())
	{
		ChooseDefinitionForContext(Context, Result.Rarity, MatchingPool, Result.ItemDefinition, Result.ItemId, bDropSuppressed);
	}

	// Secondary table fallback: use the first available entry in the matched pool when nothing was selected.
	if (!bDropSuppressed && !Result.ItemDefinition && Result.ItemId.IsNone() && PoolEntries.Num() > 0)
	{
		for (const FLootTableEntry* Entry : PoolEntries)
		{
			if (!Entry || Entry->Weight <= 0.f)
			{
				continue;
			}

			const float DropChance = FMath::Clamp(Entry->DropChance, 0.f, 1.f);
			if (DropChance <= 0.f || (DropChance < 1.f && FMath::FRand() > DropChance))
			{
				continue;
			}

			if (UItemDefinition* Loaded = Entry->ItemDefinition.LoadSynchronous())
			{
				Result.ItemDefinition = Loaded;
				Result.ItemId = Loaded->ItemId;
			}
			else if (!Entry->ItemId.IsNone())
			{
				Result.ItemId = Entry->ItemId;
			}

			if (Result.ItemDefinition || !Result.ItemId.IsNone())
			{
				break;
			}
		}
	}

	// Table-wide fallback: if the matched pool was empty, walk all pools and pick the first weighted entry.
	if (!bDropSuppressed && !Result.ItemDefinition && Result.ItemId.IsNone() && LootTable)
	{
		for (const FLootTablePool& Pool : LootTable->Pools)
		{
			TArray<const FLootTableEntry*> AnyEntries;
			CollectEntries(&Pool, AnyEntries);
			for (const FLootTableEntry* Entry : AnyEntries)
			{
				if (!Entry || Entry->Weight <= 0.f)
				{
					continue;
				}

				const float DropChance = FMath::Clamp(Entry->DropChance, 0.f, 1.f);
				if (DropChance <= 0.f || (DropChance < 1.f && FMath::FRand() > DropChance))
				{
					continue;
				}

				if (UItemDefinition* Loaded = Entry->ItemDefinition.LoadSynchronous())
				{
					Result.ItemDefinition = Loaded;
					Result.ItemId = Loaded->ItemId;
				}
				else if (!Entry->ItemId.IsNone())
				{
					Result.ItemId = Entry->ItemId;
				}

				if (Result.ItemDefinition || !Result.ItemId.IsNone())
				{
					break;
				}
			}

			if (Result.ItemDefinition || !Result.ItemId.IsNone())
			{
				break;
			}
		}
	}

	// Fallback: pick any available item definition so spawn helpers do not abort.
	if (!bDropSuppressed && !Result.ItemDefinition && Result.ItemId.IsNone())
	{
		if (UItemDefinition* Fallback = ChooseFallbackItemDefinition())
		{
			Result.ItemDefinition = Fallback;
			Result.ItemId = Fallback->ItemId;
		}
	}

	if (StatsComp)
	{
		StatsComp->RecordItemDropped(Result);
	}

	return Result;
}

bool ULootService::RollMultiDrop(const FLootContext& BaseContext, const FLootMultiDropConfig& Config, TArray<FLootDropResult>& OutResults)
{
	OutResults.Reset();

	auto ShowDebug = [&](const FString& Msg)
	{
		if (Config.bLogDebugToScreen)
		{
			DebugConfigMessage(this, Msg, FColor::Red, Config.DebugScreenDuration);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("%s"), *Msg);
		}
	};

	if (Config.TotalBaseDrops < 0 || Config.TotalVariance < 0)
	{
		ShowDebug(TEXT("Loot multi-drop config has negative TotalBaseDrops/TotalVariance."));
		return false;
	}

	for (const FLootMultiDropBucket& Bucket : Config.Buckets)
	{
		if (Bucket.BaseDrops < 0 || Bucket.Variance < 0)
		{
			ShowDebug(FString::Printf(TEXT("Loot multi-drop bucket %s has negative BaseDrops/Variance."), *Bucket.Tag.ToString()));
			return false;
		}
	}

	if (Config.Buckets.Num() == 0 && Config.TotalBaseDrops <= 0)
	{
		ShowDebug(TEXT("Loot multi-drop config is empty (no buckets and total drops <= 0)."));
		return false;
	}

	const int32 TotalTarget = RollCountWithVariance(Config.TotalBaseDrops, Config.TotalVariance);

	TArray<FLootMultiDropBucket> Buckets = Config.Buckets;
	if (Config.bShuffleBuckets && Buckets.Num() > 1)
	{
		for (int32 Idx = Buckets.Num() - 1; Idx > 0; --Idx)
		{
			const int32 SwapIdx = FMath::RandRange(0, Idx);
			if (Idx != SwapIdx)
			{
				Buckets.Swap(Idx, SwapIdx);
			}
		}
	}

	const int32 RetryBudget = FMath::Max(0, Config.UniquenessRetryCount);
	const bool bEnforceGlobalUnique = Config.bRequireTotalUnique;

	TSet<FName> GlobalSeen;

	for (const FLootMultiDropBucket& Bucket : Buckets)
	{
		const int32 RemainingRoom = (TotalTarget > 0) ? FMath::Max(0, TotalTarget - OutResults.Num()) : INT32_MAX;
		int32 TargetForBucket = RollCountWithVariance(Bucket.BaseDrops, Bucket.Variance);
		if (TotalTarget > 0 && TargetForBucket > RemainingRoom)
		{
			TargetForBucket = RemainingRoom;
		}

		if (TargetForBucket <= 0)
		{
			continue;
		}

		TSet<FName> BucketSeen;

		for (int32 RollIdx = 0; RollIdx < TargetForBucket; ++RollIdx)
		{
			FLootContext ContextForBucket = BaseContext;
			const int32 MergedMinRarity = FMath::Max(static_cast<int32>(ContextForBucket.MinimumRarity), static_cast<int32>(Bucket.MinimumRarity));
			ContextForBucket.MinimumRarity = static_cast<EItemRarity>(MergedMinRarity);

			bool bAccepted = false;

			for (int32 Attempt = 0; Attempt <= RetryBudget; ++Attempt)
			{
				FLootDropResult Candidate = RollLoot(ContextForBucket);
				const FName Key = ResolveResultId(Candidate);

				const bool bNeedUnique = Bucket.bUniqueWithinBucket || Bucket.bUniqueAcrossBuckets || bEnforceGlobalUnique;
				bool bIsDuplicate = false;

				if (bNeedUnique && !Key.IsNone())
				{
					if ((Bucket.bUniqueWithinBucket && BucketSeen.Contains(Key)) ||
						((Bucket.bUniqueAcrossBuckets || bEnforceGlobalUnique) && GlobalSeen.Contains(Key)))
					{
						bIsDuplicate = true;
					}
				}

				if (!bIsDuplicate)
				{
					OutResults.Add(Candidate);

					if (!Key.IsNone())
					{
						if (Bucket.bUniqueWithinBucket)
						{
							BucketSeen.Add(Key);
						}

						if (Bucket.bUniqueAcrossBuckets || bEnforceGlobalUnique)
						{
							GlobalSeen.Add(Key);
						}
					}

					bAccepted = true;
					break;
				}
			}

			if (!bAccepted)
			{
				ShowDebug(FString::Printf(TEXT("Loot multi-drop uniqueness exhausted in bucket %s"), *Bucket.Tag.ToString()));
			}

			if (TotalTarget > 0 && OutResults.Num() >= TotalTarget)
			{
				break;
			}
		}

		if (TotalTarget > 0 && OutResults.Num() >= TotalTarget)
		{
			break;
		}
	}

	if (TotalTarget > 0 && OutResults.Num() < TotalTarget)
	{
		const int32 Remaining = TotalTarget - OutResults.Num();
		for (int32 Idx = 0; Idx < Remaining; ++Idx)
		{
			OutResults.Add(RollLoot(BaseContext));
		}
	}

	return true;
}

float ULootService::ComputeLegendaryChance(const FLootContext& Context, const FPlayerLootStats& Stats) const
{
	const float BaseChance = FMath::Clamp(Context.BaseLegendaryChance, 0.f, 1.f);

	// Hard pity: force success when threshold reached.
	if (HardPityDrops > 0 && Stats.DropsSinceLastLegendary >= HardPityDrops)
	{
		return 1.0f;
	}

	float Chance = BaseChance;
	if (SoftPityStart > 0 && Stats.DropsSinceLastLegendary > SoftPityStart)
	{
		const int32 Over = Stats.DropsSinceLastLegendary - SoftPityStart;
		Chance += Over * SoftPitySlope;
	}

	const bool bWindowHasData = Stats.WindowCount >= StarvedWindowMinCount;
	const bool bWindowStarved = bWindowHasData && Stats.LegendariesInWindow == 0;
	if (bWindowStarved)
	{
		Chance += StarvedWindowBonus;
	}

	return FMath::Clamp(Chance, 0.f, MaxLegendaryChance);
}

UPlayerStatsTrackingComponent* ULootService::ResolvePlayerStats(const FLootContext& Context) const
{
	AActor* Actor = Context.PlayerActor.Get();
	if (!Actor)
	{
		return nullptr;
	}

	// Common case: stats component lives on player state.
	if (const APawn* Pawn = Cast<APawn>(Actor))
	{
		if (APlayerState* PS = Pawn->GetPlayerState())
		{
			if (UPlayerStatsTrackingComponent* FromPS = PS->FindComponentByClass<UPlayerStatsTrackingComponent>())
			{
				return FromPS;
			}
		}
	}

	return Actor->FindComponentByClass<UPlayerStatsTrackingComponent>();
}

UAeyerjiLootTable* ULootService::GetLootTable() const
{
	if (CachedLootTable.IsValid())
	{
		return CachedLootTable.Get();
	}

	if (LootTableAsset.IsNull())
	{
		return nullptr;
	}

	if (UAeyerjiLootTable* Loaded = LootTableAsset.LoadSynchronous())
	{
		CachedLootTable = Loaded;
		return Loaded;
	}

	return nullptr;
}

const FLootTablePool* ULootService::FindMatchingPool(const FLootContext& Context, const UAeyerjiLootTable& Table) const
{
	for (const FLootTablePool& Pool : Table.Pools)
	{
		if (Pool.SourceTag.IsValid())
		{
			if (!Context.SourceTag.IsValid() || !Context.SourceTag.MatchesTag(Pool.SourceTag))
			{
				continue;
			}
		}

		if (Pool.MinWorldTier > 0 && Context.WorldTier < Pool.MinWorldTier)
		{
			continue;
		}

		if (Pool.MaxWorldTier > 0 && Context.WorldTier > Pool.MaxWorldTier)
		{
			continue;
		}

		if (Pool.MinLevel > 0 && Context.EnemyLevel < Pool.MinLevel)
		{
			continue;
		}

		if (Pool.MaxLevel > 0 && Context.EnemyLevel > Pool.MaxLevel)
		{
			continue;
		}

		return &Pool;
	}

	return nullptr;
}

EItemRarity ULootService::ChooseRarity(const FLootContext& Context, float LegendaryChance, EItemRarity MinimumRarity) const
{
	const float Roll = FMath::FRand();
	if (Roll <= LegendaryChance)
	{
		return EItemRarity::Legendary;
	}

	// Pick among non-legendary weights if provided.
	if (Context.RarityWeights.Num() > 0)
	{
		TArray<TPair<EItemRarity, float>> Entries;
		Entries.Reserve(Context.RarityWeights.Num());

		float TotalWeight = 0.f;
		for (const TPair<EItemRarity, float>& Pair : Context.RarityWeights)
		{
			if (Pair.Key == EItemRarity::Legendary)
			{
				continue; // handled by LegendaryChance above
			}

			const float Weight = FMath::Max(0.f, Pair.Value);
			if (Weight > 0.f)
			{
				Entries.Add(Pair);
				TotalWeight += Weight;
			}
		}

		if (TotalWeight > KINDA_SMALL_NUMBER)
		{
			const float RollWeight = FMath::FRandRange(0.f, TotalWeight);
			float Accum = 0.f;
			for (const TPair<EItemRarity, float>& Pair : Entries)
			{
				Accum += Pair.Value;
				if (RollWeight <= Accum)
				{
					return Pair.Key;
				}
			}
		}
	}

	return MinimumRarity;
}

static UItemDefinition* ChooseFallbackItemDefinition()
{
	UAssetManager& Manager = UAssetManager::Get();
	const FPrimaryAssetType AssetType(UItemDefinition::StaticClass()->GetFName());

	TArray<FPrimaryAssetId> AssetIds;
	Manager.GetPrimaryAssetIdList(AssetType, AssetIds);
	if (AssetIds.Num() == 0)
	{
		return nullptr;
	}

	const int32 Index = FMath::RandRange(0, AssetIds.Num() - 1);
	const FPrimaryAssetId& Chosen = AssetIds[Index];

	if (UItemDefinition* Def = Cast<UItemDefinition>(Manager.GetPrimaryAssetObject(Chosen)))
	{
		return Def;
	}

	// Try loading if not in memory yet.
	const FSoftObjectPath Path = Manager.GetPrimaryAssetPath(Chosen);
	if (Path.IsValid())
	{
		return Cast<UItemDefinition>(Manager.GetStreamableManager().LoadSynchronous(Path, false));
	}

	return nullptr;
}

static bool SupportsRarity(const UItemDefinition& Definition, EItemRarity Rarity)
{
	for (const FItemRarityAffixRange& Range : Definition.RarityAffixRanges)
	{
		if (Range.Rarity == Rarity)
		{
			return true;
		}
	}

	return false;
}

static void CollectEntries(const FLootTablePool* Pool, TArray<const FLootTableEntry*>& OutEntries)
{
	if (!Pool)
	{
		return;
	}

	auto AppendEntries = [&](const TArray<FLootTableEntry>& Source)
	{
		for (const FLootTableEntry& Entry : Source)
		{
			OutEntries.Add(&Entry);
		}
	};

	AppendEntries(Pool->Entries);

	for (const TSoftObjectPtr<UAeyerjiLootEntrySet>& SetPtr : Pool->EntrySets)
	{
		if (const UAeyerjiLootEntrySet* Set = SetPtr.LoadSynchronous())
		{
			AppendEntries(Set->Entries);
		}
	}
}

static int32 TriangularRollInt(int32 Min, int32 Mode, int32 Max)
{
	if (Min > Max)
	{
		Swap(Min, Max);
	}

	if (Min == Max)
	{
		return Min;
	}

	const float FMin = static_cast<float>(Min);
	const float FMax = static_cast<float>(Max);
	const float FMode = FMath::Clamp(static_cast<float>(Mode), FMin, FMax);

	const float U = FMath::FRand();
	const float C = (FMode - FMin) / (FMax - FMin);

	float Sample = 0.f;
	if (U <= C)
	{
		Sample = FMin + FMath::Sqrt(U * (FMax - FMin) * (FMode - FMin));
	}
	else
	{
		Sample = FMax - FMath::Sqrt((1.f - U) * (FMax - FMin) * (FMax - FMode));
	}

	return FMath::RoundToInt(Sample);
}

// Scans cached item definitions to find a drop candidate for the provided context and rarity.
static void ChooseDefinitionForContext(const FLootContext& Context, EItemRarity Rarity, const FLootTablePool* Pool, TObjectPtr<UItemDefinition>& OutDefinition, FName& OutItemId, bool& bOutDropSuppressed)
{
	bOutDropSuppressed = false;

	if (Pool)
	{
		float TotalWeight = 0.f;
		float AnyTotal = 0.f;
		bool bHadEligibleEntries = false;
		bool bAnyPassedDropChance = false;

		struct FWeightedEntry
		{
			const FLootTableEntry* Entry = nullptr;
			float EffectiveWeight = 0.f;
		};

		TArray<FWeightedEntry> WeightedEntries;
		WeightedEntries.Reserve(Pool->Entries.Num());

		auto AppendEntries = [&](const TArray<FLootTableEntry>& Source)
		{
			for (const FLootTableEntry& Entry : Source)
			{
				WeightedEntries.Add({ &Entry, 0.f });
			}
		};

		AppendEntries(Pool->Entries);

		for (const TSoftObjectPtr<UAeyerjiLootEntrySet>& SetPtr : Pool->EntrySets)
		{
			if (const UAeyerjiLootEntrySet* Set = SetPtr.LoadSynchronous())
			{
				AppendEntries(Set->Entries);
			}
		}

		for (FWeightedEntry& Candidate : WeightedEntries)
		{
			const FLootTableEntry* Entry = Candidate.Entry;
			if (!Entry)
			{
				continue;
			}

			if (Entry->Weight <= 0.f)
			{
				continue;
			}

			if (Entry->MinLevel > 0 && Context.EnemyLevel < Entry->MinLevel)
			{
				continue;
			}

			if (Entry->MaxLevel > 0 && Context.EnemyLevel > Entry->MaxLevel)
			{
				continue;
			}

			bHadEligibleEntries = true;

			const float DropChance = FMath::Clamp(Entry->DropChance, 0.f, 1.f);
			if (DropChance <= 0.f)
			{
				continue;
			}

			if (DropChance < 1.f && FMath::FRand() > DropChance)
			{
				continue;
			}

			bAnyPassedDropChance = true;

			const float EffectiveWeight = FMath::Max(0.f, Entry->Weight);
			if (EffectiveWeight <= 0.f)
			{
				continue;
			}

			Candidate.EffectiveWeight = EffectiveWeight;
			AnyTotal += EffectiveWeight;

			if (Entry->Rarity == Rarity)
			{
				TotalWeight += EffectiveWeight;
			}
		}

		if (TotalWeight > KINDA_SMALL_NUMBER && WeightedEntries.Num() > 0)
		{
			const float Roll = FMath::FRandRange(0.f, TotalWeight);
			float Accum = 0.f;
			for (const FWeightedEntry& Candidate : WeightedEntries)
			{
				const FLootTableEntry* Entry = Candidate.Entry;
				if (!Entry || Candidate.EffectiveWeight <= 0.f)
				{
					continue;
				}

				if (Entry->Rarity != Rarity)
				{
					continue;
				}

				Accum += Candidate.EffectiveWeight;
				if (Roll <= Accum)
				{
					if (UItemDefinition* Loaded = Entry->ItemDefinition.LoadSynchronous())
					{
						OutDefinition = Loaded;
						OutItemId = Loaded->ItemId;
					}
					else if (!Entry->ItemId.IsNone())
					{
						OutItemId = Entry->ItemId;
					}

					if (OutDefinition || !OutItemId.IsNone())
					{
						return;
					}
				}
			}
		}

		// Fallback within the pool: if nothing matched the rolled rarity, pick any available entry by weight.
		if (!OutDefinition && OutItemId.IsNone())
		{
			if (AnyTotal > KINDA_SMALL_NUMBER)
			{
				const float RollAny = FMath::FRandRange(0.f, AnyTotal);
				float AccumAny = 0.f;
				for (const FWeightedEntry& Candidate : WeightedEntries)
				{
					const FLootTableEntry* Entry = Candidate.Entry;
					if (!Entry || Candidate.EffectiveWeight <= 0.f)
					{
						continue;
					}

					AccumAny += Candidate.EffectiveWeight;
					if (RollAny <= AccumAny)
					{
						if (UItemDefinition* Loaded = Entry->ItemDefinition.LoadSynchronous())
						{
							OutDefinition = Loaded;
							OutItemId = Loaded->ItemId;
						}
						else if (!Entry->ItemId.IsNone())
						{
							OutItemId = Entry->ItemId;
						}
						break;
					}
				}
			}
		}

		if (!bAnyPassedDropChance && bHadEligibleEntries)
		{
			bOutDropSuppressed = true;
			return;
		}
	}

	UAssetManager& Manager = UAssetManager::Get();
	const FPrimaryAssetType AssetType(UItemDefinition::StaticClass()->GetFName());

	// Cache all item definitions up front to keep per-roll cost low.
	static bool bCacheBuilt = false;
	static TArray<FPrimaryAssetId> CachedAssetIds;
	static TArray<TWeakObjectPtr<UItemDefinition>> CachedDefinitions;

	if (!bCacheBuilt)
	{
		bCacheBuilt = true;

		Manager.GetPrimaryAssetIdList(AssetType, CachedAssetIds);
		CachedDefinitions.SetNum(CachedAssetIds.Num());

		for (int32 Idx = 0; Idx < CachedAssetIds.Num(); ++Idx)
		{
			const FPrimaryAssetId& Id = CachedAssetIds[Idx];

			UItemDefinition* Def = Cast<UItemDefinition>(Manager.GetPrimaryAssetObject(Id));
			if (!Def)
			{
				const FSoftObjectPath Path = Manager.GetPrimaryAssetPath(Id);
				if (Path.IsValid())
				{
					Def = Cast<UItemDefinition>(Manager.GetStreamableManager().LoadSynchronous(Path, false));
				}
			}

			if (Def)
			{
				CachedDefinitions[Idx] = Def;
			}
		}
	}

	TArray<UItemDefinition*> Candidates;
	Candidates.Reserve(CachedDefinitions.Num());

	auto AppendCandidates = [&](bool bRequireSourceTag)
	{
		for (int32 Idx = 0; Idx < CachedAssetIds.Num(); ++Idx)
		{
			UItemDefinition* Def = CachedDefinitions.IsValidIndex(Idx) ? CachedDefinitions[Idx].Get() : nullptr;
			if (!Def)
			{
				const FSoftObjectPath Path = Manager.GetPrimaryAssetPath(CachedAssetIds[Idx]);
				if (Path.IsValid())
				{
					Def = Cast<UItemDefinition>(Manager.GetStreamableManager().LoadSynchronous(Path, false));
					CachedDefinitions[Idx] = Def;
				}

				if (!Def)
				{
					continue;
				}
			}

			if (!SupportsRarity(*Def, Rarity))
			{
				continue;
			}

			if (bRequireSourceTag && Context.SourceTag.IsValid() && !Def->ItemTags.HasTag(Context.SourceTag))
			{
				continue;
			}

			Candidates.Add(Def);
		}
	};

	if (Context.SourceTag.IsValid())
	{
		AppendCandidates(true);
	}

	if (Candidates.Num() == 0)
	{
		AppendCandidates(false);
	}

	if (Candidates.Num() == 0)
	{
		return;
	}

	const int32 Index = FMath::RandRange(0, Candidates.Num() - 1);
	OutDefinition = Candidates[Index];
	OutItemId = OutDefinition ? OutDefinition->ItemId : OutItemId;
}
