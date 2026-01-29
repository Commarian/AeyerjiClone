// InventoryComponent.cpp

#include "Items/InventoryComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "GAS/GE_ItemStats.h"
#include "Inventory/AeyerjiInventoryBPFL.h"
#include "Inventory/AeyerjiLootPickup.h"
#include "Items/ItemDefinition.h"
#include "Items/ItemInstance.h"
#include "Logging/AeyerjiLog.h"
#include "Systems/LootTable.h"
#include "CollisionQueryParams.h"
#include "Engine/World.h"
#include "Net/Core/PushModel/PushModel.h"
#include "Net/Core/PushModel/PushModelMacros.h"
#include "Net/UnrealNetwork.h"
#include "UObject/CoreNet.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "TimerManager.h"
#include "Containers/Set.h"
#include "GameFramework/PlayerState.h"

namespace
{
	static_assert(static_cast<int32>(EEquipmentSlot::Offense) == static_cast<int32>(EItemCategory::Offense)
		&& static_cast<int32>(EEquipmentSlot::Defense) == static_cast<int32>(EItemCategory::Defense)
		&& static_cast<int32>(EEquipmentSlot::Magic) == static_cast<int32>(EItemCategory::Magic),
		"Equipment slot and item category enums must stay aligned.");

	bool IsValidEquipmentSlot(EEquipmentSlot Slot)
	{
		if (const UEnum* SlotEnum = StaticEnum<EEquipmentSlot>())
		{
			return SlotEnum->IsValidEnumValue(static_cast<int64>(Slot));
		}
		return false;
	}

	bool IsSlotCompatibleWithDefinition(EEquipmentSlot Slot, const UItemDefinition* Definition)
	{
		if (!Definition)
		{
			return true;
		}

		return static_cast<EItemCategory>(Slot) == Definition->ItemCategory;
	}

	EEquipmentSlot ResolveEquipmentSlot(EEquipmentSlot DesiredSlot, const UItemDefinition* Definition)
	{
		if (IsValidEquipmentSlot(DesiredSlot) && IsSlotCompatibleWithDefinition(DesiredSlot, Definition))
		{
			return DesiredSlot;
		}

		if (Definition)
		{
			const EEquipmentSlot DefaultSlot = Definition->DefaultSlot;
			if (IsValidEquipmentSlot(DefaultSlot) && IsSlotCompatibleWithDefinition(DefaultSlot, Definition))
			{
				return DefaultSlot;
			}

			return static_cast<EEquipmentSlot>(Definition->ItemCategory);
		}

		return EEquipmentSlot::Offense;
	}

	FVector FindGroundedDropLocation(UWorld& World, const FVector& DesiredLocation)
	{
		const float TraceUp = 200.f;
		const float TraceDown = 2000.f;

		const FVector Start = DesiredLocation + FVector(0.f, 0.f, TraceUp);
		const FVector End = DesiredLocation - FVector(0.f, 0.f, TraceDown);

		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DropItemGroundTrace), false);
		if (World.LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
		{
			return Hit.ImpactPoint + Hit.ImpactNormal * 10.f;
		}

		return DesiredLocation;
	}

	int32 GetInventoryOwnerLevel(const UAeyerjiInventoryComponent* Inventory)
	{
		const AActor* Owner = Inventory ? Inventory->GetOwner() : nullptr;
		if (!Owner)
		{
			return 1;
		}

		auto ReadLevelFromASC = [](const UAbilitySystemComponent* ASC) -> int32
		{
			if (!ASC)
			{
				return 1;
			}

			if (const UAeyerjiAttributeSet* Attr = ASC->GetSet<UAeyerjiAttributeSet>())
			{
				return FMath::Max(1, FMath::RoundToInt(Attr->GetLevel()));
			}

			return 1;
		};

		if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Owner))
		{
			if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
			{
				return ReadLevelFromASC(ASC);
			}
		}

		if (const APawn* Pawn = Cast<APawn>(Owner))
		{
			if (const APlayerState* PS = Pawn->GetPlayerState())
			{
				if (const IAbilitySystemInterface* PSASI = Cast<IAbilitySystemInterface>(PS))
				{
					if (UAbilitySystemComponent* ASC = PSASI->GetAbilitySystemComponent())
					{
						return ReadLevelFromASC(ASC);
					}
				}
			}
		}

		return 1;
	}
}

UAeyerjiInventoryComponent::UAeyerjiInventoryComponent()
{
	SetIsReplicatedByDefault(true);
	ItemStatsEffectClass = UGE_ItemStats::StaticClass();
	LootPickupClass = AAeyerjiLootPickup::StaticClass();
}

void UAeyerjiInventoryComponent::BeginPlay()
{
	Super::BeginPlay();

	if (GetOwnerRole() == ROLE_Authority)
	{
		for (UAeyerjiItemInstance* Item : Items)
		{
			BindItemInstanceDelegates(Item);
		}
		RebuildItemSnapshots();
	}
}

void UAeyerjiInventoryComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UAeyerjiInventoryComponent, EquippedItems);
	DOREPLIFETIME_CONDITION(UAeyerjiInventoryComponent, GridPlacements, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UAeyerjiInventoryComponent, ItemSnapshots, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UAeyerjiInventoryComponent, GridColumns, COND_OwnerOnly);
	DOREPLIFETIME_CONDITION(UAeyerjiInventoryComponent, GridRows, COND_OwnerOnly);
}

UAbilitySystemComponent* UAeyerjiInventoryComponent::GetASC() const
{
	if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(GetOwner()))
	{
		return ASI->GetAbilitySystemComponent();
	}

	return nullptr;
}

bool UAeyerjiInventoryComponent::AddItemInstance(UAeyerjiItemInstance* Item, bool bSkipAutoPlacement)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return false;
	}

	if (!Item)
	{
		return false;
	}

	UE_LOG(LogTemp, Display, TEXT("[Inventory][Server] AddItemInstance %s Outer=%s UniqueId=%s bSkipAutoPlacement=%d"),
		*GetNameSafe(Item),
		*GetNameSafe(Item->GetOuter()),
		Item->UniqueId.IsValid() ? *Item->UniqueId.ToString() : TEXT("Invalid"),
		bSkipAutoPlacement ? 1 : 0);

	const bool bAlreadyOwned = Items.Contains(Item);
	UObject* PreviousOuter = nullptr;

	if (!Item->UniqueId.IsValid())
	{
		Item->UniqueId = FGuid::NewGuid();
	}

	if (!bAlreadyOwned)
	{
		if (Item->GetOuter() != this)
		{
			UE_LOG(LogTemp, Display, TEXT("[Inventory][Server] Renaming %s from %s to %s"),
				*GetNameSafe(Item),
				*GetNameSafe(Item->GetOuter()),
				*GetNameSafe(this));
			PreviousOuter = Item->GetOuter();
			Item->Rename(nullptr, this);
		}
		Item->SetNetAddressable();
		Items.Add(Item);
		BindItemInstanceDelegates(Item);
		UE_LOG(LogTemp, Display, TEXT("[Inventory][Server] Added %s Outer=%s UniqueId=%s Items=%d"),
			*GetNameSafe(Item),
			*GetNameSafe(Item->GetOuter()),
			Item->UniqueId.IsValid() ? *Item->UniqueId.ToString() : TEXT("Invalid"),
			Items.Num());
		OnInventoryChanged.Broadcast();
		BroadcastItemStateChange(EInventoryItemStateChange::Added, Item, Item->EquippedSlot, Item->EquippedSlotIndex);
		RebuildItemSnapshots();
	}

	if (bSkipAutoPlacement)
	{
		return true;
	}

	if (TryAutoPlaceItem(Item))
	{
		return true;
	}

	if (!bAlreadyOwned)
	{
		Items.RemoveSingle(Item);
		UnbindItemInstanceDelegates(Item);
		OnInventoryChanged.Broadcast();
		BroadcastItemStateChange(EInventoryItemStateChange::Removed, Item, Item->EquippedSlot, Item->EquippedSlotIndex);
		RebuildItemSnapshots();
		if (PreviousOuter)
		{
			Item->Rename(nullptr, PreviousOuter);
		}
	}

	return false;
}

void UAeyerjiInventoryComponent::Server_AddItem_Implementation(UAeyerjiItemInstance* Item)
{
	AddItemInstance(Item);
}

