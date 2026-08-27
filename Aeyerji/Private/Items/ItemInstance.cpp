// ItemInstance.cpp

#include "Items/ItemInstance.h"

#include "Attributes/AeyerjiAttributeSet.h"
#include "Items/ItemAffixDefinition.h"
#include "Items/ItemDefinition.h"
#include "Items/InventoryComponent.h"
#include "Net/UnrealNetwork.h"
#include "Systems/AeyerjiDifficultyTuning.h"
#include "Systems/LootService.h"
#include "Systems/LootTable.h"
#include "UObject/CoreNet.h"
#include "GUI/AeyerjiStringLibrary.h"
#include "UObject/UnrealType.h"

namespace
{
	constexpr int32 MaxItemAffixes = 64;
	constexpr int32 MaxItemModifiers = 2048;
	constexpr int32 MaxItemGrantedEffects = 256;
	constexpr int32 MaxItemGrantedAbilities = 256;
	constexpr int32 MaxSetByCallerMagnitudesPerEffect = 64;
	constexpr int32 MaxItemGridDimension = 64;
	constexpr int32 MaxItemAbilityLevel = 1000000;
	constexpr float MaxItemMagnitude = 1000000000.f;
	constexpr float MaxItemScalingFactor = 1000000.f;

	bool IsValidInstanceRarity(const EItemRarity Rarity)
	{
		const UEnum* Enum = StaticEnum<EItemRarity>();
		return Enum && Enum->IsValidEnumValue(static_cast<int64>(Rarity));
	}

	bool IsValidInstanceEquipmentSlot(const EEquipmentSlot Slot)
	{
		const UEnum* Enum = StaticEnum<EEquipmentSlot>();
		return Enum && Enum->IsValidEnumValue(static_cast<int64>(Slot));
	}

	bool IsValidInstanceModifierOperation(const EItemModOp Operation)
	{
		const UEnum* Enum = StaticEnum<EItemModOp>();
		return Enum && Enum->IsValidEnumValue(static_cast<int64>(Operation));
	}

	bool IsUsableInstanceAttribute(const FGameplayAttribute& Attribute)
	{
		const FProperty* Property = Attribute.GetUProperty();
		if (!Property || !FGameplayAttribute::IsGameplayAttributeDataProperty(Property))
		{
			return false;
		}

		const UClass* AttributeSetClass = Cast<UClass>(Property->GetOwner<UObject>());
		return AttributeSetClass && AttributeSetClass->IsChildOf(UAeyerjiAttributeSet::StaticClass());
	}

	float ClampItemMagnitude(const double Value, const float Fallback = 0.f)
	{
		if (!FMath::IsFinite(Value))
		{
			return Fallback;
		}
		return static_cast<float>(FMath::Clamp(Value, -static_cast<double>(MaxItemMagnitude), static_cast<double>(MaxItemMagnitude)));
	}

