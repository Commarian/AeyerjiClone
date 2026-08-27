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
	constexpr float MaxLootRuntimeScalar = 1000000.f;
	constexpr float MaxLootWeight = 1000000000.f;
	constexpr float MaxLootDebugDurationSeconds = 60.f;
	constexpr int32 MaxLootPityAttempts = 1000000;
	constexpr int32 MaxLootMultiDropResults = 1024;
	constexpr int32 MaxLootMultiDropBuckets = 256;
	constexpr int32 MaxLootUniquenessRetries = 64;
	constexpr int32 MaxLootTablePoolsToInspect = 4096;
	constexpr int32 MaxLootEntriesToInspect = 16384;
	constexpr int32 MaxLootEntrySetsToInspect = 1024;
	constexpr int32 MaxLootDefinitionsToInspect = 16384;

	bool IsValidLootServiceRarity(const EItemRarity Rarity)
	{
		return IsValidLootDropRarity(Rarity);
	}

	float ResolveLootWeight(const float Weight)
	{
		return FMath::Clamp(FMath::IsFinite(Weight) ? Weight : 0.f, 0.f, MaxLootWeight);
	}

	float ResolveLootChanceFraction(const float Percentage)
	{
		return FMath::Clamp(FMath::IsFinite(Percentage) ? Percentage : 0.f, 0.f, 100.f) / 100.f;
	}

	FLootContext SanitizeLootContext(const FLootContext& Context, const UWorld* ExpectedWorld)
	{
		FLootContext Safe = Context;
		if (!Safe.PlayerActor.IsValid()
			|| (ExpectedWorld && Safe.PlayerActor->GetWorld() != ExpectedWorld))
		{
			Safe.PlayerActor.Reset();
		}
		if (!IsValid(Safe.ForcedItemDefinition))
		{
			Safe.ForcedItemDefinition = nullptr;
		}
		Safe.EnemyLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(Safe.EnemyLevel);
		Safe.PlayerLevel = Safe.PlayerLevel > 0
			? UAeyerjiDifficultySettings::ClampGameplayLevel(Safe.PlayerLevel)
			: 0;
		Safe.WorldTier = FMath::Clamp(Safe.WorldTier, 0, MaxLootPityAttempts);
		Safe.BaseLegendaryChance = FMath::Clamp(
			FMath::IsFinite(Safe.BaseLegendaryChance) ? Safe.BaseLegendaryChance : 0.f, 0.f, 1.f);
		Safe.MinimumRarity = IsValidLootServiceRarity(Safe.MinimumRarity) ? Safe.MinimumRarity : EItemRarity::Common;
		Safe.PitySuccessRarity = IsValidLootServiceRarity(Safe.PitySuccessRarity) ? Safe.PitySuccessRarity : EItemRarity::Legendary;
		Safe.DifficultyScale = FMath::Clamp(
			FMath::IsFinite(Safe.DifficultyScale) ? Safe.DifficultyScale : 1.f, 0.f, MaxLootRuntimeScalar);
		Safe.RewardQualityMultiplier = FMath::Clamp(
			FMath::IsFinite(Safe.RewardQualityMultiplier) ? Safe.RewardQualityMultiplier : 1.f, 0.f, MaxLootRuntimeScalar);
		Safe.PitySoftStartOverride = FMath::Clamp(Safe.PitySoftStartOverride, -1, MaxLootPityAttempts);
		Safe.PityHardAttemptsOverride = FMath::Clamp(Safe.PityHardAttemptsOverride, -1, MaxLootPityAttempts);
		Safe.PitySoftSlopeOverride = FMath::IsFinite(Safe.PitySoftSlopeOverride)
			? FMath::Clamp(Safe.PitySoftSlopeOverride, -1.f, 1.f)
			: -1.f;
		Safe.PityMaxChanceOverride = FMath::IsFinite(Safe.PityMaxChanceOverride)
			? FMath::Clamp(Safe.PityMaxChanceOverride, -1.f, 1.f)
			: -1.f;
		for (auto WeightIt = Safe.RarityWeights.CreateIterator(); WeightIt; ++WeightIt)
		{
			if (!IsValidLootServiceRarity(WeightIt.Key()) || !FMath::IsFinite(WeightIt.Value()))
			{
				WeightIt.RemoveCurrent();
			}
			else
			{
				WeightIt.Value() = FMath::Clamp(WeightIt.Value(), 0.f, MaxLootWeight);
			}
		}
		return Safe;
	}

	bool RollPercentChanceInternal(const float PercentageChance)
	{
		const float ClampedPercent = FMath::Clamp(
			FMath::IsFinite(PercentageChance) ? PercentageChance : 0.f,
			0.f,
			100.f);
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
				const float Difficulty = Director->GetCurvedDifficulty();
				return FMath::Clamp(FMath::IsFinite(Difficulty) ? Difficulty : 0.f, 0.f, 1.f);
			}
		}

		return 0.f;
	}

	float ResolveLootDifficultyScalar(const FLootContext& Context, const UObject* WorldContextObject)
	{
		// Treat "1.0" as the blueprint default meaning "use the run difficulty" for the MVP.
		// Explicit overrides are still supported by setting DifficultyScale to something else.
		const float Provided = FMath::IsFinite(Context.DifficultyScale) ? Context.DifficultyScale : 1.f;
		if (Provided > 0.f && !FMath::IsNearlyEqual(Provided, 1.f))
		{
			return Provided;
		}

		const float Alpha = ResolveRunDifficultyAlpha(WorldContextObject);
		return FMath::Lerp(1.f, DifficultyLootMaxScalar, Alpha);
	}

	int32 RollCountWithVariance(int32 Base, int32 Variance)
	{
		const int32 SafeBase = FMath::Clamp(Base, 0, MaxLootMultiDropResults);
		const int32 SafeVariance = FMath::Clamp(Variance, 0, MaxLootMultiDropResults);
		if (SafeVariance <= 0)
		{
			return SafeBase;
		}

		const int32 Delta = FMath::RandRange(-SafeVariance, SafeVariance);
		return static_cast<int32>(FMath::Clamp<int64>(
			static_cast<int64>(SafeBase) + Delta,
			0,
			MaxLootMultiDropResults));
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
				const float Level = Attr->GetLevel();
				return FMath::IsFinite(Level)
					? UAeyerjiDifficultySettings::FloatToGameplayLevel(Level)
					: 0;
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

		if (IsValid(Result.ItemDefinition))
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
			const float SafeDuration = FMath::Clamp(
				FMath::IsFinite(Duration) ? Duration : 0.f,
				0.f,
				MaxLootDebugDurationSeconds);
			GEngine->AddOnScreenDebugMessage(-1, SafeDuration, Color, Message);
		}
	}

	void ApplyRewardQualityBias(TMap<EItemRarity, float>& RarityWeights, const float RewardQualityMultiplier)
	{
		const float Quality = FMath::Clamp(
			FMath::IsFinite(RewardQualityMultiplier) ? RewardQualityMultiplier : 1.f,
			0.f,
			MaxLootRuntimeScalar);
		if (RarityWeights.IsEmpty() || FMath::IsNearlyEqual(Quality, 1.f))
		{
			return;
		}

		for (TPair<EItemRarity, float>& Pair : RarityWeights)
		{
			if (!IsValidLootServiceRarity(Pair.Key) || Pair.Key == EItemRarity::Legendary
				|| !FMath::IsFinite(Pair.Value) || Pair.Value <= 0.f)
			{
				continue;
			}

			// Common remains the anchor. Epic receives the full bias, with intermediate
			// and post-Epic authored rarities receiving a smooth monotonic progression.
			const float RarityRank = static_cast<float>(static_cast<uint8>(Pair.Key));
			const float Exponent = FMath::Clamp(RarityRank / static_cast<float>(static_cast<uint8>(EItemRarity::Epic)), 0.f, 2.f);
			const double BiasedWeight = static_cast<double>(Pair.Value) * FMath::Pow(static_cast<double>(Quality), Exponent);
			Pair.Value = FMath::IsFinite(BiasedWeight)
				? static_cast<float>(FMath::Clamp(BiasedWeight, 0.0, static_cast<double>(MaxLootWeight)))
				: MaxLootWeight;
		}
	}
}

