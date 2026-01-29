// W_EquipmentSlot.cpp

#include "GUI/W_EquipmentSlot.h"

#include "Components/Border.h"
#include "Components/Image.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "GUI/AeyerjiItemDragOperation.h"
#include "Components/SizeBox.h"
#include "GUI/W_InventoryBag_Native.h"
#include "Items/ItemDefinition.h"
#include "Items/InventoryComponent.h"
#include "Items/ItemInstance.h"
#include "Inventory/AeyerjiInventoryBPFL.h"
#include "InputCoreTypes.h"
#include "Logging/AeyerjiLog.h"

void UW_EquipmentSlot::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (UW_InventoryBag_Native* OwningBag = GetTypedOuter<UW_InventoryBag_Native>())
	{
		OwningBag->RegisterEquipmentSlot(this);
	}

	//AJ_LOG(this, TEXT("Equipment slot initialized Slot=%d Widget=%s"), GetEffectiveSlotIndex(), *GetName());
	UpdateSlotVisuals();
}

void UW_EquipmentSlot::NativeDestruct()
{
	if (UW_InventoryBag_Native* OwningBag = GetTypedOuter<UW_InventoryBag_Native>())
	{
		OwningBag->UnregisterEquipmentSlot(this);
	}

	UnbindInventory();
	BindToCurrentItem(nullptr);
	Super::NativeDestruct();
}

void UW_EquipmentSlot::BindInventory(UAeyerjiInventoryComponent* InInventory)
{
	if (Inventory.Get() == InInventory)
	{
		//AJ_LOG(this, TEXT("BindInventory ignored (unchanged) Slot=%d Inventory=%s"), GetEffectiveSlotIndex(), *GetNameSafe(InInventory));
		RefreshFromInventory();
		return;
	}

	UnbindInventory();

	if (InInventory)
	{
		Inventory = InInventory;
		InInventory->OnEquippedItemChanged.AddDynamic(this, &UW_EquipmentSlot::HandleInventoryEquippedChanged);
		//AJ_LOG(this, TEXT("BindInventory Slot=%d -> %s"), GetEffectiveSlotIndex(), *GetNameSafe(InInventory));
	}
	else
	{
		Inventory.Reset();
		//AJ_LOG(this, TEXT("BindInventory Slot=%d cleared inventory"), GetEffectiveSlotIndex());
	}

	RefreshFromInventory();
}

bool UW_EquipmentSlot::IsMouseOverItem() const
{
	if (!CurrentItem.IsValid())
	{
		return false;
	}

	return IsHovered() || (ItemIcon && ItemIcon->IsHovered());
}

bool UW_EquipmentSlot::DropItemToGround(float ForwardOffset)
{
	if (!Inventory.IsValid() || !CurrentItem.IsValid())
	{
		return false;
	}

	if (UW_InventoryBag_Native* OwningBag = GetTypedOuter<UW_InventoryBag_Native>())
	{
		OwningBag->HideItemTooltip(nullptr);
	}

	return UAeyerjiInventoryBPFL::DropItemAtOwner(Inventory.Get(), CurrentItem.Get(), ForwardOffset);
}

void UW_EquipmentSlot::UnbindInventory()
{
	if (Inventory.IsValid())
	{
		Inventory->OnEquippedItemChanged.RemoveAll(this);
	}
	Inventory.Reset();
}

void UW_EquipmentSlot::RefreshFromInventory()
{
	if (!Inventory.IsValid())
	{
		//AJ_LOG(this, TEXT("RefreshFromInventory Slot=%d inventory invalid"), GetEffectiveSlotIndex());
		BindToCurrentItem(nullptr);
		return;
	}

	const EEquipmentSlot EffectiveSlot = GetEffectiveSlotType();
	const int32 EffectiveIndex = GetEffectiveSlotIndex();
	UAeyerjiItemInstance* Equipped = Inventory->GetEquipped(EffectiveSlot, EffectiveIndex);
	//AJ_LOG(this, TEXT("RefreshFromInventory Slot=%d Item=%s"), GetEffectiveSlotIndex(), Equipped ? *Equipped->UniqueId.ToString() : TEXT("None"));
	BindToCurrentItem(Equipped);
}

void UW_EquipmentSlot::BindToCurrentItem(UAeyerjiItemInstance* NewItem)
{
	if (CurrentItem.Get() == NewItem)
	{
		UpdateSlotVisuals();
		return;
	}

	ClearItemDelegate();

	CurrentItem = NewItem;
	if (NewItem)
	{
		ItemChangedHandle = NewItem->GetOnItemChangedDelegate().AddUObject(this, &UW_EquipmentSlot::HandleObservedItemChanged);
	}

	UpdateSlotVisuals();
}

