#include "Abilities/AeyerjiAbilityTuning.h"

#include "Abilities/GameplayAbility.h"
#include "Engine/DataTable.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogAeyerjiAbilityTuning, Log, All);

namespace
{
	constexpr int32 MaxResolvedAdditionalEffects = 32;
	constexpr int32 MaxResolvedTunablesPerType = 128;
	constexpr int32 MaxResolvedTargets = 256;

	float FiniteOrDefault(const float Value, const float DefaultValue)
	{
		return FMath::IsFinite(Value) ? Value : DefaultValue;
	}

	bool IsFiniteVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	template <typename TunableType, typename ValueType>
	bool FindTunableValue(const TArray<TunableType>& Tunables, FGameplayTag Key, ValueType& OutValue)
	{
		if (!Key.IsValid())
		{
			return false;
		}

		for (const TunableType& Tunable : Tunables)
		{
			if (Tunable.Key == Key)
			{
				OutValue = Tunable.Value;
				return true;
			}
		}

		return false;
	}

	template <typename TunableType>
	void ValidateTunableArray(FName RowName, const TCHAR* Label, const TArray<TunableType>& Tunables)
	{
		TSet<FGameplayTag> SeenKeys;
		for (int32 Index = 0; Index < Tunables.Num(); ++Index)
		{
			const TunableType& Tunable = Tunables[Index];
			if (!Tunable.Key.IsValid())
			{
				UE_LOG(LogAeyerjiAbilityTuning, Warning, TEXT("Ability tuning row %s has %s[%d] with an invalid key."),
					*RowName.ToString(),
					Label,
					Index);
				continue;
			}

			if (SeenKeys.Contains(Tunable.Key))
			{
				UE_LOG(LogAeyerjiAbilityTuning, Warning, TEXT("Ability tuning row %s has duplicate %s key %s."),
					*RowName.ToString(),
					Label,
					*Tunable.Key.ToString());
				continue;
			}

			SeenKeys.Add(Tunable.Key);
		}
	}

	template <typename TunableType>
	void OverlayTunables(const TArray<TunableType>& Overrides, TArray<TunableType>& InOutTunables)
	{
		for (const TunableType& Override : Overrides)
		{
			if (!Override.Key.IsValid())
			{
				continue;
			}

			bool bReplaced = false;
			for (TunableType& Existing : InOutTunables)
			{
				if (Existing.Key == Override.Key)
				{
					Existing = Override;
					bReplaced = true;
					break;
				}
			}

			if (!bReplaced)
			{
				InOutTunables.Add(Override);
			}
		}
	}

	template <typename TunableType>
	void SanitizeTunableKeys(TArray<TunableType>& InOutTunables)
	{
		TSet<FGameplayTag> SeenKeys;
		InOutTunables.RemoveAll([&SeenKeys](const TunableType& Tunable)
		{
			if (!Tunable.Key.IsValid() || SeenKeys.Contains(Tunable.Key))
			{
				return true;
			}

			SeenKeys.Add(Tunable.Key);
			return false;
		});

		if (InOutTunables.Num() > MaxResolvedTunablesPerType)
		{
			InOutTunables.SetNum(MaxResolvedTunablesPerType, EAllowShrinking::No);
		}
	}

