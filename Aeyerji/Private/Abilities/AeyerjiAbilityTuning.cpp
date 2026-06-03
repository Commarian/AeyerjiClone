#include "Abilities/AeyerjiAbilityTuning.h"

#include "Abilities/GameplayAbility.h"
#include "Engine/DataTable.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogAeyerjiAbilityTuning, Log, All);

namespace
{
	const FName DeprecatedStompAbilityTagName(TEXT("Ability.Player.Stomp"));
	const FName StompAbilityTagName(TEXT("Ability.Stomp"));
	const FName DeprecatedStompCooldownTagName(TEXT("Cooldown.Ability.Player.Stomp"));
	const FName StompCooldownTagName(TEXT("Cooldown.Stomp"));
}

void UAeyerjiAbilityTuningSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	ReloadAbilityTuningTable();
}

void UAeyerjiAbilityTuningSubsystem::ReloadAbilityTuningTable()
{
	CachedAbilityTuningTable = RuntimeAbilityTuningTable ? RuntimeAbilityTuningTable.Get() : ResolveConfiguredTable();
	RebuildCache();
}

void UAeyerjiAbilityTuningSubsystem::SetRuntimeAbilityTuningTable(UDataTable* InTable)
{
	RuntimeAbilityTuningTable = InTable;
	ReloadAbilityTuningTable();
}

const FAeyerjiAbilityTableRow* UAeyerjiAbilityTuningSubsystem::FindAbilityRow(FGameplayTag AbilityTag) const
{
	AbilityTag = NormalizeAbilityTag(AbilityTag);
	if (!AbilityTag.IsValid())
	{
		return nullptr;
	}

	if (const FAeyerjiAbilityTableRow* const* Found = RowsByTag.Find(AbilityTag))
	{
		return *Found;
	}

	return FindAbilityRowInTable(CachedAbilityTuningTable, AbilityTag);
}

bool UAeyerjiAbilityTuningSubsystem::BuildAbilitySlot(FGameplayTag AbilityTag, FAeyerjiAbilitySlot& OutSlot) const
{
	if (const FAeyerjiAbilityTableRow* Row = FindAbilityRow(AbilityTag))
	{
		return BuildAbilitySlotFromRow(*Row, OutSlot);
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

		FAeyerjiAbilitySlot Slot;
		if (BuildAbilitySlotFromRow(*Row, Slot))
		{
			OutSlots.Add(Slot);
		}
	}
}

bool UAeyerjiAbilityTuningSubsystem::BuildAbilitySlotByTag(FGameplayTag AbilityTag, FAeyerjiAbilitySlot& OutSlot) const
{
	return BuildAbilitySlot(AbilityTag, OutSlot);
}

void UAeyerjiAbilityTuningSubsystem::GetAllAbilitySlotsSorted(TArray<FAeyerjiAbilitySlot>& OutSlots) const
{
	OutSlots.Reset();

	TArray<FAeyerjiAbilityTableRow> SortedRows;
	SortedRows.Reserve(RowsByTag.Num());

	for (const TPair<FGameplayTag, const FAeyerjiAbilityTableRow*>& Pair : RowsByTag)
	{
		if (Pair.Value)
		{
			SortedRows.Add(*Pair.Value);
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

	for (const FAeyerjiAbilityTableRow& Row : SortedRows)
	{
		FAeyerjiAbilitySlot Slot;
		if (BuildAbilitySlotFromRow(Row, Slot))
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

	return Settings->AbilityTuningTable.LoadSynchronous();
}

FGameplayTag UAeyerjiAbilityTuningSubsystem::NormalizeAbilityTag(FGameplayTag AbilityTag)
{
	if (AbilityTag.GetTagName() == DeprecatedStompAbilityTagName)
	{
		const FGameplayTag NormalizedTag = FGameplayTag::RequestGameplayTag(StompAbilityTagName, false);
		if (NormalizedTag.IsValid())
		{
			return NormalizedTag;
		}
	}

	return AbilityTag;
}

FGameplayTag UAeyerjiAbilityTuningSubsystem::NormalizeCooldownTag(FGameplayTag CooldownTag)
{
	if (CooldownTag.GetTagName() == DeprecatedStompCooldownTagName)
	{
		const FGameplayTag NormalizedTag = FGameplayTag::RequestGameplayTag(StompCooldownTagName, false);
		if (NormalizedTag.IsValid())
		{
			return NormalizedTag;
		}
	}

	return CooldownTag;
}

const FAeyerjiAbilityTableRow* UAeyerjiAbilityTuningSubsystem::FindAbilityRowInTable(const UDataTable* Table, FGameplayTag AbilityTag)
{
	AbilityTag = NormalizeAbilityTag(AbilityTag);
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
		if (Row && NormalizeAbilityTag(Row->AbilityTag) == AbilityTag)
		{
			return Row;
		}
	}

	return nullptr;
}

bool UAeyerjiAbilityTuningSubsystem::BuildAbilitySlotFromRow(const FAeyerjiAbilityTableRow& Row, FAeyerjiAbilitySlot& OutSlot)
{
	const FGameplayTag AbilityTag = NormalizeAbilityTag(Row.AbilityTag);
	if (!AbilityTag.IsValid())
	{
		OutSlot = FAeyerjiAbilitySlot();
		return false;
	}

	OutSlot = FAeyerjiAbilitySlot();
	OutSlot.Tag.AddTag(AbilityTag);
	OutSlot.Description = Row.DisplayName.IsEmpty() ? AbilityTag.GetTagName() : FName(*Row.DisplayName.ToString());
	OutSlot.SavedAbilityClass = Row.AbilityClass;
	OutSlot.SavedIcon = Row.Icon;
	OutSlot.TargetMode = Row.TargetMode;
	OutSlot.Level = 1;
	OutSlot.ResolveSavedReferences();
	return true;
}

void UAeyerjiAbilityTuningSubsystem::RebuildCache()
{
	RowsByTag.Reset();

	if (!CachedAbilityTuningTable)
	{
		return;
	}

	for (const TPair<FName, uint8*>& Pair : CachedAbilityTuningTable->GetRowMap())
	{
		const FAeyerjiAbilityTableRow* Row = reinterpret_cast<const FAeyerjiAbilityTableRow*>(Pair.Value);
		if (!Row)
		{
			continue;
		}

		ValidateRow(Pair.Key, *Row);

		const FGameplayTag AbilityTag = NormalizeAbilityTag(Row->AbilityTag);
		if (AbilityTag.IsValid())
		{
			RowsByTag.Add(AbilityTag, Row);
		}
	}
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
}
