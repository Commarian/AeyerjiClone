// InventoryComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayEffectTypes.h"
#include "GameplayEffect.h"
#include "GameplayAbilitySpec.h"
#include "TimerManager.h"
#include "Items/ItemInstance.h"

#include "InventoryComponent.generated.h"

class UAbilitySystemComponent;
class UGameplayEffect;
class UActorChannel;
class FOutBunch;
struct FReplicationFlags;
class AAeyerjiLootPickup;
class UAeyerjiLootTable;

USTRUCT()
struct AEYERJI_API FItemActiveEffectSet
{
	GENERATED_BODY()

	UPROPERTY()
	FActiveGameplayEffectHandle StatsHandle;

	UPROPERTY()
	bool bAppliedItemStats = false;

	UPROPERTY()
	TArray<FActiveGameplayEffectHandle> AdditionalHandles;

	UPROPERTY()
	TArray<FGameplayAbilitySpecHandle> GrantedAbilityHandles;

	UPROPERTY()
	TArray<FGameplayTag> AddedOwnedTags;
};

USTRUCT(BlueprintType)
struct AEYERJI_API FEquippedItemEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Inventory")
	EEquipmentSlot Slot = EEquipmentSlot::Assault;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Inventory")
	int32 SlotIndex = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Inventory")
	FGuid ItemId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Instanced, Category = "Inventory")
	TObjectPtr<UAeyerjiItemInstance> Item = nullptr;

	bool IsValid() const { return Item != nullptr; }
};

USTRUCT(BlueprintType)
struct AEYERJI_API FInventoryItemGridData
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Inventory")
	FGuid ItemId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Inventory")
	FIntPoint TopLeft = FIntPoint::ZeroValue;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, SaveGame, Category = "Inventory")
	FIntPoint Size = FIntPoint(1, 1);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Instanced, Category = "Inventory")
	TObjectPtr<UAeyerjiItemInstance> ItemInstance = nullptr;

	bool IsValid() const { return ItemId.IsValid() || ItemInstance != nullptr; }
};

USTRUCT()
struct AEYERJI_API FInventoryItemSnapshot
{
	GENERATED_BODY()

	UPROPERTY(SaveGame)
	FGuid ItemId;

	UPROPERTY(SaveGame)
	TObjectPtr<UItemDefinition> Definition = nullptr;

	UPROPERTY(SaveGame)
	FName DefinitionKey = NAME_None;

	UPROPERTY(SaveGame)
	EItemRarity Rarity = EItemRarity::Common;

	UPROPERTY(SaveGame)
	int32 ItemLevel = 1;

	UPROPERTY(SaveGame)
	int32 Seed = 0;

	UPROPERTY(SaveGame)
	TArray<FRolledAffix> RolledAffixes;

	UPROPERTY(SaveGame)
	TArray<FItemStatModifier> FinalAggregatedModifiers;

	UPROPERTY(SaveGame)
	TArray<FItemGrantedEffect> GrantedEffects;

	UPROPERTY(SaveGame)
	TArray<FItemGrantedAbility> GrantedAbilities;

	UPROPERTY(SaveGame)
	EEquipmentSlot EquippedSlot = EEquipmentSlot::Assault;

	UPROPERTY(SaveGame)
	int32 SlotIndex = INDEX_NONE;

	UPROPERTY(SaveGame)
	FIntPoint InventorySize = FIntPoint(1, 1);
};

USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiInventorySaveData
{
	GENERATED_BODY()

	UPROPERTY(SaveGame)
	TArray<FInventoryItemSnapshot> ItemSnapshots;

	UPROPERTY(SaveGame)
	TArray<FInventoryItemGridData> GridPlacements;

	UPROPERTY(SaveGame)
	TArray<FEquippedItemEntry> EquippedItems;

	UPROPERTY(SaveGame)
	int32 GridColumns = 8;

	UPROPERTY(SaveGame)
	int32 GridRows = 4;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnInventoryChanged);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnEquippedItemChanged, EEquipmentSlot, Slot, int32, SlotIndex, UAeyerjiItemInstance*, Item);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(FOnEquipmentSlotUnlocksChanged, int32, PlayerLevel, int32, NormalLaneSlots, int32, CorruptionSlots, bool, bCorruptionUnlocked);

