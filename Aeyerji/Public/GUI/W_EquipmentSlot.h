// W_EquipmentSlot.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Delegates/Delegate.h"
#include "Items/ItemTypes.h"

#include "W_EquipmentSlot.generated.h"

class UAeyerjiInventoryComponent;
class UAeyerjiItemInstance;
class UAeyerjiItemDragOperation;
class UImage;
class UMaterialInterface;
class UTexture2D;

/**
 * Standalone widget that mirrors a single equipment slot from the inventory component.
 * Designers can create BP children for visual styling while keeping logic centralized here.
 */
UCLASS()
class AEYERJI_API UW_EquipmentSlot : public UUserWidget
{
	GENERATED_BODY()

public:
	UW_EquipmentSlot(const FObjectInitializer& ObjectInitializer);

	/** Slot represented by this widget instance (set per BP instance in the designer). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Equipment")
	EEquipmentSlot SlotType = EEquipmentSlot::Assault;

	/** Index of this slot within its category (0-based). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Equipment", meta = (ClampMin = "0", UIMin = "0"))
	int32 SlotIndex = 0;

	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Equipment")
	void BindInventory(UAeyerjiInventoryComponent *InInventory);

	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Equipment")
	void RefreshFromInventory();

	UFUNCTION(BlueprintPure, Category = "Aeyerji|Equipment")
	UAeyerjiItemInstance *GetCurrentItem() const { return CurrentItem.Get(); }

	/** True when the mouse is over this slot or its icon. */
	UFUNCTION(BlueprintPure, Category = "Aeyerji|Equipment")
	bool IsMouseOverItem() const;

	UFUNCTION(BlueprintPure, Category = "Aeyerji|Equipment")
	bool IsSlotLocked() const;

	UFUNCTION(BlueprintPure, Category = "Aeyerji|Equipment")
	bool IsSlotVisibleForCurrentLevel() const;

	UFUNCTION(BlueprintPure, Category = "Aeyerji|Equipment")
	bool IsSlotInteractionEnabled() const;

	UFUNCTION(BlueprintPure, Category = "Aeyerji|Equipment")
	ESlateVisibility GetSlotVisibility() const;

	UFUNCTION(BlueprintPure, Category = "Aeyerji|Equipment")
	UTexture2D* GetLockedSlotIcon() const;

	UFUNCTION(BlueprintPure, Category = "Aeyerji|Equipment")
	FText GetSlotDisplayText() const;

	UFUNCTION(BlueprintPure, Category = "Aeyerji|Equipment")
	FText GetSlotTooltipText() const;

	/** Drop the currently equipped item at the owner's feet. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Equipment")
	bool DropItemToGround(float ForwardOffset = 100.f);

	EEquipmentSlot GetEffectiveSlotType() const;
	int32 GetEffectiveSlotIndex() const;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnMouseButtonDown(
		const FGeometry &InGeometry,
		const FPointerEvent &InMouseEvent) override;
	virtual FReply NativeOnMouseButtonDoubleClick(
		const FGeometry& InGeometry,
		const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnDragDetected(
		const FGeometry &InGeometry,
		const FPointerEvent &InMouseEvent,
		UDragDropOperation *&OutOperation) override;
	virtual bool NativeOnDragOver(const FGeometry &InGeometry, const FDragDropEvent &InDragDropEvent, UDragDropOperation *InOperation) override;
	virtual bool NativeOnDrop(
		const FGeometry &InGeometry,
		const FDragDropEvent &InDragDropEvent,
		UDragDropOperation *InOperation) override;
	virtual void NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;
	virtual void NativeOnMouseLeave(const FPointerEvent& InMouseEvent) override;

	/** Bottom layer: equipped item icon, empty insert, or locked slot image. Preferred BP binding. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> InsideImage = nullptr;

	/** Top layer: lane/category frame texture. Preferred BP binding. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> BorderImage = nullptr;

	/** Legacy inside-image binding kept so older slot BPs keep rendering during migration. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> ItemIcon = nullptr;

	/** Optional icon to use when the slot is empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Equipment")
	TObjectPtr<UTexture2D> EmptySlotIcon = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Equipment|Empty")
	TSoftObjectPtr<UTexture2D> AssaultEmptyIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Equipment|Empty")
	TSoftObjectPtr<UTexture2D> GuardEmptyIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Equipment|Empty")
	TSoftObjectPtr<UTexture2D> FlowEmptyIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Equipment|Empty")
	TSoftObjectPtr<UTexture2D> CorruptionEmptyIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Equipment|Locked")
	TSoftObjectPtr<UTexture2D> AssaultLockedIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Equipment|Locked")
	TSoftObjectPtr<UTexture2D> GuardLockedIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Equipment|Locked")
	TSoftObjectPtr<UTexture2D> FlowLockedIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Equipment|Locked")
	TSoftObjectPtr<UTexture2D> CorruptionLockedIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Equipment|Visual Layers")
	TSoftObjectPtr<UMaterialInterface> AssaultBorderMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Equipment|Visual Layers")
	TSoftObjectPtr<UMaterialInterface> GuardBorderMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Equipment|Visual Layers")
	TSoftObjectPtr<UMaterialInterface> FlowBorderMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Equipment|Visual Layers")
	TSoftObjectPtr<UMaterialInterface> CorruptionBorderMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Equipment|Visual Layers")
	TObjectPtr<UMaterialInterface> GenericBorderMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Equipment|Visual Layers")
	FName RarityColorParameterName = TEXT("RarityColor");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Equipment|Visual Layers")
	TSoftObjectPtr<UTexture2D> AssaultBorderIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Equipment|Visual Layers")
	TSoftObjectPtr<UTexture2D> GuardBorderIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Equipment|Visual Layers")
	TSoftObjectPtr<UTexture2D> FlowBorderIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Equipment|Visual Layers")
	TSoftObjectPtr<UTexture2D> CorruptionBorderIcon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Equipment|Visual Layers")
	TObjectPtr<UTexture2D> GenericBorderIcon = nullptr;

	/** Optional drag visual to instantiate when items are dragged from this slot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Equipment")
	TSubclassOf<UUserWidget> DragVisualWidgetClass = nullptr;

private:
	TWeakObjectPtr<UAeyerjiInventoryComponent> Inventory;
	TWeakObjectPtr<UAeyerjiItemInstance> CurrentItem;

	/** Snapshot of the bound item's change delegate so we can refresh visuals when stats/icons change. */
	FDelegateHandle ItemChangedHandle;

	void UnbindInventory();
	void BindToCurrentItem(UAeyerjiItemInstance *NewItem);
	void UpdateSlotVisuals();
	bool CanAcceptDragOperation(UAeyerjiItemDragOperation *DragOp) const;
	bool IsItemCompatible(const UAeyerjiItemInstance *Item) const;
	bool TryEquipFromDragOperation(UAeyerjiItemDragOperation *DragOp);
	UTexture2D* GetEmptySlotIcon() const;
	UTexture2D* GetBorderSlotIcon() const;
	UMaterialInterface* GetBorderSlotMaterial() const;
	UImage* GetInsideImageWidget() const;
	void UpdateBorderVisual(const UAeyerjiItemInstance* Item);

	void ClearItemDelegate();

	UFUNCTION()
	void HandleInventoryEquippedChanged(EEquipmentSlot ChangedSlot, int32 ChangedIndex, UAeyerjiItemInstance *Item);

	void HandleObservedItemChanged();
	UWidget* CreateFallbackDragVisual() const;
};
