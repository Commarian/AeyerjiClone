// AeyerjiInventoryBPFL.cpp

#include "Inventory/AeyerjiInventoryBPFL.h"

#include "Engine/World.h"
#include "Inventory/AeyerjiLootPickup.h"
#include "Items/InventoryComponent.h"
#include "Items/ItemDefinition.h"
#include "Items/ItemGenerator.h"
#include "Items/ItemInstance.h"
#include "CharacterStatsLibrary.h"
#include "EngineUtils.h"
#include "Logging/AeyerjiLog.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Systems/LootService.h"
#include "CollisionQueryParams.h"
#include "Engine/Engine.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	TSubclassOf<AAeyerjiLootPickup> ResolveLootPickupClass(UObject* WorldContextObject)
	{
		if (const UAeyerjiInventoryComponent* Inventory = Cast<UAeyerjiInventoryComponent>(WorldContextObject))
		{
			return Inventory->GetLootPickupClass();
		}

		// Hard fallback: ensure we use the BP pickup class so designers can drive visuals (beam, VFX, etc).
		// This is intentionally cached and only warns once if missing.
		static TWeakObjectPtr<UClass> CachedBPClass;
		static bool bWarnedMissing = false;

		if (CachedBPClass.IsValid())
		{
			return CachedBPClass.Get();
		}

		const TCHAR* PickupBPPath = TEXT("/Game/Inventory/Items/BP_AeyerjiLootPickup.BP_AeyerjiLootPickup_C");
		UClass* Loaded = StaticLoadClass(AAeyerjiLootPickup::StaticClass(), nullptr, PickupBPPath);
		if (!Loaded)
		{
			if (!bWarnedMissing)
			{
				bWarnedMissing = true;
				const FString Msg = FString::Printf(TEXT("Loot pickup BP not found at '%s' (falling back to native AAeyerjiLootPickup)."), PickupBPPath);
				AJ_LOG(WorldContextObject, TEXT("%s"), *Msg);
				if (GEngine)
				{
					GEngine->AddOnScreenDebugMessage(-1, 10.f, FColor::Red, Msg);
				}
			}
			return nullptr;
		}

		CachedBPClass = Loaded;
		return Loaded;
	}

	FVector SnapDropLocationToGround(UWorld* World, const FVector& DesiredLocation, const AActor* ActorToIgnore)
	{
		if (!World)
		{
			return DesiredLocation;
		}

		FCollisionQueryParams Params(SCENE_QUERY_STAT(AeyerjiLootDropSnap), /*bTraceComplex=*/false);
		if (ActorToIgnore)
		{
			Params.AddIgnoredActor(ActorToIgnore);
		}

		const FVector TraceStart = DesiredLocation + FVector(0.f, 0.f, 200.f);
		const FVector TraceEnd = DesiredLocation - FVector(0.f, 0.f, 5000.f);

		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params) ||
			World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_WorldStatic, Params))
		{
			return Hit.ImpactPoint;
		}

		return DesiredLocation;
	}

	void CollectRecipients(UWorld* World, EItemDropDistributionMode Mode, AActor* Instigator, TArray<AActor*>& OutRecipients)
	{
		if (!World)
		{
			return;
		}

		switch (Mode)
		{
		case EItemDropDistributionMode::DropOnlyForInstigator:
			if (Instigator)
			{
				OutRecipients.Add(Instigator);
			}
			break;

		case EItemDropDistributionMode::DropIdenticalItemForEveryPlayer:
		case EItemDropDistributionMode::DropUniqueItemForEveryPlayer:
			if (AGameStateBase* GS = World->GetGameState())
			{
				for (APlayerState* PS : GS->PlayerArray)
				{
					if (PS && PS->GetPawn())
					{
						OutRecipients.Add(PS->GetPawn());
					}
				}
			}
			break;
		default:
			break;
		}
	}

	int32 DeriveSeedForRecipient(const FLootDropResult& Result, int32 SeedOverride, int32 RecipientIndex, bool bUniquePerPlayer)
	{
		const int32 BaseSeed = (Result.Seed != 0) ? Result.Seed : (SeedOverride != 0 ? SeedOverride : FMath::Rand());
		return bUniquePerPlayer ? (BaseSeed + RecipientIndex + 1) : BaseSeed;
	}

	AAeyerjiLootPickup* SpawnPickupWithInstance(
		UObject* WorldContextObject,
		UWorld* World,
		UAeyerjiItemInstance* ItemInstance,
		const FTransform& SpawnTransform)
	{
		if (!ItemInstance)
		{
			return nullptr;
		}

		const TSubclassOf<AAeyerjiLootPickup> LootPickupClass = ResolveLootPickupClass(WorldContextObject);

		return AAeyerjiLootPickup::SpawnFromInstance(
			*World,
			ItemInstance,
			SpawnTransform,
			LootPickupClass);
	}
}