bool IsValidLootDropRarity(const EItemRarity Rarity)
{
	const UEnum* const RarityEnum = StaticEnum<EItemRarity>();
	return RarityEnum && RarityEnum->IsValidEnumValue(static_cast<int64>(Rarity));
}

bool IsUsableLootDropResult(const FLootDropResult& Result)
{
	return IsValidLootDropRarity(Result.Rarity)
		&& IsValidLootDropRarity(Result.PitySuccessRarity)
		&& (IsValid(Result.ItemDefinition) || !Result.ItemDefinitionKey.IsNone());
}

FLootDropResult ULootService::RollLoot(const FLootContext& Context)
{
	const FLootContext SafeContext = SanitizeLootContext(Context, GetWorld());
	FLootDropResult Result;
	Result.SourceTag = SafeContext.SourceTag;
	Result.PityGroup = SafeContext.PityGroup;
	Result.PitySuccessRarity = SafeContext.PitySuccessRarity;

	UPlayerStatsTrackingComponent* StatsComp = ResolvePlayerStats(SafeContext);
	const FPlayerLootStats* Stats = StatsComp ? &StatsComp->GetLootStats() : nullptr;
	const int32 EffectivePlayerLevelForEligibility = ResolveEffectiveLootLevel(SafeContext);

	const UAeyerjiLootTable* LootTable = GetLootTable();
	const FLootTablePool* MatchingPool = LootTable ? FindMatchingPool(SafeContext, *LootTable) : nullptr;
	if (!MatchingPool && LootTable && LootTable->Pools.Num() > 0)
	{
		// No match found: fall back to the first pool so tables take precedence over context gaps.
		MatchingPool = &LootTable->Pools[0];
	}
	FLootEntrySetGateCache EntrySetGateCache;
	TArray<const FLootTableEntry*> PoolEntries;
	CollectEntries(MatchingPool, PoolEntries, &EntrySetGateCache);

	const float BaseLegendaryChance = SafeContext.BaseLegendaryChance;
	const float FinalLegendaryChance = Stats ? ComputeLegendaryChance(SafeContext, *Stats) : BaseLegendaryChance;

	bool bDropSuppressed = false;

	FLootContext RarityContext = SafeContext;
	const UObject* DifficultyWorldContext = SafeContext.PlayerActor.IsValid() ? SafeContext.PlayerActor.Get() : static_cast<const UObject*>(this);
	RarityContext.DifficultyScale = ResolveLootDifficultyScalar(SafeContext, DifficultyWorldContext);
	if (LootTable && LootTable->RarityWeightsTable.IsValid())
	{
		LootTable->BuildRarityWeights(EffectivePlayerLevelForEligibility, RarityContext.DifficultyScale, RarityContext.RarityWeights);
	}
	else if (PoolEntries.Num() > 0)
	{
		TMap<EItemRarity, float> Aggregated;
		for (const FLootTableEntry* Entry : PoolEntries)
		{
			if (Entry && ResolveLootWeight(Entry->Weight) > 0.f && IsValidLootServiceRarity(Entry->Rarity))
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

				const float DropChance = ResolveLootChanceFraction(Entry->PercentageChanceToDropInPool);
				const double WeightedChance = static_cast<double>(ResolveLootWeight(Entry->Weight)) * DropChance;
				const float EffectiveWeight = DropChance > 0.f
					? static_cast<float>(FMath::Clamp(WeightedChance, 0.0, static_cast<double>(MaxLootWeight)))
					: 0.f;
				if (EffectiveWeight > 0.f)
				{
					float& AggregatedWeight = Aggregated.FindOrAdd(Entry->Rarity);
					AggregatedWeight = static_cast<float>(FMath::Clamp(
						static_cast<double>(AggregatedWeight) + EffectiveWeight,
						0.0,
						static_cast<double>(MaxLootWeight)));
				}
			}
		}

		if (Aggregated.Num() > 0)
		{
			RarityContext.RarityWeights = Aggregated;
		}
	}
	else if (SafeContext.RarityWeights.Num() > 0)
	{
		RarityContext.RarityWeights = SafeContext.RarityWeights;
	}
	ApplyRewardQualityBias(RarityContext.RarityWeights, SafeContext.RewardQualityMultiplier);

	// Rarity rolls come from the pool weights when present; ensure your pool has entries with >0 weight for any rarity you allow here.
	Result.Rarity = ChooseRarity(RarityContext, FinalLegendaryChance, RarityContext.MinimumRarity);
	// Item level is intentionally exact. If the character is level 50, every rolled item is level 50.
	Result.ItemLevel = EffectivePlayerLevelForEligibility;
	Result.Seed = FMath::Rand();
	const int32 EffectivePlayerLevel = EffectivePlayerLevelForEligibility;

	// Propagate forced item selection when provided so downstream spawn has a definition/key.
	if (SafeContext.ForcedItemDefinition && IsDefinitionEligibleForLootLevel(*SafeContext.ForcedItemDefinition, EffectivePlayerLevel))
	{
		Result.ItemDefinition = SafeContext.ForcedItemDefinition;
		Result.ItemDefinitionKey = UItemDefinition::MakeDefinitionKey(SafeContext.ForcedItemDefinition);
	}

	// Pick definition/key from your actual loot tables based on rarity and context.
	if (!Result.ItemDefinition && Result.ItemDefinitionKey.IsNone())
	{
		ChooseDefinitionForContext(SafeContext, Result.Rarity, MatchingPool, Result.ItemDefinition, Result.ItemDefinitionKey, bDropSuppressed, &EntrySetGateCache, &Result.Rarity);
	}

	// Secondary table fallback: use the first available entry in the matched pool when nothing was selected.
	if (!bDropSuppressed && !Result.ItemDefinition && Result.ItemDefinitionKey.IsNone() && PoolEntries.Num() > 0)
	{
		for (const FLootTableEntry* Entry : PoolEntries)
		{
			if (!Entry || ResolveLootWeight(Entry->Weight) <= 0.f)
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

			const float DropChance = ResolveLootChanceFraction(Entry->PercentageChanceToDropInPool);
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
				Result.Rarity = IsValidLootServiceRarity(Entry->Rarity) ? Entry->Rarity : EItemRarity::Common;
				break;
			}
		}
	}

	// Table-wide fallback: if the matched pool was empty, walk all pools and pick the first weighted entry.
	if (!bDropSuppressed && !Result.ItemDefinition && Result.ItemDefinitionKey.IsNone() && LootTable)
	{
		const int32 PoolCount = FMath::Min(LootTable->Pools.Num(), MaxLootTablePoolsToInspect);
		for (int32 PoolIndex = 0; PoolIndex < PoolCount; ++PoolIndex)
		{
			const FLootTablePool& Pool = LootTable->Pools[PoolIndex];
			TArray<const FLootTableEntry*> AnyEntries;
			CollectEntries(&Pool, AnyEntries, &EntrySetGateCache);
			for (const FLootTableEntry* Entry : AnyEntries)
			{
				if (!Entry || ResolveLootWeight(Entry->Weight) <= 0.f)
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

				const float DropChance = ResolveLootChanceFraction(Entry->PercentageChanceToDropInPool);
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
					Result.Rarity = IsValidLootServiceRarity(Entry->Rarity) ? Entry->Rarity : EItemRarity::Common;
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

	Result.bCountsAsPitySuccess = SafeContext.PityGroup.IsValid()
		&& static_cast<int32>(Result.Rarity) >= static_cast<int32>(SafeContext.PitySuccessRarity)
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
	const FLootContext SafeBaseContext = SanitizeLootContext(BaseContext, GetWorld());

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

	const int32 BucketCount = FMath::Min(Config.Buckets.Num(), MaxLootMultiDropBuckets);
	for (int32 BucketIndex = 0; BucketIndex < BucketCount; ++BucketIndex)
	{
		const FLootMultiDropBucket& Bucket = Config.Buckets[BucketIndex];
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

	TArray<FLootMultiDropBucket> Buckets;
	Buckets.Reserve(BucketCount);
	for (int32 BucketIndex = 0; BucketIndex < BucketCount; ++BucketIndex)
	{
		FLootMultiDropBucket Bucket = Config.Buckets[BucketIndex];
		Bucket.BaseDrops = FMath::Clamp(Bucket.BaseDrops, 0, MaxLootMultiDropResults);
		Bucket.Variance = FMath::Clamp(Bucket.Variance, 0, MaxLootMultiDropResults);
		Bucket.MinimumRarity = IsValidLootServiceRarity(Bucket.MinimumRarity)
			? Bucket.MinimumRarity
			: EItemRarity::Common;
		Buckets.Add(MoveTemp(Bucket));
	}
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

	const int32 RetryBudget = FMath::Clamp(Config.UniquenessRetryCount, 0, MaxLootUniquenessRetries);
	const bool bEnforceGlobalUnique = Config.bRequireTotalUnique;

	TSet<FName> GlobalSeen;

	for (const FLootMultiDropBucket& Bucket : Buckets)
	{
		const int32 EffectiveTotalCap = TotalTarget > 0 ? TotalTarget : MaxLootMultiDropResults;
		const int32 RemainingRoom = FMath::Max(0, EffectiveTotalCap - OutResults.Num());
		int32 TargetForBucket = RollCountWithVariance(Bucket.BaseDrops, Bucket.Variance);
		if (TargetForBucket > RemainingRoom)
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
			FLootContext ContextForBucket = SafeBaseContext;
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

			if (OutResults.Num() >= EffectiveTotalCap)
			{
				break;
			}
		}

		if (OutResults.Num() >= (TotalTarget > 0 ? TotalTarget : MaxLootMultiDropResults))
		{
			break;
		}
	}

	if (TotalTarget > 0 && OutResults.Num() < TotalTarget)
	{
		const int32 Remaining = TotalTarget - OutResults.Num();
		for (int32 Idx = 0; Idx < Remaining; ++Idx)
		{
			OutResults.Add(RollLoot(SafeBaseContext));
		}
	}

	return true;
}

float ULootService::ComputeLegendaryChance(const FLootContext& Context, const FPlayerLootStats& Stats) const
{
	const FLootContext SafeContext = SanitizeLootContext(Context, GetWorld());
	const float BaseChance = SafeContext.BaseLegendaryChance;
	const FAeyerjiLootPityMemory* PityMemory = SafeContext.PityGroup.IsValid()
		? Stats.FindPityMemory(SafeContext.PityGroup)
		: nullptr;
	const int32 Misses = FMath::Clamp(
		PityMemory ? PityMemory->AttemptsSinceLastSuccess : Stats.DropsSinceLastLegendary,
		0,
		MaxLootPityAttempts);
	const int32 EffectiveHardPity = FMath::Clamp(
		SafeContext.PityHardAttemptsOverride >= 0 ? SafeContext.PityHardAttemptsOverride : HardPityDrops,
		0,
		MaxLootPityAttempts);
	const int32 EffectiveSoftStart = FMath::Clamp(
		SafeContext.PitySoftStartOverride >= 0 ? SafeContext.PitySoftStartOverride : SoftPityStart,
		0,
		MaxLootPityAttempts);
	const float EffectiveSoftSlope = FMath::Clamp(
		FMath::IsFinite(SafeContext.PitySoftSlopeOverride) && SafeContext.PitySoftSlopeOverride >= 0.f
			? SafeContext.PitySoftSlopeOverride
			: (FMath::IsFinite(SoftPitySlope) ? SoftPitySlope : 0.f),
		0.f,
		1.f);
	const float EffectiveMaxChance = FMath::Clamp(
		FMath::IsFinite(SafeContext.PityMaxChanceOverride) && SafeContext.PityMaxChanceOverride >= 0.f
			? SafeContext.PityMaxChanceOverride
			: (FMath::IsFinite(MaxLegendaryChance) ? MaxLegendaryChance : 0.25f),
		0.f,
		1.f);

	// Hard pity: force success when threshold reached.
	if (EffectiveHardPity > 0 && Misses >= EffectiveHardPity)
	{
		return 1.0f;
	}

	double Chance = BaseChance;
	if (EffectiveSoftStart > 0 && Misses > EffectiveSoftStart)
	{
		const int32 Over = Misses - EffectiveSoftStart;
		Chance += static_cast<double>(Over) * EffectiveSoftSlope;
	}

	const int32 SafeWindowMinCount = FMath::Clamp(StarvedWindowMinCount, 0, MaxLootPityAttempts);
	const bool bWindowHasData = Stats.WindowCount >= SafeWindowMinCount;
	const bool bWindowStarved = bWindowHasData && Stats.LegendariesInWindow == 0;
	if (bWindowStarved)
	{
		Chance += FMath::Clamp(FMath::IsFinite(StarvedWindowBonus) ? StarvedWindowBonus : 0.f, 0.f, 1.f);
	}

	return static_cast<float>(FMath::Clamp(Chance, 0.0, static_cast<double>(EffectiveMaxChance)));
}

UPlayerStatsTrackingComponent* ULootService::ResolvePlayerStats(const FLootContext& Context) const
{
	AActor* Actor = Context.PlayerActor.Get();
	if (!IsValid(Actor) || (GetWorld() && Actor->GetWorld() != GetWorld()))
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
	const int32 PoolCount = FMath::Min(Table.Pools.Num(), MaxLootTablePoolsToInspect);
	for (int32 PoolIndex = 0; PoolIndex < PoolCount; ++PoolIndex)
	{
		const FLootTablePool& Pool = Table.Pools[PoolIndex];
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
	MinimumRarity = IsValidLootServiceRarity(MinimumRarity) ? MinimumRarity : EItemRarity::Common;
	LegendaryChance = FMath::Clamp(FMath::IsFinite(LegendaryChance) ? LegendaryChance : 0.f, 0.f, 1.f);
	const float Roll = FMath::FRand();
	if (Roll <= LegendaryChance
		&& static_cast<int32>(EItemRarity::Legendary) >= static_cast<int32>(MinimumRarity))
	{
		return EItemRarity::Legendary;
	}

	// Pick among non-legendary weights if provided.
	if (Context.RarityWeights.Num() > 0)
	{
		TArray<TPair<EItemRarity, float>> Entries;
		Entries.Reserve(Context.RarityWeights.Num());

		double TotalWeight = 0.0;
		for (const TPair<EItemRarity, float>& Pair : Context.RarityWeights)
		{
			if (!IsValidLootServiceRarity(Pair.Key) || Pair.Key == EItemRarity::Legendary
				|| static_cast<int32>(Pair.Key) < static_cast<int32>(MinimumRarity))
			{
				continue; // handled by LegendaryChance above
			}

			const float Weight = ResolveLootWeight(Pair.Value);
			if (Weight > 0.f)
			{
				Entries.Emplace(Pair.Key, Weight);
				TotalWeight = FMath::Min(TotalWeight + static_cast<double>(Weight), static_cast<double>(MaxLootWeight));
			}
		}

		if (TotalWeight > KINDA_SMALL_NUMBER)
		{
			const double RollWeight = static_cast<double>(FMath::FRand()) * TotalWeight;
			double Accum = 0.0;
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
	const int32 AssetCount = FMath::Min(AssetIds.Num(), MaxLootDefinitionsToInspect);
	Candidates.Reserve(AssetCount);
	for (int32 AssetIndex = 0; AssetIndex < AssetCount; ++AssetIndex)
	{
		const FPrimaryAssetId& AssetId = AssetIds[AssetIndex];
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
	if (!IsValidLootServiceRarity(Rarity))
	{
		return false;
	}
	const int32 RangeCount = FMath::Min(Definition.RarityAffixRanges.Num(), MaxLootEntrySetsToInspect);
	for (int32 RangeIndex = 0; RangeIndex < RangeCount; ++RangeIndex)
	{
		const FItemRarityAffixRange& Range = Definition.RarityAffixRanges[RangeIndex];
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
	if (!IsValid(Set))
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
	OutEntries.Reset();
	if (!Pool)
	{
		return;
	}

	auto AppendEntries = [&](const TArray<FLootTableEntry>& Source)
	{
		const int32 Available = MaxLootEntriesToInspect - OutEntries.Num();
		const int32 EntryCount = FMath::Min(Source.Num(), FMath::Max(0, Available));
		for (int32 EntryIndex = 0; EntryIndex < EntryCount; ++EntryIndex)
		{
			OutEntries.Add(&Source[EntryIndex]);
		}
	};

	AppendEntries(Pool->Entries);

	const int32 SetCount = FMath::Min(Pool->EntrySets.Num(), MaxLootEntrySetsToInspect);
	for (int32 SetIndex = 0; SetIndex < SetCount && OutEntries.Num() < MaxLootEntriesToInspect; ++SetIndex)
	{
		const TSoftObjectPtr<UAeyerjiLootEntrySet>& SetPtr = Pool->EntrySets[SetIndex];
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
		double TotalWeight = 0.0;
		double AnyTotal = 0.0;
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
		WeightedEntries.Reserve(FMath::Min(Pool->Entries.Num(), MaxLootEntriesToInspect));

		auto AppendEntries = [&](const TArray<FLootTableEntry>& Source, const UAeyerjiLootEntrySet* OwningSet)
		{
			const int32 Available = MaxLootEntriesToInspect - WeightedEntries.Num();
			const int32 EntryCount = FMath::Min(Source.Num(), FMath::Max(0, Available));
			for (int32 EntryIndex = 0; EntryIndex < EntryCount; ++EntryIndex)
			{
				const FLootTableEntry& Entry = Source[EntryIndex];
				WeightedEntries.Add({ &Entry, OwningSet, 0.f, Entry.ItemDefinition.ToSoftObjectPath() });
			}
		};

		AppendEntries(Pool->Entries, nullptr);

		const int32 SetCount = FMath::Min(Pool->EntrySets.Num(), MaxLootEntrySetsToInspect);
		for (int32 SetIndex = 0; SetIndex < SetCount && WeightedEntries.Num() < MaxLootEntriesToInspect; ++SetIndex)
		{
			const TSoftObjectPtr<UAeyerjiLootEntrySet>& SetPtr = Pool->EntrySets[SetIndex];
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

			if (ResolveLootWeight(Entry->Weight) <= 0.f || !IsValidLootServiceRarity(Entry->Rarity))
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

			const float DropChance = ResolveLootChanceFraction(Entry->PercentageChanceToDropInPool);
			if (DropChance <= 0.f)
			{
				continue;
			}

			if (DropChance < 1.f && FMath::FRand() > DropChance)
			{
				continue;
			}

			bAnyPassedDropChance = true;

			const float EffectiveWeight = ResolveLootWeight(Entry->Weight);
			if (EffectiveWeight <= 0.f)
			{
				continue;
			}

			Candidate.EffectiveWeight = EffectiveWeight;
			AnyTotal = FMath::Min(AnyTotal + EffectiveWeight, static_cast<double>(MaxLootWeight));

			if (Entry->Rarity == Rarity)
			{
				TotalWeight = FMath::Min(TotalWeight + EffectiveWeight, static_cast<double>(MaxLootWeight));
			}
		}

		if (TotalWeight > KINDA_SMALL_NUMBER && WeightedEntries.Num() > 0)
		{
			const double Roll = static_cast<double>(FMath::FRand()) * TotalWeight;
			double Accum = 0.0;
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
				const double RollAny = static_cast<double>(FMath::FRand()) * AnyTotal;
				double AccumAny = 0.0;
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

	if (!bCacheBuilt || CachedAssetIds.IsEmpty())
	{
		bCacheBuilt = true;

		Manager.GetPrimaryAssetIdList(AssetType, CachedAssetIds);
		if (CachedAssetIds.Num() > MaxLootDefinitionsToInspect)
		{
			CachedAssetIds.SetNum(MaxLootDefinitionsToInspect, EAllowShrinking::No);
		}
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
	Candidates.Reserve(FMath::Min(CachedDefinitions.Num(), MaxLootDefinitionsToInspect));

	auto AppendCandidates = [&](bool bRequireSourceTag)
	{
		const int32 AssetCount = FMath::Min(CachedAssetIds.Num(), MaxLootDefinitionsToInspect);
		for (int32 Idx = 0; Idx < AssetCount && Candidates.Num() < MaxLootDefinitionsToInspect; ++Idx)
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
