// InventoryComponent.cpp

#include "Items/InventoryComponent.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemInterface.h"
#include "Aeyerji/AeyerjiPlayerState.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Attributes/AeyerjiStatEngineComponent.h"
#include "CharacterStatsLibrary.h"
#include "GAS/GE_ItemStats.h"
#include "Inventory/AeyerjiInventoryBPFL.h"
#include "Inventory/AeyerjiLootPickup.h"
#include "Items/ItemDefinition.h"
#include "Items/ItemInstance.h"
#include "Logging/AeyerjiLog.h"
#include "Systems/AeyerjiDifficultyTuning.h"
#include "Systems/AeyerjiSaveManagerSubsystem.h"
#include "Systems/LootService.h"
#include "Systems/LootTable.h"
#include "CollisionQueryParams.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
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
	constexpr int32 NormalSlotUnlockInterval = 10;
	constexpr int32 MinimumNormalSlotCount = 5;
	constexpr int32 CorruptionUnlockLevel = 50;
	constexpr int32 CorruptionSlotCount = 3;

	static_assert(static_cast<int32>(EEquipmentSlot::Assault) == static_cast<int32>(EItemCategory::Assault)
		&& static_cast<int32>(EEquipmentSlot::Guard) == static_cast<int32>(EItemCategory::Guard)
		&& static_cast<int32>(EEquipmentSlot::Flow) == static_cast<int32>(EItemCategory::Flow)
		&& static_cast<int32>(EEquipmentSlot::Corruption) == static_cast<int32>(EItemCategory::Corruption),
		"Equipment slot and item category enums must stay aligned.");

	bool IsValidEquipmentSlot(EEquipmentSlot Slot)
	{
		if (const UEnum* SlotEnum = StaticEnum<EEquipmentSlot>())
		{
			return SlotEnum->IsValidEnumValue(static_cast<int64>(Slot));
		}
		return false;
	}

	bool IsNormalEquipmentSlot(EEquipmentSlot Slot)
	{
		return Slot == EEquipmentSlot::Assault
			|| Slot == EEquipmentSlot::Guard
			|| Slot == EEquipmentSlot::Flow;
	}

	bool IsNormalItemCategory(EItemCategory Category)
	{
		return Category == EItemCategory::Assault
			|| Category == EItemCategory::Guard
			|| Category == EItemCategory::Flow;
	}

	FString SafeNameToString(const FName& Name)
	{
		return Name.IsValid() ? Name.ToString() : FString(TEXT("InvalidName"));
	}

	bool IsUsableItemStatAttribute(const FGameplayAttribute& Attribute)
	{
		const FProperty* Property = Attribute.GetUProperty();
		if (!Property || !FGameplayAttribute::IsGameplayAttributeDataProperty(Property))
		{
			return false;
		}

		const UClass* AttributeSetClass = Cast<UClass>(Property->GetOwner<UObject>());
		return AttributeSetClass && AttributeSetClass->IsChildOf(UAeyerjiAttributeSet::StaticClass());
	}

	int32 SanitizeItemStatModifiers(TArray<FItemStatModifier>& Modifiers)
	{
		const int32 OriginalCount = Modifiers.Num();
		Modifiers.RemoveAll(
			[](const FItemStatModifier& Modifier)
			{
				return !IsUsableItemStatAttribute(Modifier.Attribute);
			});
		return OriginalCount - Modifiers.Num();
	}

	int32 SanitizeRolledAffixes(TArray<FRolledAffix>& RolledAffixes)
	{
		int32 RemovedCount = 0;
		for (FRolledAffix& RolledAffix : RolledAffixes)
		{
			RemovedCount += SanitizeItemStatModifiers(RolledAffix.FinalModifiers);
		}
		return RemovedCount;
	}

	int32 SanitizeInventorySnapshotAttributes(FInventoryItemSnapshot& Snapshot)
	{
		int32 RemovedCount = SanitizeRolledAffixes(Snapshot.RolledAffixes);
		RemovedCount += SanitizeItemStatModifiers(Snapshot.FinalAggregatedModifiers);
		return RemovedCount;
	}

	int32 SanitizeItemInstanceAttributes(UAeyerjiItemInstance& Item)
	{
		int32 RemovedCount = SanitizeRolledAffixes(Item.RolledAffixes);
		RemovedCount += SanitizeItemStatModifiers(Item.FinalAggregatedModifiers);
		return RemovedCount;
	}

	bool IsPercentPointItemAttribute(const FGameplayAttribute& Attribute)
	{
		return Attribute == UAeyerjiAttributeSet::GetCritChanceAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetAttackDamageVarianceAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetDodgeChanceAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetPhysicalDamageBonusAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetArmorPenetrationAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetLifeStealAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetStaggerResistanceAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetCooldownReductionAttribute();
	}

	bool IsEnemyOnlyPlayerItemAttribute(const FGameplayAttribute& Attribute)
	{
		return Attribute == UAeyerjiAttributeSet::GetVisionRangeAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetProjectilePredictionAmountAttribute();
	}

	float NormalizeEquipmentModifierMagnitude(const FItemStatModifier& Modifier)
	{
		return IsPercentPointItemAttribute(Modifier.Attribute)
			? Modifier.Magnitude / 100.f
			: Modifier.Magnitude;
	}

	float ClampEquipmentAttributeValue(const FGameplayAttribute& Attribute, float Value, const UAbilitySystemComponent* ASC)
	{
		if (Attribute == UAeyerjiAttributeSet::GetHPAttribute())
		{
			const float HPMax = ASC ? ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetHPMaxAttribute()) : 0.f;
			return FMath::Clamp(Value, 0.f, HPMax);
		}
		if (Attribute == UAeyerjiAttributeSet::GetManaAttribute())
		{
			const float ManaMax = ASC ? ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetManaMaxAttribute()) : 0.f;
			return FMath::Clamp(Value, 0.f, ManaMax);
		}
		if (Attribute == UAeyerjiAttributeSet::GetHPMaxAttribute())
		{
			return FMath::Max(Value, 1.f);
		}
		if (Attribute == UAeyerjiAttributeSet::GetManaMaxAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetStrengthAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetAgilityAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetIntellectAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetPoisonAmountAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetPoisonDurationAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetTraumaAmountAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetTraumaDurationAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetCorruptionAmountAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetCorruptionDurationAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetVisionRangeAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetHearingRangeAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetSpellPowerAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetMagicAmpAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetManaRegenAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetHPRegenAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetStaggerPowerAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetPoiseAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetPoiseMaxAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetRunSpeedAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetWalkSpeedAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetProjectileSpeedRangedAttribute())
		{
			return FMath::Max(Value, 0.f);
		}
		if (Attribute == UAeyerjiAttributeSet::GetCriticalDamageMultiplierAttribute())
		{
			return FMath::Max(Value, 1.f);
		}
		if (Attribute == UAeyerjiAttributeSet::GetCritChanceAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetDodgeChanceAttribute())
		{
			return FMath::Clamp(Value, 0.f, 1.f);
		}
		if (Attribute == UAeyerjiAttributeSet::GetPhysicalDamageBonusAttribute())
		{
			return FMath::Max(-0.90f, Value);
		}
		if (Attribute == UAeyerjiAttributeSet::GetArmorPenetrationAttribute())
		{
			return FMath::Clamp(Value, 0.f, 0.75f);
		}
		if (Attribute == UAeyerjiAttributeSet::GetLifeStealAttribute())
		{
			return FMath::Clamp(Value, 0.f, 0.25f);
		}
		if (Attribute == UAeyerjiAttributeSet::GetStaggerResistanceAttribute())
		{
			return FMath::Clamp(Value, 0.f, 0.90f);
		}
		if (Attribute == UAeyerjiAttributeSet::GetAttackDamageVarianceAttribute())
		{
			return FMath::Clamp(Value, 0.f, 0.95f);
		}
		if (Attribute == UAeyerjiAttributeSet::GetCooldownReductionAttribute())
		{
			return FMath::Clamp(Value, 0.f, 0.40f);
		}
		if (Attribute == UAeyerjiAttributeSet::GetAttackSpeedAttribute())
		{
			return FMath::Clamp(Value, 0.01f, 1000.f);
		}

		return Value;
	}

	int32 GetEquipmentAttributeApplyPriority(const FGameplayAttribute& Attribute)
	{
		if (Attribute == UAeyerjiAttributeSet::GetStrengthAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetAgilityAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetIntellectAttribute())
		{
			return 0;
		}

		if (Attribute == UAeyerjiAttributeSet::GetHPMaxAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetManaMaxAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetArmorAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetAttackSpeedAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetSpellPowerAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetManaRegenAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetHPRegenAttribute()
			|| Attribute == UAeyerjiAttributeSet::GetDodgeChanceAttribute())
		{
			return 2;
		}

		return 1;
	}

	bool IsSlotCompatibleWithDefinition(EEquipmentSlot Slot, const UItemDefinition* Definition)
	{
		if (!Definition)
		{
			return IsNormalEquipmentSlot(Slot);
		}

		if (Definition->IsCorruptionItem())
		{
			return Slot == EEquipmentSlot::Corruption;
		}

		return IsNormalItemCategory(Definition->ItemCategory)
			&& IsNormalEquipmentSlot(Slot)
			&& Slot == static_cast<EEquipmentSlot>(Definition->ItemCategory);
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

			const EEquipmentSlot CategorySlot = static_cast<EEquipmentSlot>(Definition->ItemCategory);
			if (IsValidEquipmentSlot(CategorySlot) && IsSlotCompatibleWithDefinition(CategorySlot, Definition))
			{
				return CategorySlot;
			}
		}

		return EEquipmentSlot::Assault;
	}

	UItemDefinition* ResolveSnapshotDefinition(const FInventoryItemSnapshot& Snapshot, UObject* WorldContextObject)
	{
		const bool bHasUsableDefinitionKey = Snapshot.DefinitionKey.IsValid() && !Snapshot.DefinitionKey.IsNone();
		if (bHasUsableDefinitionKey)
		{
			if (UItemDefinition* Resolved = UCharacterStatsLibrary::ResolveItemDefinitionByKey(WorldContextObject, Snapshot.DefinitionKey))
			{
				UE_LOG(LogTemp, Display, TEXT("[InventorySave] Resolved snapshot ItemId=%s DefinitionKey=%s -> %s"),
					Snapshot.ItemId.IsValid() ? *Snapshot.ItemId.ToString() : TEXT("Invalid"),
					*Snapshot.DefinitionKey.ToString(),
					*GetNameSafe(Resolved));
				return Resolved;
			}

			UE_LOG(LogTemp, Warning, TEXT("[InventorySave] Could not resolve snapshot ItemId=%s DefinitionKey=%s; trying legacy Definition=%s"),
				Snapshot.ItemId.IsValid() ? *Snapshot.ItemId.ToString() : TEXT("Invalid"),
				*Snapshot.DefinitionKey.ToString(),
				*GetNameSafe(Snapshot.Definition));
		}
		else if (!Snapshot.DefinitionKey.IsNone())
		{
			UE_LOG(LogTemp, Warning, TEXT("[InventorySave] Snapshot ItemId=%s has an invalid saved DefinitionKey; trying legacy Definition=%s"),
				Snapshot.ItemId.IsValid() ? *Snapshot.ItemId.ToString() : TEXT("Invalid"),
				*GetNameSafe(Snapshot.Definition));
		}

		if (IsValid(Snapshot.Definition.Get()))
		{
			UE_LOG(LogTemp, Display, TEXT("[InventorySave] Using legacy snapshot Definition for ItemId=%s Definition=%s DerivedKey=%s"),
				Snapshot.ItemId.IsValid() ? *Snapshot.ItemId.ToString() : TEXT("Invalid"),
				*GetNameSafe(Snapshot.Definition),
				*Snapshot.Definition->GetDefinitionKey().ToString());
			return Snapshot.Definition.Get();
		}

		return nullptr;
	}

	const UAeyerjiLootTable* ResolveLootTableForInventory(const UAeyerjiInventoryComponent* Inventory)
	{
		const UWorld* World = Inventory ? Inventory->GetWorld() : nullptr;
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		const ULootService* LootService = GameInstance ? GameInstance->GetSubsystem<ULootService>() : nullptr;
		return LootService ? LootService->GetLootTable() : nullptr;
	}

	bool ShouldRebuildEmptySnapshotAggregation(const UAeyerjiItemInstance* Item)
	{
		if (!Item || !Item->Definition || Item->FinalAggregatedModifiers.Num() > 0)
		{
			return false;
		}

		return Item->Definition->BaseModifiers.Num() > 0
			|| Item->RolledAffixes.Num() > 0
			|| Item->Definition->GrantedEffects.Num() > 0
			|| Item->Definition->GrantedAbilities.Num() > 0;
	}

	FVector FindGroundedDropLocation(UWorld& World, const FVector& DesiredLocation, const AActor* ActorToIgnore)
	{
		const float TraceUp = 200.f;
		const float TraceDown = 2000.f;

		const FVector Start = DesiredLocation + FVector(0.f, 0.f, TraceUp);
		const FVector End = DesiredLocation - FVector(0.f, 0.f, TraceDown);

		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(DropItemGroundTrace), false);
		if (ActorToIgnore)
		{
			Params.AddIgnoredActor(ActorToIgnore);
		}

		FCollisionObjectQueryParams ObjectParams;
		ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);

		if (World.LineTraceSingleByObjectType(Hit, Start, End, ObjectParams, Params) && Hit.bBlockingHit)
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
				return UAeyerjiDifficultySettings::ClampGameplayLevel(FMath::RoundToInt(Attr->GetLevel()));
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

	TryBindOwnerLevelChange();
	BroadcastEquipmentSlotUnlocksIfChanged(true);

	if (GetOwnerRole() == ROLE_Authority)
	{
		for (UAeyerjiItemInstance* Item : Items)
		{
			BindItemInstanceDelegates(Item);
		}
		RebuildItemSnapshots();
		RebuildEquipmentStatContributions();
	}
}

void UAeyerjiInventoryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (GetOwnerRole() == ROLE_Authority)
	{
		if (UWorld* World = GetWorld())
		{
			if (World->GetTimerManager().IsTimerActive(InventoryAutosaveTimerHandle))
			{
				World->GetTimerManager().ClearTimer(InventoryAutosaveTimerHandle);
				HandleInventoryAutosave();
			}
		}
	}

	UnbindOwnerLevelChange();
	ClearEquipmentStatContributions();

	Super::EndPlay(EndPlayReason);
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
		if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
		{
			return ASC;
		}
	}

	if (const APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		if (APlayerState* PS = Pawn->GetPlayerState())
		{
			if (const IAbilitySystemInterface* PSASI = Cast<IAbilitySystemInterface>(PS))
			{
				return PSASI->GetAbilitySystemComponent();
			}
		}
	}

	if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(GetOwner(), true))
	{
		return ASC;
	}

	return nullptr;
}

bool UAeyerjiInventoryComponent::AddItemInstance(UAeyerjiItemInstance* Item, bool bSkipAutoPlacement)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		UE_LOG(LogTemp, Warning, TEXT("[InventoryPickup] AddItemInstance rejected: owner %s is not authority (Role=%d)"),
			*GetNameSafe(GetOwner()),
			static_cast<int32>(GetOwnerRole()));
		return false;
	}

	if (!Item)
	{
		UE_LOG(LogTemp, Warning, TEXT("[InventoryPickup] AddItemInstance rejected: null item on inventory %s"), *GetNameSafe(this));
		return false;
	}

	UE_LOG(LogTemp, Display, TEXT("[InventoryPickup] AddItemInstance Item=%s Def=%s Outer=%s UniqueId=%s SkipAutoPlace=%d Items=%d Equipped=%d Grid=%d"),
		*GetNameSafe(Item),
		*GetNameSafe(Item->Definition.Get()),
		*GetNameSafe(Item->GetOuter()),
		Item->UniqueId.IsValid() ? *Item->UniqueId.ToString() : TEXT("Invalid"),
		bSkipAutoPlacement ? 1 : 0,
		Items.Num(),
		EquippedItems.Num(),
		GridPlacements.Num());

	const bool bAlreadyOwned = Items.Contains(Item);
	UObject* PreviousOuter = nullptr;

	if (!Item->UniqueId.IsValid())
	{
		Item->UniqueId = FGuid::NewGuid();
	}

	const int32 ClampedItemLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(Item->ItemLevel);
	if (Item->ItemLevel != ClampedItemLevel)
	{
		UE_LOG(LogTemp, Display, TEXT("[ItemLevelClamp] AddItemInstance clamped %s ItemLevel %d -> %d"),
			*GetNameSafe(Item),
			Item->ItemLevel,
			ClampedItemLevel);
		Item->ItemLevel = ClampedItemLevel;
	}

	if (!bAlreadyOwned)
	{
		if (Item->GetOuter() != this)
		{
			UE_LOG(LogTemp, Display, TEXT("[InventoryPickup] Renaming item %s from %s to inventory %s"),
				*GetNameSafe(Item),
				*GetNameSafe(Item->GetOuter()),
				*GetNameSafe(this));
			PreviousOuter = Item->GetOuter();
			Item->Rename(nullptr, this);
		}
		Item->SetNetAddressable();
		Items.Add(Item);
		BindItemInstanceDelegates(Item);
		UE_LOG(LogTemp, Display, TEXT("[InventoryPickup] Added item %s Outer=%s UniqueId=%s Items=%d"),
			*GetNameSafe(Item),
			*GetNameSafe(Item->GetOuter()),
			Item->UniqueId.IsValid() ? *Item->UniqueId.ToString() : TEXT("Invalid"),
			Items.Num());
		OnInventoryChanged.Broadcast();
		BroadcastItemStateChange(EInventoryItemStateChange::Added, Item, Item->EquippedSlot, Item->EquippedSlotIndex);
		RebuildItemSnapshots();
		ScheduleInventoryAutosave(TEXT("Add"));
	}

	if (bSkipAutoPlacement)
	{
		SyncProfileInventoryCache(TEXT("Add"));
		return true;
	}

	if (TryAutoPlaceItem(Item))
	{
		UE_LOG(LogTemp, Display, TEXT("[InventoryPickup] AddItemInstance placed item %s in bag grid."), *Item->UniqueId.ToString());
		SyncProfileInventoryCache(TEXT("AddPlaced"));
		ScheduleInventoryAutosave(TEXT("AddPlaced"));
		return true;
	}

	UE_LOG(LogTemp, Warning, TEXT("[InventoryPickup] AddItemInstance rejected: no bag placement for item %s Size=(%d,%d) Grid=(%d,%d)"),
		*Item->UniqueId.ToString(),
		Item->InventorySize.X,
		Item->InventorySize.Y,
		GridColumns,
		GridRows);

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

int32 UAeyerjiInventoryComponent::GetOwnerLevelForInventoryRules() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (AutomationOwnerLevelOverride > 0)
	{
		return AutomationOwnerLevelOverride;
	}
#endif
	return GetInventoryOwnerLevel(this);
}

int32 UAeyerjiInventoryComponent::GetNormalEquipmentSlotCountForLevel(int32 PlayerLevel, int32 MaxNormalSlots)
{
	const int32 ClampedLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(PlayerLevel);
	const int32 ClampedMaxSlots = FMath::Max(MinimumNormalSlotCount, MaxNormalSlots);
	const int32 UnlocksAfterLevelOne = FMath::Clamp(ClampedLevel / NormalSlotUnlockInterval, 0, ClampedMaxSlots - 1);
	return 1 + UnlocksAfterLevelOne;
}

