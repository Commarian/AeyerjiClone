// ItemInstance.cpp

#include "Items/ItemInstance.h"

#include "Items/ItemAffixDefinition.h"
#include "Items/ItemDefinition.h"
#include "Items/InventoryComponent.h"
#include "Net/UnrealNetwork.h"
#include "Systems/LootService.h"
#include "Systems/LootTable.h"
#include "UObject/CoreNet.h"

UAeyerjiItemInstance::UAeyerjiItemInstance()
{
	SetFlags(RF_Transactional);
}

void UAeyerjiItemInstance::NotifyItemChanged()
{
	UE_LOG(LogTemp, Display, TEXT("[ItemInstance] NotifyItemChanged %s Definition=%s Icon=%s"),
		*GetName(), *GetNameSafe(Definition),
		(Definition && Definition->Icon) ? *Definition->Icon->GetName() : TEXT("None"));
	OnItemChanged.Broadcast();
}

void UAeyerjiItemInstance::ForceItemChangedForUI()
{
	UE_LOG(LogTemp, Display, TEXT("[ItemInstance] ForceItemChangedForUI %s Definition=%s Icon=%s"),
		*GetName(), *GetNameSafe(Definition),
		(Definition && Definition->Icon) ? *Definition->Icon->GetName() : TEXT("None"));
	NotifyItemChanged();
}

void UAeyerjiItemInstance::SetNetAddressable()
{
	if (!HasAnyFlags(RF_Public | RF_Standalone))
	{
		SetFlags(RF_Public | RF_Standalone);
	}
}

void UAeyerjiItemInstance::PostNetReceive()
{
	Super::PostNetReceive();
	UE_LOG(LogTemp, Display, TEXT("[ItemInstance] PostNetReceive %s Definition=%s Icon=%s"),
		*GetName(), *GetNameSafe(Definition),
		(Definition && Definition->Icon) ? *Definition->Icon->GetName() : TEXT("None"));
	NotifyItemChanged();
}

FLinearColor UAeyerjiItemInstance::RarityTint(EItemRarity RarityVariable) const
{
	switch (Rarity)
	{
	case EItemRarity::Uncommon:         return FLinearColor(0.25f, 1.f, 0.25f, 1.f);
	case EItemRarity::Rare:             return FLinearColor(0.25f, 0.6f, 1.f, 1.f);
	case EItemRarity::Epic:             return FLinearColor(0.5f, 0.25f, 1.f, 1.f);
	case EItemRarity::Pure:             return FLinearColor(0.95f, 0.9f, 0.3f, 1.f);
	case EItemRarity::Legendary:        return FLinearColor(1.f, 0.6f, 0.2f, 1.f);
	case EItemRarity::PerfectLegendary: return FLinearColor(1.f, 0.23f, 0.11f, 1.f);
	case EItemRarity::Celestial:        return FLinearColor(0.13f, 0.95f, 1.f, 1.f);
	default:                            return FLinearColor(0.35f, 0.35f, 0.35f, 1.f);
	}
};

void UAeyerjiItemInstance::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UAeyerjiItemInstance, Definition);
	DOREPLIFETIME(UAeyerjiItemInstance, Rarity);
	DOREPLIFETIME(UAeyerjiItemInstance, ItemLevel);
	DOREPLIFETIME(UAeyerjiItemInstance, UniqueId);
	DOREPLIFETIME(UAeyerjiItemInstance, Seed);
	DOREPLIFETIME(UAeyerjiItemInstance, RolledAffixes);
	DOREPLIFETIME(UAeyerjiItemInstance, FinalAggregatedModifiers);
	DOREPLIFETIME(UAeyerjiItemInstance, GrantedEffects);
	DOREPLIFETIME(UAeyerjiItemInstance, GrantedAbilities);
	DOREPLIFETIME(UAeyerjiItemInstance, EquippedSlot);
	DOREPLIFETIME(UAeyerjiItemInstance, EquippedSlotIndex);
	DOREPLIFETIME(UAeyerjiItemInstance, InventorySize);
}

