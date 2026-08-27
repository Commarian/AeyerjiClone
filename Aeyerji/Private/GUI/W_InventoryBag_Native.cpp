// W_InventoryBag_Native.cpp

#include "GUI/W_InventoryBag_Native.h"

#include "Components/GridPanel.h"
#include "Blueprint/WidgetTree.h"
#include "Components/GridSlot.h"
#include "Components/SizeBox.h"
#include "Components/Widget.h"
#include "GUI/W_ItemTile.h"
#include "GUI/W_EquipmentSlot.h"
#include "GUI/ItemTooltipData.h"
#include "Inventory/AeyerjiInventoryBPFL.h"
#include "Logging/AeyerjiLog.h"
#include "Player/PlayerParentNative.h"
#include "Engine/Texture2D.h"
#include "UObject/ConstructorHelpers.h"

UW_InventoryBag_Native::UW_InventoryBag_Native(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	static ConstructorHelpers::FObjectFinder<UTexture2D> DefaultEmptyIcon(
		TEXT("/Game/GUI/GeneralImages/item_general_empty_icon.item_general_empty_icon"));
	if (DefaultEmptyIcon.Succeeded())
	{
		EmptyInventorySlotIcon = DefaultEmptyIcon.Object;
	}
}

void UW_InventoryBag_Native::NativeConstruct()
{
	Super::NativeConstruct();

	DiscoverEquipmentSlots();
	RefreshCorruptionLaneVisibility(Inventory.IsValid() && Inventory->GetUnlockedEquipmentSlotCount(EEquipmentSlot::Corruption) > 0);

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
		Inventory->OnEquipmentSlotUnlocksChanged.RemoveAll(this);
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
		AJ_LOG(this, TEXT("[InventoryUI] BindToPlayer skipped: Player=None Widget=%s"), *GetNameSafe(this));
		return;
	}

	AJ_LOG(this, TEXT("[InventoryUI] BindToPlayer Widget=%s OldPlayer=%s NewPlayer=%s"),
		*GetNameSafe(this),
		*GetNameSafe(BoundPlayer.Get()),
		*GetNameSafe(Player));

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

bool UW_InventoryBag_Native::DropItemUnderCursor(float ForwardOffset)
{
	if (!Inventory.IsValid() || !GridPanel_Items)
	{
		return false;
	}

	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return false;
	}

	float MouseX = 0.f;
	float MouseY = 0.f;
	if (!PC->GetMousePosition(MouseX, MouseY))
	{
		return false;
	}

	const FGeometry GridGeometry = GridPanel_Items->GetCachedGeometry();
	const FVector2D GridExtent = GridGeometry.GetLocalSize();
	if (GridExtent.X <= 0.f || GridExtent.Y <= 0.f)
	{
		return false;
	}

	const FIntPoint GridSize = Inventory->GetGridSize();
	if (GridSize.X <= 0 || GridSize.Y <= 0)
	{
		return false;
	}

	if (!FMath::IsFinite(MouseX) || !FMath::IsFinite(MouseY)
		|| !FMath::IsFinite(GridExtent.X) || !FMath::IsFinite(GridExtent.Y))
	{
		return false;
	}

	const FVector2D ScreenPos(MouseX, MouseY);
	const FVector2D LocalPos = GridGeometry.AbsoluteToLocal(ScreenPos);
	if (!FMath::IsFinite(LocalPos.X) || !FMath::IsFinite(LocalPos.Y))
	{
		return false;
	}
	if (LocalPos.X < 0.f || LocalPos.Y < 0.f || LocalPos.X > GridExtent.X || LocalPos.Y > GridExtent.Y)
	{
		return false;
	}

	const float CellWidth = GridExtent.X / GridSize.X;
	const float CellHeight = GridExtent.Y / GridSize.Y;
	if (CellWidth <= 0.f || CellHeight <= 0.f)
	{
		return false;
	}

	const int32 CellX = FMath::Clamp(FMath::FloorToInt(LocalPos.X / CellWidth), 0, GridSize.X - 1);
	const int32 CellY = FMath::Clamp(FMath::FloorToInt(LocalPos.Y / CellHeight), 0, GridSize.Y - 1);

	TArray<FInventoryItemGridData> Placements;
	Inventory->GetGridPlacements(Placements);
	for (const FInventoryItemGridData& Placement : Placements)
	{
		if (!Placement.IsValid())
		{
			continue;
		}

		const int32 SizeX = FMath::Max(1, Placement.Size.X);
		const int32 SizeY = FMath::Max(1, Placement.Size.Y);
		const int64 EndX = static_cast<int64>(Placement.TopLeft.X) + SizeX;
		const int64 EndY = static_cast<int64>(Placement.TopLeft.Y) + SizeY;
		if (CellX >= Placement.TopLeft.X && static_cast<int64>(CellX) < EndX
			&& CellY >= Placement.TopLeft.Y && static_cast<int64>(CellY) < EndY)
		{
			UAeyerjiItemInstance* Item = Placement.ItemInstance ? Placement.ItemInstance.Get() : nullptr;
			if (!Item && Placement.ItemId.IsValid())
			{
				Item = Inventory->FindItemById(Placement.ItemId);
			}

			if (Item)
			{
				HideItemTooltip(nullptr);
				return UAeyerjiInventoryBPFL::DropItemAtOwner(Inventory.Get(), Item, ForwardOffset);
			}
		}
	}

	return false;
}

