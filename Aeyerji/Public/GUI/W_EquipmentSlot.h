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
class UBorder;
class UImage;

/**
 * Standalone widget that mirrors a single equipment slot from the inventory component.
 * Designers can create BP children for visual styling while keeping logic centralized here.
 */
UCLASS()
class AEYERJI_API UW_EquipmentSlot : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Slot represented by this widget instance (set per BP instance in the designer). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Equipment")
	EEquipmentSlot SlotType = EEquipmentSlot::Offense;

	/** Index of this slot within its category (0-based). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|Equipment", meta = (ClampMin = "0", UIMin = "0"))
	int32 SlotIndex = 0;

	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Equipment")
	void BindInventory(UAeyerjiInventoryComponent *InInventory);

	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Equipment")
	void RefreshFromInventory();

	UFUNCTION(BlueprintPure, Category = "Aeyerji|Equipment")
	UAeyerjiItemInstance *GetCurrentItem() const { return CurrentItem.Get(); }

	EEquipmentSlot GetEffectiveSlotType() const;
	int32 GetEffectiveSlotIndex() const;

protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeDestruct() override;
	virtual FReply NativeOnMouseButtonDown(
		const FGeometry &InGeometry,
		const FPointerEvent &InMouseEvent) override;
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
	/** Optional icon/border bound from BP to update visuals. */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UImage> ItemIcon = nullptr;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UBorder> SlotBorder = nullptr;

	/** Fallback tint when slot is empty so BP children don't need extra code. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Equipment")
	FLinearColor EmptyTint = FLinearColor::White;

	/** Optional icon to use when the slot is empty. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Equipment")
	TObjectPtr<UTexture2D> EmptySlotIcon = nullptr;

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

	void ClearItemDelegate();

	UFUNCTION()
	void HandleInventoryEquippedChanged(EEquipmentSlot ChangedSlot, int32 ChangedIndex, UAeyerjiItemInstance *Item);

	void HandleObservedItemChanged();
	UWidget* CreateFallbackDragVisual() const;
};
