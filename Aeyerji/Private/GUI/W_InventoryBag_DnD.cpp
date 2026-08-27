// W_InventoryBag_DnD.cpp

#include "GUI/W_InventoryBag_DnD.h"

#include "Blueprint/SlateBlueprintLibrary.h"
#include "Components/GridPanel.h"
#include "GUI/AeyerjiItemDragOperation.h"
#include "Items/InventoryComponent.h"

namespace
{
	bool IsFiniteScreenVector(const FVector2D& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y);
	}

	bool IsUsableInventoryGrid(const FIntPoint& GridSize)
	{
		return GridSize.X > 0 && GridSize.Y > 0 && GridSize.X <= 64 && GridSize.Y <= 64;
	}
}

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
	if (!IsValid(DragOp) || !GridPanel_Items || !InventoryComponent)
	{
		UE_LOG(LogTemp, Warning, TEXT("[InventoryBagDnD] TryMoveFromDragOperation aborted - DragOp=%d Grid=%d Inventory=%d"),
			DragOp ? 1 : 0, GridPanel_Items ? 1 : 0, InventoryComponent ? 1 : 0);
		return false;
	}

	const FVector2D ScreenPos = DragEvent.GetScreenSpacePosition();
	const FVector2D LocalPos = GridPanel_Items->GetCachedGeometry().AbsoluteToLocal(ScreenPos);
	const FIntPoint GridSize = InventoryComponent->GetGridSize();
	const FVector2D GridExtent = GridPanel_Items->GetCachedGeometry().GetLocalSize();
	if (!IsFiniteScreenVector(ScreenPos) || !IsFiniteScreenVector(LocalPos)
		|| !IsFiniteScreenVector(GridExtent) || !IsUsableInventoryGrid(GridSize)
		|| GridExtent.X <= 0.f || GridExtent.Y <= 0.f)
	{
		return false;
	}
	const float CellWidth = (GridSize.X > 0) ? GridExtent.X / FMath::Max(1, GridSize.X) : CellSize.X;
	const float CellHeight = (GridSize.Y > 0) ? GridExtent.Y / FMath::Max(1, GridSize.Y) : CellSize.Y;
	const int32 HoverCellX = FMath::Clamp(FMath::FloorToInt(LocalPos.X / FMath::Max(1.f, CellWidth)), 0, FMath::Max(0, GridSize.X - 1));
	const int32 HoverCellY = FMath::Clamp(FMath::FloorToInt(LocalPos.Y / FMath::Max(1.f, CellHeight)), 0, FMath::Max(0, GridSize.Y - 1));

	FIntPoint TargetTopLeft;
	if (!ScreenToGridCell(ScreenPos, DragOp->ItemSize, DragOp->GrabOffsetPx, TargetTopLeft))
	{
		UE_LOG(LogTemp, Warning, TEXT("[InventoryBagDnD] ScreenToGridCell failed for drop"));
		return false;
	}

	// Ensure we have an item instance reference for both bag/equipment drags.
	if (!DragOp->ItemId.IsValid())
	{
		return false;
	}
	if (UAeyerjiItemInstance* OwnedItem = InventoryComponent->FindItemById(DragOp->ItemId))
	{
		DragOp->ItemInstance = OwnedItem;
	}
	else
	{
		return false;
	}

	FIntPoint ItemSize = DragOp->ItemInstance
		? DragOp->ItemInstance->InventorySize
		: DragOp->ItemSize;
	ItemSize.X = FMath::Clamp(ItemSize.X, 1, GridSize.X);
	ItemSize.Y = FMath::Clamp(ItemSize.Y, 1, GridSize.Y);

	switch (DragOp->Source)
	{
	case EAeyerjiItemDragSource::Bag:
	{
		if (!InventoryComponent->CanPlaceItemAt(TargetTopLeft, ItemSize, DragOp->ItemId))
		{
			// If blocked, attempt a swap with the item occupying the target cell.
			TArray<FInventoryItemGridData> Placements;
			InventoryComponent->GetGridPlacements(Placements);

			auto DoesPlacementCoverCell = [](const FInventoryItemGridData& Placement, int32 CellX, int32 CellY)
			{
				const int32 SizeX = FMath::Max(1, Placement.Size.X);
				const int32 SizeY = FMath::Max(1, Placement.Size.Y);
				const int64 EndX = static_cast<int64>(Placement.TopLeft.X) + SizeX;
				const int64 EndY = static_cast<int64>(Placement.TopLeft.Y) + SizeY;
				return CellX >= Placement.TopLeft.X && static_cast<int64>(CellX) < EndX
					&& CellY >= Placement.TopLeft.Y && static_cast<int64>(CellY) < EndY;
			};

			const FInventoryItemGridData* TargetPlacement = Placements.FindByPredicate(
				[&TargetTopLeft, &HoverCellX, &HoverCellY, DoesPlacementCoverCell](const FInventoryItemGridData& Entry)
				{
					return Entry.IsValid()
						&& (DoesPlacementCoverCell(Entry, TargetTopLeft.X, TargetTopLeft.Y)
							|| DoesPlacementCoverCell(Entry, HoverCellX, HoverCellY));
				});

			if (TargetPlacement && TargetPlacement->ItemId != DragOp->ItemId && TargetPlacement->ItemId.IsValid())
			{
				InventoryComponent->Server_SwapItemsInGrid(DragOp->ItemId, TargetPlacement->ItemId);
				UE_LOG(LogTemp, Display, TEXT("[InventoryBagDnD] Swap request %s <-> %s"),
					*DragOp->ItemId.ToString(), *TargetPlacement->ItemId.ToString());
				return true;
			}

			UE_LOG(LogTemp, Warning, TEXT("[InventoryBagDnD] Bag CanPlaceItemAt rejected Item=%s Size=(%d,%d) Target=(%d,%d) and no swap target"),
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

bool UW_InventoryBag_DnD::ScreenToGridCell(
	const FVector2D& ScreenPos,
	FIntPoint ItemSize,
	const FVector2D& GrabOffsetPx,
	FIntPoint& OutTopLeft) const
{
	if (!GridPanel_Items)
	{
		return false;
	}

	const FGeometry GridGeometry = GridPanel_Items->GetCachedGeometry();
	if (!IsFiniteScreenVector(ScreenPos))
	{
		return false;
	}
	const FVector2D LocalPos = GridGeometry.AbsoluteToLocal(ScreenPos);

	UAeyerjiInventoryComponent* InventoryComponent = GetInventoryComponent();
	if (!InventoryComponent)
	{
		return false;
	}
	const FIntPoint GridSize = InventoryComponent->GetGridSize();
	const FVector2D GridExtent = GridGeometry.GetLocalSize();
	if (!IsFiniteScreenVector(LocalPos) || !IsFiniteScreenVector(GrabOffsetPx) || !IsFiniteScreenVector(GridExtent)
		|| !IsUsableInventoryGrid(GridSize) || GridExtent.X <= 0.f || GridExtent.Y <= 0.f)
	{
		return false;
	}
	ItemSize.X = FMath::Clamp(ItemSize.X, 1, GridSize.X);
	ItemSize.Y = FMath::Clamp(ItemSize.Y, 1, GridSize.Y);
	const float DerivedCellWidth = (GridSize.X > 0) ? GridExtent.X / GridSize.X : CellSize.X;
	const float DerivedCellHeight = (GridSize.Y > 0) ? GridExtent.Y / GridSize.Y : CellSize.Y;

	const float CellWidth = FMath::Max(1.f, DerivedCellWidth);
	const float CellHeight = FMath::Max(1.f, DerivedCellHeight);

	// Preserve the cell under the cursor where the drag began instead of snapping the item's top-left to the cursor.
	const FVector2D DesiredTopLeft = LocalPos - GrabOffsetPx;
	int32 CellX = FMath::FloorToInt(DesiredTopLeft.X / CellWidth);
	int32 CellY = FMath::FloorToInt(DesiredTopLeft.Y / CellHeight);

	CellX = FMath::Clamp(CellX, 0, FMath::Max(0, GridSize.X - ItemSize.X));
	CellY = FMath::Clamp(CellY, 0, FMath::Max(0, GridSize.Y - ItemSize.Y));

	OutTopLeft = FIntPoint(CellX, CellY);
	UE_LOG(LogTemp, Display, TEXT("[InventoryBagDnD] Screen=%s Local=%s -> Cell=(%d,%d) Size=(%d,%d)"),
		*ScreenPos.ToString(), *LocalPos.ToString(), CellX, CellY, ItemSize.X, ItemSize.Y);
	return true;
}
