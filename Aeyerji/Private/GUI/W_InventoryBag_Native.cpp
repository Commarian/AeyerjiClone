// W_InventoryBag_Native.cpp

#include "GUI/W_InventoryBag_Native.h"

#include "Components/GridPanel.h"
#include "Blueprint/WidgetTree.h"
#include "Components/GridSlot.h"
#include "Components/SizeBox.h"
#include "GUI/W_ItemTile.h"
#include "GUI/W_EquipmentSlot.h"
#include "GUI/ItemTooltipData.h"
#include "Logging/AeyerjiLog.h"
#include "Player/PlayerParentNative.h"

void UW_InventoryBag_Native::NativeConstruct()
{
	Super::NativeConstruct();

	DiscoverEquipmentSlots();

	if (!ItemTileClass)
	{
		ItemTileClass = UW_ItemTile::StaticClass();
	}

	if (APawn* Pawn = GetOwningPlayerPawn())
	{
		if (APlayerParentNative* Player = Cast<APlayerParentNative>(Pawn))
		{
			BindToPlayer(Player);
		}
	}
}

void UW_InventoryBag_Native::NativeDestruct()
{
	if (Inventory.IsValid())
	{
		Inventory->OnInventoryChanged.RemoveAll(this);
		Inventory->OnInventoryItemStateChanged.RemoveAll(this);
		BP_OnInventoryComponentUnbound(Inventory.Get());
	}

	ClearEquipmentSlotBindings();

	if (BoundPlayer.IsValid())
	{
		BoundPlayer->OnInventoryComponentReady.RemoveDynamic(this, &UW_InventoryBag_Native::AttachToInventory);
	}

	Super::NativeDestruct();
}

void UW_InventoryBag_Native::BindToPlayer(APlayerParentNative* Player)
{
	if (!Player)
	{
		return;
	}

	if (BoundPlayer.IsValid())
	{
		BoundPlayer->OnInventoryComponentReady.RemoveDynamic(this, &UW_InventoryBag_Native::AttachToInventory);
	}

	BoundPlayer = Player;
	Player->OnInventoryComponentReady.AddUniqueDynamic(this, &UW_InventoryBag_Native::AttachToInventory);

	if (UAeyerjiInventoryComponent* Inv = Player->EnsureInventoryComponent())
	{
		AttachToInventory(Inv);
	}
}

void UW_InventoryBag_Native::BindToInventoryComponent(UAeyerjiInventoryComponent* InInventory)
{
	AttachToInventory(InInventory);
}

void UW_InventoryBag_Native::SetCellSize(FVector2D NewCellSize)
{
	const float MinSize = 1.f;
	CellSize.X = FMath::Max(MinSize, NewCellSize.X);
	CellSize.Y = FMath::Max(MinSize, NewCellSize.Y);
	DispatchRebuild();
}

void UW_InventoryBag_Native::SetCellPadding(FMargin NewPadding)
{
	CellPadding = NewPadding;
	DispatchRebuild();
}

void UW_InventoryBag_Native::RefreshInventory()
{
	DispatchRebuild();
}

void UW_InventoryBag_Native::RegisterEquipmentSlot(UW_EquipmentSlot* SlotWidget)
{
	if (!SlotWidget)
	{
		return;
	}

	const int32 SlotIndex = SlotWidget ? SlotWidget->GetEffectiveSlotIndex() : -1;
	AJ_LOG(this, TEXT("RegisterEquipmentSlot Widget=%s SlotIndex=%d"), *GetNameSafe(SlotWidget), SlotIndex);
	RegisteredEquipmentSlots.RemoveAll([](const TWeakObjectPtr<UW_EquipmentSlot>& SlotEntry)
	{
		return !SlotEntry.IsValid();
	});

	for (const TWeakObjectPtr<UW_EquipmentSlot>& SlotEntry : RegisteredEquipmentSlots)
	{
		if (SlotEntry.Get() == SlotWidget)
		{
			if (Inventory.IsValid())
			{
				SlotWidget->BindInventory(Inventory.Get());
			}
			return;
		}
	}

	RegisteredEquipmentSlots.Add(SlotWidget);

	if (Inventory.IsValid())
	{
		SlotWidget->BindInventory(Inventory.Get());
	}
}

void UW_InventoryBag_Native::UnregisterEquipmentSlot(UW_EquipmentSlot* SlotWidget)
{
	if (!SlotWidget)
	{
		return;
	}

	SlotWidget->BindInventory(nullptr);
	RegisteredEquipmentSlots.RemoveAll([SlotWidget](const TWeakObjectPtr<UW_EquipmentSlot>& SlotEntry)
	{
		return !SlotEntry.IsValid() || SlotEntry.Get() == SlotWidget;
	});
}

void UW_InventoryBag_Native::ShowItemTooltip(UAeyerjiItemInstance* Item, FVector2D ScreenPosition, UWidget* SourceWidget, EItemTooltipSource Source)
{
	LastTooltipData = FAeyerjiItemTooltipData::FromItem(Item, Source);
	SetActiveTooltipSource(SourceWidget);
	BP_ShowItemTooltip(LastTooltipData, ScreenPosition, SourceWidget);
}

