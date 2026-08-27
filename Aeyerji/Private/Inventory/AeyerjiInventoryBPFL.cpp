// AeyerjiInventoryBPFL.cpp

#include "Inventory/AeyerjiInventoryBPFL.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Engine/World.h"
#include "Inventory/AeyerjiLootPickup.h"
#include "Items/InventoryComponent.h"
#include "Items/ItemDefinition.h"
#include "Items/ItemGenerator.h"
#include "Items/ItemInstance.h"
#include "CharacterStatsLibrary.h"
#include "EngineUtils.h"
#include "Logging/AeyerjiLog.h"
#include "Navigation/AeyerjiNavSafetyLibrary.h"
#include "Systems/AeyerjiDifficultyTuning.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Systems/LootService.h"
#include "CollisionQueryParams.h"

namespace
{
	constexpr float MaxLootSpawnDistance = 1000000.f;
	constexpr int32 MaxLootSpawnResults = 1024;
	constexpr int32 MaxLootRecipients = 128;
	constexpr int32 MaxLootActorsForLabelToggle = 100000;

	bool IsFiniteLootSpawnVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	bool IsFiniteLootSpawnRotation(const FRotator& Value)
	{
		return !Value.ContainsNaN();
	}

	bool IsValidLootDropMode(const EItemDropDistributionMode Mode)
	{
		const UEnum* Enum = StaticEnum<EItemDropDistributionMode>();
		return Enum && Enum->IsValidEnumValue(static_cast<int64>(Mode));
	}

	bool IsValidLootSpawnRarity(const EItemRarity Rarity)
	{
		const UEnum* Enum = StaticEnum<EItemRarity>();
		return Enum && Enum->IsValidEnumValue(static_cast<int64>(Rarity));
	}

	int32 DeriveLootRecipientSeed(const int32 BaseSeed, const int32 RecipientIndex)
	{
		const uint32 WrappedSeed = static_cast<uint32>(BaseSeed)
			+ static_cast<uint32>(FMath::Max(0, RecipientIndex)) + 1u;
		return static_cast<int32>(WrappedSeed);
	}

	int32 AddBoundedLootCount(const int32 Current, const int32 Added)
	{
		return static_cast<int32>(FMath::Clamp<int64>(
			static_cast<int64>(Current) + FMath::Max(0, Added),
			0,
			MAX_int32));
	}

	FString AddItemResultToString(const EAeyerjiAddItemResult Result)
	{
		if (const UEnum* ResultEnum = StaticEnum<EAeyerjiAddItemResult>())
		{
			return ResultEnum->GetNameStringByValue(static_cast<int64>(Result));
		}

		return FString::Printf(TEXT("Value_%d"), static_cast<int32>(Result));
	}

	TSubclassOf<AAeyerjiLootPickup> ResolveLootPickupClass(UObject* WorldContextObject, AActor* PreferredRecipient)
	{
		auto ResolveFromInventory = [](const UAeyerjiInventoryComponent* Inventory) -> TSubclassOf<AAeyerjiLootPickup>
		{
			return Inventory && Inventory->GetLootPickupClass()
				? Inventory->GetLootPickupClass()
				: nullptr;
		};

		if (const TSubclassOf<AAeyerjiLootPickup> ConfiguredClass = ResolveFromInventory(Cast<UAeyerjiInventoryComponent>(WorldContextObject)))
		{
			return ConfiguredClass;
		}
		if (const AActor* RecipientActor = PreferredRecipient)
		{
			if (const TSubclassOf<AAeyerjiLootPickup> ConfiguredClass = ResolveFromInventory(RecipientActor->FindComponentByClass<UAeyerjiInventoryComponent>()))
			{
				return ConfiguredClass;
			}
		}
		if (const AActor* ContextActor = Cast<AActor>(WorldContextObject))
		{
			if (const TSubclassOf<AAeyerjiLootPickup> ConfiguredClass = ResolveFromInventory(ContextActor->FindComponentByClass<UAeyerjiInventoryComponent>()))
			{
				return ConfiguredClass;
			}
		}

		// Native is the safe cooked fallback. Projects opt into Blueprint presentation through
		// the owning inventory's LootPickupClass instead of a brittle hard-coded content path.
		return AAeyerjiLootPickup::StaticClass();
	}

	constexpr float MaxLootDropNavDisplacement = 1000.f;

