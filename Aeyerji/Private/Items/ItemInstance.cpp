// ItemInstance.cpp

#include "Items/ItemInstance.h"

#include "Items/ItemAffixDefinition.h"
#include "Items/ItemDefinition.h"
#include "Items/InventoryComponent.h"
#include "Net/UnrealNetwork.h"
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
	return Definition ? Definition->DisplayName : FText::FromString(TEXT("Unknown Item"));
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

	if (Definition)
	{
		FinalAggregatedModifiers.Append(Definition->BaseModifiers);
		GrantedEffects.Append(Definition->GrantedEffects);
		GrantedAbilities.Append(Definition->GrantedAbilities);
		InventorySize = Definition->InventorySize;
	}
	else
	{
		InventorySize = FIntPoint(1, 1);
	}

	for (const FRolledAffix& Rolled : RolledAffixes)
	{
		FinalAggregatedModifiers.Append(Rolled.FinalModifiers);
		GrantedEffects.Append(Rolled.GrantedEffects);
		GrantedAbilities.Append(Rolled.GrantedAbilities);
	}

	NotifyItemChanged();
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