void UAeyerjiInventoryComponent::Server_RemoveItemById_Implementation(const FGuid& ItemId)
{
	const int32 InventoryIndex = Items.IndexOfByPredicate([&ItemId](const UAeyerjiItemInstance* Instance)
	{
		return Instance && Instance->UniqueId == ItemId;
	});

	UAeyerjiItemInstance* RemovedItem = nullptr;
	if (InventoryIndex != INDEX_NONE)
	{
		RemovedItem = Items[InventoryIndex];
		Items.RemoveAt(InventoryIndex);
		UnbindItemInstanceDelegates(RemovedItem);
		OnInventoryChanged.Broadcast();
		if (RemovedItem)
		{
			BroadcastItemStateChange(EInventoryItemStateChange::Removed, RemovedItem, RemovedItem->EquippedSlot, RemovedItem->EquippedSlotIndex);
		}
		RebuildItemSnapshots();
	}

	ClearPlacement(ItemId);

	for (int32 EquippedIndex = EquippedItems.Num() - 1; EquippedIndex >= 0; --EquippedIndex)
	{
		FEquippedItemEntry& Entry = EquippedItems[EquippedIndex];
		if (Entry.Item && Entry.Item->UniqueId == ItemId)
		{
			const EEquipmentSlot Slot = Entry.Slot;
			const int32 SlotIndex = Entry.SlotIndex;
			Entry.Item->EquippedSlot = Entry.Item->Definition
				? ResolveEquipmentSlot(Entry.Item->Definition->DefaultSlot, Entry.Item->Definition.Get())
				: ResolveEquipmentSlot(Entry.Item->EquippedSlot, nullptr);
			Entry.Item->EquippedSlotIndex = INDEX_NONE;

			RemoveItemGameplayEffect(ItemId);
			EquippedItems.RemoveAt(EquippedIndex);
			MARK_PROPERTY_DIRTY_FROM_NAME(UAeyerjiInventoryComponent, EquippedItems, this);
			OnEquippedItemChanged.Broadcast(Slot, SlotIndex, nullptr);
			BroadcastItemStateChange(EInventoryItemStateChange::Unequipped, Entry.Item, Slot, SlotIndex);
			break;
		}
	}

	RebuildItemSnapshots();
}

UAeyerjiItemInstance* UAeyerjiInventoryComponent::FindItemById(const FGuid& ItemId) const
{
	for (UAeyerjiItemInstance* Item : Items)
	{
		if (Item && Item->UniqueId == ItemId)
		{
			return Item;
		}
	}

	for (const FEquippedItemEntry& Entry : EquippedItems)
	{
		if (Entry.Item && Entry.Item->UniqueId == ItemId)
		{
			return Entry.Item;
		}
	}

	return nullptr;
}

UAeyerjiItemInstance* UAeyerjiInventoryComponent::GetEquipped(EEquipmentSlot Slot, int32 SlotIndex) const
{
	const FEquippedItemEntry* Entry = FindEquippedEntry(Slot, SlotIndex);
	if (!Entry)
	{
		return nullptr;
	}

	if (Entry->Item)
	{
		return Entry->Item;
	}

	if (Entry->ItemId.IsValid())
	{
		return FindItemById(Entry->ItemId);
	}

	return nullptr;
}

int32 UAeyerjiInventoryComponent::CountEquippedWithSameDefinition(const UAeyerjiItemInstance* ReferenceItem) const
{
	if (!ReferenceItem || !ReferenceItem->Definition)
	{
		return 0;
	}

	const UItemDefinition* TargetDefinition = ReferenceItem->Definition;
	int32 Count = 0;

	for (const FEquippedItemEntry& Entry : EquippedItems)
	{
		if (Entry.Item && Entry.Item->Definition == TargetDefinition)
		{
			++Count;
		}
	}

	return Count;
}

bool UAeyerjiInventoryComponent::GetEquipSynergyForItem(
	const UAeyerjiItemInstance* ReferenceItem,
	int32& OutStackCount,
	FLinearColor& OutColor,
	FName& OutColorParam) const
{
	OutStackCount = 0;
	OutColor = FLinearColor::White;
	OutColorParam = NAME_None;

	if (!ReferenceItem || !ReferenceItem->Definition)
	{
		return false;
	}

	const UItemDefinition* Definition = ReferenceItem->Definition;

	OutStackCount = CountEquippedWithSameDefinition(ReferenceItem);
	if (OutStackCount <= 1)
	{
		return false;
	}

	return Definition->TryGetEquipSynergyColor(OutStackCount, OutColor, OutColorParam);
}

void UAeyerjiInventoryComponent::PruneEmptyEquippedEntries()
{
	int32 Removed = 0;
	for (int32 Index = EquippedItems.Num() - 1; Index >= 0; --Index)
	{
		const FEquippedItemEntry& Entry = EquippedItems[Index];
		if (!Entry.Item && !Entry.ItemId.IsValid())
		{
			EquippedItems.RemoveAt(Index);
			++Removed;
		}
	}

	if (Removed > 0)
	{
		MARK_PROPERTY_DIRTY_FROM_NAME(UAeyerjiInventoryComponent, EquippedItems, this);
	}
}

void UAeyerjiInventoryComponent::Server_EquipItem_Implementation(const FGuid& ItemId, EEquipmentSlot Slot, int32 SlotIndex)
{
	PruneEmptyEquippedEntries();

	UAeyerjiItemInstance* Item = FindItemById(ItemId);
	const UItemDefinition* ItemDefinition = Item ? Item->Definition.Get() : nullptr;
	const EEquipmentSlot ResolvedSlot = ResolveEquipmentSlot(Slot, ItemDefinition);
	const bool bSanitizedSlot = ResolvedSlot != Slot;

	AJ_LOG(this, TEXT("Server_EquipItem ItemId=%s Slot=%d Index=%d%s"),
		ItemId.IsValid() ? *ItemId.ToString() : TEXT("Invalid"),
		static_cast<int32>(ResolvedSlot),
		SlotIndex,
		bSanitizedSlot ? TEXT(" (sanitized)") : TEXT(""));

	if (bSanitizedSlot && ItemDefinition && ItemDefinition->DefaultSlot != ResolvedSlot)
	{
		AJ_LOG(this, TEXT("Server_EquipItem sanitized slot request (Requested=%d Default=%d Category=%d). Verify ItemCategory/DefaultSlot match."),
			static_cast<int32>(Slot),
			static_cast<int32>(ItemDefinition->DefaultSlot),
			static_cast<int32>(ItemDefinition->ItemCategory));
	}

	if (!Item || !Item->Definition)
	{
		AJ_LOG(this, TEXT("Server_EquipItem aborted: missing item or definition"));
		return;
	}

	const int32 OwnerLevel = GetInventoryOwnerLevel(this);
	const int32 ItemLevel = FMath::Max(1, Item->ItemLevel);
	if (ItemLevel > OwnerLevel)
	{
		AJ_LOG(this, TEXT("Server_EquipItem rejected: item level %d exceeds owner level %d (%s)"),
			ItemLevel,
			OwnerLevel,
			*GetNameSafe(Item));
		return;
	}

	if (!Items.Contains(Item))
	{
		if (!AddItemInstance(Item, true))
		{
			AJ_LOG(this, TEXT("Server_EquipItem failed: AddItemInstance rejected %s"), *Item->UniqueId.ToString());
			return;
		}
	}

	const bool bExplicitIndex = SlotIndex != INDEX_NONE;
	SlotIndex = SanitizeSlotIndex(SlotIndex);
	if (!bExplicitIndex || SlotIndex == INDEX_NONE)
	{
		SlotIndex = FindFirstFreeSlotIndex(ResolvedSlot, Item);
	}

	if (SlotIndex == INDEX_NONE)
	{
		AJ_LOG(this, TEXT("Server_EquipItem aborted: no free slot for %s in %d"), *Item->UniqueId.ToString(), static_cast<int32>(ResolvedSlot));
		return;
	}

	FEquippedItemEntry* ExistingEntry = FindEquippedEntry(ResolvedSlot, SlotIndex);
	UAeyerjiItemInstance* CurrentlyEquipped = ExistingEntry ? ExistingEntry->Item : nullptr;
	if (CurrentlyEquipped && CurrentlyEquipped != Item)
	{
		if (!AutoPlaceItem(CurrentlyEquipped))
		{
			AJ_LOG(this, TEXT("Server_EquipItem failed: could not auto-place previous %s"), *CurrentlyEquipped->UniqueId.ToString());
			return;
		}

		CurrentlyEquipped->EquippedSlot = CurrentlyEquipped->Definition
			? ResolveEquipmentSlot(CurrentlyEquipped->Definition->DefaultSlot, CurrentlyEquipped->Definition.Get())
			: ResolvedSlot;
		CurrentlyEquipped->EquippedSlotIndex = INDEX_NONE;

		RemoveItemGameplayEffect(CurrentlyEquipped->UniqueId);
		AJ_LOG(this, TEXT("Server_EquipItem unequipped %s from slot %d index %d"),
			*CurrentlyEquipped->UniqueId.ToString(),
			static_cast<int32>(ResolvedSlot),
			SlotIndex);
		OnEquippedItemChanged.Broadcast(ResolvedSlot, SlotIndex, nullptr);
		BroadcastItemStateChange(EInventoryItemStateChange::Unequipped, CurrentlyEquipped, ResolvedSlot, SlotIndex);
		RebuildItemSnapshots();
	}

	Item->EquippedSlot = ResolvedSlot;
	Item->EquippedSlotIndex = SlotIndex;

	if (ExistingEntry)
	{
		ExistingEntry->Item = Item;
		ExistingEntry->ItemId = Item->UniqueId;
		AJ_LOG(this, TEXT("Server_EquipItem updated slot entry for %s (index %d)"), *Item->UniqueId.ToString(), SlotIndex);
	}
	else
	{
		FEquippedItemEntry NewEntry;
		NewEntry.Slot = ResolvedSlot;
		NewEntry.SlotIndex = SlotIndex;
		NewEntry.ItemId = Item->UniqueId;
		NewEntry.Item = Item;
		EquippedItems.Add(NewEntry);
		AJ_LOG(this, TEXT("Server_EquipItem added new slot entry for %s (index %d)"), *Item->UniqueId.ToString(), SlotIndex);
	}
	MARK_PROPERTY_DIRTY_FROM_NAME(UAeyerjiInventoryComponent, EquippedItems, this);

	ClearPlacement(Item->UniqueId);
	AJ_LOG(this, TEXT("Server_EquipItem cleared placement for %s"), *Item->UniqueId.ToString());

	ApplyItemGameplayEffect(Item);
	OnEquippedItemChanged.Broadcast(ResolvedSlot, SlotIndex, Item);
	BroadcastItemStateChange(EInventoryItemStateChange::Equipped, Item, ResolvedSlot, SlotIndex);
	AJ_LOG(this, TEXT("Server_EquipItem completed equip for %s Slot=%d Index=%d"),
		*Item->UniqueId.ToString(),
		static_cast<int32>(ResolvedSlot),
		SlotIndex);
	RebuildItemSnapshots();
}