	void SanitizeResolvedConfig(FAeyerjiAbilityResolvedConfig& InOutConfig)
	{
		InOutConfig.Rank = FMath::Max(1, InOutConfig.Rank);
		InOutConfig.RequiredLevel = FMath::Max(1, InOutConfig.RequiredLevel);
		InOutConfig.Cost.ManaCost = FMath::Max(0.f, FiniteOrDefault(InOutConfig.Cost.ManaCost, 0.f));
		InOutConfig.Cost.Cooldown = FMath::Max(0.f, FiniteOrDefault(InOutConfig.Cost.Cooldown, 0.f));
		InOutConfig.PreviewRange = FMath::Max(0.f, FiniteOrDefault(InOutConfig.PreviewRange, 0.f));
		InOutConfig.MaxRange = FMath::Max(0.f, FiniteOrDefault(InOutConfig.MaxRange, 0.f));
		InOutConfig.Radius = FMath::Max(0.f, FiniteOrDefault(InOutConfig.Radius, 0.f));
		InOutConfig.ArcAngleDegrees = FMath::Clamp(FiniteOrDefault(InOutConfig.ArcAngleDegrees, 90.f), 0.f, 180.f);
		InOutConfig.MaxTargets = FMath::Clamp(InOutConfig.MaxTargets, 0, MaxResolvedTargets);

		if (!StaticEnum<EAeyerjiTargetMode>()->IsValidEnumValue(static_cast<int64>(InOutConfig.TargetMode)))
		{
			InOutConfig.TargetMode = EAeyerjiTargetMode::Instant;
		}
		if (!StaticEnum<EAeyerjiAbilityTargetShape>()->IsValidEnumValue(static_cast<int64>(InOutConfig.Shape)))
		{
			InOutConfig.Shape = EAeyerjiAbilityTargetShape::SingleActor;
		}
		if (!StaticEnum<EAeyerjiAbilityTargetTeam>()->IsValidEnumValue(static_cast<int64>(InOutConfig.TargetTeam)))
		{
			InOutConfig.TargetTeam = EAeyerjiAbilityTargetTeam::Enemy;
		}
		if (!StaticEnum<EAeyerjiStat>()->IsValidEnumValue(static_cast<int64>(InOutConfig.Damage.SourceStat)))
		{
			InOutConfig.Damage.SourceStat = EAeyerjiStat::None;
		}

		InOutConfig.Damage.FlatValue = FiniteOrDefault(InOutConfig.Damage.FlatValue, 0.f);
		InOutConfig.Damage.SourceStatScalar = FiniteOrDefault(InOutConfig.Damage.SourceStatScalar, 0.f);
		InOutConfig.Damage.VarianceOverride = FMath::Clamp(
			FiniteOrDefault(InOutConfig.Damage.VarianceOverride, -1.f), -1.f, 0.95f);
		InOutConfig.Damage.CriticalMultiplierOverride = FMath::Max(
			0.f, FiniteOrDefault(InOutConfig.Damage.CriticalMultiplierOverride, 0.f));
		InOutConfig.Damage.ArmorShred = FMath::Max(0.f, FiniteOrDefault(InOutConfig.Damage.ArmorShred, 0.f));
		InOutConfig.Damage.ArmorPenetration = FMath::Clamp(
			FiniteOrDefault(InOutConfig.Damage.ArmorPenetration, 0.f), 0.f, 1.f);
		InOutConfig.Damage.StaggerMultiplier = FMath::Max(
			0.f, FiniteOrDefault(InOutConfig.Damage.StaggerMultiplier, 1.f));

		InOutConfig.AdditionalEffects.RemoveAll([](FAeyerjiAbilityAppliedEffect& Effect)
		{
			if (Effect.GameplayEffectClass.IsNull() || !FMath::IsFinite(Effect.Magnitude))
			{
				return true;
			}

			Effect.EffectLevel = FMath::Max(0.01f, FiniteOrDefault(Effect.EffectLevel, 1.f));
			return false;
		});
		if (InOutConfig.AdditionalEffects.Num() > MaxResolvedAdditionalEffects)
		{
			InOutConfig.AdditionalEffects.SetNum(MaxResolvedAdditionalEffects, EAllowShrinking::No);
		}

		InOutConfig.Visuals.MontagePlayRate = FMath::Max(
			0.01f, FiniteOrDefault(InOutConfig.Visuals.MontagePlayRate, 1.f));
		InOutConfig.Visuals.ImpactDelaySeconds = FMath::Max(
			-1.f, FiniteOrDefault(InOutConfig.Visuals.ImpactDelaySeconds, -1.f));
		if (!IsFiniteVector(InOutConfig.Visuals.NiagaraOffset))
		{
			InOutConfig.Visuals.NiagaraOffset = FVector::ZeroVector;
		}
		if (!IsFiniteVector(InOutConfig.Visuals.NiagaraScale))
		{
			InOutConfig.Visuals.NiagaraScale = FVector::OneVector;
		}

		InOutConfig.FloatTunables.RemoveAll([](const FAeyerjiAbilityFloatTunable& Tunable)
		{
			return !FMath::IsFinite(Tunable.Value);
		});
		SanitizeTunableKeys(InOutConfig.FloatTunables);
		SanitizeTunableKeys(InOutConfig.BoolTunables);
		SanitizeTunableKeys(InOutConfig.IntTunables);
		SanitizeTunableKeys(InOutConfig.TagTunables);
		SanitizeTunableKeys(InOutConfig.AssetTunables);
	}

	FString MakeRankLookupKey(const FGameplayTag AbilityTag, const int32 Rank)
	{
		return FString::Printf(TEXT("%s@%d"), *AbilityTag.ToString(), Rank);
	}
}