bool UAeyerjiInventoryBPFL::ToggleEquipState(UAeyerjiInventoryComponent* Inventory, UAeyerjiItemInstance* ItemInstance)
{
	if (!Inventory || !ItemInstance)
	{
		return false;
	}

	if (ItemInstance->EquippedSlotIndex != INDEX_NONE)
	{
		const EEquipmentSlot Slot = ItemInstance->EquippedSlot;
		const int32 SlotIndex = FMath::Max(0, ItemInstance->EquippedSlotIndex);
		Inventory->Server_UnequipSlot(Slot, SlotIndex);
		return true;
	}

	const EEquipmentSlot PreferredSlot = ItemInstance->Definition
		? ItemInstance->Definition->DefaultSlot
		: ItemInstance->EquippedSlot;
	const EEquipmentSlot FallbackSlot = ItemInstance->Definition
		? static_cast<EEquipmentSlot>(ItemInstance->Definition->ItemCategory)
		: PreferredSlot;

	Inventory->Server_EquipItem(ItemInstance->UniqueId, PreferredSlot, INDEX_NONE);
	if (ItemInstance->EquippedSlotIndex == INDEX_NONE && FallbackSlot != PreferredSlot)
	{
		Inventory->Server_EquipItem(ItemInstance->UniqueId, FallbackSlot, INDEX_NONE);
	}

	return true;
}

bool UAeyerjiInventoryBPFL::DropItemAtOwner(UAeyerjiInventoryComponent* Inventory, UAeyerjiItemInstance* ItemInstance, float ForwardOffset)
{
	if (!Inventory || !ItemInstance || !ItemInstance->UniqueId.IsValid())
	{
		return false;
	}

	Inventory->DropItemAtOwner(ItemInstance->UniqueId, ForwardOffset);
	return true;
}

EAeyerjiAddItemResult UAeyerjiInventoryBPFL::EquipFirstThenBag(
	UAeyerjiInventoryComponent* Inventory,
	UAeyerjiItemInstance* ItemInstance)
{
	if (!Inventory)
	{
		return EAeyerjiAddItemResult::Failed_NoInventory;
	}

	if (!ItemInstance)
	{
		return EAeyerjiAddItemResult::Failed_NoItem;
	}

	const bool bAlreadyOwned = Inventory->FindItemById(ItemInstance->UniqueId) != nullptr;

	// Ensure the item lives inside the inventory component.
	if (!Inventory->AddItemInstance(ItemInstance, true))
	{
		return EAeyerjiAddItemResult::Failed_BagFull;
	}

	const EEquipmentSlot PreferredSlot = ItemInstance->Definition
		? ItemInstance->Definition->DefaultSlot
		: ItemInstance->EquippedSlot;

	Inventory->Server_EquipItem(ItemInstance->UniqueId, PreferredSlot, INDEX_NONE);
	if (ItemInstance->EquippedSlotIndex != INDEX_NONE)
	{
		return EAeyerjiAddItemResult::Equipped;
	}

	if (Inventory->AutoPlaceItem(ItemInstance))
	{
		return EAeyerjiAddItemResult::Bagged;
	}

	if (!bAlreadyOwned)
	{
		Inventory->Server_RemoveItemById(ItemInstance->UniqueId);
	}
	return EAeyerjiAddItemResult::Failed_BagFull;
}