#if WITH_DEV_AUTOMATION_TESTS
void UAeyerjiInventoryComponent::SetAutomationOwnerLevelForInventoryRules(int32 InLevel)
{
	AutomationOwnerLevelOverride = InLevel > 0 ? UAeyerjiDifficultySettings::ClampGameplayLevel(InLevel) : 0;
	BroadcastEquipmentSlotUnlocksIfChanged(true);
}
#endif

int32 UAeyerjiInventoryComponent::GetVisibleEquipmentSlotCount(EEquipmentSlot Slot) const
{
	if (!IsValidEquipmentSlot(Slot))
	{
		return 0;
	}

	if (Slot == EEquipmentSlot::Corruption)
	{
		return GetUnlockedEquipmentSlotCount(Slot);
	}

	return IsNormalEquipmentSlot(Slot) ? FMath::Max(MinimumNormalSlotCount, SlotsPerEquipmentCategory) : 0;
}

int32 UAeyerjiInventoryComponent::GetUnlockedEquipmentSlotCount(EEquipmentSlot Slot) const
{
	if (!IsValidEquipmentSlot(Slot))
	{
		return 0;
	}

	if (Slot == EEquipmentSlot::Corruption)
	{
		return GetOwnerLevelForInventoryRules() >= CorruptionUnlockLevel ? CorruptionSlotCount : 0;
	}

	return IsNormalEquipmentSlot(Slot)
		? GetNormalEquipmentSlotCountForLevel(GetOwnerLevelForInventoryRules(), FMath::Max(MinimumNormalSlotCount, SlotsPerEquipmentCategory))
		: 0;
}

bool UAeyerjiInventoryComponent::IsEquipmentSlotUnlocked(EEquipmentSlot Slot, int32 SlotIndex) const
{
	if (SlotIndex == INDEX_NONE)
	{
		return GetUnlockedEquipmentSlotCount(Slot) > 0;
	}

	return SlotIndex >= 0 && SlotIndex < GetUnlockedEquipmentSlotCount(Slot);
}

bool UAeyerjiInventoryComponent::CanEquipItemInSlot(const UAeyerjiItemInstance* Item, EEquipmentSlot Slot, int32 SlotIndex) const
{
	if (!Item || !Item->Definition)
	{
		return false;
	}

	if (!IsSlotCompatibleWithDefinition(Slot, Item->Definition.Get()))
	{
		return false;
	}

	if (!IsEquipmentSlotUnlocked(Slot, SlotIndex))
	{
		return false;
	}

	const int32 OwnerLevel = GetOwnerLevelForInventoryRules();
	const int32 ItemLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(Item->ItemLevel);
	const int32 RequiredLevel = Item->Definition->GetEffectiveRequiredLevel();
	return OwnerLevel >= FMath::Max(ItemLevel, RequiredLevel);
}

void UAeyerjiInventoryComponent::RebuildEquipmentStatContributions()
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	UAbilitySystemComponent* ASC = GetASC();
	if (!ASC)
	{
		AJ_LOG(this, TEXT("[ItemStatsRebuild] skipped: ASC missing Owner=%s"), *GetNameSafe(GetOwner()));
		return;
	}

	TMap<FGameplayAttribute, float> RawTotals;
	for (const FEquippedItemEntry& Entry : EquippedItems)
	{
		UAeyerjiItemInstance* Item = Entry.Item ? Entry.Item.Get() : FindItemById(Entry.ItemId);
		if (!Item)
		{
			continue;
		}

		for (const FItemStatModifier& Modifier : Item->GetFinalAggregatedModifiers())
		{
			if (!IsUsableItemStatAttribute(Modifier.Attribute))
			{
				continue;
			}

			if (Modifier.Op != EItemModOp::Additive)
			{
				UE_LOG(LogAeyerji, Warning,
					TEXT("[ItemStatsRebuild] Ignoring non-additive item stat Item=%s Def=%s Attr=%s Op=%d Mag=%.3f"),
					*Item->UniqueId.ToString(),
					*GetNameSafe(Item->Definition.Get()),
					*Modifier.Attribute.GetName(),
					static_cast<int32>(Modifier.Op),
					Modifier.Magnitude);
				continue;
			}

			if (IsEnemyOnlyPlayerItemAttribute(Modifier.Attribute))
			{
				UE_LOG(LogAeyerji, Warning,
					TEXT("[ItemStatsRebuild] Ignoring enemy-only/player-deferred item stat Item=%s Def=%s Attr=%s Mag=%.3f"),
					*Item->UniqueId.ToString(),
					*GetNameSafe(Item->Definition.Get()),
					*Modifier.Attribute.GetName(),
					Modifier.Magnitude);
				continue;
			}

			if (!ASC->HasAttributeSetForAttribute(Modifier.Attribute))
			{
				UE_LOG(LogAeyerji, Warning,
					TEXT("[ItemStatsRebuild] Ignoring stat without attribute set Item=%s Def=%s Attr=%s Mag=%.3f"),
					*Item->UniqueId.ToString(),
					*GetNameSafe(Item->Definition.Get()),
					*Modifier.Attribute.GetName(),
					Modifier.Magnitude);
				continue;
			}

			const float NormalizedMagnitude = NormalizeEquipmentModifierMagnitude(Modifier);
			if (FMath::IsNearlyZero(NormalizedMagnitude))
			{
				continue;
			}

			RawTotals.FindOrAdd(Modifier.Attribute) += NormalizedMagnitude;
		}
	}

	TArray<FGameplayAttribute> AttributesToApply;
	AttributesToApply.Reserve(RawTotals.Num() + AppliedEquipmentStatContributions.Num());
	for (const TPair<FGameplayAttribute, float>& Pair : RawTotals)
	{
		AttributesToApply.AddUnique(Pair.Key);
	}
	for (const TPair<FGameplayAttribute, float>& Pair : AppliedEquipmentStatContributions)
	{
		if (IsUsableItemStatAttribute(Pair.Key))
		{
			AttributesToApply.AddUnique(Pair.Key);
		}
	}
	AttributesToApply.Sort(
		[](const FGameplayAttribute& Left, const FGameplayAttribute& Right)
		{
			const int32 LeftPriority = GetEquipmentAttributeApplyPriority(Left);
			const int32 RightPriority = GetEquipmentAttributeApplyPriority(Right);
			if (LeftPriority != RightPriority)
			{
				return LeftPriority < RightPriority;
			}
			return Left.GetName().Compare(Right.GetName()) < 0;
		});

	TMap<FGameplayAttribute, float> NewAppliedContributions;
	for (const FGameplayAttribute& Attribute : AttributesToApply)
	{
		if (!IsUsableItemStatAttribute(Attribute) || !ASC->HasAttributeSetForAttribute(Attribute))
		{
			continue;
		}

		const float PreviousApplied = AppliedEquipmentStatContributions.FindRef(Attribute);
		const float CurrentValue = ASC->GetNumericAttribute(Attribute);
		const float BaselineEstimate = CurrentValue - PreviousApplied;
		const float RawDesiredContribution = RawTotals.FindRef(Attribute);
		const float DesiredValue = ClampEquipmentAttributeValue(Attribute, BaselineEstimate + RawDesiredContribution, ASC);
		const float EffectiveContribution = DesiredValue - BaselineEstimate;
		const float Delta = EffectiveContribution - PreviousApplied;

		if (!FMath::IsNearlyZero(Delta))
		{
			ASC->ApplyModToAttributeUnsafe(Attribute, EGameplayModOp::Additive, Delta);
		}

		const float PostValue = ASC->GetNumericAttribute(Attribute);
		const float ActualContribution = PostValue - BaselineEstimate;
		if (!FMath::IsNearlyZero(ActualContribution))
		{
			NewAppliedContributions.Add(Attribute, ActualContribution);
		}

		AJ_LOG(this, TEXT("[ItemStatsRebuild] Attr=%s Raw=%.3f PrevApplied=%.3f Delta=%.3f Post=%.3f ActualApplied=%.3f"),
			*Attribute.GetName(),
			RawDesiredContribution,
			PreviousApplied,
			Delta,
			PostValue,
			ActualContribution);
	}

	AppliedEquipmentStatContributions = MoveTemp(NewAppliedContributions);

	if (UAeyerjiStatEngineComponent* StatEngine = GetOwner() ? GetOwner()->FindComponentByClass<UAeyerjiStatEngineComponent>() : nullptr)
	{
		StatEngine->EnsureRegenerationActive();
	}
}

float UAeyerjiInventoryComponent::GetCurrentEquipmentStatContribution(FGameplayAttribute Attribute) const
{
	return AppliedEquipmentStatContributions.FindRef(Attribute);
}

void UAeyerjiInventoryComponent::ClearEquipmentStatContributions()
{
	if (AppliedEquipmentStatContributions.Num() == 0)
	{
		return;
	}

	UAbilitySystemComponent* ASC = GetASC();
	if (!ASC || GetOwnerRole() != ROLE_Authority)
	{
		AppliedEquipmentStatContributions.Reset();
		return;
	}

	for (const TPair<FGameplayAttribute, float>& Pair : AppliedEquipmentStatContributions)
	{
		if (IsUsableItemStatAttribute(Pair.Key) && ASC->HasAttributeSetForAttribute(Pair.Key) && !FMath::IsNearlyZero(Pair.Value))
		{
			ASC->ApplyModToAttributeUnsafe(Pair.Key, EGameplayModOp::Additive, -Pair.Value);
			AJ_LOG(this, TEXT("[ItemStatsRebuild] Clear Attr=%s Applied=%.3f"), *Pair.Key.GetName(), Pair.Value);
		}
	}

	AppliedEquipmentStatContributions.Reset();
}

