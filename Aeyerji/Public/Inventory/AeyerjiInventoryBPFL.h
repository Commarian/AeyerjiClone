// AeyerjiInventoryBPFL.h
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Items/ItemTypes.h"
#include "Items/LootTypes.h"
#include "Systems/LootService.h"
#include "Styling/SlateColor.h"
#include "Items/LootSourceRuleSet.h"
#include "AeyerjiInventoryBPFL.generated.h"


class AAeyerjiLootPickup;
class UAeyerjiInventoryComponent;
class UAeyerjiItemInstance;
class UItemDefinition;
class AActor;

UENUM(BlueprintType)
enum class EAeyerjiAddItemResult : uint8
{
	Equipped,
	Bagged,
	Failed_NoInventory,
	Failed_NoItem,
	Failed_MissingDefinition,
	Failed_LevelTooHigh,
	Failed_EquipRejected,
	Failed_BagPlacementFailed,
	Failed_BagFull
};

UENUM(BlueprintType)
enum class EItemDropDistributionMode : uint8
{
	DropOnlyForInstigator           UMETA(DisplayName = "Drop Only For Instigator"),
	DropIdenticalItemForEveryPlayer UMETA(DisplayName = "Drop Identical Item For Every Player"),
	DropUniqueItemForEveryPlayer    UMETA(DisplayName = "Drop Unique Item For Every Player")
};

/** Summary returned when one or more loot results are converted into pickup actors. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiLootSpawnSummary
{
	GENERATED_BODY()

	/** Number of loot roll results requested for spawning. */
	UPROPERTY(BlueprintReadOnly, Category = "Aeyerji|Loot")
	int32 RequestedResultCount = 0;

	/** Actual pickup actors spawned. Every-player drop modes can produce more pickups than results. */
	UPROPERTY(BlueprintReadOnly, Category = "Aeyerji|Loot")
	int32 SpawnedPickupCount = 0;

	/** Failed pickup spawn attempts, including invalid results and per-recipient spawn failures. */
	UPROPERTY(BlueprintReadOnly, Category = "Aeyerji|Loot")
	int32 FailedSpawnCount = 0;

	/** Last pickup spawned by the batch, if any. */
	UPROPERTY(BlueprintReadOnly, Category = "Aeyerji|Loot")
	TObjectPtr<AAeyerjiLootPickup> LastSpawnedPickup = nullptr;

	/** Every pickup successfully created by the batch, used by authority code to apply recipient ownership. */
	UPROPERTY(BlueprintReadOnly, Category = "Aeyerji|Loot")
	TArray<TObjectPtr<AAeyerjiLootPickup>> SpawnedPickups;
};

/**
 * Blueprint helpers for the custom inventory system.
 */
