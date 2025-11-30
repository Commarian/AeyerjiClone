// W_InventoryBag_DnD.h
#pragma once

#include "CoreMinimal.h"
#include "GUI/W_InventoryBag_Native.h"

#include "W_InventoryBag_DnD.generated.h"

class UAeyerjiItemDragOperation;

/**
 * Inventory bag variant that supports drag/drop repositioning.
 */
UCLASS()
class AEYERJI_API UW_InventoryBag_DnD : public UW_InventoryBag_Native
{
	GENERATED_BODY()

protected:
	virtual bool NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;
	virtual bool NativeOnDrop(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation) override;

private:
	bool TryMoveFromDragOperation(UAeyerjiItemDragOperation* DragOp, const FGeometry& InGeometry, const FDragDropEvent& DragEvent);
	bool ScreenToGridCell(const FVector2D& ScreenPos, FIntPoint ItemSize, const FVector2D& GrabOffsetPx, FIntPoint& OutTopLeft) const;
};