void UAeyerjiInventoryComponent::RefreshEquipmentSlotUnlockState()
{
	TryBindOwnerLevelChange();
	BroadcastEquipmentSlotUnlocksIfChanged(true);
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
		const bool bMatchesItem = Entry.Item && Entry.Item->UniqueId == ItemId;
		const bool bMatchesSavedId = Entry.ItemId.IsValid() && Entry.ItemId == ItemId;
		if (bMatchesItem || bMatchesSavedId)
		{
			const EEquipmentSlot Slot = Entry.Slot;
			const int32 SlotIndex = Entry.SlotIndex;
			UAeyerjiItemInstance* UnequippedItem = Entry.Item;
			if (UnequippedItem)
			{
				UnequippedItem->EquippedSlot = UnequippedItem->Definition
					? ResolveEquipmentSlot(UnequippedItem->Definition->DefaultSlot, UnequippedItem->Definition.Get())
					: ResolveEquipmentSlot(UnequippedItem->EquippedSlot, nullptr);
				UnequippedItem->EquippedSlotIndex = INDEX_NONE;
			}

			RemoveItemGameplayEffect(ItemId);
			EquippedItems.RemoveAt(EquippedIndex);
			MARK_PROPERTY_DIRTY_FROM_NAME(UAeyerjiInventoryComponent, EquippedItems, this);
			OnEquippedItemChanged.Broadcast(Slot, SlotIndex, nullptr);
			if (UnequippedItem)
			{
				BroadcastItemStateChange(EInventoryItemStateChange::Unequipped, UnequippedItem, Slot, SlotIndex);
			}
			break;
		}
	}

	RebuildEquipmentStatContributions();
	RebuildItemSnapshots();
	SyncProfileInventoryCache(TEXT("Remove"));
	ScheduleInventoryAutosave(TEXT("Remove"));
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
	const EEquipmentSlot ResolvedSlot = IsValidEquipmentSlot(Slot)
		? Slot
		: ResolveEquipmentSlot(Slot, ItemDefinition);
	const bool bSanitizedSlot = ResolvedSlot != Slot;

	AJ_LOG(this, TEXT("[InventoryPickup] Server_EquipItem ItemId=%s Slot=%d Index=%d%s OwnerLevel=%d"),
		ItemId.IsValid() ? *ItemId.ToString() : TEXT("Invalid"),
		static_cast<int32>(ResolvedSlot),
		SlotIndex,
		bSanitizedSlot ? TEXT(" (sanitized)") : TEXT(""),
		GetOwnerLevelForInventoryRules());

	if (!Item || !Item->Definition)
	{
		AJ_LOG(this, TEXT("[InventoryPickup] Server_EquipItem aborted: missing item or definition (Item=%s Definition=%s)"),
			*GetNameSafe(Item),
			Item ? *GetNameSafe(Item->Definition.Get()) : TEXT("NULL"));
		return;
	}

	const int32 OwnerLevel = GetOwnerLevelForInventoryRules();
	const int32 ItemLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(Item->ItemLevel);
	const int32 RequiredLevel = Item->Definition->GetEffectiveRequiredLevel();
	if (FMath::Max(ItemLevel, RequiredLevel) > OwnerLevel)
	{
		AJ_LOG(this, TEXT("[InventoryPickup] Server_EquipItem rejected: item level %d or required level %d exceeds owner level %d (%s)"),
			ItemLevel,
			RequiredLevel,
			OwnerLevel,
			*GetNameSafe(Item));
		return;
	}

	const bool bExplicitIndex = SlotIndex != INDEX_NONE;
	SlotIndex = SanitizeSlotIndex(ResolvedSlot, SlotIndex);
	if (!CanEquipItemInSlot(Item, ResolvedSlot, bExplicitIndex ? SlotIndex : INDEX_NONE))
	{
		AJ_LOG(this, TEXT("[InventoryPickup] Server_EquipItem rejected: incompatible or locked slot Item=%s Slot=%d Index=%d OwnerLevel=%d Category=%d RequiredLevel=%d"),
			*Item->UniqueId.ToString(),
			static_cast<int32>(ResolvedSlot),
			SlotIndex,
			OwnerLevel,
			static_cast<int32>(Item->Definition->ItemCategory),
			RequiredLevel);
		return;
	}

	if (!Items.Contains(Item))
	{
		if (!AddItemInstance(Item, true))
		{
			AJ_LOG(this, TEXT("[InventoryPickup] Server_EquipItem failed: AddItemInstance rejected %s"), *Item->UniqueId.ToString());
			return;
		}
	}

	if (!bExplicitIndex || SlotIndex == INDEX_NONE)
	{
		SlotIndex = FindFirstFreeSlotIndex(ResolvedSlot, Item);
	}

	if (!CanEquipItemInSlot(Item, ResolvedSlot, SlotIndex))
	{
		AJ_LOG(this, TEXT("[InventoryPickup] Server_EquipItem rejected: incompatible or locked slot Item=%s Slot=%d Index=%d OwnerLevel=%d Category=%d RequiredLevel=%d"),
			*Item->UniqueId.ToString(),
			static_cast<int32>(ResolvedSlot),
			SlotIndex,
			OwnerLevel,
			static_cast<int32>(Item->Definition->ItemCategory),
			RequiredLevel);
		return;
	}

	if (SlotIndex == INDEX_NONE)
	{
		AJ_LOG(this, TEXT("[InventoryPickup] Server_EquipItem aborted: no free slot for %s in %d"), *Item->UniqueId.ToString(), static_cast<int32>(ResolvedSlot));
		return;
	}

	FEquippedItemEntry* ExistingEntry = FindEquippedEntry(ResolvedSlot, SlotIndex);
	UAeyerjiItemInstance* CurrentlyEquipped = ExistingEntry ? ExistingEntry->Item : nullptr;
	if (CurrentlyEquipped && CurrentlyEquipped != Item)
	{
		if (!AutoPlaceItem(CurrentlyEquipped))
		{
			AJ_LOG(this, TEXT("[InventoryPickup] Server_EquipItem failed: could not auto-place previous %s"), *CurrentlyEquipped->UniqueId.ToString());
			return;
		}

		CurrentlyEquipped->EquippedSlot = CurrentlyEquipped->Definition
			? ResolveEquipmentSlot(CurrentlyEquipped->Definition->DefaultSlot, CurrentlyEquipped->Definition.Get())
			: ResolvedSlot;
		CurrentlyEquipped->EquippedSlotIndex = INDEX_NONE;

		RemoveItemGameplayEffect(CurrentlyEquipped->UniqueId);
		AJ_LOG(this, TEXT("[InventoryPickup] Server_EquipItem unequipped %s from slot %d index %d"),
			*CurrentlyEquipped->UniqueId.ToString(),
			static_cast<int32>(ResolvedSlot),
			SlotIndex);
		OnEquippedItemChanged.Broadcast(ResolvedSlot, SlotIndex, nullptr);
		BroadcastItemStateChange(EInventoryItemStateChange::Unequipped, CurrentlyEquipped, ResolvedSlot, SlotIndex);
		RebuildItemSnapshots();
	}

	for (int32 ExistingItemIndex = EquippedItems.Num() - 1; ExistingItemIndex >= 0; --ExistingItemIndex)
	{
		const FEquippedItemEntry& Entry = EquippedItems[ExistingItemIndex];
		const bool bSameTargetSlot = Entry.Slot == ResolvedSlot && Entry.SlotIndex == SlotIndex;
		const bool bSameItem = (Entry.Item && Entry.Item == Item) || (Entry.ItemId.IsValid() && Entry.ItemId == Item->UniqueId);
		if (bSameItem && !bSameTargetSlot)
		{
			const EEquipmentSlot PreviousSlot = Entry.Slot;
			const int32 PreviousSlotIndex = Entry.SlotIndex;
			EquippedItems.RemoveAt(ExistingItemIndex);
			MARK_PROPERTY_DIRTY_FROM_NAME(UAeyerjiInventoryComponent, EquippedItems, this);
			OnEquippedItemChanged.Broadcast(PreviousSlot, PreviousSlotIndex, nullptr);
			AJ_LOG(this, TEXT("[InventoryPickup] Server_EquipItem moved %s from slot %d index %d to slot %d index %d"),
				*Item->UniqueId.ToString(),
				static_cast<int32>(PreviousSlot),
				PreviousSlotIndex,
				static_cast<int32>(ResolvedSlot),
				SlotIndex);
		}
	}

	ExistingEntry = FindEquippedEntry(ResolvedSlot, SlotIndex);

	Item->EquippedSlot = ResolvedSlot;
	Item->EquippedSlotIndex = SlotIndex;

	if (ExistingEntry)
	{
		ExistingEntry->Item = Item;
		ExistingEntry->ItemId = Item->UniqueId;
		AJ_LOG(this, TEXT("[InventoryPickup] Server_EquipItem updated slot entry for %s (index %d)"), *Item->UniqueId.ToString(), SlotIndex);
	}
	else
	{
		FEquippedItemEntry NewEntry;
		NewEntry.Slot = ResolvedSlot;
		NewEntry.SlotIndex = SlotIndex;
		NewEntry.ItemId = Item->UniqueId;
		NewEntry.Item = Item;
		EquippedItems.Add(NewEntry);
		AJ_LOG(this, TEXT("[InventoryPickup] Server_EquipItem added new slot entry for %s (index %d)"), *Item->UniqueId.ToString(), SlotIndex);
	}
	MARK_PROPERTY_DIRTY_FROM_NAME(UAeyerjiInventoryComponent, EquippedItems, this);

	ClearPlacement(Item->UniqueId);
	AJ_LOG(this, TEXT("[InventoryPickup] Server_EquipItem cleared placement for %s"), *Item->UniqueId.ToString());

	ApplyItemGameplayEffect(Item);
	RebuildEquipmentStatContributions();
	OnEquippedItemChanged.Broadcast(ResolvedSlot, SlotIndex, Item);
	BroadcastItemStateChange(EInventoryItemStateChange::Equipped, Item, ResolvedSlot, SlotIndex);
	AJ_LOG(this, TEXT("[InventoryPickup] Server_EquipItem completed equip for %s Slot=%d Index=%d EquippedItems=%d Grid=%d"),
		*Item->UniqueId.ToString(),
		static_cast<int32>(ResolvedSlot),
		SlotIndex,
		EquippedItems.Num(),
		GridPlacements.Num());
	RebuildItemSnapshots();
	SyncProfileInventoryCache(TEXT("Equip"));
	ScheduleInventoryAutosave(TEXT("Equip"));
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
	if (!Item)
	{
		AJ_LOG(this, TEXT("[ItemStatsDebug] ApplyItemGameplayEffect skipped: Item missing"));
		return;
	}

	if (UAbilitySystemComponent* ASC = GetASC())
	{
		const bool bTrackHandles = Multiplier > 0.f;
		AJ_LOG(this, TEXT("[ItemStatsDebug] ApplyItemGameplayEffect begin Item=%s Def=%s Id=%s Mods=%d ASC=%s Mult=%.2f"),
			*GetNameSafe(Item),
			*GetNameSafe(Item->Definition.Get()),
			Item->UniqueId.IsValid() ? *Item->UniqueId.ToString() : TEXT("Invalid"),
			Item->GetFinalAggregatedModifiers().Num(),
			*GetNameSafe(ASC),
			Multiplier);
		if (bTrackHandles)
		{
			RemoveItemGameplayEffect(Item->UniqueId);
		}

		FItemActiveEffectSet HandleSet;
		const bool bIsAuthority = GetOwner() && GetOwner()->HasAuthority();

		const TArray<FItemStatModifier>& Mods = Item->GetFinalAggregatedModifiers();
		for (int32 Index = 0; Index < Mods.Num(); ++Index)
		{
			const FItemStatModifier& Mod = Mods[Index];
			const bool bValidItemAttribute = IsUsableItemStatAttribute(Mod.Attribute);
			AJ_LOG(this, TEXT("[ItemStatsDebug] Mod[%d] aggregate-managed Attr=%s Valid=%d Op=%d Mag=%.3f Normalized=%.3f"),
				Index,
				bValidItemAttribute ? *Mod.Attribute.GetName() : TEXT("InvalidItemAttribute"),
				bValidItemAttribute ? 1 : 0,
				static_cast<int32>(Mod.Op),
				Mod.Magnitude,
				bValidItemAttribute ? NormalizeEquipmentModifierMagnitude(Mod) : 0.f);
		}

		const int32 ItemLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(Item->ItemLevel);
		const int32 ItemLevelDelta = FMath::Max(ItemLevel - 1, 0);
		float RarityMultiplier = 1.f;
		if (const UWorld* World = GetWorld())
		{
			if (const UGameInstance* GI = World->GetGameInstance())
			{
				if (const ULootService* LootService = GI->GetSubsystem<ULootService>())
				{
					if (const UAeyerjiLootTable* LootTable = LootService->GetLootTable())
					{
						if (const FRarityScalingRow* RarityScaling = LootTable->FindRarityScaling(Item->Rarity))
						{
							RarityMultiplier = FMath::Max(0.f, RarityScaling->GrantedEffectLevelMultiplier);
						}
					}
				}
			}
		}
		for (const FItemGrantedEffect& Granted : Item->GetGrantedEffects())
		{
			if (!Granted.IsValid())
			{
				UE_LOG(LogAeyerji, Verbose, TEXT("[ItemStatsDebug] GrantedEffect skipped invalid Item=%s Def=%s"),
					*GetNameSafe(Item),
					*GetNameSafe(Item->Definition.Get()));
				continue;
			}

			UE_LOG(LogAeyerji, Verbose, TEXT("[ItemStatsDebug] GrantedEffect begin Item=%s Def=%s Effect=%s Level=%.2f AppTags=%d SetByCaller=%d"),
				*GetNameSafe(Item),
				*GetNameSafe(Item->Definition.Get()),
				*GetNameSafe(Granted.EffectClass.Get()),
				Granted.EffectLevel,
				Granted.ApplicationTags.Num(),
				Granted.SetByCallerMagnitudes.Num());

			FGameplayEffectContextHandle ExtraContext = ASC->MakeEffectContext();
			ExtraContext.AddSourceObject(Item);

			FGameplayEffectSpecHandle ExtraSpecHandle = ASC->MakeOutgoingSpec(Granted.EffectClass, Granted.EffectLevel, ExtraContext);
			if (!ExtraSpecHandle.IsValid() || !ExtraSpecHandle.Data.IsValid())
			{
				UE_LOG(LogAeyerji, Verbose, TEXT("[ItemStatsDebug] GrantedEffect spec invalid Item=%s Effect=%s Level=%.2f"),
					*GetNameSafe(Item),
					*GetNameSafe(Granted.EffectClass.Get()),
					Granted.EffectLevel);
				continue;
			}

			FGameplayEffectSpec* ExtraSpec = ExtraSpecHandle.Data.Get();
			if (!ExtraSpec)
			{
				UE_LOG(LogAeyerji, Verbose, TEXT("[ItemStatsDebug] GrantedEffect spec null Item=%s Effect=%s"),
					*GetNameSafe(Item),
					*GetNameSafe(Granted.EffectClass.Get()));
				continue;
			}

			if (Granted.ApplicationTags.Num() > 0)
			{
				ExtraSpec->DynamicGrantedTags.AppendTags(Granted.ApplicationTags);
				UE_LOG(LogAeyerji, Verbose, TEXT("[ItemStatsDebug] GrantedEffect added %d app tags Item=%s Effect=%s"),
					Granted.ApplicationTags.Num(),
					*GetNameSafe(Item),
					*GetNameSafe(Granted.EffectClass.Get()));
			}

			for (const FItemSetByCallerMagnitude& SetByCaller : Granted.SetByCallerMagnitudes)
			{
				if (!SetByCaller.IsValid())
				{
					UE_LOG(LogAeyerji, Verbose, TEXT("[ItemStatsDebug] GrantedEffect set-by-caller invalid Item=%s Effect=%s"),
						*GetNameSafe(Item),
						*GetNameSafe(Granted.EffectClass.Get()));
					continue;
				}

				const float ScaledMagnitude =
					(SetByCaller.LevelOneMagnitude * (1.f + (SetByCaller.PerLevelMultiplier * ItemLevelDelta)))
					+ (SetByCaller.PerLevelAdd * ItemLevelDelta);
				const float FinalMagnitude = ScaledMagnitude * RarityMultiplier;
				ExtraSpec->SetSetByCallerMagnitude(SetByCaller.DataTag, FinalMagnitude);
				UE_LOG(LogAeyerji, Verbose, TEXT("[ItemStatsDebug] GrantedEffect set-by-caller Item=%s Effect=%s Tag=%s Mag=%.3f"),
					*GetNameSafe(Item),
					*GetNameSafe(Granted.EffectClass.Get()),
					*SetByCaller.DataTag.ToString(),
					FinalMagnitude);
			}

			FActiveGameplayEffectHandle ExtraHandle = ASC->ApplyGameplayEffectSpecToSelf(*ExtraSpec);
			if (ExtraHandle.IsValid())
			{
				HandleSet.AdditionalHandles.Add(ExtraHandle);
				UE_LOG(LogAeyerji, Verbose, TEXT("[ItemStatsDebug] GrantedEffect applied Item=%s Effect=%s"),
					*GetNameSafe(Item),
					*GetNameSafe(Granted.EffectClass.Get()));
			}
			else
			{
				UE_LOG(LogAeyerji, Verbose, TEXT("[ItemStatsDebug] GrantedEffect apply failed Item=%s Effect=%s"),
					*GetNameSafe(Item),
					*GetNameSafe(Granted.EffectClass.Get()));
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
							UpdateManagedOwnedTagCount(ASC, Tag, 1);
							HandleSet.AddedOwnedTags.Add(Tag);
						}
					}
				}
			}
		}

		if (bTrackHandles && (HandleSet.StatsHandle.IsValid()
			|| HandleSet.AdditionalHandles.Num() > 0
			|| HandleSet.GrantedAbilityHandles.Num() > 0
			|| HandleSet.AddedOwnedTags.Num() > 0))
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
						UpdateManagedOwnedTagCount(ASC, Tag, -1);
					}
				}
			}

			ActiveEffectHandles.Remove(ItemId);
		}
	}
}

