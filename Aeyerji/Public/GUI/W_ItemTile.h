// W_ItemTile.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Items/ItemInstance.h"

#include "W_ItemTile.generated.h"

class UImage;
class UOverlay;
class UAeyerjiInventoryComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture2D;

/** Simple tile that displays an item's icon and rarity tint without needing a BP child. */
UCLASS()
class AEYERJI_API UW_ItemTile : public UUserWidget
{
	GENERATED_BODY()

public:
	UW_ItemTile(const FObjectInitializer& ObjectInitializer);

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

	/** Sets the texture used when this tile is rendered as an empty inventory cell. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI")
	void SetEmptySlotIcon(UTexture2D* InIcon);

	/** Sets the material used for this tile's outer grid border. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI")
	void SetBorderMaterial(UMaterialInterface* InMaterial);

	/** Sets the overlay padding for the inside icon and border layers. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|UI")
	void SetTileLayerPadding(FMargin InIconPadding, FMargin InBorderPadding);

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
	virtual void SynchronizeProperties() override;
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
	TObjectPtr<UOverlay> RootOverlay = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UImage> IconImage = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UImage> BorderImage = nullptr;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> BorderDynamicMaterial = nullptr;

	UPROPERTY(EditAnywhere, Category = "Aeyerji|UI")
	FLinearColor EmptySlotIconTint = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|UI|Layout")
	FMargin IconLayerPadding = FMargin(8.5f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|UI|Layout")
	FMargin BorderLayerPadding = FMargin(0.f);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|UI")
	TObjectPtr<UTexture2D> EmptySlotIcon = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|UI")
	TObjectPtr<UMaterialInterface> GenericBorderMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|UI|Border")
	TSoftObjectPtr<UMaterialInterface> AssaultBorderMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|UI|Border")
	TSoftObjectPtr<UMaterialInterface> GuardBorderMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|UI|Border")
	TSoftObjectPtr<UMaterialInterface> FlowBorderMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|UI|Border")
	TSoftObjectPtr<UMaterialInterface> CorruptionBorderMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|UI")
	FName RarityColorParameterName = TEXT("RarityColor");

	TWeakObjectPtr<UAeyerjiInventoryComponent> Inventory;
	FIntPoint CachedSize = FIntPoint(1, 1);
	FVector2D PendingGrabOffset = FVector2D::ZeroVector;
	FVector2D TileVisualSize = FVector2D(64.f, 64.f);
	bool bIsPlaceholder = false;

	/** Ensures we have a minimal widget tree so the native widget renders without a blueprint. */
	void EnsureWidgetTree();
	void ApplyLayerPadding();

	void RefreshFromItem();
	void RefreshBorderVisual(UMaterialInterface* InBorderMaterial, const FLinearColor& RarityColor);
	UMaterialInterface* GetBorderMaterialForItem() const;

	UFUNCTION()
	void HandleObservedItemChanged();

	void TryEquipFromTile();
};