UENUM(BlueprintType)
enum class EInventoryItemStateChange : uint8
{
	Added,
	Removed,
	Equipped,
	Unequipped,
	Dropped
};

USTRUCT(BlueprintType)
struct FInventoryItemChangeEvent
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	EInventoryItemStateChange Change = EInventoryItemStateChange::Added;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	FGuid ItemId;

	UPROPERTY(BlueprintReadOnly, Instanced, Category = "Inventory")
	TObjectPtr<UAeyerjiItemInstance> Item = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	EEquipmentSlot Slot = EEquipmentSlot::Assault;

	UPROPERTY(BlueprintReadOnly, Category = "Inventory")
	int32 SlotIndex = INDEX_NONE;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInventoryItemStateChanged, const FInventoryItemChangeEvent&, EventData);

/**
 * Network-aware inventory component that tracks owned items and equipped slots.
 */
UCLASS(BlueprintType, Blueprintable, ClassGroup = (Aeyerji), meta = (BlueprintSpawnableComponent))
class AEYERJI_API UAeyerjiInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAeyerjiInventoryComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Instanced, Category = "Inventory")
	TArray<TObjectPtr<UAeyerjiItemInstance>> Items;

	UPROPERTY(ReplicatedUsing = OnRep_EquippedItems, VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	TArray<FEquippedItemEntry> EquippedItems;

	UPROPERTY()
	TMap<FGuid, FItemActiveEffectSet> ActiveEffectHandles;

	UPROPERTY(ReplicatedUsing = OnRep_GridPlacements, VisibleAnywhere, BlueprintReadOnly, Category = "Inventory|Grid")
	TArray<FInventoryItemGridData> GridPlacements;

	UPROPERTY(ReplicatedUsing = OnRep_ItemSnapshots)
	TArray<FInventoryItemSnapshot> ItemSnapshots;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_GridSize, Category = "Inventory|Grid")
	int32 GridColumns = 8;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_GridSize, Category = "Inventory|Grid")
	int32 GridRows = 4;

	/** Maximum number of slots allowed for each normal lane (Assault, Guard, Flow). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Equipment", meta = (ClampMin = "5", UIMin = "5"))
	int32 SlotsPerEquipmentCategory = 5;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Inventory|Loot")
	TSubclassOf<AAeyerjiLootPickup> LootPickupClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory|GAS")
	TSubclassOf<UGameplayEffect> ItemStatsEffectClass;

	UFUNCTION(BlueprintPure, Category = "Inventory")
	UAbilitySystemComponent* GetASC() const;

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Inventory")
	void Server_AddItem(UAeyerjiItemInstance* Item);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool AddItemInstance(UAeyerjiItemInstance* Item, bool bSkipAutoPlacement = false);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Inventory")
	void Server_RemoveItemById(const FGuid& ItemId);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Inventory")
	void Server_EquipItem(const FGuid& ItemId, EEquipmentSlot Slot, int32 SlotIndex = -1);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Inventory")
	void Server_UnequipSlot(EEquipmentSlot Slot, int32 SlotIndex);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Inventory")
	void Server_UnequipSlotToGrid(EEquipmentSlot Slot, int32 SlotIndex, FIntPoint PreferredTopLeft);

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	UAeyerjiItemInstance* FindItemById(const FGuid& ItemId) const;

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	UAeyerjiItemInstance* GetEquipped(EEquipmentSlot Slot, int32 SlotIndex = 0) const;

	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void GetAllEquippedItems(TArray<FEquippedItemEntry>& OutEquipped) const { OutEquipped = EquippedItems; }
	/** How many equipped items share the same definition as the given item. */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Synergy")
	int32 CountEquippedWithSameDefinition(const UAeyerjiItemInstance* ReferenceItem) const;

	/**
	 * Compute synergy info for an equipped item.
	 * Returns true when a synergy color should override the base FX color.
	 */
	bool GetEquipSynergyForItem(
		const UAeyerjiItemInstance* ReferenceItem,
		int32& OutStackCount,
		FLinearColor& OutColor,
		FName& OutColorParam) const;

	UFUNCTION(BlueprintCallable, Category = "Inventory|Grid")
	FIntPoint GetGridSize() const { return FIntPoint(GridColumns, GridRows); }

	UFUNCTION(BlueprintPure, Category = "Inventory|Loot")
	TSubclassOf<AAeyerjiLootPickup> GetLootPickupClass() const { return LootPickupClass; }

	UFUNCTION(BlueprintCallable, Category = "Inventory|Grid")
	void GetGridPlacements(TArray<FInventoryItemGridData>& OutPlacements) const { OutPlacements = GridPlacements; }

	UFUNCTION(BlueprintCallable, Category = "Inventory|Grid")
	bool GetPlacementForItem(const FGuid& ItemId, FInventoryItemGridData& OutPlacement) const;

	/** Returns the owner level used by inventory equip restrictions. */
	UFUNCTION(BlueprintPure, Category = "Inventory|Equipment")
	int32 GetOwnerLevelForInventoryRules() const;

	/** Returns the normal-lane slot count unlocked by the supplied player level. */
	UFUNCTION(BlueprintPure, Category = "Inventory|Equipment")
	static int32 GetNormalEquipmentSlotCountForLevel(int32 PlayerLevel, int32 MaxNormalSlots = 5);