void UW_EquipmentSlot::ClearItemDelegate()
{
	if (CurrentItem.IsValid() && ItemChangedHandle.IsValid())
	{
		CurrentItem->GetOnItemChangedDelegate().Remove(ItemChangedHandle);
		ItemChangedHandle.Reset();
	}
}

void UW_EquipmentSlot::HandleObservedItemChanged()
{
	UpdateSlotVisuals();
}

void UW_EquipmentSlot::UpdateSlotVisuals()
{
	const UAeyerjiItemInstance* Item = CurrentItem.Get();
	const bool bHasItem = Item != nullptr;

	if (ItemIcon)
	{
		UTexture2D* EffectiveTexture = nullptr;
		FLinearColor Tint = EmptyTint;

		if (bHasItem && Item->Definition && Item->Definition->Icon)
		{
			EffectiveTexture = Item->Definition->Icon;
			Tint = FLinearColor::White;
		}
		else if (EmptySlotIcon)
		{
			EffectiveTexture = EmptySlotIcon;
		}

		ItemIcon->SetBrushFromTexture(EffectiveTexture, EffectiveTexture != nullptr);
		ItemIcon->SetColorAndOpacity(Tint);

		const FVector2D WidgetDesired = ItemIcon->GetDesiredSize();
		const FVector2D BrushSize = ItemIcon->GetBrush().ImageSize;
		// AJ_LOG(this, TEXT("UpdateSlotVisuals Slot=%d Item=%s Texture=%s Desired=(%.1f,%.1f) Brush=(%.1f,%.1f) Tint=%s"),
		// 	GetEffectiveSlotIndex(),
		// 	bHasItem && Item ? *Item->UniqueId.ToString() : TEXT("None"),
		// 	EffectiveTexture ? *EffectiveTexture->GetName() : TEXT("None"),
		// 	WidgetDesired.X, WidgetDesired.Y,
		// 	BrushSize.X, BrushSize.Y,
		// 	*Tint.ToString());
	}

	if (SlotBorder)
	{
		const EItemRarity Rarity = bHasItem ? Item->Rarity : EItemRarity::Common;
		const float RareMultiplier = 0.15f * static_cast<int32>(Rarity);
		const FLinearColor BorderColor = bHasItem ? Item->RarityTint(Rarity): EmptyTint;
		SlotBorder->SetBrushColor(BorderColor);
	}
}

FReply UW_EquipmentSlot::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton && CurrentItem.IsValid())
	{
		return UWidgetBlueprintLibrary::DetectDragIfPressed(InMouseEvent, this, EKeys::LeftMouseButton).NativeReply;
	}

	if (InMouseEvent.GetEffectingButton() == EKeys::RightMouseButton && Inventory.IsValid() && CurrentItem.IsValid())
	{
		Inventory->Server_UnequipSlot(GetEffectiveSlotType(), GetEffectiveSlotIndex());
		return FReply::Handled();
	}

	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

FReply UW_EquipmentSlot::NativeOnMouseButtonDoubleClick(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (InMouseEvent.GetEffectingButton() != EKeys::LeftMouseButton)
	{
		return Super::NativeOnMouseButtonDoubleClick(InGeometry, InMouseEvent);
	}

	if (Inventory.IsValid() && CurrentItem.IsValid())
	{
		if (UAeyerjiInventoryBPFL::ToggleEquipState(Inventory.Get(), CurrentItem.Get()))
		{
			return FReply::Handled();
		}
	}

	return Super::NativeOnMouseButtonDoubleClick(InGeometry, InMouseEvent);
}

void UW_EquipmentSlot::NativeOnDragDetected(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent, UDragDropOperation*& OutOperation)
{
	Super::NativeOnDragDetected(InGeometry, InMouseEvent, OutOperation);

	if (!Inventory.IsValid() || !CurrentItem.IsValid())
	{
		return;
	}

	UAeyerjiItemDragOperation* DragOp = NewObject<UAeyerjiItemDragOperation>(this);
	if (!DragOp)
	{
		return;
	}

	DragOp->ItemInstance = CurrentItem.Get();
	DragOp->ItemId = CurrentItem->UniqueId;
	DragOp->ItemSize = CurrentItem->InventorySize;
	DragOp->SourceInventory = Inventory;
	DragOp->Source = EAeyerjiItemDragSource::Equipment;
	DragOp->SourceEquipmentSlot = GetEffectiveSlotType();
	DragOp->SourceEquipmentSlotIndex = GetEffectiveSlotIndex();
	DragOp->SourceGridPos = FIntPoint(-1, -1);

	if (DragVisualWidgetClass)
	{
		if (UWorld* World = GetWorld())
		{
			if (UUserWidget* DragVisual = CreateWidget<UUserWidget>(World, DragVisualWidgetClass))
			{
				DragOp->DefaultDragVisual = DragVisual;
			}
		}
	}
	else if (UWidget* FallbackVisual = CreateFallbackDragVisual())
	{
		DragOp->DefaultDragVisual = FallbackVisual;
	}

	OutOperation = DragOp;
}