	FAeyerjiNavSafetyResolveParams MakeInventoryLootDropLocalNavParams()
	{
		FAeyerjiNavSafetyResolveParams Params;
		// Loot belongs at the encounter that produced it. A broad nearest-nav search can
		// silently move every reward to an unrelated nav island on large streamed maps.
		Params.ProjectionExtent = FVector(600.f, 600.f, 1200.f);
		Params.SearchRadius = 800.f;
		Params.SearchStep = 100.f;
		Params.AdditionalGroundOffset = 2.f;
		Params.bRequireClearLocation = false;
		return Params;
	}

	FVector ResolveDropLocationToNavOrGround(
		const UObject* WorldContextObject,
		UWorld* World,
		const FVector& DesiredLocation,
		const AActor* ActorToIgnore)
	{
		if (!World || !IsFiniteLootSpawnVector(DesiredLocation))
		{
			return FVector::ZeroVector;
		}

		FAeyerjiNavSafetyResult NavResult;
		if (UAeyerjiNavSafetyLibrary::ResolveNearestNavGroundLocation(
				WorldContextObject ? WorldContextObject : World,
				DesiredLocation,
				MakeInventoryLootDropLocalNavParams(),
				NavResult))
		{
			if (!IsFiniteLootSpawnVector(NavResult.GroundedLocation))
			{
				return DesiredLocation;
			}
			const float NavDisplacementSquared = FVector::DistSquared2D(NavResult.GroundedLocation, DesiredLocation);
			if (NavDisplacementSquared <= FMath::Square(MaxLootDropNavDisplacement))
			{
				return NavResult.GroundedLocation;
			}

			UE_LOG(LogAeyerji, Warning,
				TEXT("[LootReward] Rejected remote nav location Desired=%s Resolved=%s Distance2D=%.1f; trying local ground trace"),
				*DesiredLocation.ToCompactString(),
				*NavResult.GroundedLocation.ToCompactString(),
				FMath::Sqrt(NavDisplacementSquared));
		}

		FCollisionQueryParams Params(SCENE_QUERY_STAT(AeyerjiLootDropSnap), /*bTraceComplex=*/false);
		if (IsValid(ActorToIgnore) && ActorToIgnore->GetWorld() == World)
		{
			Params.AddIgnoredActor(ActorToIgnore);
		}

		const FVector TraceStart = DesiredLocation + FVector(0.f, 0.f, 200.f);
		const FVector TraceEnd = DesiredLocation - FVector(0.f, 0.f, 5000.f);

		FHitResult Hit;
		FCollisionObjectQueryParams ObjectParams;
		ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);

		if (World->LineTraceSingleByObjectType(Hit, TraceStart, TraceEnd, ObjectParams, Params)
			&& Hit.bBlockingHit && IsFiniteLootSpawnVector(Hit.ImpactPoint))
		{
			return Hit.ImpactPoint;
		}