#if WITH_DEV_AUTOMATION_TESTS
	void SetAutomationOwnerLevelForInventoryRules(int32 InLevel);
#endif

	/** Returns how many slot widgets should be visible for this lane. Corruption can be visible while locked. */
	UFUNCTION(BlueprintPure, Category = "Inventory|Equipment")
	int32 GetVisibleEquipmentSlotCount(EEquipmentSlot Slot) const;

	UFUNCTION(BlueprintPure, Category = "Inventory|Equipment")
	int32 GetUnlockedEquipmentSlotCount(EEquipmentSlot Slot) const;

	UFUNCTION(BlueprintPure, Category = "Inventory|Equipment")
	bool IsEquipmentSlotUnlocked(EEquipmentSlot Slot, int32 SlotIndex) const;

	UFUNCTION(BlueprintPure, Category = "Inventory|Equipment")
	bool CanEquipItemInSlot(const UAeyerjiItemInstance* Item, EEquipmentSlot Slot, int32 SlotIndex) const;

	/** Forces the equipment slot unlock delegate to fire using the current replicated player level. Useful after UI binds. */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Equipment")
	void RefreshEquipmentSlotUnlockState();

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Inventory|Grid")
	void Server_MoveItemInGrid(const FGuid& ItemId, FIntPoint NewTopLeft);

	/** Swap two items in the grid if both fit in each other's positions. */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Inventory|Grid")
	void Server_SwapItemsInGrid(const FGuid& ItemIdA, const FGuid& ItemIdB);

	/** Swap two equipped indices within the same slot category. */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Inventory|Equipment")
	void Server_SwapEquippedSlots(EEquipmentSlot Slot, int32 SlotIndexA, int32 SlotIndexB);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Grid")
	void SetGridDimensions(int32 Columns, int32 Rows);

	UFUNCTION(Server, Reliable, BlueprintCallable, Category = "Inventory")
	void Server_DropItem(const FGuid& ItemId, FVector WorldLocation, FRotator WorldRotation);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Grid")
	bool AutoPlaceItem(UAeyerjiItemInstance* Item);

	UFUNCTION(BlueprintCallable, Category = "Inventory|Grid")
	bool CanPlaceItemAt(FIntPoint TopLeft, FIntPoint Size, const FGuid& IgnoredItem) const;

	UPROPERTY(BlueprintAssignable, Category = "Inventory|Events")
	FOnInventoryChanged OnInventoryChanged;

	UPROPERTY(BlueprintAssignable, Category = "Inventory|Events")
	FOnEquippedItemChanged OnEquippedItemChanged;

	UPROPERTY(BlueprintAssignable, Category = "Inventory|Events")
	FOnInventoryItemStateChanged OnInventoryItemStateChanged;

	/** Broadcasts when player level changes enough for equipment slot visibility or unlock state to refresh in UI. */
	UPROPERTY(BlueprintAssignable, Category = "Inventory|Events")
	FOnEquipmentSlotUnlocksChanged OnEquipmentSlotUnlocksChanged;

	FAeyerjiInventorySaveData BuildSaveData();
	void ApplySaveData(const FAeyerjiInventorySaveData& SaveData);

	/** Convenience helper for client/UI to request a drop. */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void DropItem(const FGuid& ItemId, FVector WorldLocation, FRotator WorldRotation);

	/** Drops the item slightly in front of the owner. */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void DropItemAtOwner(const FGuid& ItemId, float ForwardOffset = 100.f);

	/** Debug-only: recomputes item stats using the current loot table (authority only). */
	int32 DebugRefreshItemScaling(const UAeyerjiLootTable& LootTable);