void UAeyerjiInventoryComponent::UpdateManagedOwnedTagCount(UAbilitySystemComponent* ASC, const FGameplayTag& Tag, int32 Delta)
{
	if (!ASC || !Tag.IsValid() || Delta == 0)
	{
		return;
	}

	const int32 CurrentCount = ManagedOwnedTagCounts.FindRef(Tag);
	const int32 NewCount = FMath::Max(CurrentCount + Delta, 0);
	if (NewCount == CurrentCount)
	{
		return;
	}

	if (NewCount > 0)
	{
		ManagedOwnedTagCounts.Add(Tag, NewCount);
	}
	else
	{
		ManagedOwnedTagCounts.Remove(Tag);
	}

	// Drive the ASC to the exact count we expect so repeated equipment refreshes stay idempotent.
	ASC->SetLooseGameplayTagCount(Tag, NewCount);
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

	SlotIndexA = SanitizeSlotIndex(Slot, SlotIndexA);
	SlotIndexB = SanitizeSlotIndex(Slot, SlotIndexB);

	if (SlotIndexA == INDEX_NONE || SlotIndexB == INDEX_NONE || SlotIndexA == SlotIndexB)
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
	SyncProfileInventoryCache(TEXT("SwapEquipped"));
	ScheduleInventoryAutosave(TEXT("SwapEquipped"));
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
		RebuildEquipmentStatContributions();
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
		AActor* InventoryOwner = GetOwner();
		const FVector GroundedLocation = FindGroundedDropLocation(*World, WorldLocation, InventoryOwner);

		AJ_LOG(this, TEXT("Server_DropItem dropping %s at %s (grounded %s) Rot=%s Class=%s"),
			*GetNameSafe(Item),
			*WorldLocation.ToString(),
			*GroundedLocation.ToString(),
			*WorldRotation.ToString(),
			*GetNameSafe(LootPickupClass.Get()));

		if (!UAeyerjiInventoryBPFL::SpawnLootByInstance(this, Item, GroundedLocation, WorldRotation, EItemDropDistributionMode::DropOnlyForInstigator, InventoryOwner))
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

	SyncProfileInventoryCache(TEXT("Drop"));
	ScheduleInventoryAutosave(TEXT("Drop"));
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
		RebuildEquipmentStatContributions();
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

		auto BroadcastIfResolved = [this, &Entry, SanitizedIndex, CurrentItem]()
		{
			if (Entry.ItemId.IsValid() && !CurrentItem)
			{
				AJ_LOG(this, TEXT("OnRep_EquippedItems deferred unresolved item Slot=%d Index=%d ItemId=%s"),
					static_cast<int32>(Entry.Slot),
					SanitizedIndex,
					*Entry.ItemId.ToString());
				return;
			}

			OnEquippedItemChanged.Broadcast(Entry.Slot, SanitizedIndex, CurrentItem);
		};

		if (FGuid* OldIdPtr = OldEntries.Find(SlotKey))
		{
			if (*OldIdPtr != Entry.ItemId)
			{
				BroadcastIfResolved();
			}
			OldEntries.Remove(SlotKey);
		}
		else
		{
			BroadcastIfResolved();
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

	auto MakeSlotKey = [](EEquipmentSlot Slot, int32 Index)
	{
		return (static_cast<int64>(Slot) << 32) | static_cast<uint32>(Index);
	};

	TMap<int64, UAeyerjiItemInstance*> PreviousResolvedItems;
	for (const FEquippedItemEntry& Entry : EquippedItems)
	{
		PreviousResolvedItems.Add(MakeSlotKey(Entry.Slot, FMath::Max(0, Entry.SlotIndex)), Entry.Item);
	}

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
		const int32 SanitizedIndex = FMath::Max(0, Entry.SlotIndex);
		UAeyerjiItemInstance* const PreviousResolvedItem = PreviousResolvedItems.FindRef(MakeSlotKey(Entry.Slot, SanitizedIndex));
		if (Entry.Item)
		{
			const bool bNewlyResolved = PreviousResolvedItem != Entry.Item;
			AJ_LOG(this, TEXT("OnRep_Items resolved equipped slot %d index %d -> %s NewlyResolved=%s"),
				static_cast<int32>(Entry.Slot),
				SanitizedIndex,
				*Entry.Item->UniqueId.ToString(),
				bNewlyResolved ? TEXT("true") : TEXT("false"));

			if (bNewlyResolved)
			{
				OnEquippedItemChanged.Broadcast(Entry.Slot, SanitizedIndex, Entry.Item);
			}
		}
		else if (Entry.ItemId.IsValid())
		{
			AJ_LOG(this, TEXT("OnRep_Items still missing item for slot %d index %d id=%s"),
				static_cast<int32>(Entry.Slot),
				SanitizedIndex,
				*Entry.ItemId.ToString());
		}
		else
		{
			AJ_LOG(this, TEXT("OnRep_Items slot %d index %d has no assignment"),
				static_cast<int32>(Entry.Slot),
				SanitizedIndex);
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

int32 UAeyerjiInventoryComponent::SanitizeSaveDataAttributes(FAeyerjiInventorySaveData& SaveData)
{
	int32 RemovedCount = 0;
	for (FInventoryItemSnapshot& Snapshot : SaveData.ItemSnapshots)
	{
		RemovedCount += SanitizeInventorySnapshotAttributes(Snapshot);
	}
	return RemovedCount;
}

FAeyerjiInventorySaveData UAeyerjiInventoryComponent::BuildSaveData()
{
	FAeyerjiInventorySaveData SaveData;

	if (GetOwnerRole() != ROLE_Authority)
	{
		return SaveData;
	}

	RebuildItemSnapshots();

	TSet<FGuid> OwnedItemIds;
	OwnedItemIds.Reserve(Items.Num());
	for (const UAeyerjiItemInstance* Item : Items)
	{
		if (Item && Item->UniqueId.IsValid())
		{
			OwnedItemIds.Add(Item->UniqueId);
		}
	}

	for (FEquippedItemEntry& Entry : EquippedItems)
	{
		if (Entry.Item && Entry.Item->UniqueId.IsValid())
		{
			Entry.ItemId = Entry.Item->UniqueId;
		}
	}

	const int32 RemovedStaleEquipped = EquippedItems.RemoveAll(
		[&OwnedItemIds](const FEquippedItemEntry& Entry)
		{
			return !Entry.ItemId.IsValid() || !OwnedItemIds.Contains(Entry.ItemId);
		});

	if (RemovedStaleEquipped > 0)
	{
		MARK_PROPERTY_DIRTY_FROM_NAME(UAeyerjiInventoryComponent, EquippedItems, this);
		UE_LOG(LogTemp, Warning,
			TEXT("[InventorySave] BuildSaveData pruned %d equipped entries without owned item snapshots."),
			RemovedStaleEquipped);
	}

	SaveData.ItemSnapshots = ItemSnapshots;
	SaveData.GridPlacements = GridPlacements;
	SaveData.EquippedItems = EquippedItems;
	SaveData.GridColumns = GridColumns;
	SaveData.GridRows = GridRows;

	const int32 RemovedInvalidAttributes = SanitizeSaveDataAttributes(SaveData);
	if (RemovedInvalidAttributes > 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[InventorySave] BuildSaveData pruned %d invalid item stat attribute references."),
			RemovedInvalidAttributes);
	}

	for (FInventoryItemGridData& Placement : SaveData.GridPlacements)
	{
		Placement.ItemInstance = nullptr;
	}

	for (FEquippedItemEntry& Entry : SaveData.EquippedItems)
	{
		Entry.Item = nullptr;
	}

	UE_LOG(LogTemp, Display, TEXT("[InventorySave] BuildSaveData Inventory=%s Items=%d Snapshots=%d Equipped=%d Grid=%d GridSize=(%d,%d)"),
		*GetNameSafe(this),
		Items.Num(),
		SaveData.ItemSnapshots.Num(),
		SaveData.EquippedItems.Num(),
		SaveData.GridPlacements.Num(),
		SaveData.GridColumns,
		SaveData.GridRows);

	return SaveData;
}

void UAeyerjiInventoryComponent::ApplySaveData(const FAeyerjiInventorySaveData& SaveData)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	FAeyerjiInventorySaveData SanitizedSaveData = SaveData;
	const int32 RemovedInvalidAttributes = SanitizeSaveDataAttributes(SanitizedSaveData);
	if (RemovedInvalidAttributes > 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[InventorySave] ApplySaveData pruned %d invalid item stat attribute references."),
			RemovedInvalidAttributes);
	}

	const TArray<FInventoryItemSnapshot> SavedItemSnapshots = SanitizedSaveData.ItemSnapshots;
	const TArray<FInventoryItemGridData> SavedGridPlacements = SanitizedSaveData.GridPlacements;
	const TArray<FEquippedItemEntry> SavedEquippedItems = SanitizedSaveData.EquippedItems;
	const int32 SavedGridColumns = SanitizedSaveData.GridColumns;
	const int32 SavedGridRows = SanitizedSaveData.GridRows;

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
	ClearEquipmentStatContributions();

	Items.Reset();
	ItemChangedDelegateHandles.Reset();
	ActiveEffectHandles.Reset();
	ManagedOwnedTagCounts.Reset();
	GridPlacements.Reset();
	EquippedItems.Reset();
	ItemSnapshots.Reset();

	GridColumns = SavedGridColumns > 0 ? SavedGridColumns : GridColumns;
	GridRows = SavedGridRows > 0 ? SavedGridRows : GridRows;

	for (const FInventoryItemSnapshot& Snapshot : SavedItemSnapshots)
	{
		if (!Snapshot.ItemId.IsValid())
		{
			continue;
		}

		UItemDefinition* ResolvedDefinition = ResolveSnapshotDefinition(Snapshot, this);
		if (!ResolvedDefinition)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Inventory] ApplySaveData skipped item %s because definition key %s could not be resolved."),
				*Snapshot.ItemId.ToString(),
				*SafeNameToString(Snapshot.DefinitionKey));
			continue;
		}

		const FName ItemName = MakeUniqueObjectName(this, UAeyerjiItemInstance::StaticClass(), TEXT("AeyerjiItemInstance"));
		UAeyerjiItemInstance* Item = NewObject<UAeyerjiItemInstance>(this, UAeyerjiItemInstance::StaticClass(), ItemName);
		Item->Definition = ResolvedDefinition;
		Item->Rarity = Snapshot.Rarity;
		Item->ItemLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(Snapshot.ItemLevel);
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

		const int32 RemovedRuntimeAttributes = SanitizeItemInstanceAttributes(*Item);
		if (RemovedRuntimeAttributes > 0)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[InventorySave] ApplySaveData pruned %d invalid runtime item stat attribute references for ItemId=%s."),
				RemovedRuntimeAttributes,
				*Item->UniqueId.ToString());
		}

		if (ShouldRebuildEmptySnapshotAggregation(Item))
		{
			UE_LOG(LogTemp, Display, TEXT("[InventorySave] Rebuilding empty item aggregation ItemId=%s Def=%s ItemLevel=%d"),
				*Item->UniqueId.ToString(),
				*GetNameSafe(Item->Definition.Get()),
				Item->ItemLevel);
			Item->RebuildAggregation();
			if (const UAeyerjiLootTable* LootTable = ResolveLootTableForInventory(this))
			{
				Item->ApplyLootStatScaling(LootTable);
			}
		}

		Items.Add(Item);
		BindItemInstanceDelegates(Item);
	}

	GridPlacements = SavedGridPlacements;
	for (FInventoryItemGridData& Placement : GridPlacements)
	{
		Placement.ItemInstance = FindItemById(Placement.ItemId);
	}
	MARK_PROPERTY_DIRTY_FROM_NAME(UAeyerjiInventoryComponent, GridPlacements, this);

	EquippedItems = SavedEquippedItems;
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
	RebuildEquipmentStatContributions();

	OnInventoryChanged.Broadcast();
	for (const FEquippedItemEntry& Entry : EquippedItems)
	{
		OnEquippedItemChanged.Broadcast(Entry.Slot, Entry.SlotIndex, Entry.Item);
	}

	UE_LOG(LogTemp, Display, TEXT("[InventorySave] ApplySaveData complete Inventory=%s RestoredItems=%d Equipped=%d Grid=%d GridSize=(%d,%d)"),
		*GetNameSafe(this),
		Items.Num(),
		EquippedItems.Num(),
		GridPlacements.Num(),
		GridColumns,
		GridRows);
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
		UE_LOG(LogTemp, Warning, TEXT("[InventoryPickup] TryAutoPlaceItem invalid item Inventory=%s Item=%s"),
			*GetNameSafe(this),
			*GetNameSafe(Item));
		return false;
	}

	Item->SetNetAddressable();

	FInventoryItemGridData Existing;
	if (GetPlacementForItem(Item->UniqueId, Existing))
	{
		UE_LOG(LogTemp, Display, TEXT("[InventoryPickup] TryAutoPlaceItem already placed %s at (%d,%d)"),
			*Item->UniqueId.ToString(),
			Existing.TopLeft.X,
			Existing.TopLeft.Y);
		return true;
	}

	if (GridColumns <= 0 || GridRows <= 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[InventoryPickup] TryAutoPlaceItem invalid grid for %s Grid=(%d,%d)"),
			*Item->UniqueId.ToString(),
			GridColumns,
			GridRows);
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
				UE_LOG(LogTemp, Display, TEXT("[InventoryPickup] Placing %s at (%d,%d) size (%d,%d) Grid=(%d,%d)"),
					*Item->UniqueId.ToString(), X, Y, Size.X, Size.Y, GridColumns, GridRows);
				GridPlacements.Add(Candidate);
				MARK_PROPERTY_DIRTY_FROM_NAME(UAeyerjiInventoryComponent, GridPlacements, this);
				OnInventoryChanged.Broadcast();
				return true;
			}
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("[InventoryPickup] TryAutoPlaceItem failed for %s Size=(%d,%d) Grid=(%d,%d) ExistingPlacements=%d"),
		*Item->UniqueId.ToString(),
		Size.X,
		Size.Y,
		GridColumns,
		GridRows,
		GridPlacements.Num());
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

		const int32 RemovedInvalidAttributes = SanitizeItemInstanceAttributes(*Item);
		if (RemovedInvalidAttributes > 0)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[InventorySave] RebuildItemSnapshots pruned %d invalid item stat attribute references for ItemId=%s."),
				RemovedInvalidAttributes,
				Item->UniqueId.IsValid() ? *Item->UniqueId.ToString() : TEXT("Invalid"));
		}

		FInventoryItemSnapshot Snapshot;
		Snapshot.ItemId = Item->UniqueId;
		Snapshot.Definition = Item->Definition;
		Snapshot.DefinitionKey = Item->Definition ? Item->Definition->GetDefinitionKey() : NAME_None;
		Snapshot.Rarity = Item->Rarity;
		Snapshot.ItemLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(Item->ItemLevel);
		Item->ItemLevel = Snapshot.ItemLevel;
		Snapshot.Seed = Item->Seed;
		Snapshot.RolledAffixes = Item->RolledAffixes;
		Snapshot.FinalAggregatedModifiers = Item->FinalAggregatedModifiers;
		Snapshot.GrantedEffects = Item->GrantedEffects;
		Snapshot.GrantedAbilities = Item->GrantedAbilities;
		Snapshot.EquippedSlot = Item->EquippedSlot;
		Snapshot.SlotIndex = Item->EquippedSlotIndex;
		Snapshot.InventorySize = Item->InventorySize;

		ItemSnapshots.Add(MoveTemp(Snapshot));

		UE_LOG(LogTemp, Verbose, TEXT("[InventorySave] Snapshot item UniqueId=%s Def=%s DefinitionKey=%s EquippedSlot=%d SlotIndex=%d Size=(%d,%d)"),
			Item->UniqueId.IsValid() ? *Item->UniqueId.ToString() : TEXT("Invalid"),
			*GetNameSafe(Item->Definition.Get()),
			*ItemSnapshots.Last().DefinitionKey.ToString(),
			static_cast<int32>(Item->EquippedSlot),
			Item->EquippedSlotIndex,
			Item->InventorySize.X,
			Item->InventorySize.Y);
	}

	MARK_PROPERTY_DIRTY_FROM_NAME(UAeyerjiInventoryComponent, ItemSnapshots, this);
}

