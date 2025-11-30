// W_InventoryBag_DnD.cpp

#include "GUI/W_InventoryBag_DnD.h"

#include "Blueprint/SlateBlueprintLibrary.h"
#include "Components/GridPanel.h"
#include "GUI/AeyerjiItemDragOperation.h"
#include "Items/InventoryComponent.h"

bool UW_InventoryBag_DnD::NativeOnDragOver(
    const FGeometry& InGeometry,
    const FDragDropEvent& InDragDropEvent,
    UDragDropOperation* InOperation)
{
    // IMPORTANT: call Super so UUserWidget can invoke the BP OnDragOver
    const bool bHandledByBP =
        Super::NativeOnDragOver(InGeometry, InDragDropEvent, InOperation);

    // We don't need extra C++ behaviour here – let BP decide.
    return bHandledByBP;
}

bool UW_InventoryBag_DnD::NativeOnDrop(
    const FGeometry& InGeometry,
    const FDragDropEvent& InDragDropEvent,
    UDragDropOperation* InOperation)
{
    // 1) Give the BP child (W_InventoryBag) a chance to handle special zones
    const bool bHandledByBP =
        Super::NativeOnDrop(InGeometry, InDragDropEvent, InOperation);

    if (bHandledByBP)
    {
        // Drop was handled (e.g. "drop to ground" zone in BP) – do not move in grid.
        return true;
    }

    // 2) Fallback: run existing grid/equipment move logic
    if (UAeyerjiItemDragOperation* DragOp = Cast<UAeyerjiItemDragOperation>(InOperation))
    {
        return TryMoveFromDragOperation(DragOp, InGeometry, InDragDropEvent);
    }

    return false;
}


bool UW_InventoryBag_DnD::TryMoveFromDragOperation(UAeyerjiItemDragOperation* DragOp, const FGeometry& InGeometry, const FDragDropEvent& DragEvent)
{
	UAeyerjiInventoryComponent* InventoryComponent = GetInventoryComponent();
	if (!DragOp || !GridPanel_Items || !InventoryComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[InventoryBagDnD] TryMoveFromDragOperation aborted - DragOp=%d Grid=%d Inventory=%d"),
			DragOp ? 1 : 0, GridPanel_Items ? 1 : 0, InventoryComponent ? 1 : 0);
		return false;
	}

	const FVector2D ScreenPos = DragEvent.GetScreenSpacePosition();
	FIntPoint TargetTopLeft;
	if (!ScreenToGridCell(ScreenPos, DragOp->ItemSize, DragOp->GrabOffsetPx, TargetTopLeft))
	{
		UE_LOG(LogTemp, Warning, TEXT("[InventoryBagDnD] ScreenToGridCell failed for drop"));
		return false;
	}

	// Ensure we have an item instance reference for both bag/equipment drags.
	if (!DragOp->ItemInstance && DragOp->ItemId.IsValid())
	{
		DragOp->ItemInstance = InventoryComponent->FindItemById(DragOp->ItemId);
	}

	const FIntPoint ItemSize = DragOp->ItemInstance
		? DragOp->ItemInstance->InventorySize
		: DragOp->ItemSize;

	switch (DragOp->Source)
	{
	case EAeyerjiItemDragSource::Bag:
	{
		if (!InventoryComponent->CanPlaceItemAt(TargetTopLeft, ItemSize, DragOp->ItemId))
		{
			UE_LOG(LogTemp, Warning, TEXT("[InventoryBagDnD] Bag CanPlaceItemAt rejected Item=%s Size=(%d,%d) Target=(%d,%d)"),
				*DragOp->ItemId.ToString(), ItemSize.X, ItemSize.Y, TargetTopLeft.X, TargetTopLeft.Y);
			return false;
		}

		InventoryComponent->Server_MoveItemInGrid(DragOp->ItemId, TargetTopLeft);
		UE_LOG(LogTemp, Display, TEXT("[InventoryBagDnD] Move request Item=%s -> (%d,%d)"),
			*DragOp->ItemId.ToString(), TargetTopLeft.X, TargetTopLeft.Y);
		return true;
	}
	case EAeyerjiItemDragSource::Equipment:
	{
		if (!DragOp->ItemInstance)
		{
			UE_LOG(LogTemp, Warning, TEXT("[InventoryBagDnD] Equipment drag missing item instance"));
			return false;
		}

		if (!InventoryComponent->CanPlaceItemAt(TargetTopLeft, ItemSize, DragOp->ItemId))
		{
			UE_LOG(LogTemp, Warning, TEXT("[InventoryBagDnD] Equipment drop blocked Item=%s Target=(%d,%d)"),
				*DragOp->ItemInstance->UniqueId.ToString(), TargetTopLeft.X, TargetTopLeft.Y);
			return false;
		}

		InventoryComponent->Server_UnequipSlotToGrid(DragOp->SourceEquipmentSlot, DragOp->SourceEquipmentSlotIndex, TargetTopLeft);
		return true;
	}
	default:
		break;
	}

	return false;
}

bool UW_InventoryBag_DnD::ScreenToGridCell(const FVector2D& ScreenPos, FIntPoint ItemSize, const FVector2D& /*GrabOffsetPx*/, FIntPoint& OutTopLeft) const
{
	if (!GridPanel_Items)
	{
		return false;
	}

	const FGeometry GridGeometry = GridPanel_Items->GetCachedGeometry();
	const FVector2D LocalPos = GridGeometry.AbsoluteToLocal(ScreenPos);

	const FIntPoint GridSize = GetInventoryComponent()->GetGridSize();
	const FVector2D GridExtent = GridGeometry.GetLocalSize();
	const float DerivedCellWidth = (GridSize.X > 0) ? GridExtent.X / GridSize.X : CellSize.X;
	const float DerivedCellHeight = (GridSize.Y > 0) ? GridExtent.Y / GridSize.Y : CellSize.Y;

	const float CellWidth = FMath::Max(1.f, DerivedCellWidth);
	const float CellHeight = FMath::Max(1.f, DerivedCellHeight);

	int32 CellX = FMath::FloorToInt(LocalPos.X / CellWidth);
	int32 CellY = FMath::FloorToInt(LocalPos.Y / CellHeight);

	CellX = FMath::Clamp(CellX, 0, FMath::Max(0, GridSize.X - ItemSize.X));
	CellY = FMath::Clamp(CellY, 0, FMath::Max(0, GridSize.Y - ItemSize.Y));

	OutTopLeft = FIntPoint(CellX, CellY);
	UE_LOG(LogTemp, Display, TEXT("[InventoryBagDnD] Screen=%s Local=%s -> Cell=(%d,%d) Size=(%d,%d)"),
		*ScreenPos.ToString(), *LocalPos.ToString(), CellX, CellY, ItemSize.X, ItemSize.Y);
	return true;
}