bool FAeyerjiAbilityTableRow::TryGetFloatTunable(FGameplayTag Key, float& OutValue) const
{
	return FindTunableValue(FloatTunables, Key, OutValue);
}

bool FAeyerjiAbilityTableRow::TryGetBoolTunable(FGameplayTag Key, bool& OutValue) const
{
	return FindTunableValue(BoolTunables, Key, OutValue);
}

bool FAeyerjiAbilityTableRow::TryGetIntTunable(FGameplayTag Key, int32& OutValue) const
{
	return FindTunableValue(IntTunables, Key, OutValue);
}

bool FAeyerjiAbilityTableRow::TryGetTagTunable(FGameplayTag Key, FGameplayTag& OutValue) const
{
	return FindTunableValue(TagTunables, Key, OutValue);
}

bool FAeyerjiAbilityTableRow::TryGetAssetTunable(FGameplayTag Key, FSoftObjectPath& OutValue) const
{
	return FindTunableValue(AssetTunables, Key, OutValue);
}

bool FAeyerjiAbilityResolvedConfig::TryGetFloatTunable(FGameplayTag Key, float& OutValue) const
{
	return FindTunableValue(FloatTunables, Key, OutValue);
}

bool FAeyerjiAbilityResolvedConfig::TryGetBoolTunable(FGameplayTag Key, bool& OutValue) const
{
	return FindTunableValue(BoolTunables, Key, OutValue);
}

bool FAeyerjiAbilityResolvedConfig::TryGetIntTunable(FGameplayTag Key, int32& OutValue) const
{
	return FindTunableValue(IntTunables, Key, OutValue);
}

bool FAeyerjiAbilityResolvedConfig::TryGetTagTunable(FGameplayTag Key, FGameplayTag& OutValue) const
{
	return FindTunableValue(TagTunables, Key, OutValue);
}

bool FAeyerjiAbilityResolvedConfig::TryGetAssetTunable(FGameplayTag Key, FSoftObjectPath& OutValue) const
{
	return FindTunableValue(AssetTunables, Key, OutValue);
}

void UAeyerjiAbilityTuningSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ReloadAbilityTuningTable();
}

void UAeyerjiAbilityTuningSubsystem::ReloadAbilityTuningTable()
{
	CachedAbilityTuningTable = RuntimeAbilityTuningTable ? RuntimeAbilityTuningTable.Get() : ResolveConfiguredTable();
	CachedAbilityRankTuningTable = RuntimeAbilityRankTuningTable ? RuntimeAbilityRankTuningTable.Get() : ResolveConfiguredRankTable();
	RebuildCache();
}

void UAeyerjiAbilityTuningSubsystem::SetRuntimeAbilityTuningTable(UDataTable* InTable)
{
	RuntimeAbilityTuningTable = InTable;
	ReloadAbilityTuningTable();
}

void UAeyerjiAbilityTuningSubsystem::SetRuntimeAbilityRankTuningTable(UDataTable* InTable)
{
	RuntimeAbilityRankTuningTable = InTable;
	ReloadAbilityTuningTable();
}

const FAeyerjiAbilityTableRow* UAeyerjiAbilityTuningSubsystem::FindAbilityRow(FGameplayTag AbilityTag) const
{
	if (!AbilityTag.IsValid())
	{
		return nullptr;
	}

	if (!CachedAbilityTuningTable)
	{
		const_cast<UAeyerjiAbilityTuningSubsystem*>(this)->ReloadAbilityTuningTable();
	}

	if (const FAeyerjiAbilityTableRow* const* Found = RowsByTag.Find(AbilityTag))
	{
		return *Found;
	}

	const FAeyerjiAbilityTableRow* Row = FindAbilityRowInTable(CachedAbilityTuningTable, AbilityTag);
	if (!Row)
	{
		LogLookupMiss(AbilityTag);
	}

	return Row;
}

const FAeyerjiAbilityRankTableRow* UAeyerjiAbilityTuningSubsystem::FindAbilityRankRow(FGameplayTag AbilityTag, int32 Rank) const
{
	if (!AbilityTag.IsValid() || Rank <= 1)
	{
		return nullptr;
	}

	if (!CachedAbilityRankTuningTable)
	{
		const_cast<UAeyerjiAbilityTuningSubsystem*>(this)->ReloadAbilityTuningTable();
	}

	if (const TMap<int32, const FAeyerjiAbilityRankTableRow*>* RanksForAbility = RankRowsByTag.Find(AbilityTag))
	{
		if (const FAeyerjiAbilityRankTableRow* const* Found = RanksForAbility->Find(Rank))
		{
			return *Found;
		}
	}

	const FAeyerjiAbilityRankTableRow* Row = FindAbilityRankRowInTable(CachedAbilityRankTuningTable, AbilityTag, Rank);
	if (!Row)
	{
		LogRankLookupMiss(AbilityTag, Rank);
	}

	return Row;
}