AAeyerjiLootPickup* UAeyerjiInventoryBPFL::SpawnLootByDefinition(
	UObject* WorldContextObject,
	UItemDefinition* Definition,
	int32 ItemLevel,
	EItemRarity Rarity,
	FVector Location,
	FRotator Rotation,
	int32 SeedOverride,
	EItemDropDistributionMode DropMode,
	AActor* Instigator)
{
	if (!WorldContextObject || !Definition)
	{
		AJ_LOG(WorldContextObject, TEXT("SpawnLootByDefinition aborted - WorldContext=%s Definition=%s"),
			*GetNameSafe(WorldContextObject),
		*GetNameSafe(Definition));
		return nullptr;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		AJ_LOG(WorldContextObject, TEXT("SpawnLootByDefinition aborted - World=%s NetMode=%d"),
			*GetNameSafe(World),
			World ? static_cast<int32>(World->GetNetMode()) : -1);
		return nullptr;
	}

	TArray<AActor*> Recipients;
	CollectRecipients(World, DropMode, Instigator, Recipients);
	const bool bUniquePerPlayer = (DropMode == EItemDropDistributionMode::DropUniqueItemForEveryPlayer);
	const int32 BaseSeed = (SeedOverride != 0) ? SeedOverride : FMath::Rand();

	if (Recipients.Num() == 0)
	{
		Recipients.Add(nullptr); // fallback: behave like instigator-only
	}

	const FVector SnappedLocation = SnapDropLocationToGround(World, Location, Instigator);

	AAeyerjiLootPickup* LastSpawned = nullptr;
	for (int32 Idx = 0; Idx < Recipients.Num(); ++Idx)
	{
		const int32 SeedToUse = bUniquePerPlayer ? (BaseSeed + Idx + 1) : BaseSeed;

		const TSubclassOf<AAeyerjiLootPickup> LootPickupClass = ResolveLootPickupClass(WorldContextObject);

		AAeyerjiLootPickup* Spawned = AAeyerjiLootPickup::SpawnFromDefinition(
			*World,
			Definition,
			ItemLevel,
			Rarity,
			FTransform(Rotation, SnappedLocation),
			SeedToUse,
			LootPickupClass);

		AJ_LOG(WorldContextObject, TEXT("SpawnLootByDefinition %s Mode=%d Recipient=%s Seed=%d Class=%s Location=%s Result=%s"),
			*GetNameSafe(WorldContextObject),
			static_cast<int32>(DropMode),
			*GetNameSafe(Recipients[Idx]),
			SeedToUse,
			*GetNameSafe(LootPickupClass.Get()),
			*SnappedLocation.ToString(),
			*GetNameSafe(Spawned));

		LastSpawned = Spawned;
	}

	return LastSpawned;
}

AAeyerjiLootPickup* UAeyerjiInventoryBPFL::SpawnLootByInstance(
	UObject* WorldContextObject,
	UAeyerjiItemInstance* ItemInstance,
	FVector Location,
	FRotator Rotation,
	EItemDropDistributionMode DropMode,
	AActor* Instigator)
{
	if (!WorldContextObject || !ItemInstance)
	{
		AJ_LOG(WorldContextObject, TEXT("SpawnLootByInstance aborted - WorldContext=%s Item=%s"),
			*GetNameSafe(WorldContextObject),
			*GetNameSafe(ItemInstance));
		return nullptr;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		AJ_LOG(WorldContextObject, TEXT("SpawnLootByInstance aborted - World=%s NetMode=%d"),
			*GetNameSafe(World),
			World ? static_cast<int32>(World->GetNetMode()) : -1);
		return nullptr;
	}

	TArray<AActor*> Recipients;
	CollectRecipients(World, DropMode, Instigator, Recipients);
	const bool bUniquePerPlayer = (DropMode == EItemDropDistributionMode::DropUniqueItemForEveryPlayer);
	const int32 BaseSeed = (ItemInstance->Seed != 0) ? ItemInstance->Seed : FMath::Rand();

	if (Recipients.Num() == 0)
	{
		Recipients.Add(nullptr);
	}

	const FVector SnappedLocation = SnapDropLocationToGround(World, Location, Instigator);

	AAeyerjiLootPickup* LastSpawned = nullptr;
	for (int32 Idx = 0; Idx < Recipients.Num(); ++Idx)
	{
		// Duplicate the item instance for each recipient so pickups are independent.
		UAeyerjiItemInstance* Copy = DuplicateObject<UAeyerjiItemInstance>(ItemInstance, World);
		if (!Copy)
		{
			continue;
		}

		Copy->Seed = bUniquePerPlayer ? (BaseSeed + Idx + 1) : BaseSeed;

		LastSpawned = SpawnPickupWithInstance(WorldContextObject, World, Copy, FTransform(Rotation, SnappedLocation));

		AJ_LOG(WorldContextObject, TEXT("SpawnLootByInstance %s Mode=%d Recipient=%s Seed=%d Location=%s Result=%s"),
			*GetNameSafe(WorldContextObject),
			static_cast<int32>(DropMode),
			*GetNameSafe(Recipients[Idx]),
			Copy->Seed,
			*SnappedLocation.ToString(),
			*GetNameSafe(LastSpawned));
	}

	return LastSpawned;
}

