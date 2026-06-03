// W_InventoryBag_Native.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Items/InventoryComponent.h"
#include "GUI/ItemTooltipData.h"

#include "W_InventoryBag_Native.generated.h"

class APlayerParentNative;
class UGridPanel;
class UW_ItemTile;
class UW_EquipmentSlot;
class UWidget;
class UTexture2D;
class UMaterialInterface;

/** Native logic for the replicated inventory bag grid. */
UCLASS()
class AEYERJI_API UW_InventoryBag_Native : public UUserWidget
{
	GENERATED_BODY()

public:
	UW_InventoryBag_Native(const FObjectInitializer& ObjectInitializer);

	/** Manual bind from HUDs if auto-binding via owning pawn is not enough. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI")
	void BindToPlayer(APlayerParentNative* Player);

	/** Explicit bind that skips the player helper and works directly with a component (e.g. test harnesses). */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI")
	void BindToInventoryComponent(UAeyerjiInventoryComponent* InInventory);

	/** Force a refresh from the cached inventory. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI")
	void RefreshInventory();

	/** Drops the item under the cursor within the grid (if any). */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI")
	bool DropItemUnderCursor(float ForwardOffset = 100.f);

	UFUNCTION(BlueprintPure, Category = "Aeyerji|UI")
	UAeyerjiInventoryComponent* GetInventoryComponent() const { return Inventory.Get(); }

	UFUNCTION(BlueprintPure, Category = "Aeyerji|UI")
	UAeyerjiItemInstance* GetItemById(const FGuid& ItemId) const { return ResolveItem(ItemId); }

	/** Allows designer-authored slot widgets to auto-bind/unbind. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI")
	void RegisterEquipmentSlot(UW_EquipmentSlot* SlotWidget);

	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI")
	void UnregisterEquipmentSlot(UW_EquipmentSlot* SlotWidget);

	/** Request/clear the tooltip from any child widget (tiles, equipment slots). */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI|Tooltip")
	void ShowItemTooltip(UAeyerjiItemInstance* Item, FVector2D ScreenPosition, UWidget* SourceWidget, EItemTooltipSource Source = EItemTooltipSource::InventoryTile);

	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI|Tooltip")
	void HideItemTooltip(UWidget* SourceWidget);

	UFUNCTION(BlueprintPure, Category = "Aeyerji|UI|Tooltip")
	const FAeyerjiItemTooltipData& GetLastTooltipData() const { return LastTooltipData; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** Designers can override this in BP to fully control how tiles are laid out. */
	UFUNCTION(BlueprintNativeEvent, Category = "Aeyerji|UI")
	void RebuildInventoryGrid(const TArray<FInventoryItemGridData>& Placements);
	virtual void RebuildInventoryGrid_Implementation(const TArray<FInventoryItemGridData>& Placements);

	/** Optional BP hooks for when inventory bindings change. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Aeyerji|UI")
	void BP_OnInventoryComponentBound(UAeyerjiInventoryComponent* NewInventory);

	UFUNCTION(BlueprintImplementableEvent, Category = "Aeyerji|UI")
	void BP_OnInventoryComponentUnbound(UAeyerjiInventoryComponent* OldInventory);

	UFUNCTION(BlueprintImplementableEvent, Category = "Aeyerji|UI")
	void BP_OnInventoryItemStateChanged(const FInventoryItemChangeEvent& EventData);

	/** Lets the inventory BP update optional lane panels, headings, or lock overlays when level-gated slots change. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Aeyerji|UI")
	void BP_OnEquipmentSlotUnlocksChanged(int32 PlayerLevel, int32 NormalLaneSlots, int32 CorruptionSlots, bool bCorruptionUnlocked);

	/** Designers implement these to spawn/dismiss their tooltip widget. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Aeyerji|UI|Tooltip")
	void BP_ShowItemTooltip(const FAeyerjiItemTooltipData& TooltipData, FVector2D ScreenPosition, UWidget* SourceWidget);

	UFUNCTION(BlueprintImplementableEvent, Category = "Aeyerji|UI|Tooltip")
	void BP_HideItemTooltip(const FAeyerjiItemTooltipData& TooltipData, UWidget* SourceWidget);

	/** Grid panel defined in UMG. Replace UniformGrid with GridPanel named GridPanel_Items. */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UGridPanel> GridPanel_Items = nullptr;

	/** Optional Corruption lane container; hidden until Corruption unlocks at level 50. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UWidget> CorruptionBorder = nullptr;

	/** Tile widget class; defaults to the native item tile. */
	UPROPERTY(EditAnywhere, Category = "Aeyerji|UI")
	TSubclassOf<UW_ItemTile> ItemTileClass;

	/** Size in pixels of a single grid cell before spanning. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|UI", meta = (ClampMin = "1.0"))
	FVector2D CellSize = FVector2D(64.f, 64.f);

	/** Padding between tiles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|UI")
	FMargin CellPadding = FMargin(2.f);

	/** Padding applied to the inside image layer of generated inventory grid tiles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|UI")
	FMargin InventoryTileIconPadding = FMargin(8.5f);

	/** Padding applied to the border layer of generated inventory grid tiles. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|UI")
	FMargin InventoryTileBorderPadding = FMargin(0.f);

	/** Icon used for empty inventory bag cells. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|UI")
	TObjectPtr<UTexture2D> EmptyInventorySlotIcon = nullptr;

	/** Generic border material used by empty inventory grid cells and as item-tile fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|UI")
	TObjectPtr<UMaterialInterface> InventoryTileGenericBorderMaterial = nullptr;

	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI")
	void SetCellSize(FVector2D NewCellSize);

	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI")
	void SetCellPadding(FMargin NewPadding);

private:
	TWeakObjectPtr<UAeyerjiInventoryComponent> Inventory;
	TWeakObjectPtr<APlayerParentNative> BoundPlayer;

	void DispatchRebuild();
	UAeyerjiItemInstance* ResolveItem(const FGuid& Id) const;

	/** Subscribe to player inventory events. */
	UFUNCTION()
	void AttachToInventory(UAeyerjiInventoryComponent* Inv);

	/** Inventory change callback wired to the component delegate. */
	UFUNCTION()
	void HandleInventoryGridChanged();

	/** Detailed item state updates (pickup, drop, equip, etc). */
	UFUNCTION()
	void HandleInventoryItemStateChanged(const FInventoryItemChangeEvent& EventData);

	UFUNCTION()
	void HandleEquipmentSlotUnlocksChanged(int32 PlayerLevel, int32 NormalLaneSlots, int32 CorruptionSlots, bool bCorruptionUnlocked);

	void RefreshCorruptionLaneVisibility(bool bCorruptionUnlocked);
	void RefreshRegisteredEquipmentSlots();
	void ClearEquipmentSlotBindings();
	void DiscoverEquipmentSlots();
	void SetActiveTooltipSource(UWidget* SourceWidget);

	UPROPERTY()
	TArray<TWeakObjectPtr<UW_EquipmentSlot>> RegisteredEquipmentSlots;

	UPROPERTY()
	FAeyerjiItemTooltipData LastTooltipData;

	TWeakObjectPtr<UWidget> ActiveTooltipSource;
};