bool UAeyerjiAbilityTuningSubsystem::ResolveAbilityConfig(FGameplayTag AbilityTag, int32 Rank, FAeyerjiAbilityResolvedConfig& OutConfig) const
{
	const FAeyerjiAbilityTableRow* BaseRow = FindAbilityRow(AbilityTag);
	if (!BaseRow || !MakeResolvedConfigFromBaseRow(*BaseRow, OutConfig))
	{
		OutConfig = FAeyerjiAbilityResolvedConfig();
		return false;
	}

	OutConfig.Rank = FMath::Max(1, Rank);
	if (OutConfig.Rank > 1)
	{
		if (const FAeyerjiAbilityRankTableRow* RankRow = FindAbilityRankRow(AbilityTag, OutConfig.Rank))
		{
			ApplyRankOverrides(*RankRow, OutConfig);
		}
	}

	return true;
}

bool UAeyerjiAbilityTuningSubsystem::BuildAbilitySlot(FGameplayTag AbilityTag, FAeyerjiAbilitySlot& OutSlot) const
{
	FAeyerjiAbilityResolvedConfig Config;
	if (ResolveAbilityConfig(AbilityTag, 1, Config))
	{
		return BuildAbilitySlotFromConfig(Config, OutSlot);
	}

	OutSlot = FAeyerjiAbilitySlot();
	return false;
}

void UAeyerjiAbilityTuningSubsystem::GetAllAbilityRows(TArray<const FAeyerjiAbilityTableRow*>& OutRows) const
{
	OutRows.Reset();
	for (const TPair<FGameplayTag, const FAeyerjiAbilityTableRow*>& Pair : RowsByTag)
	{
		if (Pair.Value)
		{
			OutRows.Add(Pair.Value);
		}
	}
}

void UAeyerjiAbilityTuningSubsystem::GetAllAbilitySlots(TArray<FAeyerjiAbilitySlot>& OutSlots) const
{
	OutSlots.Reset();

	TArray<const FAeyerjiAbilityTableRow*> Rows;
	GetAllAbilityRows(Rows);

	Rows.Sort([](const FAeyerjiAbilityTableRow& A, const FAeyerjiAbilityTableRow& B)
	{
		return A.AbilityTag.GetTagName().LexicalLess(B.AbilityTag.GetTagName());
	});

	for (const FAeyerjiAbilityTableRow* Row : Rows)
	{
		if (!Row)
		{
			continue;
		}

		FAeyerjiAbilityResolvedConfig Config;
		if (!MakeResolvedConfigFromBaseRow(*Row, Config))
		{
			continue;
		}

		FAeyerjiAbilitySlot Slot;
		if (BuildAbilitySlotFromConfig(Config, Slot))
		{
			OutSlots.Add(Slot);
		}
	}
}

bool UAeyerjiAbilityTuningSubsystem::BuildAbilitySlotByTag(FGameplayTag AbilityTag, FAeyerjiAbilitySlot& OutSlot) const
{
	return BuildAbilitySlot(AbilityTag, OutSlot);
}

int32 UAeyerjiAbilityTuningSubsystem::GetMaxAbilityRank(FGameplayTag AbilityTag) const
{
	if (!FindAbilityRow(AbilityTag))
	{
		return 0;
	}

	int32 MaxRank = 1;
	if (const TMap<int32, const FAeyerjiAbilityRankTableRow*>* RanksForAbility = RankRowsByTag.Find(AbilityTag))
	{
		for (const TPair<int32, const FAeyerjiAbilityRankTableRow*>& Pair : *RanksForAbility)
		{
			MaxRank = FMath::Max(MaxRank, Pair.Key);
		}
	}

	return MaxRank;
}

bool UAeyerjiAbilityTuningSubsystem::HasAuthoredAbilityRank(FGameplayTag AbilityTag, int32 Rank) const
{
	if (Rank <= 1)
	{
		return FindAbilityRow(AbilityTag) != nullptr;
	}

	if (const TMap<int32, const FAeyerjiAbilityRankTableRow*>* RanksForAbility = RankRowsByTag.Find(AbilityTag))
	{
		return RanksForAbility->Contains(Rank);
	}

	return false;
}