void UAeyerjiInventoryComponent::SyncProfileInventoryCache(const TCHAR* Reason)
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const APlayerState* PlayerState = OwnerPawn ? OwnerPawn->GetPlayerState() : Cast<APlayerState>(GetOwner());
	if (!PlayerState)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[InventorySave] RuntimeCacheSync skipped Reason=%s Inventory=%s Owner=%s Detail=NoPlayerState"),
			Reason ? Reason : TEXT("Unknown"),
			*GetNameSafe(this),
			*GetNameSafe(GetOwner()));
		return;
	}

	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UAeyerjiSaveManagerSubsystem* SaveManager =
		GameInstance ? GameInstance->GetSubsystem<UAeyerjiSaveManagerSubsystem>() : nullptr;
	if (!SaveManager)
	{
		return;
	}

	UAeyerjiSaveGame* CachedProfile = nullptr;
	if (!SaveManager->GetServerCachedProfile(PlayerState, CachedProfile) || !CachedProfile)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[InventorySave] RuntimeCacheSync skipped Reason=%s Inventory=%s PlayerState=%s Detail=NoServerCachedProfile Items=%d Equipped=%d Grid=%d"),
			Reason ? Reason : TEXT("Unknown"),
			*GetNameSafe(this),
			*GetNameSafe(PlayerState),
			Items.Num(),
			EquippedItems.Num(),
			GridPlacements.Num());
		return;
	}

	CachedProfile->Inventory = BuildSaveData();
	UE_LOG(LogTemp, Display,
		TEXT("[InventorySave] RuntimeCacheSync Reason=%s Inventory=%s PlayerState=%s CachedItems=%d CachedEquipped=%d CachedGrid=%d"),
		Reason ? Reason : TEXT("Unknown"),
		*GetNameSafe(this),
		*GetNameSafe(PlayerState),
		CachedProfile->Inventory.ItemSnapshots.Num(),
		CachedProfile->Inventory.EquippedItems.Num(),
		CachedProfile->Inventory.GridPlacements.Num());
}