		UE_LOG(LogAeyerji, Warning,
			TEXT("[LootReward] Drop location could not resolve to nav or ground; using requested location Desired=%s ActorToIgnore=%s"),
			*DesiredLocation.ToCompactString(),
			*GetNameSafe(ActorToIgnore));
		return DesiredLocation;
	}

	void CollectRecipients(UWorld* World, EItemDropDistributionMode Mode, AActor* Instigator, TArray<AActor*>& OutRecipients)
	{
		OutRecipients.Reset();
		if (!World || !IsValidLootDropMode(Mode))
		{
			return;
		}
		auto AddRecipient = [World, &OutRecipients](AActor* Recipient)
		{
			if (OutRecipients.Num() < MaxLootRecipients && IsValid(Recipient)
				&& Recipient->GetWorld() == World && !OutRecipients.Contains(Recipient))
			{
				OutRecipients.Add(Recipient);
			}
		};

		switch (Mode)
		{
		case EItemDropDistributionMode::DropOnlyForInstigator:
			AddRecipient(Instigator);
			break;

		case EItemDropDistributionMode::DropIdenticalItemForEveryPlayer:
		case EItemDropDistributionMode::DropUniqueItemForEveryPlayer:
			if (AGameStateBase* GS = World->GetGameState())
			{
				const int32 PlayerCount = FMath::Min(GS->PlayerArray.Num(), MaxLootRecipients);
				for (int32 PlayerIndex = 0; PlayerIndex < PlayerCount; ++PlayerIndex)
				{
					APlayerState* PS = GS->PlayerArray[PlayerIndex];
					if (IsValid(PS) && PS->GetWorld() == World)
					{
						AddRecipient(PS->GetPawn());
					}
				}
			}
			break;
		default:
			break;
		}
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

	int32 ResolveDirectSpawnItemLevel(int32 RequestedItemLevel, AActor* RecipientOrInstigator)
	{
		const int32 ActorLevel = ResolveActorGameplayLevel(RecipientOrInstigator);
		if (RequestedItemLevel <= 1 && ActorLevel > 1)
		{
			return ActorLevel;
		}

		return UAeyerjiDifficultySettings::ClampGameplayLevel(RequestedItemLevel);
	}

	int32 DeriveSeedForRecipient(const FLootDropResult& Result, int32 SeedOverride, int32 RecipientIndex, bool bUniquePerPlayer)
	{
		const int32 BaseSeed = (Result.Seed != 0) ? Result.Seed : (SeedOverride != 0 ? SeedOverride : FMath::Rand());
		return bUniquePerPlayer ? DeriveLootRecipientSeed(BaseSeed, RecipientIndex) : BaseSeed;
	}

	AAeyerjiLootPickup* SpawnPickupWithInstance(
		UObject* WorldContextObject,
		UWorld* World,
		UAeyerjiItemInstance* ItemInstance,
		const FTransform& SpawnTransform,
		AActor* PreferredRecipient)
	{
		if (!ItemInstance)
		{
			return nullptr;
		}

		const TSubclassOf<AAeyerjiLootPickup> LootPickupClass = ResolveLootPickupClass(WorldContextObject, PreferredRecipient);

		return AAeyerjiLootPickup::SpawnFromInstance(
			*World,
			ItemInstance,
			SpawnTransform,
			LootPickupClass);
	}

	FVector ResolveBatchSpawnLocation(
		const FVector& BaseLocation,
		const int32 ResultIndex,
		const int32 ResultCount,
		const float ScatterRadius,
		const float ScatterYawOffset)
	{
		if (ResultCount <= 1 || ScatterRadius <= KINDA_SMALL_NUMBER)
		{
			return BaseLocation;
		}

		const float SafeYawOffset = FMath::IsFinite(ScatterYawOffset) ? FMath::UnwindDegrees(ScatterYawOffset) : 0.f;
		const float AngleDegrees = SafeYawOffset + (360.f * static_cast<float>(ResultIndex) / static_cast<float>(ResultCount));
		const float AngleRadians = FMath::DegreesToRadians(AngleDegrees);
		const FVector ScatterOffset(FMath::Cos(AngleRadians) * ScatterRadius, FMath::Sin(AngleRadians) * ScatterRadius, 0.f);
		return BaseLocation + ScatterOffset;
	}

	void AccumulateLootSpawnSummary(FAeyerjiLootSpawnSummary& Total, const FAeyerjiLootSpawnSummary& Added)
	{
		Total.RequestedResultCount = AddBoundedLootCount(Total.RequestedResultCount, Added.RequestedResultCount);
		Total.SpawnedPickupCount = AddBoundedLootCount(Total.SpawnedPickupCount, Added.SpawnedPickupCount);
		Total.FailedSpawnCount = AddBoundedLootCount(Total.FailedSpawnCount, Added.FailedSpawnCount);
		if (Added.LastSpawnedPickup)
		{
			Total.LastSpawnedPickup = Added.LastSpawnedPickup;
		}
		const int32 Available = MaxLootSpawnResults * MaxLootRecipients - Total.SpawnedPickups.Num();
		const int32 CopyCount = FMath::Min(Added.SpawnedPickups.Num(), FMath::Max(0, Available));
		for (int32 Index = 0; Index < CopyCount; ++Index)
		{
			Total.SpawnedPickups.Add(Added.SpawnedPickups[Index]);
		}
	}

	FAeyerjiLootSpawnSummary SpawnLootFromResultInternal(
		UObject* WorldContextObject,
		const FLootDropResult& Result,
		FVector Location,
		FRotator Rotation,
		int32 SeedOverride,
		EItemDropDistributionMode DropMode,
		AActor* Instigator)
	{
		FAeyerjiLootSpawnSummary Summary;
		Summary.RequestedResultCount = 1;

		if (!WorldContextObject)
		{
			AJ_LOG(WorldContextObject, TEXT("[LootReward] SpawnLootFromResult aborted - null WorldContext"));
			++Summary.FailedSpawnCount;
			return Summary;
		}

		UWorld* World = WorldContextObject->GetWorld();
		if (!World || World->GetNetMode() == NM_Client || !IsFiniteLootSpawnVector(Location)
			|| !IsFiniteLootSpawnRotation(Rotation) || !IsValidLootDropMode(DropMode)
			|| !IsValidLootSpawnRarity(Result.Rarity)
			|| (Instigator && (!IsValid(Instigator) || Instigator->GetWorld() != World)))
		{
			AJ_LOG(WorldContextObject, TEXT("[LootReward] SpawnLootFromResult aborted - World=%s NetMode=%d"),
				*GetNameSafe(World),
				World ? static_cast<int32>(World->GetNetMode()) : -1);
			++Summary.FailedSpawnCount;
			return Summary;
		}

		UItemDefinition* Definition = IsValid(Result.ItemDefinition) ? Result.ItemDefinition.Get() : nullptr;
		if (!Definition && Result.ItemDefinitionKey != NAME_None)
		{
			Definition = UCharacterStatsLibrary::ResolveItemDefinitionByKey(WorldContextObject, Result.ItemDefinitionKey);
		}

		if (!IsValid(Definition))
		{
			AJ_LOG(WorldContextObject, TEXT("[LootReward] SpawnLootFromResult aborted - missing item definition DefinitionKey=%s"),
				*Result.ItemDefinitionKey.ToString());
			++Summary.FailedSpawnCount;
			return Summary;
		}

		const EItemRarity Rarity = Result.Rarity;
		const bool bUniquePerPlayer = (DropMode == EItemDropDistributionMode::DropUniqueItemForEveryPlayer);
		const int32 BaseSeed = (Result.Seed != 0) ? Result.Seed : (SeedOverride != 0 ? SeedOverride : FMath::Rand());

		TArray<AActor*> Recipients;
		CollectRecipients(World, DropMode, Instigator, Recipients);
		if (Recipients.Num() == 0)
		{
			Recipients.Add(nullptr);
		}

		const FVector SnappedLocation = ResolveDropLocationToNavOrGround(WorldContextObject, World, Location, Instigator);

		for (int32 Idx = 0; Idx < Recipients.Num(); ++Idx)
		{
			AActor* Recipient = Recipients[Idx] ? Recipients[Idx] : Instigator;
			const int32 ItemLevel = ResolveDirectSpawnItemLevel(Result.ItemLevel, Recipient);
			if (ItemLevel < Definition->GetEffectiveRequiredLevel())
			{
				AJ_LOG(WorldContextObject, TEXT("[LootReward] SpawnLootFromResult rejected - Def=%s RequestedLevel=%d ResolvedLevel=%d Recipient=%s RequiredLevel=%d"),
					*GetNameSafe(Definition),
					Result.ItemLevel,
					ItemLevel,
					*GetNameSafe(Recipient),
					Definition->GetEffectiveRequiredLevel());
				++Summary.FailedSpawnCount;
				continue;
			}

			const int32 Seed = bUniquePerPlayer ? DeriveLootRecipientSeed(BaseSeed, Idx) : BaseSeed;
			UAeyerjiItemInstance* Instance = UItemGenerator::RollItemInstance(WorldContextObject, Definition, ItemLevel, Rarity, Seed, Definition->DefaultSlot);
			if (!Instance)
			{
				AJ_LOG(WorldContextObject, TEXT("[LootReward] SpawnLootFromResult failed to roll item instance for %s DefinitionKey=%s Recipient=%s"),
					*GetNameSafe(Definition),
					*Result.ItemDefinitionKey.ToString(),
					*GetNameSafe(Recipient));
				++Summary.FailedSpawnCount;
				continue;
			}

			AAeyerjiLootPickup* SpawnedPickup = SpawnPickupWithInstance(WorldContextObject, World, Instance, FTransform(Rotation, SnappedLocation), Recipient);
			if (SpawnedPickup)
			{
				++Summary.SpawnedPickupCount;
				Summary.LastSpawnedPickup = SpawnedPickup;
				Summary.SpawnedPickups.Add(SpawnedPickup);
			}
			else
			{
				++Summary.FailedSpawnCount;
			}

			AJ_LOG(WorldContextObject, TEXT("[LootReward] SpawnLootFromResult Context=%s Mode=%d Recipient=%s Seed=%d Location=%s Pickup=%s"),
				*GetNameSafe(WorldContextObject),
				static_cast<int32>(DropMode),
				*GetNameSafe(Recipient),
				Seed,
				*SnappedLocation.ToString(),
				*GetNameSafe(SpawnedPickup));
		}

		return Summary;
	}
}