void UAeyerjiInventoryComponent::Server_UnequipSlot_Implementation(EEquipmentSlot Slot, int32 SlotIndex)
{
	UnequipSlotInternal(Slot, SlotIndex, nullptr);
}

void UAeyerjiInventoryComponent::Server_UnequipSlotToGrid_Implementation(EEquipmentSlot Slot, int32 SlotIndex, FIntPoint PreferredTopLeft)
{
	UnequipSlotInternal(Slot, SlotIndex, &PreferredTopLeft);
}

void UAeyerjiInventoryComponent::ApplyItemGameplayEffect(UAeyerjiItemInstance* Item, float Multiplier)
{
	if (!Item || !ItemStatsEffectClass)
	{
		AJ_LOG(this, TEXT("[ItemStatsDebug] ApplyItemGameplayEffect skipped Item=%s ItemStatsEffectClass=%s"),
			*GetNameSafe(Item),
			*GetNameSafe(ItemStatsEffectClass.Get()));
		return;
	}

	if (UAbilitySystemComponent* ASC = GetASC())
	{
		const UGameplayEffect* StatsGECDO = ItemStatsEffectClass ? ItemStatsEffectClass->GetDefaultObject<UGameplayEffect>() : nullptr;
		const int32 ExecCount = StatsGECDO ? StatsGECDO->Executions.Num() : 0;
		const bool bTrackHandles = Multiplier > 0.f;
		AJ_LOG(this, TEXT("[ItemStatsDebug] ApplyItemGameplayEffect begin Item=%s Def=%s Id=%s Mods=%d ASC=%s StatsGE=%s Execs=%d Mult=%.2f"),
			*GetNameSafe(Item),
			*GetNameSafe(Item->Definition.Get()),
			Item->UniqueId.IsValid() ? *Item->UniqueId.ToString() : TEXT("Invalid"),
			Item->GetFinalAggregatedModifiers().Num(),
			*GetNameSafe(ASC),
			*GetNameSafe(ItemStatsEffectClass.Get()),
			ExecCount,
			Multiplier);
		if (bTrackHandles)
		{
			RemoveItemGameplayEffect(Item->UniqueId);
		}

		FItemActiveEffectSet HandleSet;
		const bool bIsAuthority = GetOwner() && GetOwner()->HasAuthority();

		FGameplayEffectContextHandle Context = ASC->MakeEffectContext();
		Context.AddSourceObject(Item);

		constexpr float Level = 1.f;
		const FName MultiplierName(TEXT("ItemStatsMultiplier"));

		bool bHasValidModifier = false;
		const TArray<FItemStatModifier>& Mods = Item->GetFinalAggregatedModifiers();
		TArray<float> PreValues;
		PreValues.Reserve(Mods.Num());
		for (int32 Index = 0; Index < Mods.Num(); ++Index)
		{
			const FItemStatModifier& Mod = Mods[Index];
			const bool bAttrValid = Mod.Attribute.IsValid();
			const bool bHasAttrSet = bAttrValid ? ASC->HasAttributeSetForAttribute(Mod.Attribute) : false;
			const float PreValue = (bAttrValid && bHasAttrSet) ? ASC->GetNumericAttribute(Mod.Attribute) : 0.f;
			PreValues.Add(PreValue);
			AJ_LOG(this, TEXT("[ItemStatsDebug] Mod[%d] Attr=%s Valid=%d Op=%d Mag=%.3f"),
				Index,
				*Mod.Attribute.GetName(),
				bAttrValid ? 1 : 0,
				static_cast<int32>(Mod.Op),
				Mod.Magnitude);
			if (bAttrValid)
			{
				AJ_LOG(this, TEXT("[ItemStatsDebug] Mod[%d] Attr=%s HasAttrSet=%d Pre=%.3f"),
					Index,
					*Mod.Attribute.GetName(),
					bHasAttrSet ? 1 : 0,
					PreValue);
			}

			if (bAttrValid)
			{
				bHasValidModifier = true;
			}
		}

		if (bHasValidModifier)
		{
			// Use the class CDO so the GameplayEffect definition replicates cleanly to clients.
			const FGameplayEffectSpecHandle StatSpecHandle = ASC->MakeOutgoingSpec(ItemStatsEffectClass, Level, Context);
			if (StatSpecHandle.IsValid() && StatSpecHandle.Data.IsValid())
			{
				StatSpecHandle.Data->SetSetByCallerMagnitude(MultiplierName, Multiplier);
				StatSpecHandle.Data->SetDuration(UGameplayEffect::INSTANT_APPLICATION, true);
				ASC->ApplyGameplayEffectSpecToSelf(*StatSpecHandle.Data.Get());
				HandleSet.bAppliedItemStats = bTrackHandles;
			}

			for (int32 Index = 0; Index < Mods.Num(); ++Index)
			{
				const FItemStatModifier& Mod = Mods[Index];
				if (!Mod.Attribute.IsValid())
				{
					continue;
				}

				const bool bHasAttrSet = ASC->HasAttributeSetForAttribute(Mod.Attribute);
				const float PostValue = bHasAttrSet ? ASC->GetNumericAttribute(Mod.Attribute) : 0.f;
				const float PreValue = PreValues.IsValidIndex(Index) ? PreValues[Index] : 0.f;
				AJ_LOG(this, TEXT("[ItemStatsDebug] Mod[%d] Attr=%s HasAttrSet=%d Pre=%.3f Post=%.3f Delta=%.3f"),
					Index,
					*Mod.Attribute.GetName(),
					bHasAttrSet ? 1 : 0,
					PreValue,
					PostValue,
					PostValue - PreValue);
			}
		}
		else
		{
			AJ_LOG(this, TEXT("[ItemStatsDebug] No valid modifiers found for Item=%s (Definition=%s)"),
				*GetNameSafe(Item),
				*GetNameSafe(Item->Definition.Get()));
		}

		for (const FItemGrantedEffect& Granted : Item->GetGrantedEffects())
		{
			if (!Granted.IsValid())
			{
				continue;
			}

			FGameplayEffectContextHandle ExtraContext = ASC->MakeEffectContext();
			ExtraContext.AddSourceObject(Item);

			FGameplayEffectSpecHandle ExtraSpecHandle = ASC->MakeOutgoingSpec(Granted.EffectClass, Granted.EffectLevel, ExtraContext);
			if (!ExtraSpecHandle.IsValid())
			{
				continue;
			}

			FGameplayEffectSpec* ExtraSpec = ExtraSpecHandle.Data.Get();
			if (!ExtraSpec)
			{
				continue;
			}

			if (Granted.ApplicationTags.Num() > 0)
			{
				ExtraSpec->DynamicGrantedTags.AppendTags(Granted.ApplicationTags);
			}

			FActiveGameplayEffectHandle ExtraHandle = ASC->ApplyGameplayEffectSpecToSelf(*ExtraSpec);
			if (ExtraHandle.IsValid())
			{
				HandleSet.AdditionalHandles.Add(ExtraHandle);
			}
		}

		if (bIsAuthority)
		{
			for (const FItemGrantedAbility& AbilityGrant : Item->GetGrantedAbilities())
			{
				if (!AbilityGrant.IsValid())
				{
					continue;
				}

				FGameplayAbilitySpec AbilitySpec(AbilityGrant.AbilityClass, AbilityGrant.AbilityLevel);
				if (AbilityGrant.InputID != INDEX_NONE)
				{
					AbilitySpec.InputID = AbilityGrant.InputID;
				}

				AbilitySpec.SourceObject = Item;

				if (AbilityGrant.OwnedTags.Num() > 0)
				{
					FGameplayTagContainer& SpecTags = AbilitySpec.GetDynamicSpecSourceTags();
					SpecTags.AppendTags(AbilityGrant.OwnedTags);
				}

				const FGameplayAbilitySpecHandle AbilityHandle = ASC->GiveAbility(AbilitySpec);
				if (AbilityHandle.IsValid())
				{
					HandleSet.GrantedAbilityHandles.Add(AbilityHandle);

					if (AbilityGrant.OwnedTags.Num() > 0)
					{
						for (const FGameplayTag& Tag : AbilityGrant.OwnedTags)
						{
							ASC->AddLooseGameplayTag(Tag);
							HandleSet.AddedOwnedTags.Add(Tag);
						}
					}
				}
			}
		}

		if (bTrackHandles && (HandleSet.StatsHandle.IsValid()
			|| HandleSet.AdditionalHandles.Num() > 0
			|| HandleSet.GrantedAbilityHandles.Num() > 0
			|| HandleSet.AddedOwnedTags.Num() > 0
			|| HandleSet.bAppliedItemStats))
		{
			ActiveEffectHandles.Add(Item->UniqueId, MoveTemp(HandleSet));
			AJ_LOG(this, TEXT("[ItemStatsDebug] Applied handles Stats=%d Extra=%d Abilities=%d Tags=%d"),
				HandleSet.StatsHandle.IsValid() ? 1 : 0,
				HandleSet.AdditionalHandles.Num(),
				HandleSet.GrantedAbilityHandles.Num(),
				HandleSet.AddedOwnedTags.Num());
		}
		else
		{
			AJ_LOG(this, TEXT("[ItemStatsDebug] No handles created for Item=%s"), *GetNameSafe(Item));
		}
	}
	else
	{
		AJ_LOG(this, TEXT("[ItemStatsDebug] ApplyItemGameplayEffect skipped: ASC missing for Item=%s Owner=%s"),
			*GetNameSafe(Item),
			*GetNameSafe(GetOwner()));
	}
}