	float ResolveItemScalingFactor(const float Value, const float Fallback = 1.f)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.f, MaxItemScalingFactor) : Fallback;
	}

	bool TrySanitizeInstanceModifier(FItemStatModifier& Modifier, const float Multiplier)
	{
		if (!IsUsableInstanceAttribute(Modifier.Attribute) || !IsValidInstanceModifierOperation(Modifier.Op))
		{
			return false;
		}
		Modifier.Magnitude = ClampItemMagnitude(
			static_cast<double>(Modifier.Magnitude) * static_cast<double>(Multiplier));
		return true;
	}

	bool TrySanitizeInstanceEffect(FItemGrantedEffect& Effect, const float LevelMultiplier)
	{
		if (!Effect.IsValid())
		{
			return false;
		}
		Effect.EffectLevel = FMath::Clamp(
			ClampItemMagnitude(static_cast<double>(Effect.EffectLevel) * static_cast<double>(LevelMultiplier), 1.f),
			KINDA_SMALL_NUMBER,
			MaxItemScalingFactor);
		if (Effect.SetByCallerMagnitudes.Num() > MaxSetByCallerMagnitudesPerEffect)
		{
			Effect.SetByCallerMagnitudes.SetNum(MaxSetByCallerMagnitudesPerEffect, EAllowShrinking::No);
		}
		Effect.SetByCallerMagnitudes.RemoveAll([](FItemSetByCallerMagnitude& Magnitude)
		{
			if (!Magnitude.IsValid() || !FMath::IsFinite(Magnitude.LevelOneMagnitude)
				|| !FMath::IsFinite(Magnitude.PerLevelMultiplier) || !FMath::IsFinite(Magnitude.PerLevelAdd))
			{
				return true;
			}
			Magnitude.LevelOneMagnitude = ClampItemMagnitude(Magnitude.LevelOneMagnitude);
			Magnitude.PerLevelMultiplier = ClampItemMagnitude(Magnitude.PerLevelMultiplier);
			Magnitude.PerLevelAdd = ClampItemMagnitude(Magnitude.PerLevelAdd);
			return false;
		});
		return true;
	}

	bool TrySanitizeInstanceAbility(FItemGrantedAbility& Ability)
	{
		if (!Ability.IsValid())
		{
			return false;
		}
		Ability.AbilityLevel = FMath::Clamp(Ability.AbilityLevel, 1, MaxItemAbilityLevel);
		Ability.InputID = FMath::Clamp(Ability.InputID, INDEX_NONE, MaxItemAbilityLevel);
		return true;
	}
}

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
	// Dynamic replicated subobjects need to be public, but RF_Standalone would keep discarded items alive.
	if (!HasAnyFlags(RF_Public))
	{
		SetFlags(RF_Public);
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
	switch (RarityVariable)
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
	// Fallback resolved from GlobalStringTable.csv. Reimport string table after CSV update.
	const FText BaseName = Definition ? Definition->DisplayName : AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("UnknownItem"));

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

	FFormatNamedArguments FormatArguments;
	FormatArguments.Add(TEXT("BaseName"), BaseName);
	if (bHasPrefix && bHasSuffix)
	{
		FormatArguments.Add(TEXT("Prefix"), NameFormat->Prefix);
		FormatArguments.Add(TEXT("Suffix"), NameFormat->Suffix);
		return FText::Format(
			AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("ItemDisplayNamePrefixSuffixFormat")),
			FormatArguments);
	}
	if (bHasPrefix)
	{
		FormatArguments.Add(TEXT("Prefix"), NameFormat->Prefix);
		return FText::Format(
			AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("ItemDisplayNamePrefixFormat")),
			FormatArguments);
	}

	FormatArguments.Add(TEXT("Suffix"), NameFormat->Suffix);
	return FText::Format(
		AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("ItemDisplayNameSuffixFormat")),
		FormatArguments);
}

FAeyerjiPickupVisualConfig UAeyerjiItemInstance::GetPickupVisualConfig() const
{
	return Definition ? Definition->PickupVisuals : FAeyerjiPickupVisualConfig();
}

EItemCategory UAeyerjiItemInstance::GetItemCategory() const
{
	return Definition ? Definition->ItemCategory : EItemCategory::Assault;
}