void UAeyerjiInventoryComponent::ScheduleInventoryAutosave(const TCHAR* Reason)
{
	if (GetOwnerRole() != ROLE_Authority || !bAutoCommitInventoryChanges)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (InventoryAutoCommitDelaySeconds <= 0.f)
	{
		HandleInventoryAutosave();
		return;
	}

	World->GetTimerManager().SetTimer(
		InventoryAutosaveTimerHandle,
		this,
		&UAeyerjiInventoryComponent::HandleInventoryAutosave,
		InventoryAutoCommitDelaySeconds,
		false);

	UE_LOG(LogTemp, Verbose,
		TEXT("[InventorySave] AutosaveScheduled Reason=%s Inventory=%s Delay=%.2f Items=%d Equipped=%d Grid=%d"),
		Reason ? Reason : TEXT("Unknown"),
		*GetNameSafe(this),
		InventoryAutoCommitDelaySeconds,
		Items.Num(),
		EquippedItems.Num(),
		GridPlacements.Num());
}

void UAeyerjiInventoryComponent::HandleInventoryAutosave()
{
	if (GetOwnerRole() != ROLE_Authority)
	{
		return;
	}

	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	AAeyerjiPlayerState* PlayerState = OwnerPawn ? OwnerPawn->GetPlayerState<AAeyerjiPlayerState>() : nullptr;
	if (!OwnerPawn || !PlayerState)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[InventorySave] AutosaveSkipped Inventory=%s Owner=%s Detail=MissingPawnOrPlayerState"),
			*GetNameSafe(this),
			*GetNameSafe(GetOwner()));
		return;
	}

	const bool bCommitted = PlayerState->CommitCheckpointProfileFromPawn(
		EAeyerjiSaveCheckpointReason::Manual,
		OwnerPawn,
		/*bBumpRevision=*/true);

	UE_LOG(LogTemp, Display,
		TEXT("[InventorySave] AutosaveCheckpoint Result=%d Inventory=%s Pawn=%s Items=%d Equipped=%d Grid=%d"),
		bCommitted ? 1 : 0,
		*GetNameSafe(this),
		*GetNameSafe(OwnerPawn),
		Items.Num(),
		EquippedItems.Num(),
		GridPlacements.Num());
}