void UAeyerjiInventoryComponent::RemoveItemGameplayEffect(const FGuid& ItemId)
{
	if (UAbilitySystemComponent* ASC = GetASC())
	{
		const bool bIsAuthority = GetOwner() && GetOwner()->HasAuthority();

		if (FItemActiveEffectSet* HandleSet = ActiveEffectHandles.Find(ItemId))
		{
			if (HandleSet->StatsHandle.IsValid())
			{
				ASC->RemoveActiveGameplayEffect(HandleSet->StatsHandle);
			}
			else if (HandleSet->bAppliedItemStats)
			{
				if (UAeyerjiItemInstance* Item = FindItemById(ItemId))
				{
					ApplyItemGameplayEffect(Item, -1.f);
				}
				else
				{
					AJ_LOG(this, TEXT("[ItemStatsDebug] RemoveItemGameplayEffect could not find ItemId=%s for inverse apply"),
						*ItemId.ToString());
				}
			}

			for (FActiveGameplayEffectHandle& Extra : HandleSet->AdditionalHandles)
			{
				if (Extra.IsValid())
				{
					ASC->RemoveActiveGameplayEffect(Extra);
				}
			}

			if (bIsAuthority)
			{
				for (FGameplayAbilitySpecHandle& AbilityHandle : HandleSet->GrantedAbilityHandles)
				{
					if (AbilityHandle.IsValid())
					{
						ASC->ClearAbility(AbilityHandle);
					}
				}

				if (HandleSet->AddedOwnedTags.Num() > 0)
				{
					for (const FGameplayTag& Tag : HandleSet->AddedOwnedTags)
					{
						ASC->RemoveLooseGameplayTag(Tag);
					}
				}
			}

			ActiveEffectHandles.Remove(ItemId);
		}
	}
}

bool UAeyerjiInventoryComponent::GetPlacementForItem(const FGuid& ItemId, FInventoryItemGridData& OutPlacement) const
{
	if (!ItemId.IsValid())
	{
		return false;
	}

	if (const FInventoryItemGridData* Found = GridPlacements.FindByPredicate(
		[&ItemId](const FInventoryItemGridData& Entry)
		{
			return Entry.ItemId == ItemId;
		}))
	{
		OutPlacement = *Found;
		return true;
	}

	return false;
}

bool UAeyerjiInventoryComponent::AutoPlaceItem(UAeyerjiItemInstance* Item)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return false;
	}

	return TryAutoPlaceItem(Item);
}

void UAeyerjiInventoryComponent::Server_MoveItemInGrid_Implementation(const FGuid& ItemId, FIntPoint NewTopLeft)
{
	if (!ItemId.IsValid())
	{
		return;
	}

	FInventoryItemGridData* Existing = GridPlacements.FindByPredicate(
		[&ItemId](const FInventoryItemGridData& Entry)
		{
			return Entry.ItemId == ItemId;
		});

	if (!Existing)
	{
		return;
	}

	UAeyerjiItemInstance* Item = FindItemById(ItemId);
	if (!Item)
	{
		return;
	}

	FInventoryItemGridData Candidate = *Existing;
	Candidate.TopLeft = NewTopLeft;
	Candidate.Size = Item->InventorySize;
	Candidate.ItemInstance = Item;

	if (!CanPlaceAt(Candidate, ItemId))
	{
		return;
	}

	*Existing = Candidate;
	MARK_PROPERTY_DIRTY_FROM_NAME(UAeyerjiInventoryComponent, GridPlacements, this);
	OnInventoryChanged.Broadcast();
}

void UAeyerjiInventoryComponent::Server_SwapItemsInGrid_Implementation(const FGuid& ItemIdA, const FGuid& ItemIdB)
{
	if (!ItemIdA.IsValid() || !ItemIdB.IsValid() || ItemIdA == ItemIdB)
	{
		return;
	}

	FInventoryItemGridData* PlacementA = GridPlacements.FindByPredicate(
		[&ItemIdA](const FInventoryItemGridData& Entry)
		{
			return Entry.ItemId == ItemIdA;
		});

	FInventoryItemGridData* PlacementB = GridPlacements.FindByPredicate(
		[&ItemIdB](const FInventoryItemGridData& Entry)
		{
			return Entry.ItemId == ItemIdB;
		});

	if (!PlacementA || !PlacementB)
	{
		return;
	}

	UAeyerjiItemInstance* ItemA = FindItemById(ItemIdA);
	UAeyerjiItemInstance* ItemB = FindItemById(ItemIdB);
	if (!ItemA || !ItemB)
	{
		return;
	}

	FInventoryItemGridData CandidateA = *PlacementA;
	CandidateA.TopLeft = PlacementB->TopLeft;
	CandidateA.Size = ItemA->InventorySize;
	CandidateA.ItemInstance = ItemA;

	FInventoryItemGridData CandidateB = *PlacementB;
	CandidateB.TopLeft = PlacementA->TopLeft;
	CandidateB.Size = ItemB->InventorySize;
	CandidateB.ItemInstance = ItemB;

	if (!CanPlaceAt(CandidateA, ItemIdB) || !CanPlaceAt(CandidateB, ItemIdA))
	{
		return;
	}

	*PlacementA = CandidateA;
	*PlacementB = CandidateB;

	MARK_PROPERTY_DIRTY_FROM_NAME(UAeyerjiInventoryComponent, GridPlacements, this);
	OnInventoryChanged.Broadcast();
}