void UAeyerjiAbilityTuningSubsystem::GetAllAbilitySlotsSorted(TArray<FAeyerjiAbilitySlot>& OutSlots) const
{
	OutSlots.Reset();

	TArray<const FAeyerjiAbilityTableRow*> SortedRows;
	SortedRows.Reserve(RowsByTag.Num());

	for (const TPair<FGameplayTag, const FAeyerjiAbilityTableRow*>& Pair : RowsByTag)
	{
		if (Pair.Value)
		{
			SortedRows.Add(Pair.Value);
		}
	}

	SortedRows.StableSort([](const FAeyerjiAbilityTableRow& A, const FAeyerjiAbilityTableRow& B)
	{
		if (A.UIOrder != B.UIOrder)
		{
			return A.UIOrder < B.UIOrder;
		}

		return A.AbilityTag.ToString().Compare(B.AbilityTag.ToString(), ESearchCase::IgnoreCase) < 0;
	});

	for (const FAeyerjiAbilityTableRow* Row : SortedRows)
	{
		if (!Row)
		{
			continue;
		}

		FAeyerjiAbilityResolvedConfig Config;
		if (!MakeResolvedConfigFromBaseRow(*Row, Config))
		{
			continue;
		}

		FAeyerjiAbilitySlot Slot;
		if (BuildAbilitySlotFromConfig(Config, Slot))
		{
			OutSlots.Add(Slot);
		}
	}
}

UDataTable* UAeyerjiAbilityTuningSubsystem::ResolveConfiguredTable()
{
	const UAeyerjiAbilityTuningSettings* Settings = GetDefault<UAeyerjiAbilityTuningSettings>();
	if (!Settings || Settings->AbilityTuningTable.IsNull())
	{
		return nullptr;
	}

	if (UDataTable* LoadedTable = Settings->AbilityTuningTable.Get())
	{
		return LoadedTable;
	}

	return Settings->AbilityTuningTable.LoadSynchronous();
}

UDataTable* UAeyerjiAbilityTuningSubsystem::ResolveConfiguredRankTable()
{
	const UAeyerjiAbilityTuningSettings* Settings = GetDefault<UAeyerjiAbilityTuningSettings>();
	if (!Settings || Settings->AbilityRankTuningTable.IsNull())
	{
		return nullptr;
	}

	if (UDataTable* LoadedTable = Settings->AbilityRankTuningTable.Get())
	{
		return LoadedTable;
	}

	return Settings->AbilityRankTuningTable.LoadSynchronous();
}

const FAeyerjiAbilityTableRow* UAeyerjiAbilityTuningSubsystem::FindAbilityRowInTable(const UDataTable* Table, FGameplayTag AbilityTag)
{
	if (!Table || !AbilityTag.IsValid())
	{
		return nullptr;
	}

	const FName ExactRowName = AbilityTag.GetTagName();
	if (const FAeyerjiAbilityTableRow* DirectRow = Table->FindRow<FAeyerjiAbilityTableRow>(ExactRowName, TEXT("AeyerjiAbilityTuning Direct"), false))
	{
		return DirectRow;
	}

	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FAeyerjiAbilityTableRow* Row = reinterpret_cast<const FAeyerjiAbilityTableRow*>(Pair.Value);
		if (Row && Row->AbilityTag == AbilityTag)
		{
			return Row;
		}
	}

	return nullptr;
}

const FAeyerjiAbilityRankTableRow* UAeyerjiAbilityTuningSubsystem::FindAbilityRankRowInTable(const UDataTable* Table, FGameplayTag AbilityTag, int32 Rank)
{
	if (!Table || !AbilityTag.IsValid() || Rank <= 1)
	{
		return nullptr;
	}

	const FName ExactRowName(*FString::Printf(TEXT("%s.%d"), *AbilityTag.ToString(), Rank));
	if (const FAeyerjiAbilityRankTableRow* DirectRow = Table->FindRow<FAeyerjiAbilityRankTableRow>(ExactRowName, TEXT("AeyerjiAbilityRankTuning Direct"), false))
	{
		return DirectRow;
	}

	for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
	{
		const FAeyerjiAbilityRankTableRow* Row = reinterpret_cast<const FAeyerjiAbilityRankTableRow*>(Pair.Value);
		if (Row && Row->AbilityTag == AbilityTag && Row->Rank == Rank)
		{
			return Row;
		}
	}

	return nullptr;
}

