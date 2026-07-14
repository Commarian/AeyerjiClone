// LootService.cpp

#include "Systems/LootService.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Player/PlayerStatsTrackingComponent.h"
#include "Director/AeyerjiLevelDirector.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Items/ItemDefinition.h"
#include "Engine/AssetManager.h"
#include "Systems/AeyerjiDifficultyTuning.h"
#include "Systems/LootTable.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"

using FLootEntrySetGateCache = TMap<const UAeyerjiLootEntrySet*, bool>;

static UItemDefinition* ChooseFallbackItemDefinition(int32 EffectivePlayerLevel = MAX_int32);
static bool SupportsRarity(const UItemDefinition& Definition, EItemRarity Rarity);
static bool IsDefinitionEligibleForLootLevel(const UItemDefinition& Definition, int32 EffectivePlayerLevel);
static bool RollPercentChance(float PercentageChance);
static bool PassesEntrySetGate(const UAeyerjiLootEntrySet* Set, FLootEntrySetGateCache* GateCache);
static void ChooseDefinitionForContext(const FLootContext& Context, EItemRarity Rarity, const FLootTablePool* Pool, TObjectPtr<UItemDefinition>& OutDefinition, FName& OutDefinitionKey, bool& bOutDropSuppressed, FLootEntrySetGateCache* GateCache, EItemRarity* OutResolvedRarity = nullptr);
static void CollectEntries(const FLootTablePool* Pool, TArray<const FLootTableEntry*>& OutEntries, FLootEntrySetGateCache* GateCache);
static int32 ResolveEffectiveLootLevel(const FLootContext& Context);

namespace
{
	constexpr float DifficultyLootMaxScalar = 100.f;

	bool RollPercentChanceInternal(const float PercentageChance)
	{
		const float ClampedPercent = FMath::Clamp(PercentageChance, 0.f, 100.f);
		if (ClampedPercent <= 0.f)
		{
			return false;
		}

		if (ClampedPercent >= 100.f)
		{
			return true;
		}

		return FMath::FRand() <= (ClampedPercent / 100.f);
	}

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