void UAeyerjiInventoryComponent::Server_SwapEquippedSlots_Implementation(EEquipmentSlot Slot, int32 SlotIndexA, int32 SlotIndexB)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	SlotIndexA = SanitizeSlotIndex(SlotIndexA);
	SlotIndexB = SanitizeSlotIndex(SlotIndexB);

	if (SlotIndexA == SlotIndexB)
	{
		return;
	}

	FEquippedItemEntry* EntryA = FindEquippedEntry(Slot, SlotIndexA);
	FEquippedItemEntry* EntryB = FindEquippedEntry(Slot, SlotIndexB);

	if (!EntryA && !EntryB)
	{
		return;
	}

	auto UpdateEntryMeta = [Slot](FEquippedItemEntry& Entry, int32 NewIndex)
	{
		Entry.Slot = Slot;
		Entry.SlotIndex = NewIndex;
		if (Entry.Item)
		{
			Entry.Item->EquippedSlot = Slot;
			Entry.Item->EquippedSlotIndex = NewIndex;
		}
	};

	if (EntryA && EntryB)
	{
		Swap(*EntryA, *EntryB);
		UpdateEntryMeta(*EntryA, SlotIndexA);
		UpdateEntryMeta(*EntryB, SlotIndexB);
	}
	else if (EntryA && !EntryB)
	{
		EntryA->SlotIndex = SlotIndexB;
		if (EntryA->Item)
		{
			EntryA->Item->EquippedSlotIndex = SlotIndexB;
		}
	}
	else if (!EntryA && EntryB)
	{
		EntryB->SlotIndex = SlotIndexA;
		if (EntryB->Item)
		{
			EntryB->Item->EquippedSlotIndex = SlotIndexA;
		}
	}

	MARK_PROPERTY_DIRTY_FROM_NAME(UAeyerjiInventoryComponent, EquippedItems, this);
	OnEquippedItemChanged.Broadcast(Slot, SlotIndexA, GetEquipped(Slot, SlotIndexA));
	OnEquippedItemChanged.Broadcast(Slot, SlotIndexB, GetEquipped(Slot, SlotIndexB));
	RebuildItemSnapshots();
}

void UAeyerjiInventoryComponent::SetGridDimensions(int32 Columns, int32 Rows)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	const int32 NewColumns = FMath::Max(1, Columns);
	const int32 NewRows = FMath::Max(1, Rows);

	if (GridColumns == NewColumns && GridRows == NewRows)
	{
		return;
	}

	GridColumns = NewColumns;
	GridRows = NewRows;

	MARK_PROPERTY_DIRTY_FROM_NAME(UAeyerjiInventoryComponent, GridColumns, this);
	MARK_PROPERTY_DIRTY_FROM_NAME(UAeyerjiInventoryComponent, GridRows, this);

	OnInventoryChanged.Broadcast();
}

void UAeyerjiInventoryComponent::Server_DropItem_Implementation(const FGuid& ItemId, FVector WorldLocation, FRotator WorldRotation)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	UAeyerjiItemInstance* Item = FindItemById(ItemId);
	if (!Item)
	{
		return;
	}

	const int32 EquippedIndex = EquippedItems.IndexOfByPredicate(
		[Item](const FEquippedItemEntry& Entry)
		{
			return Entry.Item == Item;
		});

	EEquipmentSlot PreviousSlot = Item->Definition
		? ResolveEquipmentSlot(Item->Definition->DefaultSlot, Item->Definition.Get())
		: ResolveEquipmentSlot(Item->EquippedSlot, Item->Definition.Get());
	int32 PreviousSlotIndex = Item->EquippedSlotIndex;
	const bool bWasEquipped = EquippedIndex != INDEX_NONE;

	if (bWasEquipped)
	{
		PreviousSlot = EquippedItems[EquippedIndex].Slot;
		PreviousSlotIndex = EquippedItems[EquippedIndex].SlotIndex;
		RemoveItemGameplayEffect(Item->UniqueId);
		EquippedItems.RemoveAt(EquippedIndex);
		MARK_PROPERTY_DIRTY_FROM_NAME(UAeyerjiInventoryComponent, EquippedItems, this);
		OnEquippedItemChanged.Broadcast(PreviousSlot, PreviousSlotIndex, nullptr);
		BroadcastItemStateChange(EInventoryItemStateChange::Unequipped, Item, PreviousSlot, PreviousSlotIndex);
	}

	ClearPlacement(ItemId);

	if (Items.RemoveSingle(Item) > 0)
	{
		UnbindItemInstanceDelegates(Item);
		OnInventoryChanged.Broadcast();
		BroadcastItemStateChange(EInventoryItemStateChange::Removed, Item, PreviousSlot, PreviousSlotIndex);
		RebuildItemSnapshots();
	}

	Item->EquippedSlot = Item->Definition
		? ResolveEquipmentSlot(Item->Definition->DefaultSlot, Item->Definition.Get())
		: ResolveEquipmentSlot(PreviousSlot, nullptr);
	Item->EquippedSlotIndex = INDEX_NONE;

	ActiveEffectHandles.Remove(Item->UniqueId);

	if (UWorld* World = GetWorld())
	{
		const FVector GroundedLocation = FindGroundedDropLocation(*World, WorldLocation);

		AJ_LOG(this, TEXT("Server_DropItem dropping %s at %s (grounded %s) Rot=%s Class=%s"),
			*GetNameSafe(Item),
			*WorldLocation.ToString(),
			*GroundedLocation.ToString(),
			*WorldRotation.ToString(),
			*GetNameSafe(LootPickupClass.Get()));

		if (!UAeyerjiInventoryBPFL::SpawnLootByInstance(this, Item, GroundedLocation, WorldRotation))
		{
			if (!AddItemInstance(Item, true))
			{
				if (bWasEquipped)
				{
					Server_EquipItem_Implementation(Item->UniqueId, PreviousSlot, PreviousSlotIndex);
				}
				return;
			}

			if (!AutoPlaceItem(Item) && bWasEquipped)
			{
				Server_EquipItem_Implementation(Item->UniqueId, PreviousSlot, PreviousSlotIndex);
			}
		}
	}

	BroadcastItemStateChange(EInventoryItemStateChange::Dropped, Item, PreviousSlot, PreviousSlotIndex);
}

void UAeyerjiInventoryComponent::DropItem(const FGuid& ItemId, FVector WorldLocation, FRotator WorldRotation)
{
	if (GetOwnerRole() == ROLE_Authority)
	{
		Server_DropItem_Implementation(ItemId, WorldLocation, WorldRotation);
		return;
	}

	Server_DropItem(ItemId, WorldLocation, WorldRotation);
}

void UAeyerjiInventoryComponent::DropItemAtOwner(const FGuid& ItemId, float ForwardOffset)
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	const FVector DropLocation = OwnerActor->GetActorLocation() + OwnerActor->GetActorForwardVector() * FMath::Max(ForwardOffset, 0.f);
	const FRotator DropRotation = OwnerActor->GetActorRotation();
	DropItem(ItemId, DropLocation, DropRotation);
}

int32 UAeyerjiInventoryComponent::DebugRefreshItemScaling(const UAeyerjiLootTable& LootTable)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return 0;
	}

	TSet<UAeyerjiItemInstance*> ItemsToProcess;
	for (UAeyerjiItemInstance* Item : Items)
	{
		ItemsToProcess.Add(Item);
	}
	for (const FEquippedItemEntry& Entry : EquippedItems)
	{
		ItemsToProcess.Add(Entry.Item);
	}

	int32 UpdatedCount = 0;
	for (UAeyerjiItemInstance* Item : ItemsToProcess)
	{
		if (!Item)
		{
			continue;
		}

		Item->RebuildAggregation();
		Item->ApplyLootStatScaling(&LootTable);

		const bool bIsEquipped = EquippedItems.ContainsByPredicate(
			[Item](const FEquippedItemEntry& Entry)
			{
				return Entry.Item == Item || Entry.ItemId == Item->UniqueId;
			});

		if (bIsEquipped)
		{
			ApplyItemGameplayEffect(Item);
		}

		Item->ForceItemChangedForUI();
		++UpdatedCount;
	}

	if (UpdatedCount > 0)
	{
		RebuildItemSnapshots();
	}

	AJ_LOG(this, TEXT("DebugRefreshItemScaling updated %d items"), UpdatedCount);
	return UpdatedCount;
}