void UAeyerjiItemInstance::RebuildAggregation()
{
	FinalAggregatedModifiers.Reset();
	GrantedEffects.Reset();
	GrantedAbilities.Reset();
	Rarity = IsValidInstanceRarity(Rarity) ? Rarity : EItemRarity::Common;
	ItemLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(ItemLevel);
	EquippedSlot = IsValidInstanceEquipmentSlot(EquippedSlot) ? EquippedSlot : EEquipmentSlot::Assault;

	auto AppendModifiers = [this](const TArray<FItemStatModifier>& Source, const float Multiplier)
	{
		const int32 Available = MaxItemModifiers - FinalAggregatedModifiers.Num();
		const int32 Count = FMath::Min(Source.Num(), Available);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FItemStatModifier Modifier = Source[Index];
			if (TrySanitizeInstanceModifier(Modifier, Multiplier))
			{
				FinalAggregatedModifiers.Add(MoveTemp(Modifier));
			}
		}
	};

	auto AppendEffects = [this](const TArray<FItemGrantedEffect>& Source, const float LevelMultiplier)
	{
		const int32 Available = MaxItemGrantedEffects - GrantedEffects.Num();
		const int32 Count = FMath::Min(Source.Num(), Available);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FItemGrantedEffect Effect = Source[Index];
			if (TrySanitizeInstanceEffect(Effect, LevelMultiplier))
			{
				GrantedEffects.Add(MoveTemp(Effect));
			}
		}
	};

	auto AppendAbilities = [this](const TArray<FItemGrantedAbility>& Source)
	{
		const int32 Available = MaxItemGrantedAbilities - GrantedAbilities.Num();
		const int32 Count = FMath::Min(Source.Num(), Available);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FItemGrantedAbility Ability = Source[Index];
			if (TrySanitizeInstanceAbility(Ability))
			{
				GrantedAbilities.Add(MoveTemp(Ability));
			}
		}
	};

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
	const float BaseModifierMultiplier = RarityScaling
		? ResolveItemScalingFactor(RarityScaling->BaseModifierMultiplier)
		: 1.f;
	const float AffixModifierMultiplier = RarityScaling
		? ResolveItemScalingFactor(RarityScaling->AffixModifierMultiplier)
		: 1.f;
	const float GrantedEffectLevelMultiplier = RarityScaling
		? ResolveItemScalingFactor(RarityScaling->GrantedEffectLevelMultiplier)
		: 1.f;

	if (IsValid(Definition))
	{
		AppendModifiers(Definition->BaseModifiers, BaseModifierMultiplier);
		AppendEffects(Definition->GrantedEffects, GrantedEffectLevelMultiplier);
		AppendAbilities(Definition->GrantedAbilities);
		InventorySize.X = FMath::Clamp(Definition->InventorySize.X, 1, MaxItemGridDimension);
		InventorySize.Y = FMath::Clamp(Definition->InventorySize.Y, 1, MaxItemGridDimension);
	}
	else
	{
		InventorySize = FIntPoint(1, 1);
	}

	const int32 AffixCount = FMath::Min(RolledAffixes.Num(), MaxItemAffixes);
	for (int32 AffixIndex = 0; AffixIndex < AffixCount; ++AffixIndex)
	{
		const FRolledAffix& Rolled = RolledAffixes[AffixIndex];
		AppendModifiers(Rolled.FinalModifiers, AffixModifierMultiplier);
		AppendEffects(Rolled.GrantedEffects, GrantedEffectLevelMultiplier);
		AppendAbilities(Rolled.GrantedAbilities);
	}

	NotifyItemChanged();
}