bool UW_EquipmentSlot::NativeOnDragOver(const FGeometry& InGeometry, const FDragDropEvent& InDragDropEvent, UDragDropOperation* InOperation)
{
	if (UAeyerjiItemDragOperation* DragOp = Cast<UAeyerjiItemDragOperation>(InOperation))
	{
		if (CanAcceptDragOperation(DragOp))
		{
			return true;
		}
	}

	return Super::NativeOnDragOver(InGeometry, InDragDropEvent, InOperation);
}

bool UW_EquipmentSlot::NativeOnDrop(
    const FGeometry& InGeometry,
    const FDragDropEvent& InDragDropEvent,
    UDragDropOperation* InOperation)
{
    UAeyerjiItemDragOperation* DragOp =
        Cast<UAeyerjiItemDragOperation>(InOperation);

    if (!Inventory.IsValid() || !DragOp || !DragOp->ItemInstance)
    {
        return false;
    }

    // Only react to bag or equipment items – you can add more cases later.
    switch (DragOp->Source)
    {
    case EAeyerjiItemDragSource::Bag:
    {
        // If something is already here, try to place it back into the dragged item's original grid slot.
        if (UAeyerjiItemInstance* Existing = Inventory->GetEquipped(GetEffectiveSlotType(), GetEffectiveSlotIndex()))
        {
            const bool bHasPreferred = DragOp->OriginalTopLeft.X >= 0 && DragOp->OriginalTopLeft.Y >= 0;
            if (bHasPreferred && Inventory->CanPlaceItemAt(DragOp->OriginalTopLeft, Existing->InventorySize, DragOp->ItemId))
            {
                Inventory->Server_UnequipSlotToGrid(GetEffectiveSlotType(), GetEffectiveSlotIndex(), DragOp->OriginalTopLeft);
            }
        }

        // Bag → Equipment: equip into *this* slot
        Inventory->Server_EquipItem(
            DragOp->ItemInstance->UniqueId,
            GetEffectiveSlotType(),
            GetEffectiveSlotIndex());

        return true;
    }

    case EAeyerjiItemDragSource::Equipment:
    {
        // Equipment → Equipment: optional swap/redirect
        const EEquipmentSlot SourceSlot = DragOp->SourceEquipmentSlot;
        const EEquipmentSlot TargetSlot = GetEffectiveSlotType();

        if (SourceSlot == TargetSlot)
        {
            if (DragOp->SourceEquipmentSlotIndex != GetEffectiveSlotIndex())
            {
                Inventory->Server_SwapEquippedSlots(TargetSlot, DragOp->SourceEquipmentSlotIndex, GetEffectiveSlotIndex());
            }
            return true;
        }

        // Minimal behaviour: just equip the dragged item into the new slot.
        // Server_EquipItem should automatically clear its old slot.
        Inventory->Server_EquipItem(
            DragOp->ItemInstance->UniqueId,
            TargetSlot,
            INDEX_NONE);

        return true;
    }

    default:
        break;
    }

    return false;
}

void UW_EquipmentSlot::NativeOnMouseEnter(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	Super::NativeOnMouseEnter(InGeometry, InMouseEvent);

	if (CurrentItem.IsValid())
	{
		if (UW_InventoryBag_Native* OwningBag = GetTypedOuter<UW_InventoryBag_Native>())
		{
			OwningBag->ShowItemTooltip(CurrentItem.Get(), InMouseEvent.GetScreenSpacePosition(), this, EItemTooltipSource::EquipmentSlot);
		}
	}
}

void UW_EquipmentSlot::NativeOnMouseLeave(const FPointerEvent& InMouseEvent)
{
	if (UW_InventoryBag_Native* OwningBag = GetTypedOuter<UW_InventoryBag_Native>())
	{
		OwningBag->HideItemTooltip(this);
	}

	Super::NativeOnMouseLeave(InMouseEvent);
}