void UAeyerjiInventoryComponent::OnRep_EquippedItems(const TArray<FEquippedItemEntry>& PreviousEquipped)
{
	ResolveEquippedItems();
	AJ_LOG(this, TEXT("OnRep_EquippedItems Prev=%d New=%d"), PreviousEquipped.Num(), EquippedItems.Num());

	auto MakeSlotKey = [](EEquipmentSlot Slot, int32 Index)
	{
		return (static_cast<int64>(Slot) << 32) | static_cast<uint32>(Index);
	};

	TMap<int64, FGuid> OldEntries;
	for (const FEquippedItemEntry& Entry : PreviousEquipped)
	{
		const int32 SanitizedIndex = FMath::Max(0, Entry.SlotIndex);
		OldEntries.Add(MakeSlotKey(Entry.Slot, SanitizedIndex), Entry.ItemId);
	}

	for (const FEquippedItemEntry& Entry : EquippedItems)
	{
		UAeyerjiItemInstance* const CurrentItem = Entry.Item;
		const int32 SanitizedIndex = FMath::Max(0, Entry.SlotIndex);
		const int64 SlotKey = MakeSlotKey(Entry.Slot, SanitizedIndex);
		if (FGuid* OldIdPtr = OldEntries.Find(SlotKey))
		{
			if (*OldIdPtr != Entry.ItemId)
			{
				OnEquippedItemChanged.Broadcast(Entry.Slot, SanitizedIndex, CurrentItem);
			}
			OldEntries.Remove(SlotKey);
		}
		else
		{
			OnEquippedItemChanged.Broadcast(Entry.Slot, SanitizedIndex, CurrentItem);
		}
	}

	for (const TPair<int64, FGuid>& Pair : OldEntries)
	{
		const EEquipmentSlot RemovedSlot = static_cast<EEquipmentSlot>(Pair.Key >> 32);
		const int32 RemovedIndex = static_cast<int32>(Pair.Key & 0xFFFFFFFF);
		OnEquippedItemChanged.Broadcast(RemovedSlot, RemovedIndex, nullptr);
	}
}

void UAeyerjiInventoryComponent::OnRep_Items()
{
	UE_LOG(LogTemp, Display, TEXT("[Inventory] OnRep_Items count=%d"), Items.Num());

	for (TObjectPtr<UAeyerjiItemInstance>& Item : Items)
	{
		if (Item)
		{
			UE_LOG(LogTemp, Display, TEXT("[Inventory] OnRep_Items item %s (Id=%s) Outer=%s"),
				*Item->GetName(), *Item->UniqueId.ToString(), *GetNameSafe(Item->GetOuter()));
			Item->ForceItemChangedForUI();
		}
	}

	ResolveEquippedItems();

	for (const FEquippedItemEntry& Entry : EquippedItems)
	{
		if (Entry.Item)
		{
			AJ_LOG(this, TEXT("OnRep_Items resolved equipped slot %d -> %s"),
				static_cast<int32>(Entry.Slot),
				*Entry.Item->UniqueId.ToString());
			OnEquippedItemChanged.Broadcast(Entry.Slot, Entry.SlotIndex, Entry.Item);
		}
		else if (Entry.ItemId.IsValid())
		{
			AJ_LOG(this, TEXT("OnRep_Items still missing item for slot %d id=%s"),
				static_cast<int32>(Entry.Slot),
				*Entry.ItemId.ToString());
		}
		else
		{
			AJ_LOG(this, TEXT("OnRep_Items slot %d has no assignment"), static_cast<int32>(Entry.Slot));
		}
	}

	if (SyncGridItemInstances())
	{
		OnInventoryChanged.Broadcast();
	}
	else
	{
		ScheduleGridSyncRetry();
	}
}

void UAeyerjiInventoryComponent::OnRep_GridPlacements()
{
	UE_LOG(LogTemp, Display, TEXT("[Inventory] OnRep_GridPlacements count=%d"), GridPlacements.Num());
	if (SyncGridItemInstances())
	{
		OnInventoryChanged.Broadcast();
	}
	else
	{
		ScheduleGridSyncRetry();
	}
}

void UAeyerjiInventoryComponent::OnRep_ItemSnapshots()
{
	if (GetOwnerRole() == ROLE_Authority)
	{
		return;
	}

	UE_LOG(LogTemp, Display, TEXT("[Inventory] OnRep_ItemSnapshots count=%d"), ItemSnapshots.Num());
	RefreshClientItemsFromSnapshots();
	OnRep_Items();
}

void UAeyerjiInventoryComponent::OnRep_GridSize()
{
	OnInventoryChanged.Broadcast();
}

FAeyerjiInventorySaveData UAeyerjiInventoryComponent::BuildSaveData()
{
	FAeyerjiInventorySaveData SaveData;

	if (GetOwnerRole() != ROLE_Authority)
	{
		return SaveData;
	}

	RebuildItemSnapshots();

	SaveData.ItemSnapshots = ItemSnapshots;
	SaveData.GridPlacements = GridPlacements;
	SaveData.EquippedItems = EquippedItems;
	SaveData.GridColumns = GridColumns;
	SaveData.GridRows = GridRows;

	for (FInventoryItemGridData& Placement : SaveData.GridPlacements)
	{
		Placement.ItemInstance = nullptr;
	}

	for (FEquippedItemEntry& Entry : SaveData.EquippedItems)
	{
		Entry.Item = nullptr;
	}

	return SaveData;
}

void UAeyerjiInventoryComponent::ApplySaveData(const FAeyerjiInventorySaveData& SaveData)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	for (UAeyerjiItemInstance* Item : Items)
	{
		UnbindItemInstanceDelegates(Item);
	}

	TArray<FGuid> ActiveIds;
	ActiveEffectHandles.GetKeys(ActiveIds);
	for (const FGuid& ActiveId : ActiveIds)
	{
		RemoveItemGameplayEffect(ActiveId);
	}

	Items.Reset();
	ItemChangedDelegateHandles.Reset();
	ActiveEffectHandles.Reset();
	GridPlacements.Reset();
	EquippedItems.Reset();
	ItemSnapshots.Reset();

	GridColumns = SaveData.GridColumns > 0 ? SaveData.GridColumns : GridColumns;
	GridRows = SaveData.GridRows > 0 ? SaveData.GridRows : GridRows;

	for (const FInventoryItemSnapshot& Snapshot : SaveData.ItemSnapshots)
	{
		if (!Snapshot.ItemId.IsValid())
		{
			continue;
		}

		UAeyerjiItemInstance* Item = NewObject<UAeyerjiItemInstance>(this);
		Item->Definition = Snapshot.Definition;
		Item->Rarity = Snapshot.Rarity;
		Item->ItemLevel = Snapshot.ItemLevel;
		Item->Seed = Snapshot.Seed;
		Item->UniqueId = Snapshot.ItemId;
		Item->RolledAffixes = Snapshot.RolledAffixes;
		Item->FinalAggregatedModifiers = Snapshot.FinalAggregatedModifiers;
		Item->GrantedEffects = Snapshot.GrantedEffects;
		Item->GrantedAbilities = Snapshot.GrantedAbilities;
		Item->EquippedSlot = Snapshot.EquippedSlot;
		Item->EquippedSlotIndex = Snapshot.SlotIndex;
		Item->InventorySize = Snapshot.InventorySize;
		Item->SetNetAddressable();

		AddItemInstance(Item, /*bSkipAutoPlacement=*/true);
	}

	GridPlacements = SaveData.GridPlacements;
	for (FInventoryItemGridData& Placement : GridPlacements)
	{
		Placement.ItemInstance = FindItemById(Placement.ItemId);
	}
	MARK_PROPERTY_DIRTY_FROM_NAME(UAeyerjiInventoryComponent, GridPlacements, this);

	EquippedItems = SaveData.EquippedItems;
	for (FEquippedItemEntry& Entry : EquippedItems)
	{
		Entry.Item = FindItemById(Entry.ItemId);
	}
	ResolveEquippedItems();
	MARK_PROPERTY_DIRTY_FROM_NAME(UAeyerjiInventoryComponent, EquippedItems, this);

	RebuildItemSnapshots();

	// Re-apply gameplay effects / abilities for equipped items after loading
	for (FEquippedItemEntry& Entry : EquippedItems)
	{
		if (Entry.Item)
		{
			ApplyItemGameplayEffect(Entry.Item);
			BroadcastItemStateChange(EInventoryItemStateChange::Equipped, Entry.Item, Entry.Slot, Entry.SlotIndex);
		}
	}

	OnInventoryChanged.Broadcast();
	for (const FEquippedItemEntry& Entry : EquippedItems)
	{
		OnEquippedItemChanged.Broadcast(Entry.Slot, Entry.SlotIndex, Entry.Item);
	}
}

void UAeyerjiInventoryComponent::BroadcastItemStateChange(EInventoryItemStateChange Change, UAeyerjiItemInstance* Item, EEquipmentSlot Slot, int32 SlotIndex)
{
	if (!Item)
	{
		return;
	}

	FInventoryItemChangeEvent Event;
	Event.Change = Change;
	Event.Item = Item;
	Event.ItemId = Item->UniqueId;
	Event.Slot = Slot;
	Event.SlotIndex = SlotIndex;

	OnInventoryItemStateChanged.Broadcast(Event);
}

bool UAeyerjiInventoryComponent::CanPlaceItemAt(FIntPoint TopLeft, FIntPoint Size, const FGuid& IgnoredItem) const
{
	FInventoryItemGridData Placement;
	Placement.ItemId = IgnoredItem.IsValid() ? IgnoredItem : FGuid::NewGuid();
	Placement.TopLeft = TopLeft;
	Placement.Size = Size;
	return CanPlaceAt(Placement, IgnoredItem);
}

