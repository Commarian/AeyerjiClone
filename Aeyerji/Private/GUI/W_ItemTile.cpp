// W_ItemTile.cpp

#include "GUI/W_ItemTile.h"

#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/SizeBox.h"
#include "GUI/AeyerjiItemDragOperation.h"
#include "Items/InventoryComponent.h"
#include "Inventory/AeyerjiInventoryBPFL.h"
#include "GUI/W_InventoryBag_Native.h"
#include "Items/ItemDefinition.h"
#include "Logging/AeyerjiLog.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	FString ItemTileRarityToLogString(EItemRarity Rarity)
	{
		if (const UEnum* Enum = StaticEnum<EItemRarity>())
		{
			return Enum->GetNameStringByValue(static_cast<int64>(Rarity));
		}

		return FString::FromInt(static_cast<int32>(Rarity));
	}
}

UW_ItemTile::UW_ItemTile(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AssaultBorderMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/GUI/GeneralImages/MI_UI_Equip_Assault.MI_UI_Equip_Assault")));
	GuardBorderMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/GUI/GeneralImages/MI_UI_Equip_Guard.MI_UI_Equip_Guard")));
	FlowBorderMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/GUI/GeneralImages/MI_UI_Equip_Flow.MI_UI_Equip_Flow")));
}

void UW_ItemTile::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	EnsureWidgetTree();
	ApplyLayerPadding();
}

void UW_ItemTile::NativeDestruct()
{
	if (Item)
	{
		Item->GetOnItemChangedDelegate().RemoveAll(this);
	}
	Super::NativeDestruct();
}

void UW_ItemTile::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	EnsureWidgetTree();
	ApplyLayerPadding();
}

void UW_ItemTile::EnsureWidgetTree()
{
	if (RootOverlay && IconImage && BorderImage)
	{
		return;
	}

	if (!WidgetTree)
	{
		WidgetTree = NewObject<UWidgetTree>(this, TEXT("WidgetTree"));
	}

	RootOverlay = WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(), TEXT("RootOverlay"));
	IconImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("IconImage"));
	BorderImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("BorderImage"));

	check(RootOverlay && IconImage && BorderImage);

	IconImage->SetBrushFromTexture(nullptr);
	IconImage->SetBrushTintColor(FSlateColor(FLinearColor::White));
	IconImage->SetDesiredSizeOverride(FVector2D::ZeroVector);

	BorderImage->SetBrushFromTexture(nullptr);
	BorderImage->SetColorAndOpacity(FLinearColor::White);
	BorderImage->SetVisibility(ESlateVisibility::Collapsed);

	if (UOverlaySlot* IconSlot = RootOverlay->AddChildToOverlay(IconImage))
	{
		IconSlot->SetHorizontalAlignment(HAlign_Fill);
		IconSlot->SetVerticalAlignment(VAlign_Fill);
		IconSlot->SetPadding(IconLayerPadding);
	}

	if (UOverlaySlot* BorderSlot = RootOverlay->AddChildToOverlay(BorderImage))
	{
		BorderSlot->SetHorizontalAlignment(HAlign_Fill);
		BorderSlot->SetVerticalAlignment(VAlign_Fill);
		BorderSlot->SetPadding(BorderLayerPadding);
	}

	WidgetTree->RootWidget = RootOverlay;
}