AAeyerjiLootPickup* UAeyerjiInventoryBPFL::SpawnLootFromResult(
	UObject* WorldContextObject,
	const FLootDropResult& Result,
	FVector Location,
	FRotator Rotation,
	int32 SeedOverride,
	EItemDropDistributionMode DropMode,
	AActor* Instigator)
{
	if (!WorldContextObject)
	{
		AJ_LOG(WorldContextObject, TEXT("SpawnLootFromResult aborted - null WorldContext"));
		return nullptr;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		AJ_LOG(WorldContextObject, TEXT("SpawnLootFromResult aborted - World=%s NetMode=%d"),
			*GetNameSafe(World),
			World ? static_cast<int32>(World->GetNetMode()) : -1);
		return nullptr;
	}

	UItemDefinition* Definition = Result.ItemDefinition;
	if (!Definition && Result.ItemId != NAME_None)
	{
		Definition = UCharacterStatsLibrary::ResolveItemDefinitionById(WorldContextObject, Result.ItemId);
	}

	if (!Definition)
	{
		AJ_LOG(WorldContextObject, TEXT("SpawnLootFromResult aborted - missing item definition (ItemId=%s)"), *Result.ItemId.ToString());
		return nullptr;
	}

	const int32 ItemLevel = FMath::Max(1, Result.ItemLevel);
	const EItemRarity Rarity = Result.Rarity;
	const bool bUniquePerPlayer = (DropMode == EItemDropDistributionMode::DropUniqueItemForEveryPlayer);
	const int32 BaseSeed = (Result.Seed != 0) ? Result.Seed : (SeedOverride != 0 ? SeedOverride : FMath::Rand());

	TArray<AActor*> Recipients;
	CollectRecipients(World, DropMode, Instigator, Recipients);
	if (Recipients.Num() == 0)
	{
		Recipients.Add(nullptr); // fallback: behave like instigator-only
	}

	const FVector SnappedLocation = SnapDropLocationToGround(World, Location, Instigator);

	AAeyerjiLootPickup* LastSpawned = nullptr;
	for (int32 Idx = 0; Idx < Recipients.Num(); ++Idx)
	{
		const int32 Seed = bUniquePerPlayer ? (BaseSeed + Idx + 1) : BaseSeed;

		UAeyerjiItemInstance* Instance = UItemGenerator::RollItemInstance(WorldContextObject, Definition, ItemLevel, Rarity, Seed, Definition->DefaultSlot);
		if (!Instance)
		{
			AJ_LOG(WorldContextObject, TEXT("SpawnLootFromResult failed to roll item instance for %s (ItemId=%s)"), *GetNameSafe(Definition), *Result.ItemId.ToString());
			continue;
		}

		LastSpawned = SpawnPickupWithInstance(WorldContextObject, World, Instance, FTransform(Rotation, SnappedLocation));

		AJ_LOG(WorldContextObject, TEXT("SpawnLootFromResult %s Mode=%d Recipient=%s Seed=%d Location=%s Result=%s"),
			*GetNameSafe(WorldContextObject),
			static_cast<int32>(DropMode),
			*GetNameSafe(Recipients[Idx]),
			Seed,
			*SnappedLocation.ToString(),
			*GetNameSafe(LastSpawned));
	}

	return LastSpawned;
}

void UAeyerjiInventoryBPFL::SpawnMultiDropFromContext(
	UObject* WorldContextObject,
	FLootContext BaseContext,
	const FLootMultiDropConfig& Config,
	FVector Location,
	FRotator Rotation,
	EItemDropDistributionMode DropMode,
	AActor* Instigator)
{
	if (!WorldContextObject)
	{
		AJ_LOG(WorldContextObject, TEXT("SpawnMultiDropFromContext aborted - null WorldContext"));
		return;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		AJ_LOG(WorldContextObject, TEXT("SpawnMultiDropFromContext aborted - World=%s NetMode=%d"),
			*GetNameSafe(World),
			World ? static_cast<int32>(World->GetNetMode()) : -1);
		return;
	}

	ULootService* LootService = UCharacterStatsLibrary::GetLootService(WorldContextObject);
	if (!LootService)
	{
		AJ_LOG(WorldContextObject, TEXT("SpawnMultiDropFromContext aborted - LootService missing"));
		return;
	}

	TArray<FLootDropResult> Results;
	if (!LootService->RollMultiDrop(BaseContext, Config, Results))
	{
		return;
	}

	for (const FLootDropResult& Result : Results)
	{
		SpawnLootFromResult(WorldContextObject, Result, Location, Rotation, /*SeedOverride=*/0, DropMode, Instigator);
	}
}