bool UW_EquipmentSlot::CanAcceptDragOperation(UAeyerjiItemDragOperation* DragOp) const
{
	if (!DragOp)
	{
		return false;
	}

	const UAeyerjiInventoryComponent* InventoryComponent = Inventory.Get();
	if (!InventoryComponent)
	{
		return false;
	}

	// Only accept drags originating from the same inventory for now.
	if (DragOp->SourceInventory.IsValid() && DragOp->SourceInventory.Get() != Inventory.Get())
	{
		return false;
	}

	UAeyerjiItemInstance* Item = InventoryComponent->FindItemById(DragOp->ItemId);
	return IsItemCompatible(Item);
}

bool UW_EquipmentSlot::IsItemCompatible(const UAeyerjiItemInstance* Item) const
{
	if (!Item || !Item->Definition)
	{
		return false;
	}

	const EEquipmentSlot EffectiveSlot = GetEffectiveSlotType();

	// Primary check: matches default or currently equipped slot.
	if (Item->Definition->DefaultSlot == EffectiveSlot || Item->EquippedSlot == EffectiveSlot)
	{
		return true;
	}

	// Fallback: allow category-aligned items even if the asset's default slot is stale.
	return static_cast<EItemCategory>(EffectiveSlot) == Item->Definition->ItemCategory;
}

bool UW_EquipmentSlot::TryEquipFromDragOperation(UAeyerjiItemDragOperation* DragOp)
{
	if (!DragOp)
	{
		return false;
	}

	UAeyerjiInventoryComponent* InventoryComponent = Inventory.Get();
	if (!InventoryComponent || !CanAcceptDragOperation(DragOp))
	{
		return false;
	}

	InventoryComponent->Server_EquipItem(DragOp->ItemId, GetEffectiveSlotType(), GetEffectiveSlotIndex());
	return true;
}

void UW_EquipmentSlot::HandleInventoryEquippedChanged(EEquipmentSlot ChangedSlot, int32 ChangedIndex, UAeyerjiItemInstance* Item)
{
	const EEquipmentSlot EffectiveSlot = GetEffectiveSlotType();
	const int32 EffectiveIndex = GetEffectiveSlotIndex();
	if (ChangedSlot != EffectiveSlot || ChangedIndex != EffectiveIndex)
	{
		return;
	}

	UAeyerjiItemInstance* EffectiveItem = Item;
	if (!EffectiveItem && Inventory.IsValid())
	{
		EffectiveItem = Inventory->GetEquipped(EffectiveSlot, EffectiveIndex);
	}

	// AJ_LOG(this, TEXT("HandleInventoryEquippedChanged Slot=%d Item=%s Effective=%s"),
	// 	GetEffectiveSlotIndex(),
	// 	Item ? *Item->UniqueId.ToString() : TEXT("None"),
	// 	EffectiveItem ? *EffectiveItem->UniqueId.ToString() : TEXT("None"));

	BindToCurrentItem(EffectiveItem);
}

EEquipmentSlot UW_EquipmentSlot::GetEffectiveSlotType() const
{
	return SlotType;
}

int32 UW_EquipmentSlot::GetEffectiveSlotIndex() const
{
	return FMath::Max(0, SlotIndex);
}

UWidget* UW_EquipmentSlot::CreateFallbackDragVisual() const
{
	const UAeyerjiItemInstance* Item = CurrentItem.Get();
	const UTexture2D* IconTexture = (Item && Item->Definition) ? Item->Definition->Icon : nullptr;
	if (!IconTexture)
	{
		return nullptr;
	}

	const float VisualScale = 0.85f;
	FVector2D SlotSize = FVector2D(64.f, 64.f);
	if (SlotBorder)
	{
		SlotSize = SlotBorder->GetCachedGeometry().GetLocalSize();
	}
	else if (ItemIcon)
	{
		SlotSize = ItemIcon->GetCachedGeometry().GetLocalSize();
	}
	if (SlotSize.IsNearlyZero())
	{
		SlotSize = FVector2D(64.f, 64.f);
	}
	const FVector2D VisualSize = SlotSize * VisualScale;

	USizeBox* Wrapper = NewObject<USizeBox>(const_cast<UW_EquipmentSlot*>(this));
	if (!Wrapper)
	{
		return nullptr;
	}
	Wrapper->SetWidthOverride(VisualSize.X);
	Wrapper->SetHeightOverride(VisualSize.Y);

	UImage* ImageWidget = NewObject<UImage>(Wrapper);
	if (!ImageWidget)
	{
		return nullptr;
	}
	ImageWidget->SetBrushFromTexture(const_cast<UTexture2D*>(IconTexture), true);
	ImageWidget->SetOpacity(0.9f);
	ImageWidget->SetDesiredSizeOverride(VisualSize);

	Wrapper->AddChild(ImageWidget);
	return Wrapper;
}