void UW_ItemTile::ApplyLayerPadding()
{
	if (IconImage)
	{
		if (UOverlaySlot* IconSlot = Cast<UOverlaySlot>(IconImage->Slot))
		{
			IconSlot->SetPadding(IconLayerPadding);
		}
	}

	if (BorderImage)
	{
		if (UOverlaySlot* BorderSlot = Cast<UOverlaySlot>(BorderImage->Slot))
		{
			BorderSlot->SetPadding(BorderLayerPadding);
		}
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

void UW_ItemTile::SetEmptySlotIcon(UTexture2D* InIcon)
{
	EmptySlotIcon = InIcon;
	if (bIsPlaceholder)
	{
		RefreshFromItem();
	}
}

void UW_ItemTile::SetBorderMaterial(UMaterialInterface* InMaterial)
{
	EnsureWidgetTree();
	AJ_LOG(this, TEXT("[ItemBorder] GridTile SetBorderMaterial GenericMaterial=%s Item=%s Placeholder=%s"),
		*GetNameSafe(InMaterial),
		Item ? *Item->UniqueId.ToString() : TEXT("None"),
		bIsPlaceholder ? TEXT("true") : TEXT("false"));
	GenericBorderMaterial = InMaterial;
	BorderDynamicMaterial = nullptr;
	if (bIsPlaceholder || Item)
	{
		RefreshFromItem();
	}
	else
	{
		RefreshBorderVisual(nullptr, FLinearColor::Transparent);
	}
}

void UW_ItemTile::SetTileLayerPadding(FMargin InIconPadding, FMargin InBorderPadding)
{
	IconLayerPadding = InIconPadding;
	BorderLayerPadding = InBorderPadding;
	EnsureWidgetTree();
	ApplyLayerPadding();
	AJ_LOG(this, TEXT("[ItemBorder] GridTile layer padding Icon=%s Border=%s"),
		*FString::Printf(TEXT("L=%.2f T=%.2f R=%.2f B=%.2f"), IconLayerPadding.Left, IconLayerPadding.Top, IconLayerPadding.Right, IconLayerPadding.Bottom),
		*FString::Printf(TEXT("L=%.2f T=%.2f R=%.2f B=%.2f"), BorderLayerPadding.Left, BorderLayerPadding.Top, BorderLayerPadding.Right, BorderLayerPadding.Bottom));
}

void UW_ItemTile::BindInventory(UAeyerjiInventoryComponent* InInventory)
{
	Inventory = InInventory;
}

bool UW_ItemTile::IsMouseOverItem() const
{
	if (bIsPlaceholder)
	{
		return false;
	}

	return IsHovered() || (IconImage && IconImage->IsHovered());
}

bool UW_ItemTile::DropItemToGround(float ForwardOffset)
{
	if (!Inventory.IsValid() || !Item || !ItemId.IsValid())
	{
		return false;
	}

	if (UW_InventoryBag_Native* OwningBag = GetTypedOuter<UW_InventoryBag_Native>())
	{
		OwningBag->HideItemTooltip(nullptr);
	}

	return UAeyerjiInventoryBPFL::DropItemAtOwner(Inventory.Get(), Item, ForwardOffset);
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

FReply UW_ItemTile::NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (bIsPlaceholder)
	{
		return FReply::Unhandled();
	}

	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		if (Inventory.IsValid() && Item)
		{
			if (UAeyerjiInventoryBPFL::ToggleEquipState(Inventory.Get(), Item))
			{
				return FReply::Handled();
			}
		}
	}

	return Super::NativeOnMouseButtonDoubleClick(InGeometry, InMouseEvent);
}

void UW_ItemTile::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
	Super::NativeOnDragDetected(InGeometry, InMouseEvent, OutOperation);

	if (bIsPlaceholder || !Inventory.IsValid() || !Item || !ItemId.IsValid())
	{
		return;
	}

	const FName DragOpName = MakeUniqueObjectName(this, UAeyerjiItemDragOperation::StaticClass(), TEXT("AeyerjiItemDragOperation"));
	UAeyerjiItemDragOperation* DragOp = NewObject<UAeyerjiItemDragOperation>(this, UAeyerjiItemDragOperation::StaticClass(), DragOpName);
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

		const FName DragSizeBoxName = MakeUniqueObjectName(DragOp, USizeBox::StaticClass(), TEXT("ItemTileDragSizeBox"));
		if (USizeBox* DragSizeBox = NewObject<USizeBox>(DragOp, USizeBox::StaticClass(), DragSizeBoxName))
		{
			const FVector2D VisualSize = TileVisualSize * 0.85f;
			DragSizeBox->SetWidthOverride(VisualSize.X);
			DragSizeBox->SetHeightOverride(VisualSize.Y);

			const FName DragImageName = MakeUniqueObjectName(DragSizeBox, UImage::StaticClass(), TEXT("ItemTileDragImage"));
			UImage* DragImage = NewObject<UImage>(DragSizeBox, UImage::StaticClass(), DragImageName);
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
		AJ_LOG(this, TEXT("[ItemBorder] GridTile placeholder generic border Item=None EmptyIcon=%s Material=%s"),
			*GetNameSafe(EmptySlotIcon.Get()),
			*GetNameSafe(GenericBorderMaterial));
		RefreshBorderVisual(GenericBorderMaterial, FLinearColor::Transparent);
		return;
	}

	UTexture2D* Icon = nullptr;
	EItemRarity Rarity = EItemRarity::Common;
	FLinearColor RarityTint = FLinearColor::White;

	if (Item && Item->Definition)
	{
		Icon = Item->Definition->Icon;
		Rarity = Item->Rarity;
		RarityTint = Item->RarityTint(Rarity);
	}

	UMaterialInterface* SelectedBorderMaterial = GetBorderMaterialForItem();
	AJ_LOG(this, TEXT("[ItemBorder] GridTile item border Item=%s Definition=%s Category=%d Rarity=%s RarityColor=%s Material=%s"),
		Item ? *Item->UniqueId.ToString() : TEXT("None"),
		(Item && Item->Definition) ? *GetNameSafe(Item->Definition) : TEXT("None"),
		(Item && Item->Definition) ? static_cast<int32>(Item->Definition->ItemCategory) : -1,
		*ItemTileRarityToLogString(Rarity),
		*RarityTint.ToString(),
		*GetNameSafe(SelectedBorderMaterial));

	IconImage->SetBrushFromTexture(Icon, false);
	IconImage->SetDesiredSizeOverride(FVector2D::ZeroVector);
	IconImage->SetColorAndOpacity(FLinearColor::White);
	RefreshBorderVisual(SelectedBorderMaterial, RarityTint);
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

void UW_ItemTile::RefreshBorderVisual(UMaterialInterface* InBorderMaterial, const FLinearColor& RarityColor)
{
	if (!BorderImage)
	{
		AJ_LOG(this, TEXT("[ItemBorder] GridTile missing BorderImage Material=%s RarityColor=%s"),
			*GetNameSafe(InBorderMaterial),
			*RarityColor.ToString());
		return;
	}

	if (InBorderMaterial)
	{
		BorderDynamicMaterial = UMaterialInstanceDynamic::Create(InBorderMaterial, this);
		AJ_LOG(this, TEXT("[ItemBorder] GridTile created dynamic border material BaseMaterial=%s DynamicMaterial=%s"),
			*GetNameSafe(InBorderMaterial),
			*GetNameSafe(BorderDynamicMaterial));

		if (BorderDynamicMaterial)
		{
			BorderDynamicMaterial->SetVectorParameterValue(RarityColorParameterName, RarityColor);
			AJ_LOG(this, TEXT("[ItemBorder] GridTile set dynamic material parameter DynamicMaterial=%s Param=%s Value=%s Placeholder=%s"),
				*GetNameSafe(BorderDynamicMaterial),
				*RarityColorParameterName.ToString(),
				*RarityColor.ToString(),
				bIsPlaceholder ? TEXT("true") : TEXT("false"));

			BorderImage->SetBrushFromMaterial(BorderDynamicMaterial);
			BorderImage->SetColorAndOpacity(FLinearColor::White);
			BorderImage->SetVisibility(ESlateVisibility::HitTestInvisible);
			return;
		}

		AJ_LOG(this, TEXT("[ItemBorder] GridTile failed to create dynamic border material BaseMaterial=%s"),
			*GetNameSafe(InBorderMaterial));
	}

	AJ_LOG(this, TEXT("[ItemBorder] GridTile hiding border Placeholder=%s Material=%s Color=%s"),
		bIsPlaceholder ? TEXT("true") : TEXT("false"),
		*GetNameSafe(InBorderMaterial),
		*RarityColor.ToString());
	BorderImage->SetBrushFromTexture(nullptr, false);
	BorderImage->SetVisibility(ESlateVisibility::Collapsed);
	BorderImage->SetColorAndOpacity(FLinearColor::White);
}

UMaterialInterface* UW_ItemTile::GetBorderMaterialForItem() const
{
	if (!Item || !Item->Definition)
	{
		return nullptr;
	}

	switch (Item->Definition->ItemCategory)
	{
	case EItemCategory::Assault:
		if (!AssaultBorderMaterial.IsNull())
		{
			return AssaultBorderMaterial.LoadSynchronous();
		}
		break;
	case EItemCategory::Guard:
		if (!GuardBorderMaterial.IsNull())
		{
			return GuardBorderMaterial.LoadSynchronous();
		}
		break;
	case EItemCategory::Flow:
		if (!FlowBorderMaterial.IsNull())
		{
			return FlowBorderMaterial.LoadSynchronous();
		}
		break;
	case EItemCategory::Corruption:
		if (!CorruptionBorderMaterial.IsNull())
		{
			return CorruptionBorderMaterial.LoadSynchronous();
		}
		break;
	default:
		break;
	}

	return GenericBorderMaterial;
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