bool UAeyerjiAbilityTuningSubsystem::BuildAbilitySlotFromRow(const FAeyerjiAbilityTableRow& Row, FAeyerjiAbilitySlot& OutSlot)
{
	FAeyerjiAbilityResolvedConfig Config;
	return MakeResolvedConfigFromBaseRow(Row, Config) && BuildAbilitySlotFromConfig(Config, OutSlot);
}

bool UAeyerjiAbilityTuningSubsystem::BuildAbilitySlotFromConfig(const FAeyerjiAbilityResolvedConfig& Config, FAeyerjiAbilitySlot& OutSlot)
{
	if (!Config.AbilityTag.IsValid())
	{
		OutSlot = FAeyerjiAbilitySlot();
		return false;
	}

	OutSlot = FAeyerjiAbilitySlot();
	OutSlot.Tag.AddTag(Config.AbilityTag);
	OutSlot.Description = Config.DisplayName.IsEmpty() ? Config.AbilityTag.GetTagName() : FName(*Config.DisplayName.ToString());
	OutSlot.SavedAbilityClass = Config.AbilityClass;
	OutSlot.SavedIcon = Config.Icon;
	OutSlot.TargetMode = Config.TargetMode;
	OutSlot.Level = FMath::Max(1, Config.Rank);
	OutSlot.Class = Config.AbilityClass.Get();
	OutSlot.Icon = Config.Icon.Get();
	return true;
}

bool UAeyerjiAbilityTuningSubsystem::MakeResolvedConfigFromBaseRow(const FAeyerjiAbilityTableRow& Row, FAeyerjiAbilityResolvedConfig& OutConfig)
{
	if (!Row.AbilityTag.IsValid())
	{
		OutConfig = FAeyerjiAbilityResolvedConfig();
		return false;
	}

	OutConfig = FAeyerjiAbilityResolvedConfig();
	OutConfig.AbilityClass = Row.AbilityClass;
	OutConfig.AbilityTag = Row.AbilityTag;
	OutConfig.Rank = 1;
	OutConfig.TargetMode = Row.TargetMode;
	OutConfig.DisplayName = Row.DisplayName;
	OutConfig.Description = Row.Description;
	OutConfig.Icon = Row.Icon;
	OutConfig.UIOrder = Row.UIOrder;
	OutConfig.RequiredLevel = Row.RequiredLevel;
	OutConfig.bUnlockedByDefault = Row.bUnlockedByDefault;
	OutConfig.Cost = Row.Cost;
	OutConfig.CooldownTag = Row.CooldownTag;
	OutConfig.PreviewRange = Row.PreviewRange;
	OutConfig.MaxRange = Row.MaxRange;
	OutConfig.Shape = Row.Shape;
	OutConfig.TargetTeam = Row.TargetTeam;
	OutConfig.Radius = Row.Radius;
	OutConfig.ArcAngleDegrees = Row.ArcAngleDegrees;
	OutConfig.MaxTargets = Row.MaxTargets;
	OutConfig.Damage = Row.Damage;
	OutConfig.AdditionalEffects = Row.AdditionalEffects;
	OutConfig.Visuals = Row.Visuals;
	OutConfig.FloatTunables = Row.FloatTunables;
	OutConfig.BoolTunables = Row.BoolTunables;
	OutConfig.IntTunables = Row.IntTunables;
	OutConfig.TagTunables = Row.TagTunables;
	OutConfig.AssetTunables = Row.AssetTunables;
	SanitizeResolvedConfig(OutConfig);
	return true;
}