void UW_InventoryBag_Native::RegisterEquipmentSlot(UW_EquipmentSlot* SlotWidget)
{
	if (!SlotWidget)
	{
		return;
	}

	int32 SlotIndex = SlotWidget ? SlotWidget->GetEffectiveSlotIndex() : -1;
	const EEquipmentSlot SlotType = SlotWidget->GetEffectiveSlotType();
	AJ_LOG(this, TEXT("RegisterEquipmentSlot Widget=%s Slot=%d SlotIndex=%d"), *GetNameSafe(SlotWidget), static_cast<int32>(SlotType), SlotIndex);
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

	bool bDuplicateResolvedIndex = false;
	TSet<int32> UsedIndicesForSlot;
	for (const TWeakObjectPtr<UW_EquipmentSlot>& SlotEntry : RegisteredEquipmentSlots)
	{
		const UW_EquipmentSlot* ExistingSlot = SlotEntry.Get();
		if (!ExistingSlot || ExistingSlot->GetEffectiveSlotType() != SlotType)
		{
			continue;
		}

		const int32 ExistingIndex = ExistingSlot->GetEffectiveSlotIndex();
		UsedIndicesForSlot.Add(ExistingIndex);
		if (ExistingIndex == SlotIndex)
		{
			bDuplicateResolvedIndex = true;
			AJ_LOG(this, TEXT("[EquipmentSlotIndex] Duplicate equipment slot binding Slot=%d Index=%d Existing=%s New=%s. Check widget names or SlotIndex values."),
				static_cast<int32>(SlotType),
				SlotIndex,
				*GetNameSafe(ExistingSlot),
				*GetNameSafe(SlotWidget));
		}
	}

	if (bDuplicateResolvedIndex)
	{
		const int32 MaxCandidateSlots = Inventory.IsValid()
			? FMath::Max(1, Inventory->GetVisibleEquipmentSlotCount(SlotType))
			: (SlotType == EEquipmentSlot::Corruption ? 3 : 5);

		for (int32 CandidateIndex = 0; CandidateIndex < MaxCandidateSlots; ++CandidateIndex)
		{
			if (!UsedIndicesForSlot.Contains(CandidateIndex))
			{
				SlotWidget->SetRuntimeSlotIndexOverride(CandidateIndex);
				SlotIndex = CandidateIndex;
				AJ_LOG(this, TEXT("[EquipmentSlotIndex] Auto-corrected duplicate equipment slot Widget=%s Slot=%d NewIndex=%d"),
					*GetNameSafe(SlotWidget),
					static_cast<int32>(SlotType),
					SlotIndex);
				break;
			}
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
		AJ_LOG(this, TEXT("[InventoryUI] AttachToInventory skipped: Inventory=None Widget=%s"), *GetNameSafe(this));
		return;
	}

	if (Inventory.Get() == Inv)
	{
		AJ_LOG(this, TEXT("[InventoryUI] AttachToInventory already bound. Widget=%s Inventory=%s Items=%d Equipped=%d Grid=%d"),
			*GetNameSafe(this),
			*GetNameSafe(Inv),
			Inv->Items.Num(),
			Inv->EquippedItems.Num(),
			Inv->GridPlacements.Num());
		DiscoverEquipmentSlots();
		RefreshRegisteredEquipmentSlots();
		Inv->RefreshEquipmentSlotUnlockState();
		RefreshCorruptionLaneVisibility(Inv->GetUnlockedEquipmentSlotCount(EEquipmentSlot::Corruption) > 0);
		DispatchRebuild();
		return;
	}

	if (Inventory.IsValid())
	{
		AJ_LOG(this, TEXT("[InventoryUI] Unbinding inventory widget. Widget=%s OldInventory=%s"),
			*GetNameSafe(this),
			*GetNameSafe(Inventory.Get()));
		Inventory->OnInventoryChanged.RemoveAll(this);
		Inventory->OnInventoryItemStateChanged.RemoveAll(this);
		Inventory->OnEquipmentSlotUnlocksChanged.RemoveAll(this);
		BP_OnInventoryComponentUnbound(Inventory.Get());
		ClearEquipmentSlotBindings();
	}

	Inventory = Inv;
	AJ_LOG(this, TEXT("[InventoryUI] Bound inventory widget. Widget=%s Inventory=%s Owner=%s Items=%d Equipped=%d Grid=%d"),
		*GetNameSafe(this),
		*GetNameSafe(Inv),
		*GetNameSafe(Inv->GetOwner()),
		Inv->Items.Num(),
		Inv->EquippedItems.Num(),
		Inv->GridPlacements.Num());
	Inventory->OnInventoryChanged.AddDynamic(this, &UW_InventoryBag_Native::HandleInventoryGridChanged);
	Inventory->OnInventoryItemStateChanged.AddDynamic(this, &UW_InventoryBag_Native::HandleInventoryItemStateChanged);
	Inventory->OnEquipmentSlotUnlocksChanged.AddDynamic(this, &UW_InventoryBag_Native::HandleEquipmentSlotUnlocksChanged);

	DiscoverEquipmentSlots();
	RefreshRegisteredEquipmentSlots();
	BP_OnInventoryComponentBound(Inv);
	Inventory->RefreshEquipmentSlotUnlockState();
	RefreshCorruptionLaneVisibility(Inventory->GetUnlockedEquipmentSlotCount(EEquipmentSlot::Corruption) > 0);
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

void UW_InventoryBag_Native::HandleEquipmentSlotUnlocksChanged(int32 PlayerLevel, int32 NormalLaneSlots, int32 CorruptionSlots, bool bCorruptionUnlocked)
{
	AJ_LOG(this, TEXT("HandleEquipmentSlotUnlocksChanged Level=%d NormalSlots=%d CorruptionSlots=%d CorruptionUnlocked=%s"),
		PlayerLevel,
		NormalLaneSlots,
		CorruptionSlots,
		bCorruptionUnlocked ? TEXT("true") : TEXT("false"));
	RefreshCorruptionLaneVisibility(bCorruptionUnlocked);
	DiscoverEquipmentSlots();
	RefreshRegisteredEquipmentSlots();
	BP_OnEquipmentSlotUnlocksChanged(PlayerLevel, NormalLaneSlots, CorruptionSlots, bCorruptionUnlocked);
	InvalidateLayoutAndVolatility();
}

void UW_InventoryBag_Native::RefreshCorruptionLaneVisibility(bool bCorruptionUnlocked)
{
	if (!CorruptionBorder && WidgetTree)
	{
		CorruptionBorder = WidgetTree->FindWidget(FName(TEXT("CorruptionBorder")));
	}

	if (!CorruptionBorder)
	{
		AJ_LOG(this, TEXT("[ItemBorder][CorruptionLane] Refresh skipped: CorruptionBorder binding missing Widget=%s Inventory=%s Unlocked=%s"),
			*GetNameSafe(this),
			*GetNameSafe(Inventory.Get()),
			bCorruptionUnlocked ? TEXT("true") : TEXT("false"));
		return;
	}

	const int32 PlayerLevel = Inventory.IsValid() ? Inventory->GetOwnerLevelForInventoryRules() : 1;
	const int32 CorruptionSlots = Inventory.IsValid() ? Inventory->GetUnlockedEquipmentSlotCount(EEquipmentSlot::Corruption) : 0;
	AJ_LOG(this, TEXT("[ItemBorder][CorruptionLane] Refresh Widget=%s Border=%s Level=%d CorruptionSlots=%d Unlocked=%s Visibility=%s"),
		*GetNameSafe(this),
		*GetNameSafe(CorruptionBorder),
		PlayerLevel,
		CorruptionSlots,
		bCorruptionUnlocked ? TEXT("true") : TEXT("false"),
		bCorruptionUnlocked ? TEXT("Visible") : TEXT("Collapsed"));
	CorruptionBorder->SetVisibility(bCorruptionUnlocked ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
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
			//AJ_LOG(this, TEXT("RefreshRegisteredEquipmentSlots -> %s"), *GetNameSafe(SlotEntry.Get()));
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
		Tile->SetBorderMaterial(InventoryTileGenericBorderMaterial);
		Tile->SetTileLayerPadding(InventoryTileIconPadding, InventoryTileBorderPadding);
		Tile->SetTileVisualSize(TileSize);

		const FName TileContainerName = MakeUniqueObjectName(this, USizeBox::StaticClass(), TEXT("InventoryTileContainer"));
		USizeBox* TileContainer = NewObject<USizeBox>(this, USizeBox::StaticClass(), TileContainerName);
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

				EmptyTile->SetEmptySlotIcon(EmptyInventorySlotIcon);
				EmptyTile->SetBorderMaterial(InventoryTileGenericBorderMaterial);
				EmptyTile->SetTileLayerPadding(InventoryTileIconPadding, InventoryTileBorderPadding);
				EmptyTile->SetupEmptySlot();
				EmptyTile->SetTileVisualSize(CellSize);
				const FName EmptyContainerName = MakeUniqueObjectName(this, USizeBox::StaticClass(), TEXT("InventoryEmptyTileContainer"));
				USizeBox* EmptyContainer = NewObject<USizeBox>(this, USizeBox::StaticClass(), EmptyContainerName);
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