	int32 ResolveActorGameplayLevel(const AActor* Actor)
	{
		if (!Actor)
		{
			return 0;
		}

		auto ReadLevelFromASC = [](const UAbilitySystemComponent* ASC) -> int32
		{
			if (!ASC)
			{
				return 0;
			}

			if (const UAeyerjiAttributeSet* Attr = ASC->GetSet<UAeyerjiAttributeSet>())
			{
				return UAeyerjiDifficultySettings::ClampGameplayLevel(FMath::RoundToInt(Attr->GetLevel()));
			}

			return 0;
		};

		if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Actor))
		{
			if (const int32 Level = ReadLevelFromASC(ASI->GetAbilitySystemComponent()); Level > 0)
			{
				return Level;
			}
		}

		if (const APawn* Pawn = Cast<APawn>(Actor))
		{
			if (const APlayerState* PS = Pawn->GetPlayerState())
			{
				if (const IAbilitySystemInterface* PSASI = Cast<IAbilitySystemInterface>(PS))
				{
					if (const int32 Level = ReadLevelFromASC(PSASI->GetAbilitySystemComponent()); Level > 0)
					{
						return Level;
					}
				}
			}
		}

		return 0;
	}

	FName ResolveResultDefinitionKey(const FLootDropResult& Result)
	{
		if (!Result.ItemDefinitionKey.IsNone())
		{
			return Result.ItemDefinitionKey;
		}

		if (Result.ItemDefinition)
		{
			return Result.ItemDefinition->GetDefinitionKey();
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

	void ApplyRewardQualityBias(TMap<EItemRarity, float>& RarityWeights, const float RewardQualityMultiplier)
	{
		const float Quality = FMath::Max(RewardQualityMultiplier, 0.f);
		if (RarityWeights.IsEmpty() || FMath::IsNearlyEqual(Quality, 1.f))
		{
			return;
		}

		for (TPair<EItemRarity, float>& Pair : RarityWeights)
		{
			if (Pair.Key == EItemRarity::Legendary || Pair.Value <= 0.f)
			{
				continue;
			}

			// Common remains the anchor. Epic receives the full bias, with intermediate
			// and post-Epic authored rarities receiving a smooth monotonic progression.
			const float RarityRank = static_cast<float>(static_cast<uint8>(Pair.Key));
			const float Exponent = FMath::Clamp(RarityRank / static_cast<float>(static_cast<uint8>(EItemRarity::Epic)), 0.f, 2.f);
			Pair.Value *= FMath::Pow(Quality, Exponent);
		}
	}
}

FLootDropResult ULootService::RollLoot(const FLootContext& Context)
{
	FLootDropResult Result;
	Result.SourceTag = Context.SourceTag;
	Result.PityGroup = Context.PityGroup;
	Result.PitySuccessRarity = Context.PitySuccessRarity;

	UPlayerStatsTrackingComponent* StatsComp = ResolvePlayerStats(Context);
	const FPlayerLootStats* Stats = StatsComp ? &StatsComp->GetLootStats() : nullptr;
	const int32 EffectivePlayerLevelForEligibility = ResolveEffectiveLootLevel(Context);

	const UAeyerjiLootTable* LootTable = GetLootTable();
	const FLootTablePool* MatchingPool = LootTable ? FindMatchingPool(Context, *LootTable) : nullptr;
	if (!MatchingPool && LootTable && LootTable->Pools.Num() > 0)
	{
		// No match found: fall back to the first pool so tables take precedence over context gaps.
		MatchingPool = &LootTable->Pools[0];
	}
	FLootEntrySetGateCache EntrySetGateCache;
	TArray<const FLootTableEntry*> PoolEntries;
	CollectEntries(MatchingPool, PoolEntries, &EntrySetGateCache);

	const float BaseLegendaryChance = FMath::Clamp(Context.BaseLegendaryChance, 0.f, 1.f);
	const float FinalLegendaryChance = Stats ? ComputeLegendaryChance(Context, *Stats) : BaseLegendaryChance;

	bool bDropSuppressed = false;

	FLootContext RarityContext = Context;
	const UObject* DifficultyWorldContext = Context.PlayerActor.IsValid() ? Context.PlayerActor.Get() : static_cast<const UObject*>(this);
	RarityContext.DifficultyScale = ResolveLootDifficultyScalar(Context, DifficultyWorldContext);
	if (LootTable && LootTable->RarityWeightsTable.IsValid())
	{
		LootTable->BuildRarityWeights(EffectivePlayerLevelForEligibility, RarityContext.DifficultyScale, RarityContext.RarityWeights);
	}
	else if (PoolEntries.Num() > 0)
	{
		TMap<EItemRarity, float> Aggregated;
		for (const FLootTableEntry* Entry : PoolEntries)
		{
			if (Entry && Entry->Weight > 0.f)
			{
				const FSoftObjectPath EntryPath = Entry->ItemDefinition.ToSoftObjectPath();
				if (!EntryPath.IsValid())
				{
					continue;
				}

				UItemDefinition* LoadedDefinition = Entry->ItemDefinition.LoadSynchronous();
				if (!LoadedDefinition || !IsDefinitionEligibleForLootLevel(*LoadedDefinition, EffectivePlayerLevelForEligibility))
				{
					continue;
				}

				const float DropChance = FMath::Clamp(Entry->PercentageChanceToDropInPool, 0.f, 100.f) / 100.f;
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
	ApplyRewardQualityBias(RarityContext.RarityWeights, Context.RewardQualityMultiplier);

	// Rarity rolls come from the pool weights when present; ensure your pool has entries with >0 weight for any rarity you allow here.
	Result.Rarity = ChooseRarity(RarityContext, FinalLegendaryChance, RarityContext.MinimumRarity);
	// Item level is intentionally exact. If the character is level 50, every rolled item is level 50.
	Result.ItemLevel = EffectivePlayerLevelForEligibility;
	Result.Seed = FMath::Rand();
	const int32 EffectivePlayerLevel = EffectivePlayerLevelForEligibility;

	// Propagate forced item selection when provided so downstream spawn has a definition/key.
	if (Context.ForcedItemDefinition && IsDefinitionEligibleForLootLevel(*Context.ForcedItemDefinition, EffectivePlayerLevel))
	{
		Result.ItemDefinition = Context.ForcedItemDefinition;
		Result.ItemDefinitionKey = UItemDefinition::MakeDefinitionKey(Context.ForcedItemDefinition);
	}

	// Pick definition/key from your actual loot tables based on rarity and context.
	if (!Result.ItemDefinition && Result.ItemDefinitionKey.IsNone())
	{
		ChooseDefinitionForContext(Context, Result.Rarity, MatchingPool, Result.ItemDefinition, Result.ItemDefinitionKey, bDropSuppressed, &EntrySetGateCache, &Result.Rarity);
	}

	// Secondary table fallback: use the first available entry in the matched pool when nothing was selected.
	if (!bDropSuppressed && !Result.ItemDefinition && Result.ItemDefinitionKey.IsNone() && PoolEntries.Num() > 0)
	{
		for (const FLootTableEntry* Entry : PoolEntries)
		{
			if (!Entry || Entry->Weight <= 0.f)
			{
				continue;
			}

			const FSoftObjectPath EntryPath = Entry->ItemDefinition.ToSoftObjectPath();
			if (!EntryPath.IsValid())
			{
				continue;
			}

			if (Entry->MinLevel > 0 && EffectivePlayerLevel < Entry->MinLevel)
			{
				continue;
			}

			if (Entry->MaxLevel > 0 && EffectivePlayerLevel > Entry->MaxLevel)
			{
				continue;
			}

			const float DropChance = FMath::Clamp(Entry->PercentageChanceToDropInPool, 0.f, 100.f) / 100.f;
			if (DropChance <= 0.f || (DropChance < 1.f && FMath::FRand() > DropChance))
			{
				continue;
			}

			if (UItemDefinition* Loaded = Entry->ItemDefinition.LoadSynchronous())
			{
				if (!IsDefinitionEligibleForLootLevel(*Loaded, EffectivePlayerLevel))
				{
					continue;
				}
				Result.ItemDefinition = Loaded;
				Result.ItemDefinitionKey = UItemDefinition::MakeDefinitionKey(Loaded);
			}
			else
			{
				continue;
			}

			if (Result.ItemDefinition || !Result.ItemDefinitionKey.IsNone())
			{
				Result.Rarity = Entry->Rarity;
				break;
			}
		}
	}

	// Table-wide fallback: if the matched pool was empty, walk all pools and pick the first weighted entry.
	if (!bDropSuppressed && !Result.ItemDefinition && Result.ItemDefinitionKey.IsNone() && LootTable)
	{
		for (const FLootTablePool& Pool : LootTable->Pools)
		{
			TArray<const FLootTableEntry*> AnyEntries;
			CollectEntries(&Pool, AnyEntries, &EntrySetGateCache);
			for (const FLootTableEntry* Entry : AnyEntries)
			{
				if (!Entry || Entry->Weight <= 0.f)
				{
					continue;
				}

				const FSoftObjectPath EntryPath = Entry->ItemDefinition.ToSoftObjectPath();
				if (!EntryPath.IsValid())
				{
					continue;
				}

				if (Entry->MinLevel > 0 && EffectivePlayerLevel < Entry->MinLevel)
				{
					continue;
				}

				if (Entry->MaxLevel > 0 && EffectivePlayerLevel > Entry->MaxLevel)
				{
					continue;
				}

				const float DropChance = FMath::Clamp(Entry->PercentageChanceToDropInPool, 0.f, 100.f) / 100.f;
				if (DropChance <= 0.f || (DropChance < 1.f && FMath::FRand() > DropChance))
				{
					continue;
				}

				if (UItemDefinition* Loaded = Entry->ItemDefinition.LoadSynchronous())
				{
					if (!IsDefinitionEligibleForLootLevel(*Loaded, EffectivePlayerLevel))
					{
						continue;
					}
					Result.ItemDefinition = Loaded;
					Result.ItemDefinitionKey = UItemDefinition::MakeDefinitionKey(Loaded);
				}
				else
				{
					continue;
				}

				if (Result.ItemDefinition || !Result.ItemDefinitionKey.IsNone())
				{
					Result.Rarity = Entry->Rarity;
					break;
				}
			}

			if (Result.ItemDefinition || !Result.ItemDefinitionKey.IsNone())
			{
				break;
			}
		}
	}

	// Fallback: pick any available item definition so spawn helpers do not abort.
	if (!bDropSuppressed && !Result.ItemDefinition && Result.ItemDefinitionKey.IsNone())
	{
		if (UItemDefinition* Fallback = ChooseFallbackItemDefinition(EffectivePlayerLevel))
		{
			Result.ItemDefinition = Fallback;
			Result.ItemDefinitionKey = UItemDefinition::MakeDefinitionKey(Fallback);
		}
	}

	if (Result.ItemDefinition && Result.ItemDefinitionKey.IsNone())
	{
		Result.ItemDefinitionKey = Result.ItemDefinition->GetDefinitionKey();
	}

	Result.bCountsAsPitySuccess = Context.PityGroup.IsValid()
		&& static_cast<int32>(Result.Rarity) >= static_cast<int32>(Context.PitySuccessRarity)
		&& (Result.ItemDefinition || !Result.ItemDefinitionKey.IsNone());

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
				const FName Key = ResolveResultDefinitionKey(Candidate);

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
	const FAeyerjiLootPityMemory* PityMemory = Context.PityGroup.IsValid()
		? Stats.FindPityMemory(Context.PityGroup)
		: nullptr;
	const int32 Misses = PityMemory ? PityMemory->AttemptsSinceLastSuccess : Stats.DropsSinceLastLegendary;
	const int32 EffectiveHardPity = Context.PityHardAttemptsOverride >= 0 ? Context.PityHardAttemptsOverride : HardPityDrops;
	const int32 EffectiveSoftStart = Context.PitySoftStartOverride >= 0 ? Context.PitySoftStartOverride : SoftPityStart;
	const float EffectiveSoftSlope = Context.PitySoftSlopeOverride >= 0.f ? Context.PitySoftSlopeOverride : SoftPitySlope;
	const float EffectiveMaxChance = Context.PityMaxChanceOverride >= 0.f ? Context.PityMaxChanceOverride : MaxLegendaryChance;

	// Hard pity: force success when threshold reached.
	if (EffectiveHardPity > 0 && Misses >= EffectiveHardPity)
	{
		return 1.0f;
	}

	float Chance = BaseChance;
	if (EffectiveSoftStart > 0 && Misses > EffectiveSoftStart)
	{
		const int32 Over = Misses - EffectiveSoftStart;
		Chance += Over * EffectiveSoftSlope;
	}

	const bool bWindowHasData = Stats.WindowCount >= StarvedWindowMinCount;
	const bool bWindowStarved = bWindowHasData && Stats.LegendariesInWindow == 0;
	if (bWindowStarved)
	{
		Chance += StarvedWindowBonus;
	}

	return FMath::Clamp(Chance, 0.f, EffectiveMaxChance);
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

		const int32 EffectiveLevel = ResolveEffectiveLootLevel(Context);
		if (Pool.MinLevel > 0 && EffectiveLevel < Pool.MinLevel)
		{
			continue;
		}

		if (Pool.MaxLevel > 0 && EffectiveLevel > Pool.MaxLevel)
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

static UItemDefinition* ChooseFallbackItemDefinition(int32 EffectivePlayerLevel)
{
	UAssetManager& Manager = UAssetManager::Get();
	const FPrimaryAssetType AssetType(UItemDefinition::StaticClass()->GetFName());

	TArray<FPrimaryAssetId> AssetIds;
	Manager.GetPrimaryAssetIdList(AssetType, AssetIds);
	if (AssetIds.Num() == 0)
	{
		return nullptr;
	}

	TArray<UItemDefinition*> Candidates;
	Candidates.Reserve(AssetIds.Num());
	for (const FPrimaryAssetId& AssetId : AssetIds)
	{
		UItemDefinition* Def = Cast<UItemDefinition>(Manager.GetPrimaryAssetObject(AssetId));
		if (!Def)
		{
			const FSoftObjectPath Path = Manager.GetPrimaryAssetPath(AssetId);
			if (Path.IsValid())
			{
				Def = Cast<UItemDefinition>(Manager.GetStreamableManager().LoadSynchronous(Path, false));
			}
		}

		if (Def && IsDefinitionEligibleForLootLevel(*Def, EffectivePlayerLevel))
		{
			Candidates.Add(Def);
		}
	}

	return Candidates.Num() > 0 ? Candidates[FMath::RandRange(0, Candidates.Num() - 1)] : nullptr;
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

static bool IsDefinitionEligibleForLootLevel(const UItemDefinition& Definition, int32 EffectivePlayerLevel)
{
	const int32 Level = UAeyerjiDifficultySettings::ClampGameplayLevel(EffectivePlayerLevel);
	return Level >= Definition.GetEffectiveRequiredLevel();
}

static int32 ResolveEffectiveLootLevel(const FLootContext& Context)
{
	if (Context.PlayerLevel > 0)
	{
		return UAeyerjiDifficultySettings::ClampGameplayLevel(Context.PlayerLevel);
	}

	if (const int32 ActorLevel = ResolveActorGameplayLevel(Context.PlayerActor.Get()); ActorLevel > 0)
	{
		return ActorLevel;
	}

	return UAeyerjiDifficultySettings::ClampGameplayLevel(Context.EnemyLevel);
}

static bool RollPercentChance(const float PercentageChance)
{
	return RollPercentChanceInternal(PercentageChance);
}

static bool PassesEntrySetGate(const UAeyerjiLootEntrySet* Set, FLootEntrySetGateCache* GateCache)
{
	if (!Set)
	{
		return true;
	}

	if (!GateCache)
	{
		return RollPercentChance(Set->OverallDropChance);
	}

	if (const bool* bCachedResult = GateCache->Find(Set))
	{
		return *bCachedResult;
	}

	const bool bPassed = RollPercentChance(Set->OverallDropChance);
	GateCache->Add(Set, bPassed);
	return bPassed;
}

static void CollectEntries(const FLootTablePool* Pool, TArray<const FLootTableEntry*>& OutEntries, FLootEntrySetGateCache* GateCache)
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
		const UAeyerjiLootEntrySet* Set = SetPtr.LoadSynchronous();
		if (!Set)
		{
			continue;
		}

		if (!PassesEntrySetGate(Set, GateCache))
		{
			continue;
		}

		AppendEntries(Set->Entries);
	}
}

// Scans cached item definitions to find a drop candidate for the provided context and rarity.
static void ChooseDefinitionForContext(const FLootContext& Context, EItemRarity Rarity, const FLootTablePool* Pool, TObjectPtr<UItemDefinition>& OutDefinition, FName& OutDefinitionKey, bool& bOutDropSuppressed, FLootEntrySetGateCache* GateCache, EItemRarity* OutResolvedRarity)
{
	bOutDropSuppressed = false;
	const int32 EffectivePlayerLevel = ResolveEffectiveLootLevel(Context);

	if (Pool)
	{
		float TotalWeight = 0.f;
		float AnyTotal = 0.f;
		bool bHadEligibleEntries = false;
		bool bAnyPassedDropChance = false;

		struct FWeightedEntry
		{
			const FLootTableEntry* Entry = nullptr;
			const UAeyerjiLootEntrySet* OwningSet = nullptr;
			float EffectiveWeight = 0.f;
			FSoftObjectPath DefinitionPath;
		};

		TArray<FWeightedEntry> WeightedEntries;
		WeightedEntries.Reserve(Pool->Entries.Num());

		auto AppendEntries = [&](const TArray<FLootTableEntry>& Source, const UAeyerjiLootEntrySet* OwningSet)
		{
			for (const FLootTableEntry& Entry : Source)
			{
				WeightedEntries.Add({ &Entry, OwningSet, 0.f, Entry.ItemDefinition.ToSoftObjectPath() });
			}
		};

		AppendEntries(Pool->Entries, nullptr);

		for (const TSoftObjectPtr<UAeyerjiLootEntrySet>& SetPtr : Pool->EntrySets)
		{
			if (const UAeyerjiLootEntrySet* Set = SetPtr.LoadSynchronous())
			{
				AppendEntries(Set->Entries, Set);
			}
		}

		for (FWeightedEntry& Candidate : WeightedEntries)
		{
			const FLootTableEntry* Entry = Candidate.Entry;
			if (!Entry)
			{
				continue;
			}

			if (!Candidate.DefinitionPath.IsValid())
			{
				continue;
			}

			if (Entry->Weight <= 0.f)
			{
				continue;
			}

			UItemDefinition* LoadedDefinition = Entry->ItemDefinition.LoadSynchronous();
			if (!LoadedDefinition || !IsDefinitionEligibleForLootLevel(*LoadedDefinition, EffectivePlayerLevel))
			{
				continue;
			}

			if (Entry->MinLevel > 0 && EffectivePlayerLevel < Entry->MinLevel)
			{
				continue;
			}

			if (Entry->MaxLevel > 0 && EffectivePlayerLevel > Entry->MaxLevel)
			{
				continue;
			}

			bHadEligibleEntries = true;

			if (Candidate.OwningSet && !PassesEntrySetGate(Candidate.OwningSet, GateCache))
			{
				continue;
			}

			const float DropChance = FMath::Clamp(Entry->PercentageChanceToDropInPool, 0.f, 100.f) / 100.f;
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
						OutDefinitionKey = UItemDefinition::MakeDefinitionKey(Loaded);
					}

					if (OutDefinition || !OutDefinitionKey.IsNone())
					{
						if (OutResolvedRarity)
						{
							*OutResolvedRarity = Entry->Rarity;
						}
						return;
					}
				}
			}
		}

		// Fallback within the pool: if nothing matched the rolled rarity, pick any available entry by weight.
		if (!OutDefinition && OutDefinitionKey.IsNone())
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
							OutDefinitionKey = UItemDefinition::MakeDefinitionKey(Loaded);
						}

						if (OutDefinition || !OutDefinitionKey.IsNone())
						{
							if (OutResolvedRarity)
							{
								*OutResolvedRarity = Entry->Rarity;
							}
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

			if (!IsDefinitionEligibleForLootLevel(*Def, EffectivePlayerLevel))
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
	OutDefinitionKey = UItemDefinition::MakeDefinitionKey(OutDefinition);
	if (OutResolvedRarity)
	{
		*OutResolvedRarity = Rarity;
	}
}