void UAeyerjiAbilityTuningSubsystem::ApplyRankOverrides(const FAeyerjiAbilityRankTableRow& RankRow, FAeyerjiAbilityResolvedConfig& InOutConfig)
{
	InOutConfig.Rank = FMath::Max(1, RankRow.Rank);

	if (RankRow.bOverrideCost)
	{
		InOutConfig.Cost = RankRow.Cost;
	}

	if (RankRow.bOverridePreviewRange)
	{
		InOutConfig.PreviewRange = RankRow.PreviewRange;
	}

	if (RankRow.bOverrideMaxRange)
	{
		InOutConfig.MaxRange = RankRow.MaxRange;
	}

	if (RankRow.bOverrideRadius)
	{
		InOutConfig.Radius = RankRow.Radius;
	}

	if (RankRow.bOverrideArcAngleDegrees)
	{
		InOutConfig.ArcAngleDegrees = RankRow.ArcAngleDegrees;
	}

	if (RankRow.bOverrideMaxTargets)
	{
		InOutConfig.MaxTargets = RankRow.MaxTargets;
	}

	if (RankRow.bOverrideDamage)
	{
		InOutConfig.Damage = RankRow.Damage;
	}

	if (RankRow.bOverrideAdditionalEffects)
	{
		InOutConfig.AdditionalEffects = RankRow.AdditionalEffects;
	}

	OverlayTunables(RankRow.FloatTunables, InOutConfig.FloatTunables);
	OverlayTunables(RankRow.BoolTunables, InOutConfig.BoolTunables);
	OverlayTunables(RankRow.IntTunables, InOutConfig.IntTunables);
	OverlayTunables(RankRow.TagTunables, InOutConfig.TagTunables);
	OverlayTunables(RankRow.AssetTunables, InOutConfig.AssetTunables);
	SanitizeResolvedConfig(InOutConfig);
}

void UAeyerjiAbilityTuningSubsystem::RebuildCache()
{
	RowsByTag.Reset();
	RankRowsByTag.Reset();
	LoggedLookupMisses.Reset();
	LoggedRankLookupMisses.Reset();

	if (!CachedAbilityTuningTable)
	{
		UE_LOG(LogAeyerjiAbilityTuning, Warning, TEXT("Ability tuning table is not loaded."));
	}
	else if (CachedAbilityTuningTable->GetRowStruct() != FAeyerjiAbilityTableRow::StaticStruct())
	{
		UE_LOG(LogAeyerjiAbilityTuning, Error, TEXT("Ability tuning table %s has row struct %s, expected %s."),
			*GetNameSafe(CachedAbilityTuningTable),
			*GetNameSafe(CachedAbilityTuningTable->GetRowStruct()),
			*GetNameSafe(FAeyerjiAbilityTableRow::StaticStruct()));
	}
	else
	{
		for (const TPair<FName, uint8*>& Pair : CachedAbilityTuningTable->GetRowMap())
		{
			const FAeyerjiAbilityTableRow* Row = reinterpret_cast<const FAeyerjiAbilityTableRow*>(Pair.Value);
			if (!Row)
			{
				continue;
			}

			ValidateRow(Pair.Key, *Row);
			if (Row->AbilityTag.IsValid())
			{
				RowsByTag.Add(Row->AbilityTag, Row);
			}
		}
	}

	if (CachedAbilityRankTuningTable)
	{
		if (CachedAbilityRankTuningTable->GetRowStruct() != FAeyerjiAbilityRankTableRow::StaticStruct())
		{
			UE_LOG(LogAeyerjiAbilityTuning, Error, TEXT("Ability rank tuning table %s has row struct %s, expected %s."),
				*GetNameSafe(CachedAbilityRankTuningTable),
				*GetNameSafe(CachedAbilityRankTuningTable->GetRowStruct()),
				*GetNameSafe(FAeyerjiAbilityRankTableRow::StaticStruct()));
		}
		else
		{
			for (const TPair<FName, uint8*>& Pair : CachedAbilityRankTuningTable->GetRowMap())
			{
				const FAeyerjiAbilityRankTableRow* Row = reinterpret_cast<const FAeyerjiAbilityRankTableRow*>(Pair.Value);
				if (!Row)
				{
					continue;
				}

				ValidateRankRow(Pair.Key, *Row);
				if (Row->AbilityTag.IsValid() && Row->Rank > 1)
				{
					RankRowsByTag.FindOrAdd(Row->AbilityTag).Add(Row->Rank, Row);
				}
			}
		}
	}

	UE_LOG(LogAeyerjiAbilityTuning, Log, TEXT("Loaded ability tuning table %s with %d base rows and %d rank groups."),
		*GetNameSafe(CachedAbilityTuningTable),
		RowsByTag.Num(),
		RankRowsByTag.Num());
}