bool UAeyerjiInventoryBPFL::ToggleEquipState(UAeyerjiInventoryComponent* Inventory, UAeyerjiItemInstance* ItemInstance)
{
	if (!IsValid(Inventory) || !IsValid(ItemInstance) || !ItemInstance->UniqueId.IsValid()
		|| Inventory->FindItemById(ItemInstance->UniqueId) != ItemInstance)
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
	if (!IsValid(Inventory) || !IsValid(ItemInstance) || !ItemInstance->UniqueId.IsValid()
		|| Inventory->FindItemById(ItemInstance->UniqueId) != ItemInstance
		|| !FMath::IsFinite(ForwardOffset))
	{
		return false;
	}

	Inventory->DropItemAtOwner(ItemInstance->UniqueId, FMath::Clamp(ForwardOffset, -MaxLootSpawnDistance, MaxLootSpawnDistance));
	return true;
}

EAeyerjiAddItemResult UAeyerjiInventoryBPFL::EquipFirstThenBag(
	UAeyerjiInventoryComponent* Inventory,
	UAeyerjiItemInstance* ItemInstance)
{
	if (!IsValid(Inventory) || !IsValid(Inventory->GetOwner()) || Inventory->GetOwnerRole() != ROLE_Authority)
	{
		UE_LOG(LogTemp, Warning, TEXT("[InventoryPickup] EquipFirstThenBag failed: null inventory Item=%s"),
			*GetNameSafe(ItemInstance));
		return EAeyerjiAddItemResult::Failed_NoInventory;
	}

	if (!IsValid(ItemInstance))
	{
		UE_LOG(LogTemp, Warning, TEXT("[InventoryPickup] EquipFirstThenBag failed: null item Inventory=%s"),
			*GetNameSafe(Inventory));
		return EAeyerjiAddItemResult::Failed_NoItem;
	}

	if (!IsValid(ItemInstance->Definition))
	{
		UE_LOG(LogTemp, Display, TEXT("[InventoryPickup] EquipFirstThenBag failed: item %s has no definition UniqueId=%s"),
			*GetNameSafe(ItemInstance),
			ItemInstance->UniqueId.IsValid() ? *ItemInstance->UniqueId.ToString() : TEXT("Invalid"));
		return EAeyerjiAddItemResult::Failed_MissingDefinition;
	}

	const bool bAlreadyOwned = Inventory->FindItemById(ItemInstance->UniqueId) != nullptr;
	const int32 OwnerLevel = Inventory->GetOwnerLevelForInventoryRules();
	ItemInstance->ItemLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(ItemInstance->ItemLevel);
	const int32 ItemLevel = ItemInstance->ItemLevel;

	UE_LOG(LogTemp, Display, TEXT("[InventoryPickup] EquipFirstThenBag start Inventory=%s Owner=%s Item=%s UniqueId=%s Def=%s ItemLevel=%d OwnerLevel=%d AlreadyOwned=%d Grid=(%d,%d)"),
		*GetNameSafe(Inventory),
		*GetNameSafe(Inventory->GetOwner()),
		*GetNameSafe(ItemInstance),
		ItemInstance->UniqueId.IsValid() ? *ItemInstance->UniqueId.ToString() : TEXT("Invalid"),
		*GetNameSafe(ItemInstance->Definition.Get()),
		ItemLevel,
		OwnerLevel,
		bAlreadyOwned ? 1 : 0,
		Inventory->GetGridSize().X,
		Inventory->GetGridSize().Y);

	// Ensure the item lives inside the inventory component.
	if (!Inventory->AddItemInstance(ItemInstance, true))
	{
		UE_LOG(LogTemp, Warning, TEXT("[InventoryPickup] EquipFirstThenBag failed: AddItemInstance rejected item %s"),
			*GetNameSafe(ItemInstance));
		return EAeyerjiAddItemResult::Failed_BagFull;
	}

	const EEquipmentSlot PreferredSlot = ItemInstance->Definition->DefaultSlot;

	Inventory->Server_EquipItem(ItemInstance->UniqueId, PreferredSlot, INDEX_NONE);
	if (ItemInstance->EquippedSlotIndex != INDEX_NONE)
	{
		UE_LOG(LogTemp, Display, TEXT("[InventoryPickup] EquipFirstThenBag result=%s Item=%s Slot=%d Index=%d"),
			*AddItemResultToString(EAeyerjiAddItemResult::Equipped),
			*ItemInstance->UniqueId.ToString(),
			static_cast<int32>(ItemInstance->EquippedSlot),
			ItemInstance->EquippedSlotIndex);
		return EAeyerjiAddItemResult::Equipped;
	}

	UE_LOG(LogTemp, Display, TEXT("[InventoryPickup] EquipFirstThenBag equip rejected for %s PreferredSlot=%d ItemLevel=%d OwnerLevel=%d; trying bag."),
		*ItemInstance->UniqueId.ToString(),
		static_cast<int32>(PreferredSlot),
		ItemLevel,
		OwnerLevel);

	if (Inventory->AutoPlaceItem(ItemInstance))
	{
		FInventoryItemGridData Placement;
		const bool bHasPlacement = Inventory->GetPlacementForItem(ItemInstance->UniqueId, Placement);
		UE_LOG(LogTemp, Display, TEXT("[InventoryPickup] EquipFirstThenBag result=%s Item=%s Placement=%s"),
			*AddItemResultToString(EAeyerjiAddItemResult::Bagged),
			*ItemInstance->UniqueId.ToString(),
			bHasPlacement ? *FString::Printf(TEXT("(%d,%d)"), Placement.TopLeft.X, Placement.TopLeft.Y) : TEXT("None"));
		return EAeyerjiAddItemResult::Bagged;
	}

	if (!bAlreadyOwned)
	{
		Inventory->Server_RemoveItemById(ItemInstance->UniqueId);
	}

	const EAeyerjiAddItemResult Failure = ItemLevel > OwnerLevel
		? EAeyerjiAddItemResult::Failed_LevelTooHigh
		: EAeyerjiAddItemResult::Failed_BagPlacementFailed;
	UE_LOG(LogTemp, Warning, TEXT("[InventoryPickup] EquipFirstThenBag result=%s Item=%s ItemLevel=%d OwnerLevel=%d Grid=(%d,%d)"),
		*AddItemResultToString(Failure),
		*GetNameSafe(ItemInstance),
		ItemLevel,
		OwnerLevel,
		Inventory->GetGridSize().X,
		Inventory->GetGridSize().Y);
	return Failure;
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
	if (!IsValid(WorldContextObject) || !IsValid(Definition))
	{
		AJ_LOG(WorldContextObject, TEXT("SpawnLootByDefinition aborted - WorldContext=%s Definition=%s"),
			*GetNameSafe(WorldContextObject),
		*GetNameSafe(Definition));
		return nullptr;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !IsFiniteLootSpawnVector(Location)
		|| !IsFiniteLootSpawnRotation(Rotation) || !IsValidLootDropMode(DropMode)
		|| !IsValidLootSpawnRarity(Rarity)
		|| (Instigator && (!IsValid(Instigator) || Instigator->GetWorld() != World)))
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

	const FVector SnappedLocation = ResolveDropLocationToNavOrGround(WorldContextObject, World, Location, Instigator);

	AAeyerjiLootPickup* LastSpawned = nullptr;
	for (int32 Idx = 0; Idx < Recipients.Num(); ++Idx)
	{
		AActor* Recipient = Recipients[Idx] ? Recipients[Idx] : Instigator;
		const int32 ResolvedItemLevel = ResolveDirectSpawnItemLevel(ItemLevel, Recipient);
		if (ResolvedItemLevel < Definition->GetEffectiveRequiredLevel())
		{
			AJ_LOG(WorldContextObject, TEXT("SpawnLootByDefinition rejected - Def=%s RequestedLevel=%d ResolvedLevel=%d Recipient=%s RequiredLevel=%d"),
				*GetNameSafe(Definition),
				ItemLevel,
				ResolvedItemLevel,
				*GetNameSafe(Recipient),
				Definition->GetEffectiveRequiredLevel());
			continue;
		}

		const int32 SeedToUse = bUniquePerPlayer ? DeriveLootRecipientSeed(BaseSeed, Idx) : BaseSeed;

		const TSubclassOf<AAeyerjiLootPickup> LootPickupClass = ResolveLootPickupClass(WorldContextObject, Recipient);

		AAeyerjiLootPickup* Spawned = AAeyerjiLootPickup::SpawnFromDefinition(
			*World,
			Definition,
			ResolvedItemLevel,
			Rarity,
			FTransform(Rotation, SnappedLocation),
			SeedToUse,
			LootPickupClass);

		AJ_LOG(WorldContextObject, TEXT("SpawnLootByDefinition %s Mode=%d Recipient=%s RequestedLevel=%d ResolvedLevel=%d Seed=%d Class=%s Location=%s Result=%s"),
			*GetNameSafe(WorldContextObject),
			static_cast<int32>(DropMode),
			*GetNameSafe(Recipient),
			ItemLevel,
			ResolvedItemLevel,
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
	if (!IsValid(WorldContextObject) || !IsValid(ItemInstance) || !IsValid(ItemInstance->Definition)
		|| !IsValidLootSpawnRarity(ItemInstance->Rarity))
	{
		AJ_LOG(WorldContextObject, TEXT("SpawnLootByInstance aborted - WorldContext=%s Item=%s"),
			*GetNameSafe(WorldContextObject),
			*GetNameSafe(ItemInstance));
		return nullptr;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !IsFiniteLootSpawnVector(Location)
		|| !IsFiniteLootSpawnRotation(Rotation) || !IsValidLootDropMode(DropMode)
		|| (Instigator && (!IsValid(Instigator) || Instigator->GetWorld() != World)))
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

	const FVector SnappedLocation = ResolveDropLocationToNavOrGround(WorldContextObject, World, Location, Instigator);

	AAeyerjiLootPickup* LastSpawned = nullptr;
	for (int32 Idx = 0; Idx < Recipients.Num(); ++Idx)
	{
		// Duplicate the item instance for each recipient so pickups are independent.
		UAeyerjiItemInstance* Copy = DuplicateObject<UAeyerjiItemInstance>(ItemInstance, World);
		if (!Copy)
		{
			continue;
		}

		Copy->ItemLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(Copy->ItemLevel);
		Copy->Seed = bUniquePerPlayer ? DeriveLootRecipientSeed(BaseSeed, Idx) : BaseSeed;

		AActor* Recipient = Recipients[Idx] ? Recipients[Idx] : Instigator;
		LastSpawned = SpawnPickupWithInstance(WorldContextObject, World, Copy, FTransform(Rotation, SnappedLocation), Recipient);

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
	const FAeyerjiLootSpawnSummary Summary = SpawnLootFromResultInternal(
		WorldContextObject,
		Result,
		Location,
		Rotation,
		SeedOverride,
		DropMode,
		Instigator);
	return Summary.LastSpawnedPickup;
}

FAeyerjiLootSpawnSummary UAeyerjiInventoryBPFL::SpawnLootResults(
	UObject* WorldContextObject,
	const TArray<FLootDropResult>& Results,
	FVector Location,
	FRotator Rotation,
	int32 SeedOverride,
	EItemDropDistributionMode DropMode,
	AActor* Instigator,
	float LootReleaseScatterRadius,
	float LootReleaseScatterYawOffset)
{
	FAeyerjiLootSpawnSummary Summary;
	if (Results.IsEmpty() || !IsValid(WorldContextObject) || !IsFiniteLootSpawnVector(Location)
		|| !IsFiniteLootSpawnRotation(Rotation) || !IsValidLootDropMode(DropMode)
		|| !FMath::IsFinite(LootReleaseScatterRadius) || !FMath::IsFinite(LootReleaseScatterYawOffset))
	{
		AJ_LOG(WorldContextObject, TEXT("[LootReward] SpawnLootResults skipped - no results Context=%s"),
			*GetNameSafe(WorldContextObject));
		return Summary;
	}

	const int32 ResultCount = FMath::Min(Results.Num(), MaxLootSpawnResults);
	const float SafeScatterRadius = FMath::Clamp(LootReleaseScatterRadius, 0.f, MaxLootSpawnDistance);
	const int32 ExcessResultCount = FMath::Max(0, Results.Num() - ResultCount);
	Summary.RequestedResultCount = ExcessResultCount;
	Summary.FailedSpawnCount = ExcessResultCount;
	for (int32 ResultIndex = 0; ResultIndex < ResultCount; ++ResultIndex)
	{
		const FVector SpawnLocation = ResolveBatchSpawnLocation(
			Location,
			ResultIndex,
			ResultCount,
			SafeScatterRadius,
			LootReleaseScatterYawOffset);

		const FAeyerjiLootSpawnSummary ResultSummary = SpawnLootFromResultInternal(
			WorldContextObject,
			Results[ResultIndex],
			SpawnLocation,
			Rotation,
			SeedOverride,
			DropMode,
			Instigator);
		AccumulateLootSpawnSummary(Summary, ResultSummary);
	}

	AJ_LOG(WorldContextObject, TEXT("[LootReward] SpawnLootResults complete Context=%s RequestedResults=%d SpawnedPickups=%d FailedSpawns=%d DropMode=%d ScatterRadius=%.1f Location=%s"),
		*GetNameSafe(WorldContextObject),
		Summary.RequestedResultCount,
		Summary.SpawnedPickupCount,
		Summary.FailedSpawnCount,
		static_cast<int32>(DropMode),
		SafeScatterRadius,
		*Location.ToCompactString());
	return Summary;
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
	if (!IsValid(WorldContextObject) || !IsFiniteLootSpawnVector(Location)
		|| !IsFiniteLootSpawnRotation(Rotation) || !IsValidLootDropMode(DropMode))
	{
		AJ_LOG(WorldContextObject, TEXT("[LootReward] SpawnMultiDropFromContext aborted - null WorldContext"));
		return;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World || World->GetNetMode() == NM_Client
		|| (Instigator && (!IsValid(Instigator) || Instigator->GetWorld() != World)))
	{
		AJ_LOG(WorldContextObject, TEXT("[LootReward] SpawnMultiDropFromContext aborted - World=%s NetMode=%d"),
			*GetNameSafe(World),
			World ? static_cast<int32>(World->GetNetMode()) : -1);
		return;
	}

	ULootService* LootService = UCharacterStatsLibrary::GetLootService(WorldContextObject);
	if (!LootService)
	{
		AJ_LOG(WorldContextObject, TEXT("[LootReward] SpawnMultiDropFromContext aborted - LootService missing"));
		return;
	}

	if (!BaseContext.PlayerActor.IsValid() && Instigator)
	{
		BaseContext.PlayerActor = Instigator;
	}

	TArray<FLootDropResult> Results;
	if (!LootService->RollMultiDrop(BaseContext, Config, Results))
	{
		return;
	}

	const FAeyerjiLootSpawnSummary Summary = SpawnLootResults(
		WorldContextObject,
		Results,
		Location,
		Rotation,
		/*SeedOverride=*/0,
		DropMode,
		Instigator);
	AJ_LOG(WorldContextObject, TEXT("[LootReward] SpawnMultiDropFromContext SourceTag=%s RolledResults=%d SpawnedPickups=%d FailedSpawns=%d Buckets=%d Location=%s"),
		*BaseContext.SourceTag.ToString(),
		Results.Num(),
		Summary.SpawnedPickupCount,
		Summary.FailedSpawnCount,
		Config.Buckets.Num(),
		*Location.ToCompactString());
}

void UAeyerjiInventoryBPFL::SpawnDistributedLootFromContext(
	UObject* WorldContextObject,
	FLootContext BaseContext,
	FVector Location,
	FRotator Rotation,
	EItemDropDistributionMode DropMode,
	AActor* Instigator)
{
	if (!IsValid(WorldContextObject) || !IsFiniteLootSpawnVector(Location)
		|| !IsFiniteLootSpawnRotation(Rotation) || !IsValidLootDropMode(DropMode))
	{
		AJ_LOG(WorldContextObject, TEXT("SpawnDistributedLootFromContext aborted - null WorldContext"));
		return;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World || World->GetNetMode() == NM_Client
		|| (Instigator && (!IsValid(Instigator) || Instigator->GetWorld() != World)))
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

	if (!BaseContext.PlayerActor.IsValid() && Instigator)
	{
		BaseContext.PlayerActor = Instigator;
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
		int32 UpdatedActorCount = 0;
		for (TActorIterator<AAeyerjiLootPickup> It(World); It && UpdatedActorCount < MaxLootActorsForLabelToggle; ++It)
		{
			if (IsValid(*It))
			{
				It->SetLabelVisible(bVisible);
				++UpdatedActorCount;
			}
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