void UAeyerjiInventoryComponent::ResolveEquippedItems()
{
	PruneEmptyEquippedEntries();
	bool bRemovedEquippedItem = false;

	for (int32 EntryIndex = EquippedItems.Num() - 1; EntryIndex >= 0; --EntryIndex)
	{
		FEquippedItemEntry& Entry = EquippedItems[EntryIndex];
		UAeyerjiItemInstance* const ResolvedItem = Entry.ItemId.IsValid() ? FindItemById(Entry.ItemId) : nullptr;
		if (!ResolvedItem && Entry.ItemId.IsValid())
		{
			AJ_LOG(this, TEXT("ResolveEquippedItems unresolved ItemId=%s Slot=%d Index=%d"),
				*Entry.ItemId.ToString(),
				static_cast<int32>(Entry.Slot),
				Entry.SlotIndex);
		}

		Entry.Item = ResolvedItem;
		const bool bSavedSlotCompatible = !Entry.Item || IsSlotCompatibleWithDefinition(Entry.Slot, Entry.Item->Definition.Get());
		const EEquipmentSlot SanitizedSlot = bSavedSlotCompatible
			? ResolveEquipmentSlot(Entry.Slot, Entry.Item ? Entry.Item->Definition.Get() : nullptr)
			: Entry.Slot;
		if (bSavedSlotCompatible && SanitizedSlot != Entry.Slot)
		{
			AJ_LOG(this, TEXT("ResolveEquippedItems sanitized slot %d -> %d for %s"),
				static_cast<int32>(Entry.Slot),
				static_cast<int32>(SanitizedSlot),
				Entry.Item ? *Entry.Item->UniqueId.ToString() : TEXT("None"));
			Entry.Slot = SanitizedSlot;
		}

		const int32 SanitizedIndex = SanitizeSlotIndex(Entry.Slot, Entry.SlotIndex);
		if (SanitizedIndex != Entry.SlotIndex)
		{
			AJ_LOG(this, TEXT("ResolveEquippedItems sanitized slot index %d -> %d for %s"),
				Entry.SlotIndex,
				SanitizedIndex,
				Entry.Item ? *Entry.Item->UniqueId.ToString() : TEXT("None"));
			Entry.SlotIndex = SanitizedIndex;
		}

		if (Entry.Item && !CanEquipItemInSlot(Entry.Item, Entry.Slot, Entry.SlotIndex))
		{
			const EEquipmentSlot RemovedSlot = Entry.Slot;
			const int32 RemovedSlotIndex = Entry.SlotIndex;
			AJ_LOG(this, TEXT("ResolveEquippedItems unequipping locked or incompatible item %s Slot=%d Index=%d OwnerLevel=%d"),
				*Entry.Item->UniqueId.ToString(),
				static_cast<int32>(RemovedSlot),
				RemovedSlotIndex,
				GetOwnerLevelForInventoryRules());

			Entry.Item->EquippedSlot = ResolveEquipmentSlot(Entry.Item->Definition ? Entry.Item->Definition->DefaultSlot : Entry.Slot, Entry.Item->Definition.Get());
			Entry.Item->EquippedSlotIndex = INDEX_NONE;
			if (GetOwnerRole() == ROLE_Authority)
			{
				RemoveItemGameplayEffect(Entry.Item->UniqueId);
				AutoPlaceItem(Entry.Item);
			}
			EquippedItems.RemoveAt(EntryIndex);
			bRemovedEquippedItem = true;
			MARK_PROPERTY_DIRTY_FROM_NAME(UAeyerjiInventoryComponent, EquippedItems, this);
			OnEquippedItemChanged.Broadcast(RemovedSlot, RemovedSlotIndex, nullptr);
			continue;
		}

		if (Entry.Item)
		{
			Entry.Item->EquippedSlot = Entry.Slot;
			Entry.Item->EquippedSlotIndex = Entry.SlotIndex;
		}
	}

	if (bRemovedEquippedItem)
	{
		RebuildEquipmentStatContributions();
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
	RebuildEquipmentStatContributions();
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

	TArray<FInventoryItemSnapshot> SanitizedSnapshots = ItemSnapshots;
	int32 RemovedInvalidAttributes = 0;
	for (FInventoryItemSnapshot& Snapshot : SanitizedSnapshots)
	{
		RemovedInvalidAttributes += SanitizeInventorySnapshotAttributes(Snapshot);
	}
	if (RemovedInvalidAttributes > 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Inventory] RefreshClientItemsFromSnapshots pruned %d invalid item stat attribute references."),
			RemovedInvalidAttributes);
	}

	for (const FInventoryItemSnapshot& Snapshot : SanitizedSnapshots)
	{
		if (!Snapshot.ItemId.IsValid())
		{
			continue;
		}

		SnapshotIds.Add(Snapshot.ItemId);

		UAeyerjiItemInstance* Item = FindItemById(Snapshot.ItemId);
		if (!Item)
		{
			const FName ItemName = MakeUniqueObjectName(this, UAeyerjiItemInstance::StaticClass(), TEXT("AeyerjiItemInstance"));
			Item = NewObject<UAeyerjiItemInstance>(this, UAeyerjiItemInstance::StaticClass(), ItemName);
			Items.Add(Item);
		}

		Item->Definition = ResolveSnapshotDefinition(Snapshot, this);
		Item->Rarity = Snapshot.Rarity;
		Item->ItemLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(Snapshot.ItemLevel);
		Item->Seed = Snapshot.Seed;
		Item->UniqueId = Snapshot.ItemId;
		Item->RolledAffixes = Snapshot.RolledAffixes;
		Item->FinalAggregatedModifiers = Snapshot.FinalAggregatedModifiers;
		Item->GrantedEffects = Snapshot.GrantedEffects;
		Item->GrantedAbilities = Snapshot.GrantedAbilities;
		Item->EquippedSlot = Snapshot.EquippedSlot;
		Item->EquippedSlotIndex = Snapshot.SlotIndex;
		Item->InventorySize = Snapshot.InventorySize;
		SanitizeItemInstanceAttributes(*Item);
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

void UAeyerjiInventoryComponent::TryBindOwnerLevelChange()
{
	UnbindOwnerLevelChange();

	UAbilitySystemComponent* ASC = GetASC();
	if (!ASC)
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(LevelBindingRetryHandle, this, &UAeyerjiInventoryComponent::TryBindOwnerLevelChange, 0.25f, false);
		}
		return;
	}

	const FGameplayAttribute LevelAttribute = UAeyerjiAttributeSet::GetLevelAttribute();
	LevelChangedHandle = ASC->GetGameplayAttributeValueChangeDelegate(LevelAttribute).AddUObject(this, &UAeyerjiInventoryComponent::HandleOwnerLevelChanged);
	LevelBoundASC = ASC;
	BroadcastEquipmentSlotUnlocksIfChanged(true);
}

void UAeyerjiInventoryComponent::UnbindOwnerLevelChange()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LevelBindingRetryHandle);
	}

	if (LevelChangedHandle.IsValid())
	{
		if (UAbilitySystemComponent* ASC = LevelBoundASC.Get())
		{
			ASC->GetGameplayAttributeValueChangeDelegate(UAeyerjiAttributeSet::GetLevelAttribute()).Remove(LevelChangedHandle);
		}
		LevelChangedHandle.Reset();
	}

	LevelBoundASC.Reset();
}

void UAeyerjiInventoryComponent::HandleOwnerLevelChanged(const FOnAttributeChangeData& Data)
{
	(void)Data;

	BroadcastEquipmentSlotUnlocksIfChanged();

	if (GetOwnerRole() == ROLE_Authority)
	{
		ResolveEquippedItems();
		RebuildEquipmentStatContributions();
		RebuildItemSnapshots();
	}
}

void UAeyerjiInventoryComponent::BroadcastEquipmentSlotUnlocksIfChanged(bool bForce)
{
	const int32 PlayerLevel = GetOwnerLevelForInventoryRules();
	const int32 NormalLaneSlots = GetUnlockedEquipmentSlotCount(EEquipmentSlot::Assault);
	const int32 CorruptionSlots = GetUnlockedEquipmentSlotCount(EEquipmentSlot::Corruption);

	if (!bForce
		&& PlayerLevel == LastBroadcastPlayerLevel
		&& NormalLaneSlots == LastBroadcastNormalLaneSlots
		&& CorruptionSlots == LastBroadcastCorruptionSlots)
	{
		return;
	}

	LastBroadcastPlayerLevel = PlayerLevel;
	LastBroadcastNormalLaneSlots = NormalLaneSlots;
	LastBroadcastCorruptionSlots = CorruptionSlots;
	OnEquipmentSlotUnlocksChanged.Broadcast(PlayerLevel, NormalLaneSlots, CorruptionSlots, CorruptionSlots > 0);
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

int32 UAeyerjiInventoryComponent::SanitizeSlotIndex(EEquipmentSlot Slot, int32 SlotIndex) const
{
	if (SlotIndex == INDEX_NONE)
	{
		return INDEX_NONE;
	}

	const int32 MaxSlots = GetUnlockedEquipmentSlotCount(Slot);
	if (MaxSlots <= 0)
	{
		return INDEX_NONE;
	}
	return FMath::Clamp(SlotIndex, 0, MaxSlots - 1);
}

int32 UAeyerjiInventoryComponent::FindFirstFreeSlotIndex(EEquipmentSlot Slot, const UAeyerjiItemInstance* IgnoredItem) const
{
	const int32 MaxSlots = GetUnlockedEquipmentSlotCount(Slot);
	if (MaxSlots <= 0)
	{
		return INDEX_NONE;
	}

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
	SlotIndex = SanitizeSlotIndex(Slot, SlotIndex);
	if (SlotIndex == INDEX_NONE)
	{
		return false;
	}
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
	RebuildEquipmentStatContributions();
	OnEquippedItemChanged.Broadcast(Slot, SlotIndex, nullptr);

	RebuildItemSnapshots();
	SyncProfileInventoryCache(TEXT("Unequip"));
	ScheduleInventoryAutosave(TEXT("Unequip"));
	return true;
}