void UAeyerjiAbilityTuningSubsystem::LogLookupMiss(FGameplayTag AbilityTag) const
{
	if (LoggedLookupMisses.Contains(AbilityTag))
	{
		return;
	}

	LoggedLookupMisses.Add(AbilityTag);

	const UAeyerjiAbilityTuningSettings* Settings = GetDefault<UAeyerjiAbilityTuningSettings>();
	const FString ConfiguredPath = Settings
		? Settings->AbilityTuningTable.ToSoftObjectPath().ToString()
		: FString(TEXT("<no settings object>"));

	FString RowNames;
	if (CachedAbilityTuningTable)
	{
		TArray<FName> Names = CachedAbilityTuningTable->GetRowNames();
		for (const FName RowName : Names)
		{
			if (!RowNames.IsEmpty())
			{
				RowNames += TEXT(", ");
			}
			RowNames += RowName.ToString();
		}
	}

	UE_LOG(LogAeyerjiAbilityTuning, Warning, TEXT("Ability tuning lookup miss Tag=%s Table=%s Config=%s RowStruct=%s Rows=%d [%s]"),
		*AbilityTag.ToString(),
		*GetNameSafe(CachedAbilityTuningTable),
		*ConfiguredPath,
		CachedAbilityTuningTable ? *GetNameSafe(CachedAbilityTuningTable->GetRowStruct()) : TEXT("<none>"),
		CachedAbilityTuningTable ? CachedAbilityTuningTable->GetRowMap().Num() : 0,
		*RowNames);
}

void UAeyerjiAbilityTuningSubsystem::LogRankLookupMiss(FGameplayTag AbilityTag, int32 Rank) const
{
	const FString LookupKey = MakeRankLookupKey(AbilityTag, Rank);
	if (LoggedRankLookupMisses.Contains(LookupKey))
	{
		return;
	}

	LoggedRankLookupMisses.Add(LookupKey);

	const UAeyerjiAbilityTuningSettings* Settings = GetDefault<UAeyerjiAbilityTuningSettings>();
	const FString ConfiguredPath = Settings
		? Settings->AbilityRankTuningTable.ToSoftObjectPath().ToString()
		: FString(TEXT("<no settings object>"));

	UE_LOG(LogAeyerjiAbilityTuning, Verbose, TEXT("Ability rank tuning lookup miss Tag=%s Rank=%d Table=%s Config=%s"),
		*AbilityTag.ToString(),
		Rank,
		*GetNameSafe(CachedAbilityRankTuningTable),
		*ConfiguredPath);
}

void UAeyerjiAbilityTuningSubsystem::ValidateRow(FName RowName, const FAeyerjiAbilityTableRow& Row) const
{
	if (!Row.AbilityTag.IsValid())
	{
		UE_LOG(LogAeyerjiAbilityTuning, Warning, TEXT("Ability tuning row %s has no AbilityTag."), *RowName.ToString());
		return;
	}

	if (RowName != Row.AbilityTag.GetTagName())
	{
		UE_LOG(LogAeyerjiAbilityTuning, Warning, TEXT("Ability tuning row %s uses AbilityTag %s. RowName should match the tag."),
			*RowName.ToString(),
			*Row.AbilityTag.ToString());
	}

	if (Row.AbilityClass.IsNull())
	{
		UE_LOG(LogAeyerjiAbilityTuning, Warning, TEXT("Ability tuning row %s has no AbilityClass."), *RowName.ToString());
	}

	ValidateTunableArray(RowName, TEXT("FloatTunables"), Row.FloatTunables);
	ValidateTunableArray(RowName, TEXT("BoolTunables"), Row.BoolTunables);
	ValidateTunableArray(RowName, TEXT("IntTunables"), Row.IntTunables);
	ValidateTunableArray(RowName, TEXT("TagTunables"), Row.TagTunables);
	ValidateTunableArray(RowName, TEXT("AssetTunables"), Row.AssetTunables);
}

void UAeyerjiAbilityTuningSubsystem::ValidateRankRow(FName RowName, const FAeyerjiAbilityRankTableRow& Row) const
{
	if (!Row.AbilityTag.IsValid())
	{
		UE_LOG(LogAeyerjiAbilityTuning, Warning, TEXT("Ability rank row %s has no AbilityTag."), *RowName.ToString());
	}

	if (Row.Rank <= 1)
	{
		UE_LOG(LogAeyerjiAbilityTuning, Warning, TEXT("Ability rank row %s has invalid Rank=%d. Rank rows should start at 2."),
			*RowName.ToString(),
			Row.Rank);
	}

	ValidateTunableArray(RowName, TEXT("FloatTunables"), Row.FloatTunables);
	ValidateTunableArray(RowName, TEXT("BoolTunables"), Row.BoolTunables);
	ValidateTunableArray(RowName, TEXT("IntTunables"), Row.IntTunables);
	ValidateTunableArray(RowName, TEXT("TagTunables"), Row.TagTunables);
	ValidateTunableArray(RowName, TEXT("AssetTunables"), Row.AssetTunables);
}