void UAeyerjiInventoryBPFL::SpawnDistributedLootFromContext(
	UObject* WorldContextObject,
	FLootContext BaseContext,
	FVector Location,
	FRotator Rotation,
	EItemDropDistributionMode DropMode,
	AActor* Instigator)
{
	if (!WorldContextObject)
	{
		AJ_LOG(WorldContextObject, TEXT("SpawnDistributedLootFromContext aborted - null WorldContext"));
		return;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		AJ_LOG(WorldContextObject, TEXT("SpawnDistributedLootFromContext aborted - World=%s NetMode=%d"),
			*GetNameSafe(World),
			World ? static_cast<int32>(World->GetNetMode()) : -1);
		return;
	}

	ULootService* LootService = UCharacterStatsLibrary::GetLootService(WorldContextObject);
	if (!LootService)
	{
		AJ_LOG(WorldContextObject, TEXT("SpawnDistributedLootFromContext aborted - LootService missing"));
		return;
	}

	// If mode is unique per player, roll individually per recipient. Otherwise just spawn via the normal path.
	if (DropMode != EItemDropDistributionMode::DropUniqueItemForEveryPlayer)
	{
		SpawnLootFromResult(WorldContextObject, LootService->RollLoot(BaseContext), Location, Rotation, /*SeedOverride=*/0, DropMode, Instigator);
		return;
	}

	// Unique per player: gather recipients and roll per player so pity/stats are per-player.
	TArray<AActor*> Recipients;
	CollectRecipients(World, DropMode, Instigator, Recipients);
	if (Recipients.Num() == 0)
	{
		Recipients.Add(nullptr);
	}

	for (AActor* Recipient : Recipients)
	{
		FLootContext ContextForPlayer = BaseContext;
		ContextForPlayer.PlayerActor = Recipient ? Recipient : BaseContext.PlayerActor;

		FLootDropResult Result = LootService->RollLoot(ContextForPlayer);

		SpawnLootFromResult(WorldContextObject, Result, Location, Rotation, /*SeedOverride=*/0, EItemDropDistributionMode::DropOnlyForInstigator, Recipient);
	}
}

void UAeyerjiInventoryBPFL::SetAllLootLabelsVisible(UObject* WorldContext, bool bVisible)
{
	if (!WorldContext)
	{
		return;
	}

	if (UWorld* World = WorldContext->GetWorld())
	{
		for (TActorIterator<AAeyerjiLootPickup> It(World); It; ++It)
		{
			It->SetLabelVisible(bVisible);
		}
	}
}

FLinearColor UAeyerjiInventoryBPFL::GetRarityColor(EItemRarity Rarity)
{
	switch (Rarity)
	{
		case EItemRarity::Uncommon:         return FLinearColor(0.10f, 1.00f, 0.10f, 1.f); // vivid green
		case EItemRarity::Rare:             return FLinearColor(0.10f, 0.45f, 1.00f, 1.f); // vivid blue
		case EItemRarity::Epic:             return FLinearColor(0.65f, 0.12f, 1.00f, 1.f); // vivid purple
		case EItemRarity::Pure:             return FLinearColor(1.00f, 0.95f, 0.15f, 1.f); // vivid gold/yellow
		case EItemRarity::Legendary:        return FLinearColor(1.00f, 0.50f, 0.05f, 1.f); // vivid orange
		case EItemRarity::PerfectLegendary: return FLinearColor(1.00f, 0.16f, 0.05f, 1.f); // hot red-orange
		case EItemRarity::Celestial:        return FLinearColor(0.00f, 0.90f, 1.00f, 1.f); // vivid cyan
		default:                            return FLinearColor(0.45f, 0.45f, 0.45f, 1.f);
	}
}

FSlateColor UAeyerjiInventoryBPFL::GetRaritySlateColor(EItemRarity Rarity)
{
	return FSlateColor(GetRarityColor(Rarity));
}

FLootContext UAeyerjiInventoryBPFL::ResolveLootContext(const ULootSourceRuleSet* RuleSet, FLootContext BaseContext, const FGameplayTagContainer& SourceTags)
{
	if (!RuleSet)
	{
		return BaseContext;
	}
	return RuleSet->ResolveContext(BaseContext, SourceTags);
}