void UAeyerjiItemInstance::ApplyLootStatScaling(const UAeyerjiLootTable* LootTable)
{
	if (!IsValid(LootTable))
	{
		return;
	}
	if (FinalAggregatedModifiers.Num() > MaxItemModifiers)
	{
		FinalAggregatedModifiers.SetNum(MaxItemModifiers, EAllowShrinking::No);
	}
	FinalAggregatedModifiers.RemoveAll([](FItemStatModifier& Modifier)
	{
		return !TrySanitizeInstanceModifier(Modifier, 1.f);
	});

	const int32 Level = UAeyerjiDifficultySettings::ClampGameplayLevel(ItemLevel);
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

		const double PerLevelMultiplier = FMath::IsFinite(Scaling->PerLevelMultiplier)
			? FMath::Clamp(static_cast<double>(Scaling->PerLevelMultiplier), -static_cast<double>(MaxItemScalingFactor), static_cast<double>(MaxItemScalingFactor))
			: 0.0;
		const double PerLevelAdd = FMath::IsFinite(Scaling->PerLevelAdd)
			? FMath::Clamp(static_cast<double>(Scaling->PerLevelAdd), -static_cast<double>(MaxItemMagnitude), static_cast<double>(MaxItemMagnitude))
			: 0.0;
		const double Multiplier = 1.0 + (PerLevelMultiplier * static_cast<double>(LevelDelta));
		Mod.Magnitude = ClampItemMagnitude(
			(static_cast<double>(Mod.Magnitude) * Multiplier)
			+ (PerLevelAdd * static_cast<double>(LevelDelta)));
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
	Definition = IsValid(InDefinition) ? InDefinition : nullptr;
	Rarity = IsValidInstanceRarity(InRarity) ? InRarity : EItemRarity::Common;
	ItemLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(InItemLevel);
	if (Definition)
	{
		ItemLevel = FMath::Max(ItemLevel, Definition->GetEffectiveRequiredLevel());
	}
	Seed = InSeed;
	EquippedSlot = IsValidInstanceEquipmentSlot(InSlot)
		? InSlot
		: (Definition && IsValidInstanceEquipmentSlot(Definition->DefaultSlot)
			? Definition->DefaultSlot
			: EEquipmentSlot::Assault);
	EquippedSlotIndex = INDEX_NONE;
	UniqueId = FGuid::NewGuid();

	RolledAffixes.Reset();

	FRandomStream RNG(Seed);

	TSet<const UItemAffixDefinition*> AddedAffixes;
	const int32 AffixCount = FMath::Min3(ChosenAffixes.Num(), ChosenTiers.Num(), MaxItemAffixes);
	for (int32 Index = 0; Index < AffixCount; ++Index)
	{
		UItemAffixDefinition* Affix = ChosenAffixes[Index];
		const FAffixTier* Tier = ChosenTiers.IsValidIndex(Index) ? ChosenTiers[Index] : nullptr;

		if (!IsValid(Affix) || Tier == nullptr || AddedAffixes.Contains(Affix)
			|| (Definition && !Affix->IsAllowedFor(Definition->ItemCategory, EquippedSlot)))
		{
			continue;
		}
		FAffixTier SafeTier = *Tier;
		SafeTier.Weight = FMath::Max(0, SafeTier.Weight);
		SafeTier.MinItemLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(SafeTier.MinItemLevel);
		SafeTier.MinRoll = FMath::Clamp(FMath::IsFinite(SafeTier.MinRoll) ? SafeTier.MinRoll : 0.f, -MaxItemMagnitude, MaxItemMagnitude);
		SafeTier.MaxRoll = FMath::Clamp(FMath::IsFinite(SafeTier.MaxRoll) ? SafeTier.MaxRoll : SafeTier.MinRoll, -MaxItemMagnitude, MaxItemMagnitude);
		if (SafeTier.Weight <= 0 || ItemLevel < SafeTier.MinItemLevel)
		{
			continue;
		}

		TArray<FItemStatModifier> FinalMods;
		Affix->BuildFinalModifiers(SafeTier, RNG, FinalMods);
		if (FinalMods.Num() > MaxItemModifiers)
		{
			FinalMods.SetNum(MaxItemModifiers, EAllowShrinking::No);
		}
		FinalMods.RemoveAll([](FItemStatModifier& Modifier)
		{
			return !TrySanitizeInstanceModifier(Modifier, 1.f);
		});

		FRolledAffix Rolled;
		Rolled.AffixId = Affix->AffixId;
		Rolled.DisplayName = Affix->DisplayName;
		Rolled.FinalModifiers = MoveTemp(FinalMods);
		const int32 EffectCount = FMath::Min(Affix->GrantedEffects.Num(), MaxItemGrantedEffects);
		for (int32 EffectIndex = 0; EffectIndex < EffectCount; ++EffectIndex)
		{
			FItemGrantedEffect Effect = Affix->GrantedEffects[EffectIndex];
			if (TrySanitizeInstanceEffect(Effect, 1.f))
			{
				Rolled.GrantedEffects.Add(MoveTemp(Effect));
			}
		}
		const int32 AbilityCount = FMath::Min(Affix->GrantedAbilities.Num(), MaxItemGrantedAbilities);
		for (int32 AbilityIndex = 0; AbilityIndex < AbilityCount; ++AbilityIndex)
		{
			FItemGrantedAbility Ability = Affix->GrantedAbilities[AbilityIndex];
			if (TrySanitizeInstanceAbility(Ability))
			{
				Rolled.GrantedAbilities.Add(MoveTemp(Ability));
			}
		}

		RolledAffixes.Add(MoveTemp(Rolled));
		AddedAffixes.Add(Affix);
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