void UW_InventoryBag_Native::HideItemTooltip(UWidget* SourceWidget)
{
	// Only the active source (or a null override) may hide the tooltip.
	if (ActiveTooltipSource.IsValid() && SourceWidget && ActiveTooltipSource.Get() != SourceWidget)
	{
		return;
	}

	BP_HideItemTooltip(LastTooltipData, SourceWidget);
	ActiveTooltipSource.Reset();
	LastTooltipData = FAeyerjiItemTooltipData();
}

void UW_InventoryBag_Native::AttachToInventory(UAeyerjiInventoryComponent* Inv)
{
	if (!Inv)
	{
		return;
	}

	if (Inventory.Get() == Inv)
	{
		DispatchRebuild();
		return;
	}

	if (Inventory.IsValid())
	{
		Inventory->OnInventoryChanged.RemoveAll(this);
		Inventory->OnInventoryItemStateChanged.RemoveAll(this);
		BP_OnInventoryComponentUnbound(Inventory.Get());
		ClearEquipmentSlotBindings();
	}

	Inventory = Inv;
	Inventory->OnInventoryChanged.AddDynamic(this, &UW_InventoryBag_Native::HandleInventoryGridChanged);
	Inventory->OnInventoryItemStateChanged.AddDynamic(this, &UW_InventoryBag_Native::HandleInventoryItemStateChanged);

	RefreshRegisteredEquipmentSlots();
	BP_OnInventoryComponentBound(Inv);
	DispatchRebuild();
}

void UW_InventoryBag_Native::HandleInventoryGridChanged()
{
	AJ_LOG(this, TEXT("HandleInventoryGridChanged BoundInv=%s"), *GetNameSafe(Inventory.Get()));
	DispatchRebuild();
	RefreshRegisteredEquipmentSlots();
}

void UW_InventoryBag_Native::HandleInventoryItemStateChanged(const FInventoryItemChangeEvent& EventData)
{
	AJ_LOG(this, TEXT("HandleInventoryItemStateChanged Change=%d Slot=%d Item=%s"),
		static_cast<int32>(EventData.Change),
		static_cast<int32>(EventData.Slot),
		EventData.Item ? *EventData.Item->UniqueId.ToString() : TEXT("None"));
	BP_OnInventoryItemStateChanged(EventData);
	RefreshRegisteredEquipmentSlots();
}

void UW_InventoryBag_Native::RefreshRegisteredEquipmentSlots()
{
	if (!Inventory.IsValid())
	{
		return;
	}

	RegisteredEquipmentSlots.RemoveAll([](const TWeakObjectPtr<UW_EquipmentSlot>& SlotEntry)
	{
		return !SlotEntry.IsValid();
	});

	for (const TWeakObjectPtr<UW_EquipmentSlot>& SlotEntry : RegisteredEquipmentSlots)
	{
		if (SlotEntry.IsValid())
		{
			AJ_LOG(this, TEXT("RefreshRegisteredEquipmentSlots -> %s"), *GetNameSafe(SlotEntry.Get()));
			SlotEntry->BindInventory(Inventory.Get());
		}
	}
}

void UW_InventoryBag_Native::ClearEquipmentSlotBindings()
{
	for (const TWeakObjectPtr<UW_EquipmentSlot>& SlotEntry : RegisteredEquipmentSlots)
	{
		if (SlotEntry.IsValid())
		{
			SlotEntry->BindInventory(nullptr);
		}
	}
}

void UW_InventoryBag_Native::DiscoverEquipmentSlots()
{
	if (!WidgetTree)
	{
		return;
	}

	TArray<UWidget*> AllWidgets;
	WidgetTree->GetAllWidgets(AllWidgets);
	for (UWidget* Widget : AllWidgets)
	{
		if (UW_EquipmentSlot* EquipmentSlot = Cast<UW_EquipmentSlot>(Widget))
		{
			RegisterEquipmentSlot(EquipmentSlot);
		}
	}
}

UAeyerjiItemInstance* UW_InventoryBag_Native::ResolveItem(const FGuid& Id) const
{
	return Inventory.IsValid() ? Inventory->FindItemById(Id) : nullptr;
}

void UW_InventoryBag_Native::DispatchRebuild()
{
	if (!Inventory.IsValid())
	{
		UE_LOG(LogTemp, Display, TEXT("[InventoryBag] DispatchRebuild aborted, inventory invalid"));
		return;
	}

	TArray<FInventoryItemGridData> Placements;
	Inventory->GetGridPlacements(Placements);
	UE_LOG(LogTemp, Display, TEXT("[InventoryBag] RebuildGrid with %d placements"), Placements.Num());
	RebuildInventoryGrid(Placements);
}