FText UAeyerjiItemInstance::GetDisplayName() const
{
	const FText BaseName = Definition ? Definition->DisplayName : FText::FromString(TEXT("Unknown Item"));

	const UObject* OuterObj = GetOuter();
	const UWorld* World = OuterObj ? OuterObj->GetWorld() : nullptr;
	const UAeyerjiLootTable* Table = nullptr;
	if (World)
	{
		if (const UGameInstance* GI = World->GetGameInstance())
		{
			if (const ULootService* LootService = GI->GetSubsystem<ULootService>())
			{
				Table = LootService->GetLootTable();
			}
		}
	}

	const FItemRarityNameFormat* NameFormat = Table ? Table->FindNameFormat(Rarity) : nullptr;
	if (!NameFormat)
	{
		return BaseName;
	}

	const bool bHasPrefix = !NameFormat->Prefix.IsEmpty();
	const bool bHasSuffix = !NameFormat->Suffix.IsEmpty();
	if (!bHasPrefix && !bHasSuffix)
	{
		return BaseName;
	}

	FString Combined;
	if (bHasPrefix)
	{
		Combined += NameFormat->Prefix.ToString();
		Combined += TEXT(" ");
	}
	Combined += BaseName.ToString();
	if (bHasSuffix)
	{
		Combined += TEXT(" ");
		Combined += NameFormat->Suffix.ToString();
	}

	return FText::FromString(Combined);
}

FAeyerjiPickupVisualConfig UAeyerjiItemInstance::GetPickupVisualConfig() const
{
	return Definition ? Definition->PickupVisuals : FAeyerjiPickupVisualConfig();
}

EItemCategory UAeyerjiItemInstance::GetItemCategory() const
{
	return Definition ? Definition->ItemCategory : EItemCategory::Offense;
}

void UAeyerjiItemInstance::RebuildAggregation()
{
	FinalAggregatedModifiers.Reset();
	GrantedEffects.Reset();
	GrantedAbilities.Reset();
	const ULootService* LootService = nullptr;
	const UAeyerjiLootTable* LootTable = nullptr;
	if (const UObject* OuterObj = GetOuter())
	{
		if (const UWorld* World = OuterObj->GetWorld())
		{
			if (const UGameInstance* GI = World->GetGameInstance())
			{
				LootService = GI->GetSubsystem<ULootService>();
				LootTable = LootService ? LootService->GetLootTable() : nullptr;
			}
		}
	}

	const FRarityScalingRow* RarityScaling = LootTable ? LootTable->FindRarityScaling(Rarity) : nullptr;

	if (Definition)
	{
		for (FItemStatModifier Mod : Definition->BaseModifiers)
		{
			if (RarityScaling)
			{
				Mod.Magnitude *= RarityScaling->BaseModifierMultiplier;
			}
			FinalAggregatedModifiers.Add(Mod);
		}

		for (FItemGrantedEffect Effect : Definition->GrantedEffects)
		{
			if (RarityScaling)
			{
				Effect.EffectLevel *= RarityScaling->GrantedEffectLevelMultiplier;
			}
			GrantedEffects.Add(Effect);
		}

		GrantedAbilities.Append(Definition->GrantedAbilities);
		InventorySize = Definition->InventorySize;
	}
	else
	{
		InventorySize = FIntPoint(1, 1);
	}

	for (const FRolledAffix& Rolled : RolledAffixes)
	{
		TArray<FItemStatModifier> ScaledMods = Rolled.FinalModifiers;
		if (RarityScaling)
		{
			for (FItemStatModifier& Mod : ScaledMods)
			{
				Mod.Magnitude *= RarityScaling->AffixModifierMultiplier;
			}
		}

		TArray<FItemGrantedEffect> ScaledEffects = Rolled.GrantedEffects;
		if (RarityScaling)
		{
			for (FItemGrantedEffect& Effect : ScaledEffects)
			{
				Effect.EffectLevel *= RarityScaling->GrantedEffectLevelMultiplier;
			}
		}

		FinalAggregatedModifiers.Append(ScaledMods);
		GrantedEffects.Append(ScaledEffects);
		GrantedAbilities.Append(Rolled.GrantedAbilities);
	}

	NotifyItemChanged();
}