bool UAeyerjiInventoryComponent::SyncGridItemInstances()
{
	bool bAllResolved = true;
	for (FInventoryItemGridData& Placement : GridPlacements)
	{
		if (!Placement.ItemInstance || Placement.ItemInstance->UniqueId != Placement.ItemId)
		{
			Placement.ItemInstance = FindItemById(Placement.ItemId);
			if (!Placement.ItemInstance)
			{
				bAllResolved = false;
				UE_LOG(LogTemp, Warning, TEXT("[Inventory] SyncGridItemInstances unresolved %s"), *Placement.ItemId.ToString());
			}
			else
			{
				UE_LOG(LogTemp, Display, TEXT("[Inventory] SyncGridItemInstances resolved %s -> %s"),
					*Placement.ItemId.ToString(), *Placement.ItemInstance->GetName());
			}
		}
	}

	UE_LOG(LogTemp, Display, TEXT("[Inventory] SyncGridItemInstances %s (%d placements)"),
		bAllResolved ? TEXT("complete") : TEXT("pending"), GridPlacements.Num());
	return bAllResolved;
}

bool UAeyerjiInventoryComponent::TryAutoPlaceItem(UAeyerjiItemInstance* Item)
{
	if (!Item || !Item->UniqueId.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Inventory] TryAutoPlaceItem invalid item"));
		return false;
	}

	Item->SetNetAddressable();

	FInventoryItemGridData Existing;
	if (GetPlacementForItem(Item->UniqueId, Existing))
	{
		return true;
	}

	if (GridColumns <= 0 || GridRows <= 0)
	{
		return false;
	}

	FIntPoint Size = Item->InventorySize;
	Size.X = FMath::Max(1, Size.X);
	Size.Y = FMath::Max(1, Size.Y);

	for (int32 Y = 0; Y <= GridRows - Size.Y; ++Y)
	{
		for (int32 X = 0; X <= GridColumns - Size.X; ++X)
		{
			FInventoryItemGridData Candidate;
			Candidate.ItemId = Item->UniqueId;
			Candidate.TopLeft = FIntPoint(X, Y);
			Candidate.Size = Size;
			Candidate.ItemInstance = Item;

			if (CanPlaceAt(Candidate))
			{
				UE_LOG(LogTemp, Display, TEXT("[Inventory] Placing %s at (%d,%d) size (%d,%d)"),
					*Item->UniqueId.ToString(), X, Y, Size.X, Size.Y);
				GridPlacements.Add(Candidate);
				MARK_PROPERTY_DIRTY_FROM_NAME(UAeyerjiInventoryComponent, GridPlacements, this);
				OnInventoryChanged.Broadcast();
				return true;
			}
		}
	}

	return false;
}

bool UAeyerjiInventoryComponent::CanPlaceAt(const FInventoryItemGridData& Placement, const FGuid& IgnoredItem) const
{
	if (!Placement.ItemId.IsValid() && !IgnoredItem.IsValid())
	{
		return false;
	}

	const FIntPoint Size = FIntPoint(
		FMath::Max(1, Placement.Size.X),
		FMath::Max(1, Placement.Size.Y));

	if (Placement.TopLeft.X < 0 || Placement.TopLeft.Y < 0)
	{
		return false;
	}

	if (Placement.TopLeft.X + Size.X > GridColumns || Placement.TopLeft.Y + Size.Y > GridRows)
	{
		return false;
	}

	for (const FInventoryItemGridData& Existing : GridPlacements)
	{
		if (Existing.ItemId == Placement.ItemId || (IgnoredItem.IsValid() && Existing.ItemId == IgnoredItem))
		{
			continue;
		}

		const bool bSeparateX =
			Placement.TopLeft.X + Size.X <= Existing.TopLeft.X ||
			Existing.TopLeft.X + Existing.Size.X <= Placement.TopLeft.X;

			const bool bSeparateY =
				Placement.TopLeft.Y + Size.Y <= Existing.TopLeft.Y ||
				Existing.TopLeft.Y + Existing.Size.Y <= Placement.TopLeft.Y;

		if (!(bSeparateX || bSeparateY))
		{
			UE_LOG(LogTemp, Display, TEXT("[Inventory] CanPlaceAt blocked by %s at (%d,%d) size (%d,%d)"),
				*Existing.ItemId.ToString(), Existing.TopLeft.X, Existing.TopLeft.Y, Existing.Size.X, Existing.Size.Y);
			return false;
		}
	}

	return true;
}

void UAeyerjiInventoryComponent::ScheduleGridSyncRetry()
{
	if (bGridSyncRetryScheduled || !GetWorld())
	{
		return;
	}

	bGridSyncRetryScheduled = true;
	GetWorld()->GetTimerManager().SetTimer(GridSyncRetryHandle, this, &UAeyerjiInventoryComponent::HandleDeferredGridSync, 0.05f, false);
}

void UAeyerjiInventoryComponent::HandleDeferredGridSync()
{
	bGridSyncRetryScheduled = false;
	if (SyncGridItemInstances())
	{
		OnInventoryChanged.Broadcast();
	}
	else
	{
		ScheduleGridSyncRetry();
	}
}

void UAeyerjiInventoryComponent::RebuildItemSnapshots()
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	ItemSnapshots.Reset();
	ItemSnapshots.Reserve(Items.Num());

	for (UAeyerjiItemInstance* Item : Items)
	{
		if (!Item)
		{
			continue;
		}

		FInventoryItemSnapshot Snapshot;
		Snapshot.ItemId = Item->UniqueId;
		Snapshot.Definition = Item->Definition;
		Snapshot.Rarity = Item->Rarity;
		Snapshot.ItemLevel = Item->ItemLevel;
		Snapshot.Seed = Item->Seed;
		Snapshot.RolledAffixes = Item->RolledAffixes;
		Snapshot.FinalAggregatedModifiers = Item->FinalAggregatedModifiers;
		Snapshot.GrantedEffects = Item->GrantedEffects;
		Snapshot.GrantedAbilities = Item->GrantedAbilities;
		Snapshot.EquippedSlot = Item->EquippedSlot;
		Snapshot.SlotIndex = Item->EquippedSlotIndex;
		Snapshot.InventorySize = Item->InventorySize;

		ItemSnapshots.Add(MoveTemp(Snapshot));
	}

	MARK_PROPERTY_DIRTY_FROM_NAME(UAeyerjiInventoryComponent, ItemSnapshots, this);
}

void UAeyerjiInventoryComponent::ResolveEquippedItems()
{
	PruneEmptyEquippedEntries();

	for (FEquippedItemEntry& Entry : EquippedItems)
	{
		UAeyerjiItemInstance* const ResolvedItem = Entry.ItemId.IsValid() ? FindItemById(Entry.ItemId) : nullptr;
		if (!ResolvedItem && Entry.ItemId.IsValid())
		{
			AJ_LOG(this, TEXT("ResolveEquippedItems unresolved ItemId=%s Slot=%d"),
				*Entry.ItemId.ToString(),
				static_cast<int32>(Entry.Slot));
		}

		Entry.Item = ResolvedItem;
		const EEquipmentSlot SanitizedSlot = ResolveEquipmentSlot(Entry.Slot, Entry.Item ? Entry.Item->Definition.Get() : nullptr);
		if (SanitizedSlot != Entry.Slot)
		{
			AJ_LOG(this, TEXT("ResolveEquippedItems sanitized slot %d -> %d for %s"),
				static_cast<int32>(Entry.Slot),
				static_cast<int32>(SanitizedSlot),
				Entry.Item ? *Entry.Item->UniqueId.ToString() : TEXT("None"));
			Entry.Slot = SanitizedSlot;
		}

		const int32 SanitizedIndex = SanitizeSlotIndex(Entry.SlotIndex);
		if (SanitizedIndex != Entry.SlotIndex)
		{
			AJ_LOG(this, TEXT("ResolveEquippedItems sanitized slot index %d -> %d for %s"),
				Entry.SlotIndex,
				SanitizedIndex,
				Entry.Item ? *Entry.Item->UniqueId.ToString() : TEXT("None"));
			Entry.SlotIndex = SanitizedIndex;
		}

		if (Entry.Item)
		{
			Entry.Item->EquippedSlot = Entry.Slot;
			Entry.Item->EquippedSlotIndex = Entry.SlotIndex;
		}
	}
}