UCLASS()
class AEYERJI_API UAeyerjiInventoryBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Double-click helper: equips when in bag, unequips when already equipped (auto-places back into grid). */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Inventory")
	static bool ToggleEquipState(UAeyerjiInventoryComponent* Inventory, UAeyerjiItemInstance* ItemInstance);

	/** Client helper: drop the given item at the owner's feet (uses DropItemAtOwner under the hood). */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Inventory")
	static bool DropItemAtOwner(UAeyerjiInventoryComponent* Inventory, UAeyerjiItemInstance* ItemInstance, float ForwardOffset = 100.f);

	/** Attempt to equip the item (preferred slot) otherwise place it in the bag grid. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Inventory")
	static EAeyerjiAddItemResult EquipFirstThenBag(
		UAeyerjiInventoryComponent* Inventory,
		UAeyerjiItemInstance* ItemInstance);

	/** Return a consistent UI color for a given item rarity. */
	UFUNCTION(BlueprintPure, Category = "Aeyerji|Inventory|Display")
	static FLinearColor GetRarityColor(EItemRarity Rarity);

	/** Same as GetRarityColor but wrapped as a Slate color for UMG brush usage. */
	UFUNCTION(BlueprintPure, Category = "Aeyerji|Inventory|Display")
	static FSlateColor GetRaritySlateColor(EItemRarity Rarity);

	/** Spawn a loot pickup that rolls an item instance from the given definition (SERVER only). */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Inventory", meta = (WorldContext = "WorldContextObject"))
	static AAeyerjiLootPickup* SpawnLootByDefinition(
		UObject* WorldContextObject,
		UItemDefinition* Definition,
		int32 ItemLevel,
		EItemRarity Rarity,
		FVector Location,
		FRotator Rotation,
		int32 SeedOverride = 0,
		EItemDropDistributionMode DropMode = EItemDropDistributionMode::DropOnlyForInstigator,
		AActor* Instigator = nullptr);

	/** Spawn a loot pickup from an already rolled item instance (SERVER only). */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Inventory", meta = (WorldContext = "WorldContextObject"))
	static AAeyerjiLootPickup* SpawnLootByInstance(
		UObject* WorldContextObject,
		UAeyerjiItemInstance* ItemInstance,
		FVector Location,
		FRotator Rotation,
		EItemDropDistributionMode DropMode = EItemDropDistributionMode::DropOnlyForInstigator,
		AActor* Instigator = nullptr);

	/** Spawn a loot pickup directly from a loot roll result (SERVER only). */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Inventory", meta = (WorldContext = "WorldContextObject"))
	static AAeyerjiLootPickup* SpawnLootFromResult(
		UObject* WorldContextObject,
		const FLootDropResult& Result,
		FVector Location,
		FRotator Rotation,
		int32 SeedOverride = 0,
		EItemDropDistributionMode DropMode = EItemDropDistributionMode::DropOnlyForInstigator,
		AActor* Instigator = nullptr);

	/** Spawn pickups for already rolled loot results and report exact pickup counts (SERVER only). */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Loot", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "SeedOverride,DropMode,Instigator,LootReleaseScatterRadius,LootReleaseScatterYawOffset"))
	static FAeyerjiLootSpawnSummary SpawnLootResults(
		UObject* WorldContextObject,
		const TArray<FLootDropResult>& Results,
		FVector Location,
		FRotator Rotation,
		int32 SeedOverride = 0,
		EItemDropDistributionMode DropMode = EItemDropDistributionMode::DropOnlyForInstigator,
		AActor* Instigator = nullptr,
		float LootReleaseScatterRadius = 0.f,
		float LootReleaseScatterYawOffset = 0.f);

	/** Roll and spawn multiple drops using a multi-drop config (SERVER only). */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Loot", meta = (WorldContext = "WorldContextObject"))
	static void SpawnMultiDropFromContext(
		UObject* WorldContextObject,
		FLootContext BaseContext,
		const FLootMultiDropConfig& Config,
		FVector Location,
		FRotator Rotation,
		EItemDropDistributionMode DropMode,
		AActor* Instigator = nullptr);

	/** Rolls and spawns loot for one or many players using the specified distribution mode (SERVER only). 
	 * ULootService::RollLoot already calls RecordItemDropped on the player’s UPlayerStatsTrackingComponent. 
	 * This uses RollLoot under the hood (once, or per-recipient in the unique mode), 
	 * so drop stats are recorded there. If you also call RecordItemDropped in BP after spawning, 
	 * you’ll double-count—remove the BP call and let the C++ handle it.
	*/
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Loot", meta = (WorldContext = "WorldContextObject"))
	static void SpawnDistributedLootFromContext(
		UObject* WorldContextObject,
		FLootContext BaseContext,
		FVector Location,
		FRotator Rotation,
		EItemDropDistributionMode DropMode,
		AActor* Instigator = nullptr);

	/** Toggle visibility for all loot labels in the world (client cosmetic helper). */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Loot", meta = (WorldContext = "WorldContext"))
	static void SetAllLootLabelsVisible(UObject* WorldContext, bool bVisible);

	// Convenience: safe resolve (returns BaseContext if RuleSet is null).
	UFUNCTION(BlueprintCallable, BlueprintPure, Category="Aeyerji|Loot")
	static FLootContext ResolveLootContext(const ULootSourceRuleSet* RuleSet, FLootContext BaseContext, const FGameplayTagContainer& SourceTags);
};
