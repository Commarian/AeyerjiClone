// W_ItemTile.cpp

#include "GUI/W_ItemTile.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Image.h"
#include "Components/SizeBox.h"
#include "GUI/AeyerjiItemDragOperation.h"
#include "Items/InventoryComponent.h"
#include "GUI/W_InventoryBag_Native.h"
#include "Items/ItemDefinition.h"
#include "Logging/AeyerjiLog.h"

void UW_ItemTile::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	EnsureWidgetTree();
}

void UW_ItemTile::NativeDestruct()
{
	if (Item)
	{
		Item->GetOnItemChangedDelegate().RemoveAll(this);
	}
	Super::NativeDestruct();
}

void UW_ItemTile::EnsureWidgetTree()
{
	if (RootBorder && IconImage)
	{
		return;
	}

	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"));
	}

	RootBorder = WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(), TEXT("RootBorder"));
	IconImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("IconImage"));

	check(RootBorder && IconImage);

	RootBorder->SetPadding(FMargin(2.f));
	RootBorder->SetBrushColor(FLinearColor::Black);
	IconImage->SetBrushFromTexture(nullptr);
	IconImage->SetBrushTintColor(FSlateColor(FLinearColor::White));
	IconImage->SetDesiredSizeOverride(FVector2D::ZeroVector);

	RootBorder->SetContent(IconImage);
	WidgetTree->RootWidget = RootBorder;
}

FLinearColor UW_ItemTile::RarityTint(EItemRarity Rarity) const
{
	switch (Rarity)
	{
	case EItemRarity::Uncommon:         return FLinearColor(0.25f, 1.f, 0.25f, 1.f);
	case EItemRarity::Rare:             return FLinearColor(0.25f, 0.6f, 1.f, 1.f);
	case EItemRarity::Epic:             return FLinearColor(0.5f, 0.25f, 1.f, 1.f);
	case EItemRarity::Pure:             return FLinearColor(0.95f, 0.9f, 0.3f, 1.f);
	case EItemRarity::Legendary:        return FLinearColor(1.f, 0.6f, 0.2f, 1.f);
	case EItemRarity::PerfectLegendary: return FLinearColor(1.f, 0.23f, 0.11f, 1.f);
	case EItemRarity::Celestial:        return FLinearColor(0.13f, 0.95f, 1.f, 1.f);
	default:                            return FLinearColor(0.35f, 0.35f, 0.35f, 1.f);
	}
}

void UW_ItemTile::SetupFromItem_Implementation(UAeyerjiItemInstance* InItem)
{
	EnsureWidgetTree();

	if (Item)
	{
		Item->GetOnItemChangedDelegate().RemoveAll(this);
	}

	Item = InItem;
	ItemId = Item ? Item->UniqueId : FGuid();
	CachedSize = Item ? Item->InventorySize : FIntPoint(1, 1);
	bIsPlaceholder = false;
	SetIsEnabled(true);

	if (Item)
	{
		Item->GetOnItemChangedDelegate().AddUObject(this, &UW_ItemTile::HandleObservedItemChanged);
	}

	RefreshFromItem();
}

void UW_ItemTile::SetupEmptySlot()
{
	EnsureWidgetTree();

	if (Item)
	{
		Item->GetOnItemChangedDelegate().RemoveAll(this);
	}

	Item = nullptr;
	ItemId.Invalidate();
	CachedSize = FIntPoint(1, 1);
	bIsPlaceholder = true;
	SetIsEnabled(false);

	RefreshFromItem();
}

void UW_ItemTile::SetTileVisualSize(FVector2D InSize)
{
	TileVisualSize.X = FMath::Max(1.f, InSize.X);
	TileVisualSize.Y = FMath::Max(1.f, InSize.Y);
}

void UW_ItemTile::BindInventory(UAeyerjiInventoryComponent* InInventory)
{
	Inventory = InInventory;
}