void UW_InventoryBag_Native::RebuildInventoryGrid_Implementation(const TArray<FInventoryItemGridData>& Placements)
{
	if (!GridPanel_Items)
	{
		UE_LOG(LogAeyerji, Warning, TEXT("W_InventoryBag_Native %s missing GridPanel_Items binding, inventory UI cannot rebuild."), *GetName());
		return;
	}

	GridPanel_Items->ClearChildren();

	const FIntPoint GridSize = Inventory.IsValid() ? Inventory->GetGridSize() : FIntPoint::ZeroValue;
	const int32 NumCells = (GridSize.X > 0 && GridSize.Y > 0) ? GridSize.X * GridSize.Y : 0;
	TArray<bool> Occupied;
	if (NumCells > 0)
	{
		Occupied.Init(false, NumCells);
	}

	for (const FInventoryItemGridData& Placement : Placements)
	{
		if (!Placement.IsValid())
		{
			continue;
		}

		UW_ItemTile* Tile = CreateWidget<UW_ItemTile>(this, ItemTileClass);
		if (!Tile)
		{
			continue;
		}

		UAeyerjiItemInstance* Item = Placement.ItemInstance ? Placement.ItemInstance.Get() : ResolveItem(Placement.ItemId);
		if (Item)
		{
			UE_LOG(LogTemp, Display, TEXT("[InventoryBag] Placing item %s TopLeft=(%d,%d) Size=(%d,%d)"),
				*Placement.ItemId.ToString(), Placement.TopLeft.X, Placement.TopLeft.Y, Placement.Size.X, Placement.Size.Y);
			Tile->SetupFromItem(Item);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[InventoryBag] Missing item instance for %s"), *Placement.ItemId.ToString());
		}

		Tile->BindInventory(Inventory.Get());

		const int32 SpanX = FMath::Max(1, Placement.Size.X);
		const int32 SpanY = FMath::Max(1, Placement.Size.Y);
		const FVector2D TileSize(CellSize.X * SpanX, CellSize.Y * SpanY);
		Tile->SetTileVisualSize(TileSize);

		USizeBox* TileContainer = NewObject<USizeBox>(this, NAME_None);
		if (TileContainer)
		{
			TileContainer->SetWidthOverride(TileSize.X);
			TileContainer->SetHeightOverride(TileSize.Y);
			TileContainer->AddChild(Tile);
		}

		UWidget* ChildToAdd = TileContainer ? static_cast<UWidget*>(TileContainer) : static_cast<UWidget*>(Tile);

		if (UGridSlot* GridSlot = GridPanel_Items->AddChildToGrid(ChildToAdd, Placement.TopLeft.Y, Placement.TopLeft.X))
		{
			GridSlot->SetRowSpan(SpanY);
			GridSlot->SetColumnSpan(SpanX);
			GridSlot->SetPadding(CellPadding);
		}

		if (Occupied.Num() > 0)
		{
			for (int32 Y = 0; Y < SpanY; ++Y)
			{
				const int32 CellY = Placement.TopLeft.Y + Y;
				if (CellY < 0 || CellY >= GridSize.Y)
				{
					continue;
				}

				for (int32 X = 0; X < SpanX; ++X)
				{
					const int32 CellX = Placement.TopLeft.X + X;
					if (CellX < 0 || CellX >= GridSize.X)
					{
						continue;
					}

					const int32 Index = CellY * GridSize.X + CellX;
					if (Occupied.IsValidIndex(Index))
					{
						Occupied[Index] = true;
					}
				}
			}
		}
	}

	if (Occupied.Num() > 0)
	{
		for (int32 Row = 0; Row < GridSize.Y; ++Row)
		{
			for (int32 Column = 0; Column < GridSize.X; ++Column)
			{
				const int32 Index = Row * GridSize.X + Column;
				if (!Occupied.IsValidIndex(Index) || Occupied[Index])
				{
					continue;
				}

				UW_ItemTile* EmptyTile = CreateWidget<UW_ItemTile>(this, ItemTileClass);
				if (!EmptyTile)
				{
					continue;
				}

				EmptyTile->SetupEmptySlot();
				EmptyTile->SetTileVisualSize(CellSize);
				USizeBox* EmptyContainer = NewObject<USizeBox>(this, NAME_None);
				if (EmptyContainer)
				{
					EmptyContainer->SetWidthOverride(CellSize.X);
					EmptyContainer->SetHeightOverride(CellSize.Y);
					EmptyContainer->AddChild(EmptyTile);
				}

				UWidget* EmptyChild = EmptyContainer ? static_cast<UWidget*>(EmptyContainer) : static_cast<UWidget*>(EmptyTile);

				if (UGridSlot* GridSlot = GridPanel_Items->AddChildToGrid(EmptyChild, Row, Column))
				{
					GridSlot->SetPadding(CellPadding);
				}
			}
		}
	}
}

void UW_InventoryBag_Native::SetActiveTooltipSource(UWidget* SourceWidget)
{
	if (!SourceWidget)
	{
		ActiveTooltipSource.Reset();
		return;
	}

	ActiveTooltipSource = SourceWidget;
}