protected:
	void ApplyItemGameplayEffect(UAeyerjiItemInstance* Item, float Multiplier = 1.f);
	void RemoveItemGameplayEffect(const FGuid& ItemId);
	void UpdateManagedOwnedTagCount(UAbilitySystemComponent* ASC, const FGameplayTag& Tag, int32 Delta);

	bool TryAutoPlaceItem(UAeyerjiItemInstance* Item);
	bool CanPlaceAt(const FInventoryItemGridData& Placement, const FGuid& IgnoredItem = FGuid()) const;
	void ClearPlacement(const FGuid& ItemId);

	UFUNCTION()
	void OnRep_EquippedItems(const TArray<FEquippedItemEntry>& PreviousEquipped);

	UFUNCTION()
	void OnRep_GridPlacements();

	UFUNCTION()
	void OnRep_ItemSnapshots();

	UFUNCTION()
	void OnRep_GridSize();

	void OnRep_Items();

	void BroadcastItemStateChange(EInventoryItemStateChange Change, UAeyerjiItemInstance* Item, EEquipmentSlot Slot = EEquipmentSlot::Assault, int32 SlotIndex = INDEX_NONE);

	bool SyncGridItemInstances();
	void ScheduleGridSyncRetry();
	void HandleDeferredGridSync();

	void RebuildItemSnapshots();
	void ResolveEquippedItems();
	/** Refreshes the in-memory server profile after runtime inventory changes without committing to disk. */
	void SyncProfileInventoryCache(const TCHAR* Reason);
	void BindItemInstanceDelegates(UAeyerjiItemInstance* Item);
	void UnbindItemInstanceDelegates(UAeyerjiItemInstance* Item);
	void HandleServerItemStateChanged();
	void RefreshClientItemsFromSnapshots();
	void TryBindOwnerLevelChange();
	void UnbindOwnerLevelChange();
	void HandleOwnerLevelChanged(const FOnAttributeChangeData& Data);
	void BroadcastEquipmentSlotUnlocksIfChanged(bool bForce = false);

	UPROPERTY()
	FTimerHandle GridSyncRetryHandle;
	bool bGridSyncRetryScheduled = false;

	FTimerHandle LevelBindingRetryHandle;
	FDelegateHandle LevelChangedHandle;
	TWeakObjectPtr<UAbilitySystemComponent> LevelBoundASC;
	int32 LastBroadcastPlayerLevel = INDEX_NONE;
	int32 LastBroadcastNormalLaneSlots = INDEX_NONE;
	int32 LastBroadcastCorruptionSlots = INDEX_NONE;

	TMap<UAeyerjiItemInstance*, FDelegateHandle> ItemChangedDelegateHandles;
	TMap<FGameplayTag, int32> ManagedOwnedTagCounts;

#if WITH_DEV_AUTOMATION_TESTS
	int32 AutomationOwnerLevelOverride = 0;
#endif

	int32 SanitizeSlotIndex(EEquipmentSlot Slot, int32 SlotIndex) const;
	int32 FindFirstFreeSlotIndex(EEquipmentSlot Slot, const UAeyerjiItemInstance* IgnoredItem = nullptr) const;
	FEquippedItemEntry* FindEquippedEntry(EEquipmentSlot Slot, int32 SlotIndex);
	const FEquippedItemEntry* FindEquippedEntry(EEquipmentSlot Slot, int32 SlotIndex) const;
	bool TryPlaceItemAt(UAeyerjiItemInstance* Item, const FIntPoint& TopLeft);
	bool UnequipSlotInternal(EEquipmentSlot Slot, int32 SlotIndex, const FIntPoint* PreferredTopLeft);
	void PruneEmptyEquippedEntries();
};