FReply UW_ItemTile::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bIsPlaceholder)
	{
		return FReply::Unhandled();
	}

	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		TryEquipFromTile();
		return FReply::Handled();
	}

	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		PendingGrabOffset = InGeometry.AbsoluteToLocal(InMouseEvent.GetScreenSpacePosition());
		return UWidgetBlueprintLibrary::DetectDragIfPressed(InMouseEvent, this, EKeys::LeftMouseButton).NativeReply;
	}

	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UW_ItemTile::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
	if (bIsPlaceholder || !Inventory.IsValid() || !Item || !ItemId.IsValid())
	{
		return;
	}

	UAeyerjiItemDragOperation* DragOp = NewObject<UAeyerjiItemDragOperation>(this);
	DragOp->ItemId = ItemId;
	DragOp->ItemSize = CachedSize;
	DragOp->SourceInventory = Inventory;
	DragOp->GrabOffsetPx = PendingGrabOffset;
	DragOp->ItemInstance = Item;
	DragOp->SourceGridPos = FIntPoint(-1, -1);

	if (Inventory.IsValid())
	{
		FInventoryItemGridData Existing;
		if (Inventory->GetPlacementForItem(ItemId, Existing))
		{
			DragOp->OriginalTopLeft = Existing.TopLeft;
			DragOp->SourceGridPos = Existing.TopLeft;
		}
	}

	if (IconImage)
	{
		UWidget* DragWidget = nullptr;

		if (USizeBox* DragSizeBox = NewObject<USizeBox>(DragOp))
		{
			const FVector2D VisualSize = TileVisualSize * 0.85f;
			DragSizeBox->SetWidthOverride(VisualSize.X);
			DragSizeBox->SetHeightOverride(VisualSize.Y);

			UImage* DragImage = NewObject<UImage>(DragSizeBox);
			DragImage->SetBrush(IconImage->GetBrush());
			DragImage->SetDesiredSizeOverride(VisualSize);
			DragImage->SetColorAndOpacity(FLinearColor::White);

			DragSizeBox->AddChild(DragImage);
			DragWidget = DragSizeBox;
		}

		DragOp->DefaultDragVisual = DragWidget;
	}

	OutOperation = DragOp;
}

void UW_ItemTile::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);

	if (bIsPlaceholder || !Item)
	{
		return;
	}

	if (UW_InventoryBag_Native* OwningBag = GetTypedOuter<UW_InventoryBag_Native>())
	{
		OwningBag->ShowItemTooltip(Item, InMouseEvent.GetScreenSpacePosition(), this, EItemTooltipSource::InventoryTile);
	}
}

void UW_ItemTile::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	if (UW_InventoryBag_Native* OwningBag = GetTypedOuter<UW_InventoryBag_Native>())
	{
		OwningBag->HideItemTooltip(this);
	}

	Super::NativeOnMouseLeave(InMouseEvent);
}

void UW_ItemTile::HandleObservedItemChanged()
{
	RefreshFromItem();
}

void UW_ItemTile::RefreshFromItem()
{
	if (bIsPlaceholder)
	{
		IconImage->SetBrushFromTexture(EmptySlotIcon, false);
		IconImage->SetColorAndOpacity(EmptySlotIconTint);
		RootBorder->SetBrushColor(EmptySlotBorderColor);
		return;
	}

	UTexture2D* Icon = nullptr;
	EItemRarity Rarity = EItemRarity::Common;

	if (Item && Item->Definition)
	{
		Icon = Item->Definition->Icon;
		Rarity = Item->Rarity;
	}

	IconImage->SetBrushFromTexture(Icon, false);
	IconImage->SetDesiredSizeOverride(FVector2D::ZeroVector);
	IconImage->SetColorAndOpacity(FLinearColor::White);
	RootBorder->SetBrushColor(RarityTint(Rarity));
	if (!Icon)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ItemTile] %s missing icon data (Item=%s Definition=%s)"),
			*GetName(),
			Item ? *Item->UniqueId.ToString() : TEXT("None"),
			(Item && Item->Definition) ? *Item->Definition->GetName() : TEXT("None"));
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("[ItemTile] %s set icon %s for item %s"),
			*GetName(), *Icon->GetName(), Item ? *Item->UniqueId.ToString() : TEXT("None"));
	}
}

void UW_ItemTile::TryEquipFromTile()
{
	AJ_LOG(this, TEXT("TryEquipFromTile1 Item=%s ItemId=%s"),
		Item ? *Item->UniqueId.ToString() : TEXT("None"),
		*ItemId.ToString());
	if (!Inventory.IsValid() || !Item || !ItemId.IsValid())
	{
		return;
	}

	const EEquipmentSlot TargetSlot = Item->Definition
		? Item->Definition->DefaultSlot
		: Item->EquippedSlot;

	AJ_LOG(this, TEXT("TryEquipFromTile2 Item=%s ItemId=%s"),
		Item ? *Item->UniqueId.ToString() : TEXT("None"),
		*ItemId.ToString());

	// If DefaultSlot is stale, the server will sanitize to the category slot; use category as a fallback.
	const EEquipmentSlot FallbackSlot =
		Item->Definition
			? static_cast<EEquipmentSlot>(Item->Definition->ItemCategory)
			: TargetSlot;
	Inventory->Server_EquipItem(ItemId, TargetSlot, INDEX_NONE);
	// Also try the category-aligned slot if the first request is rejected by the server.
	if (Item->EquippedSlotIndex == INDEX_NONE && FallbackSlot != TargetSlot)
	{
		Inventory->Server_EquipItem(ItemId, FallbackSlot, INDEX_NONE);
	}
}
