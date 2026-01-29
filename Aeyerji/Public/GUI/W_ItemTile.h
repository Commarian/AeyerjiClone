// W_ItemTile.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Items/ItemInstance.h"

#include "W_ItemTile.generated.h"

class UBorder;
class UImage;
class UAeyerjiInventoryComponent;

/** Simple tile that displays an item's icon and rarity tint without needing a BP child. */
UCLASS()
class AEYERJI_API UW_ItemTile : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Initialize the tile from an item instance. Safe to call on owning client. Blueprint overrides can customize visuals. */
	UFUNCTION(BlueprintCallable, BlueprintNativeEvent, Category = "Aeyerji|UI")
	void SetupFromItem(UAeyerjiItemInstance* InItem);
	virtual void SetupFromItem_Implementation(UAeyerjiItemInstance* InItem);

	/** Configure this tile to represent an empty slot (no drag/drop). */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI")
	void SetupEmptySlot();

	/** Allows parents to communicate the actual pixel size of this tile for drag visuals. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI")
	void SetTileVisualSize(FVector2D InSize);

	/** Expose the item id for drag/drop or tooltips. */
	UFUNCTION(BlueprintPure, Category = "Aeyerji|UI")
	FGuid GetItemId() const { return ItemId; }

	/** Direct access for designer-authored logic. */
	UFUNCTION(BlueprintPure, Category = "Aeyerji|UI")
	UAeyerjiItemInstance* GetItemInstance() const { return Item; }

	/** True when the mouse is over this tile or its icon. */
	UFUNCTION(BlueprintPure, Category = "Aeyerji|UI")
	bool IsMouseOverItem() const;

	/** UI helper so BP can drop the bound item to the ground (owner forward offset). */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI")
	bool DropItemToGround(float ForwardOffset = 100.f);

	/** Injects the inventory component so the tile can request RPCs (equip/move). */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI")
	void BindInventory(UAeyerjiInventoryComponent* InInventory);

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation) override;
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;

	UPROPERTY(Transient)
	TObjectPtr<UAeyerjiItemInstance> Item = nullptr;

	UPROPERTY(Transient)
	FGuid ItemId;

	UPROPERTY(Transient)
	TObjectPtr<UBorder> RootBorder = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UImage> IconImage = nullptr;

	UPROPERTY(EditAnywhere, Category = "Aeyerji|UI")
	FLinearColor EmptySlotBorderColor = FLinearColor(0.08f, 0.08f, 0.08f, 1.f);

	UPROPERTY(EditAnywhere, Category = "Aeyerji|UI")
	FLinearColor EmptySlotIconTint = FLinearColor(1.f, 1.f, 1.f, 0.15f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|UI")
	TObjectPtr<UTexture2D> EmptySlotIcon = nullptr;

	TWeakObjectPtr<UAeyerjiInventoryComponent> Inventory;
	FIntPoint CachedSize = FIntPoint(1, 1);
	FVector2D PendingGrabOffset = FVector2D::ZeroVector;
	FVector2D TileVisualSize = FVector2D(64.f, 64.f);
	bool bIsPlaceholder = false;

	/** Ensures we have a minimal widget tree so the native widget renders without a blueprint. */
	void EnsureWidgetTree();

	void RefreshFromItem();

	UFUNCTION()
	void HandleObservedItemChanged();

	void TryEquipFromTile();
};