void UAeyerjiInventoryComponent::BindItemInstanceDelegates(UAeyerjiItemInstance* Item)
{
	if (GetOwnerRole() != ROLE_Authority || !Item)
	{
		return;
	}

	if (ItemChangedDelegateHandles.Contains(Item))
	{
		return;
	}

	const FDelegateHandle Handle = Item->GetOnItemChangedDelegate().AddUObject(this, &UAeyerjiInventoryComponent::HandleServerItemStateChanged);
	ItemChangedDelegateHandles.Add(Item, Handle);
}

void UAeyerjiInventoryComponent::UnbindItemInstanceDelegates(UAeyerjiItemInstance* Item)
{
	if (!Item)
	{
		return;
	}

	if (FDelegateHandle* Handle = ItemChangedDelegateHandles.Find(Item))
	{
		Item->GetOnItemChangedDelegate().Remove(*Handle);
		ItemChangedDelegateHandles.Remove(Item);
	}
}

void UAeyerjiInventoryComponent::HandleServerItemStateChanged()
{
	RebuildItemSnapshots();
}

void UAeyerjiInventoryComponent::RefreshClientItemsFromSnapshots()
{
	if (GetOwnerRole() == ROLE_Authority)
	{
		return;
	}

	TSet<FGuid> SnapshotIds;
	SnapshotIds.Reserve(ItemSnapshots.Num());

	for (const FInventoryItemSnapshot& Snapshot : ItemSnapshots)
	{
		if (!Snapshot.ItemId.IsValid())
		{
			continue;
		}

		SnapshotIds.Add(Snapshot.ItemId);

		UAeyerjiItemInstance* Item = FindItemById(Snapshot.ItemId);
		if (!Item)
		{
			Item = NewObject<UAeyerjiItemInstance>(this);
			Items.Add(Item);
		}

		Item->Definition = Snapshot.Definition;
		Item->Rarity = Snapshot.Rarity;
		Item->ItemLevel = Snapshot.ItemLevel;
		Item->Seed = Snapshot.Seed;
		Item->UniqueId = Snapshot.ItemId;
		Item->RolledAffixes = Snapshot.RolledAffixes;
		Item->FinalAggregatedModifiers = Snapshot.FinalAggregatedModifiers;
		Item->GrantedEffects = Snapshot.GrantedEffects;
		Item->GrantedAbilities = Snapshot.GrantedAbilities;
		Item->EquippedSlot = Snapshot.EquippedSlot;
		Item->EquippedSlotIndex = Snapshot.SlotIndex;
		Item->InventorySize = Snapshot.InventorySize;
		Item->ForceItemChangedForUI();
	}

	for (int32 Index = Items.Num() - 1; Index >= 0; --Index)
	{
		UAeyerjiItemInstance* Item = Items[Index];
		if (!Item || !SnapshotIds.Contains(Item->UniqueId))
		{
			Items.RemoveAt(Index);
		}
	}
}

void UAeyerjiInventoryComponent::ClearPlacement(const FGuid& ItemId)
{
	if (!ItemId.IsValid())
	{
		return;
	}

	const int32 Index = GridPlacements.IndexOfByPredicate(
		[&ItemId](const FInventoryItemGridData& Entry)
		{
			return Entry.ItemId == ItemId;
		});

	if (Index != INDEX_NONE)
	{
		GridPlacements.RemoveAt(Index);
		MARK_PROPERTY_DIRTY_FROM_NAME(UAeyerjiInventoryComponent, GridPlacements, this);
		OnInventoryChanged.Broadcast();
	}
}

int32 UAeyerjiInventoryComponent::SanitizeSlotIndex(int32 SlotIndex) const
{
	if (SlotIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	const int32 MaxSlots = FMath::Max(1, SlotsPerEquipmentCategory);
	return FMath::Clamp(SlotIndex, 0, MaxSlots - 1);
}

int32 UAeyerjiInventoryComponent::FindFirstFreeSlotIndex(EEquipmentSlot Slot, const UAeyerjiItemInstance* IgnoredItem) const
{
	const int32 MaxSlots = FMath::Max(1, SlotsPerEquipmentCategory);
	TSet<int32> UsedIndices;

	for (const FEquippedItemEntry& Entry : EquippedItems)
	{
		if (Entry.Slot != Slot)
		{
			continue;
		}

		const bool bHasRealItem = Entry.Item != nullptr || Entry.ItemId.IsValid();
		if (!bHasRealItem)
		{
			continue;
		}

		if (IgnoredItem && (Entry.Item == IgnoredItem || Entry.ItemId == IgnoredItem->UniqueId))
		{
			continue;
		}

		UsedIndices.Add(Entry.SlotIndex);
	}

	for (int32 Index = 0; Index < MaxSlots; ++Index)
	{
		if (!UsedIndices.Contains(Index))
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

FEquippedItemEntry* UAeyerjiInventoryComponent::FindEquippedEntry(EEquipmentSlot Slot, int32 SlotIndex)
{
	return EquippedItems.FindByPredicate([Slot, SlotIndex](const FEquippedItemEntry& Entry)
	{
		return Entry.Slot == Slot && Entry.SlotIndex == SlotIndex;
	});
}

const FEquippedItemEntry* UAeyerjiInventoryComponent::FindEquippedEntry(EEquipmentSlot Slot, int32 SlotIndex) const
{
	return EquippedItems.FindByPredicate([Slot, SlotIndex](const FEquippedItemEntry& Entry)
	{
		return Entry.Slot == Slot && Entry.SlotIndex == SlotIndex;
	});
}

bool UAeyerjiInventoryComponent::TryPlaceItemAt(UAeyerjiItemInstance* Item, const FIntPoint& TopLeft)
{
	if (GetOwnerRole() != ROLE_Authority || !Item || !Item->UniqueId.IsValid())
	{
		return false;
	}

	if (GridColumns <= 0 || GridRows <= 0)
	{
		return false;
	}

	const FIntPoint Size(
		FMath::Max(1, Item->InventorySize.X),
		FMath::Max(1, Item->InventorySize.Y));

	FInventoryItemGridData Candidate;
	Candidate.ItemId = Item->UniqueId;
	Candidate.TopLeft = TopLeft;
	Candidate.Size = Size;
	Candidate.ItemInstance = Item;

	if (!CanPlaceAt(Candidate))
	{
		return false;
	}

	Item->SetNetAddressable();
	ClearPlacement(Item->UniqueId);
	GridPlacements.Add(Candidate);
	MARK_PROPERTY_DIRTY_FROM_NAME(UAeyerjiInventoryComponent, GridPlacements, this);
	OnInventoryChanged.Broadcast();
	return true;
}

bool UAeyerjiInventoryComponent::UnequipSlotInternal(EEquipmentSlot Slot, int32 SlotIndex, const FIntPoint* PreferredTopLeft)
{
	SlotIndex = SanitizeSlotIndex(SlotIndex);
	const int32 EntryIndex = EquippedItems.IndexOfByPredicate(
		[Slot, SlotIndex](const FEquippedItemEntry& Entry)
		{
			return Entry.Slot == Slot && Entry.SlotIndex == SlotIndex;
		});

	if (EntryIndex == INDEX_NONE)
	{
		return false;
	}

	FEquippedItemEntry& Entry = EquippedItems[EntryIndex];
	UAeyerjiItemInstance* EquippedItem = Entry.Item;
	if (!EquippedItem)
	{
		EquippedItems.RemoveAt(EntryIndex);
		MARK_PROPERTY_DIRTY_FROM_NAME(UAeyerjiInventoryComponent, EquippedItems, this);
		OnEquippedItemChanged.Broadcast(Slot, SlotIndex, nullptr);
		return false;
	}

	bool bPlaced = false;
	if (PreferredTopLeft)
	{
		bPlaced = TryPlaceItemAt(EquippedItem, *PreferredTopLeft);
	}

	if (!bPlaced)
	{
		bPlaced = AutoPlaceItem(EquippedItem);
	}

	if (!bPlaced)
	{
		AJ_LOG(this, TEXT("UnequipSlotInternal failed to place %s back into bag"), *EquippedItem->UniqueId.ToString());
		return false;
	}

	EquippedItem->EquippedSlot = EquippedItem->Definition
		? ResolveEquipmentSlot(EquippedItem->Definition->DefaultSlot, EquippedItem->Definition.Get())
		: ResolveEquipmentSlot(Slot, nullptr);
	EquippedItem->EquippedSlotIndex = INDEX_NONE;

	RemoveItemGameplayEffect(EquippedItem->UniqueId);
	BroadcastItemStateChange(EInventoryItemStateChange::Unequipped, EquippedItem, Slot, SlotIndex);

	EquippedItems.RemoveAt(EntryIndex);
	MARK_PROPERTY_DIRTY_FROM_NAME(UAeyerjiInventoryComponent, EquippedItems, this);
	OnEquippedItemChanged.Broadcast(Slot, SlotIndex, nullptr);

	RebuildItemSnapshots();
	return true;
}