void UAeyerjiItemInstance::ApplyLootStatScaling(const UAeyerjiLootTable* LootTable)
{
	if (!LootTable)
	{
		return;
	}

	const int32 Level = FMath::Max(ItemLevel, 1);
	const int32 LevelDelta = FMath::Max(Level - 1, 0);

	if (LevelDelta <= 0 || FinalAggregatedModifiers.Num() == 0)
	{
		return;
	}

	for (FItemStatModifier& Mod : FinalAggregatedModifiers)
	{
		const FItemStatScalingRow* Scaling = LootTable->FindScalingForAttribute(Mod.Attribute);
		if (!Scaling)
		{
			continue;
		}

		const float Mult = 1.f + Scaling->PerLevelMultiplier * LevelDelta;
		Mod.Magnitude = (Mod.Magnitude * Mult) + (Scaling->PerLevelAdd * LevelDelta);
	}
}

void UAeyerjiItemInstance::InitializeFromDefinition(
	UItemDefinition* InDefinition,
	EItemRarity InRarity,
	int32 InItemLevel,
	int32 InSeed,
	EEquipmentSlot InSlot,
	const TArray<UItemAffixDefinition*>& ChosenAffixes,
	const TArray<const FAffixTier*>& ChosenTiers)
{
	Definition = InDefinition;
	Rarity = InRarity;
	ItemLevel = InItemLevel;
	Seed = InSeed;
	EquippedSlot = InSlot;
	EquippedSlotIndex = INDEX_NONE;
	UniqueId = FGuid::NewGuid();

	RolledAffixes.Reset();

	FRandomStream RNG(Seed);

	GrantedEffects.Reset();
	GrantedAbilities.Reset();
	if (Definition)
	{
		GrantedEffects.Append(Definition->GrantedEffects);
		GrantedAbilities.Append(Definition->GrantedAbilities);
		InventorySize = Definition->InventorySize;
	}
	else
	{
		InventorySize = FIntPoint(1, 1);
	}

	for (int32 Index = 0; Index < ChosenAffixes.Num(); ++Index)
	{
		UItemAffixDefinition* Affix = ChosenAffixes[Index];
		const FAffixTier* Tier = ChosenTiers.IsValidIndex(Index) ? ChosenTiers[Index] : nullptr;

		if (!Affix || Tier == nullptr)
		{
			continue;
		}

		TArray<FItemStatModifier> FinalMods;
		Affix->BuildFinalModifiers(*Tier, RNG, FinalMods);

		FRolledAffix Rolled;
		Rolled.AffixId = Affix->AffixId;
		Rolled.DisplayName = Affix->DisplayName;
		Rolled.FinalModifiers = MoveTemp(FinalMods);
		Rolled.GrantedEffects = Affix->GrantedEffects;
		Rolled.GrantedAbilities = Affix->GrantedAbilities;

		RolledAffixes.Add(MoveTemp(Rolled));
		GrantedEffects.Append(Affix->GrantedEffects);
		GrantedAbilities.Append(Affix->GrantedAbilities);
	}

	RebuildAggregation();
}

void UAeyerjiItemInstance::OnRep_Definition()
{
	NotifyItemChanged();
}

void UAeyerjiItemInstance::OnRep_Rarity()
{
	NotifyItemChanged();
}

void UAeyerjiItemInstance::OnRep_InventorySize()
{
	NotifyItemChanged();
}
