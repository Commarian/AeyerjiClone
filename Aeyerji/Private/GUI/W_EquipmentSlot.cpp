// W_EquipmentSlot.cpp

#include "GUI/W_EquipmentSlot.h"

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
#include "GUI/AeyerjiStringLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	FString EquipmentSlotToLogString(EEquipmentSlot Slot)
	{
		if (const UEnum* Enum = StaticEnum<EEquipmentSlot>())
		{
			return Enum->GetNameStringByValue(static_cast<int64>(Slot));
		}

		return FString::FromInt(static_cast<int32>(Slot));
	}

	FString ItemRarityToLogString(EItemRarity Rarity)
	{
		if (const UEnum* Enum = StaticEnum<EItemRarity>())
		{
			return Enum->GetNameStringByValue(static_cast<int64>(Rarity));
		}

		return FString::FromInt(static_cast<int32>(Rarity));
	}

	FString ItemCategoryToLogString(EItemCategory Category)
	{
		if (const UEnum* Enum = StaticEnum<EItemCategory>())
		{
			return Enum->GetNameStringByValue(static_cast<int64>(Category));
		}

		return FString::FromInt(static_cast<int32>(Category));
	}

	bool LooksLikeEquipmentSlotWidgetName(const FString& Name)
	{
		return Name.Contains(TEXT("ItemIcon"), ESearchCase::IgnoreCase)
			|| Name.Contains(TEXT("Equipment"), ESearchCase::IgnoreCase)
			|| Name.Contains(TEXT("Assault"), ESearchCase::IgnoreCase)
			|| Name.Contains(TEXT("Offense"), ESearchCase::IgnoreCase)
			|| Name.Contains(TEXT("Guard"), ESearchCase::IgnoreCase)
			|| Name.Contains(TEXT("Defense"), ESearchCase::IgnoreCase)
			|| Name.Contains(TEXT("Flow"), ESearchCase::IgnoreCase)
			|| Name.Contains(TEXT("Magic"), ESearchCase::IgnoreCase)
			|| Name.Contains(TEXT("Corruption"), ESearchCase::IgnoreCase);
	}

	bool TryParseEquipmentSlotIndex(const FString& Name, int32& OutIndex)
	{
		if (!LooksLikeEquipmentSlotWidgetName(Name))
		{
			return false;
		}

		int32 DigitStart = Name.Len();
		while (DigitStart > 0 && FChar::IsDigit(Name[DigitStart - 1]))
		{
			--DigitStart;
		}

		if (DigitStart >= Name.Len())
		{
			return false;
		}

		OutIndex = FCString::Atoi(*Name.Mid(DigitStart));
		return OutIndex >= 0;
	}
}

UW_EquipmentSlot::UW_EquipmentSlot(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	AssaultLockedIcon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/GUI/GeneralImages/locked_icon_assault.locked_icon_assault")));
	GuardLockedIcon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/GUI/GeneralImages/locked_icon_guard.locked_icon_guard")));
	FlowLockedIcon = TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/GUI/GeneralImages/lock_icon_flow.lock_icon_flow")));
	AssaultBorderMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/GUI/GeneralImages/MI_UI_Equip_Assault.MI_UI_Equip_Assault")));
	GuardBorderMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/GUI/GeneralImages/MI_UI_Equip_Guard.MI_UI_Equip_Guard")));
	FlowBorderMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/GUI/GeneralImages/MI_UI_Equip_Flow.MI_UI_Equip_Flow")));
}

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

	const UImage* EffectiveInsideImage = GetInsideImageWidget();
	return IsHovered() || (EffectiveInsideImage && EffectiveInsideImage->IsHovered());
}

bool UW_EquipmentSlot::IsSlotLocked() const
{
	if (!IsSlotVisibleForCurrentLevel())
	{
		return true;
	}

	return Inventory.IsValid()
		? !Inventory->IsEquipmentSlotUnlocked(GetEffectiveSlotType(), GetEffectiveSlotIndex())
		: GetEffectiveSlotType() == EEquipmentSlot::Corruption;
}

bool UW_EquipmentSlot::IsSlotVisibleForCurrentLevel() const
{
	return Inventory.IsValid()
		? GetEffectiveSlotIndex() < Inventory->GetVisibleEquipmentSlotCount(GetEffectiveSlotType())
		: GetEffectiveSlotType() != EEquipmentSlot::Corruption;
}

bool UW_EquipmentSlot::IsSlotInteractionEnabled() const
{
	return IsSlotVisibleForCurrentLevel() && !IsSlotLocked();
}

ESlateVisibility UW_EquipmentSlot::GetSlotVisibility() const
{
	return IsSlotVisibleForCurrentLevel() ? ESlateVisibility::Visible : ESlateVisibility::Collapsed;
}

UTexture2D* UW_EquipmentSlot::GetLockedSlotIcon() const
{
	if (!IsSlotLocked())
	{
		return nullptr;
	}

	switch (GetEffectiveSlotType())
	{
	case EEquipmentSlot::Assault:
		return AssaultLockedIcon.LoadSynchronous();
	case EEquipmentSlot::Guard:
		return GuardLockedIcon.LoadSynchronous();
	case EEquipmentSlot::Flow:
		return FlowLockedIcon.LoadSynchronous();
	case EEquipmentSlot::Corruption:
		return CorruptionLockedIcon.LoadSynchronous();
	default:
		return nullptr;
	}
}

FText UW_EquipmentSlot::GetSlotDisplayText() const
{
	// Lane names live in GlobalStringTable.csv. Reimport after CSV edits.
	using namespace AeyerjiStringLibrary;
	switch (GetEffectiveSlotType())
	{
	case EEquipmentSlot::Assault:
		return GetGlobalStringTableText(TEXT("Assault"));
	case EEquipmentSlot::Guard:
		return GetGlobalStringTableText(TEXT("Guard"));
	case EEquipmentSlot::Flow:
		return GetGlobalStringTableText(TEXT("Flow"));
	case EEquipmentSlot::Corruption:
		return GetGlobalStringTableText(TEXT("Corruption"));
	default:
		return GetGlobalStringTableText(TEXT("Assault"));
	}
}

FText UW_EquipmentSlot::GetSlotTooltipText() const
{
	if (GetEffectiveSlotType() == EEquipmentSlot::Corruption && IsSlotLocked())
	{
		// Multi-line description from GlobalStringTable.csv
		return AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("CorruptionSlotTooltip"));
	}

	return GetSlotDisplayText();
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
	const bool bLocked = IsSlotLocked();

	SetVisibility(GetSlotVisibility());
	SetIsEnabled(IsSlotInteractionEnabled() || bLocked);

	UImage* EffectiveInsideImage = GetInsideImageWidget();
	if (EffectiveInsideImage)
	{
		UTexture2D* EffectiveTexture = nullptr;

		if (bLocked)
		{
			EffectiveTexture = GetLockedSlotIcon();
		}
		else if (bHasItem && Item->Definition && Item->Definition->Icon)
		{
			EffectiveTexture = Item->Definition->Icon;
		}
		else if (UTexture2D* LaneEmptyIcon = GetEmptySlotIcon())
		{
			EffectiveTexture = LaneEmptyIcon;
		}

		EffectiveInsideImage->SetBrushFromTexture(EffectiveTexture, EffectiveTexture != nullptr);
		EffectiveInsideImage->SetColorAndOpacity(FLinearColor::White);

		const FVector2D WidgetDesired = EffectiveInsideImage->GetDesiredSize();
		const FVector2D BrushSize = EffectiveInsideImage->GetBrush().ImageSize;
		// AJ_LOG(this, TEXT("UpdateSlotVisuals Slot=%d Item=%s Texture=%s Desired=(%.1f,%.1f) Brush=(%.1f,%.1f) Tint=%s"),
		// 	GetEffectiveSlotIndex(),
		// 	bHasItem && Item ? *Item->UniqueId.ToString() : TEXT("None"),
		// 	EffectiveTexture ? *EffectiveTexture->GetName() : TEXT("None"),
		// 	WidgetDesired.X, WidgetDesired.Y,
		// 	BrushSize.X, BrushSize.Y,
		// 	*FLinearColor::White.ToString());
	}

	UpdateBorderVisual(Item);
}

FReply UW_EquipmentSlot::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	if (!IsSlotInteractionEnabled())
	{
		return FReply::Unhandled();
	}

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
	if (!IsSlotInteractionEnabled())
	{
		return FReply::Unhandled();
	}

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

	const FName DragOpName = MakeUniqueObjectName(this, UAeyerjiItemDragOperation::StaticClass(), TEXT("AeyerjiItemDragOperation"));
	UAeyerjiItemDragOperation* DragOp = NewObject<UAeyerjiItemDragOperation>(this, UAeyerjiItemDragOperation::StaticClass(), DragOpName);
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
		if (IsSlotInteractionEnabled() && CanAcceptDragOperation(DragOp))
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

	const bool bCanAcceptDrop = DragOp && CanAcceptDragOperation(DragOp);
    if (!Inventory.IsValid()
		|| !DragOp
		|| !DragOp->ItemInstance
		|| !IsSlotInteractionEnabled()
		|| !bCanAcceptDrop)
    {
		if (DragOp)
		{
			const UItemDefinition* Definition = DragOp->ItemInstance ? DragOp->ItemInstance->Definition.Get() : nullptr;
			AJ_LOG(this, TEXT("[ItemBorder][EquipmentDrop] Drop rejected Widget=%s TargetSlot=%s TargetIndex=%d Item=%s Category=%s Inventory=%s Interaction=%s CanAccept=%s"),
				*GetNameSafe(this),
				*EquipmentSlotToLogString(GetEffectiveSlotType()),
				GetEffectiveSlotIndex(),
				DragOp->ItemInstance ? *DragOp->ItemInstance->UniqueId.ToString() : TEXT("None"),
				Definition ? *ItemCategoryToLogString(Definition->ItemCategory) : TEXT("None"),
				*GetNameSafe(Inventory.Get()),
				IsSlotInteractionEnabled() ? TEXT("true") : TEXT("false"),
				bCanAcceptDrop ? TEXT("true") : TEXT("false"));

			if (DragOp->Source == EAeyerjiItemDragSource::Bag
				|| DragOp->Source == EAeyerjiItemDragSource::Equipment)
			{
				AJ_LOG(this, TEXT("[ItemBorder][EquipmentDrop] Invalid equipment drop consumed so item stays in original slot Source=%d SourceSlot=%s SourceIndex=%d TargetSlot=%s TargetIndex=%d Item=%s"),
					static_cast<int32>(DragOp->Source),
					*EquipmentSlotToLogString(DragOp->SourceEquipmentSlot),
					DragOp->SourceEquipmentSlotIndex,
					*EquipmentSlotToLogString(GetEffectiveSlotType()),
					GetEffectiveSlotIndex(),
					DragOp->ItemInstance ? *DragOp->ItemInstance->UniqueId.ToString() : TEXT("None"));
				return true;
			}
		}
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

	return Inventory.IsValid()
		&& Inventory->CanEquipItemInSlot(Item, EffectiveSlot, GetEffectiveSlotIndex());
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

UTexture2D* UW_EquipmentSlot::GetEmptySlotIcon() const
{
	switch (GetEffectiveSlotType())
	{
	case EEquipmentSlot::Assault:
		if (!AssaultEmptyIcon.IsNull())
		{
			return AssaultEmptyIcon.LoadSynchronous();
		}
		break;
	case EEquipmentSlot::Guard:
		if (!GuardEmptyIcon.IsNull())
		{
			return GuardEmptyIcon.LoadSynchronous();
		}
		break;
	case EEquipmentSlot::Flow:
		if (!FlowEmptyIcon.IsNull())
		{
			return FlowEmptyIcon.LoadSynchronous();
		}
		break;
	case EEquipmentSlot::Corruption:
		if (!CorruptionEmptyIcon.IsNull())
		{
			return CorruptionEmptyIcon.LoadSynchronous();
		}
		break;
	default:
		break;
	}

	return EmptySlotIcon;
}

UTexture2D* UW_EquipmentSlot::GetBorderSlotIcon() const
{
	switch (GetEffectiveSlotType())
	{
	case EEquipmentSlot::Assault:
		if (!AssaultBorderIcon.IsNull())
		{
			return AssaultBorderIcon.LoadSynchronous();
		}
		break;
	case EEquipmentSlot::Guard:
		if (!GuardBorderIcon.IsNull())
		{
			return GuardBorderIcon.LoadSynchronous();
		}
		break;
	case EEquipmentSlot::Flow:
		if (!FlowBorderIcon.IsNull())
		{
			return FlowBorderIcon.LoadSynchronous();
		}
		break;
	case EEquipmentSlot::Corruption:
		if (!CorruptionBorderIcon.IsNull())
		{
			return CorruptionBorderIcon.LoadSynchronous();
		}
		break;
	default:
		break;
	}

	return GenericBorderIcon;
}

UMaterialInterface* UW_EquipmentSlot::GetBorderSlotMaterial() const
{
	switch (GetEffectiveSlotType())
	{
	case EEquipmentSlot::Assault:
		if (!AssaultBorderMaterial.IsNull())
		{
			return AssaultBorderMaterial.LoadSynchronous();
		}
		break;
	case EEquipmentSlot::Guard:
		if (!GuardBorderMaterial.IsNull())
		{
			return GuardBorderMaterial.LoadSynchronous();
		}
		break;
	case EEquipmentSlot::Flow:
		if (!FlowBorderMaterial.IsNull())
		{
			return FlowBorderMaterial.LoadSynchronous();
		}
		break;
	case EEquipmentSlot::Corruption:
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

UImage* UW_EquipmentSlot::GetInsideImageWidget() const
{
	return InsideImage ? InsideImage.Get() : ItemIcon.Get();
}

void UW_EquipmentSlot::UpdateBorderVisual(const UAeyerjiItemInstance* Item)
{
	if (!BorderImage)
	{
		AJ_LOG(this, TEXT("[ItemBorder] EquipmentSlot missing BorderImage binding Slot=%s Index=%d Item=%s"),
			*EquipmentSlotToLogString(GetEffectiveSlotType()),
			GetEffectiveSlotIndex(),
			Item ? *Item->UniqueId.ToString() : TEXT("None"));
		return;
	}

	if (UMaterialInterface* BorderMaterial = GetBorderSlotMaterial())
	{
		const FLinearColor RarityColor = Item
			? Item->RarityTint(Item->Rarity)
			: FLinearColor::Transparent;
		AJ_LOG(this, TEXT("[ItemBorder] EquipmentSlot material border Slot=%s Index=%d Material=%s Item=%s Rarity=%s RarityColor=%s Param=%s"),
			*EquipmentSlotToLogString(GetEffectiveSlotType()),
			GetEffectiveSlotIndex(),
			*GetNameSafe(BorderMaterial),
			Item ? *Item->UniqueId.ToString() : TEXT("None"),
			Item ? *ItemRarityToLogString(Item->Rarity) : TEXT("None"),
			*RarityColor.ToString(),
			*RarityColorParameterName.ToString());

		BorderImage->SetBrushFromMaterial(BorderMaterial);
		BorderImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		BorderImage->SetColorAndOpacity(FLinearColor::White);

		if (UMaterialInstanceDynamic* DynamicMaterial = BorderImage->GetDynamicMaterial())
		{
			DynamicMaterial->SetVectorParameterValue(RarityColorParameterName, RarityColor);
			AJ_LOG(this, TEXT("[ItemBorder] EquipmentSlot set dynamic material parameter DynamicMaterial=%s Param=%s Value=%s"),
				*GetNameSafe(DynamicMaterial),
				*RarityColorParameterName.ToString(),
				*RarityColor.ToString());
		}
		else
		{
			AJ_LOG(this, TEXT("[ItemBorder] EquipmentSlot failed to get dynamic material after SetBrushFromMaterial Slot=%s Index=%d Material=%s"),
				*EquipmentSlotToLogString(GetEffectiveSlotType()),
				GetEffectiveSlotIndex(),
				*GetNameSafe(BorderMaterial));
		}
		return;
	}

	if (UTexture2D* BorderTexture = GetBorderSlotIcon())
	{
		AJ_LOG(this, TEXT("[ItemBorder] EquipmentSlot texture fallback Slot=%s Index=%d Texture=%s Item=%s"),
			*EquipmentSlotToLogString(GetEffectiveSlotType()),
			GetEffectiveSlotIndex(),
			*GetNameSafe(BorderTexture),
			Item ? *Item->UniqueId.ToString() : TEXT("None"));
		BorderImage->SetBrushFromTexture(BorderTexture, true);
		BorderImage->SetVisibility(ESlateVisibility::HitTestInvisible);
		BorderImage->SetColorAndOpacity(FLinearColor::White);
		return;
	}

	AJ_LOG(this, TEXT("[ItemBorder] EquipmentSlot no border material or texture Slot=%s Index=%d Item=%s"),
		*EquipmentSlotToLogString(GetEffectiveSlotType()),
		GetEffectiveSlotIndex(),
		Item ? *Item->UniqueId.ToString() : TEXT("None"));
	BorderImage->SetBrushFromTexture(nullptr, false);
	BorderImage->SetVisibility(ESlateVisibility::Collapsed);
	BorderImage->SetColorAndOpacity(FLinearColor::White);
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
	if (RuntimeSlotIndexOverride != INDEX_NONE)
	{
		return RuntimeSlotIndexOverride;
	}

	int32 InferredIndex = INDEX_NONE;
	if (bInferSlotIndexFromWidgetName && TryInferSlotIndexFromWidgetName(InferredIndex))
	{
		return InferredIndex;
	}

	return FMath::Max(0, SlotIndex);
}

void UW_EquipmentSlot::SetRuntimeSlotIndexOverride(int32 InSlotIndex)
{
	RuntimeSlotIndexOverride = InSlotIndex >= 0 ? InSlotIndex : INDEX_NONE;
	UpdateSlotVisuals();
}

bool UW_EquipmentSlot::TryInferSlotIndexFromWidgetName(int32& OutSlotIndex) const
{
	if (TryParseEquipmentSlotIndex(GetName(), OutSlotIndex))
	{
		return true;
	}

	if (const UImage* EffectiveInsideImage = GetInsideImageWidget())
	{
		if (TryParseEquipmentSlotIndex(EffectiveInsideImage->GetName(), OutSlotIndex))
		{
			return true;
		}
	}

	return false;
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
	if (BorderImage)
	{
		SlotSize = BorderImage->GetCachedGeometry().GetLocalSize();
	}
	else if (const UImage* EffectiveInsideImage = GetInsideImageWidget())
	{
		SlotSize = EffectiveInsideImage->GetCachedGeometry().GetLocalSize();
	}
	if (SlotSize.IsNearlyZero())
	{
		SlotSize = FVector2D(64.f, 64.f);
	}
	const FVector2D VisualSize = SlotSize * VisualScale;

	UW_EquipmentSlot* MutableThis = const_cast<UW_EquipmentSlot*>(this);
	const FName WrapperName = MakeUniqueObjectName(MutableThis, USizeBox::StaticClass(), TEXT("EquipmentDragWrapper"));
	USizeBox* Wrapper = NewObject<USizeBox>(MutableThis, USizeBox::StaticClass(), WrapperName);
	if (!Wrapper)
	{
		return nullptr;
	}
	Wrapper->SetWidthOverride(VisualSize.X);
	Wrapper->SetHeightOverride(VisualSize.Y);

	const FName ImageName = MakeUniqueObjectName(Wrapper, UImage::StaticClass(), TEXT("EquipmentDragImage"));
	UImage* ImageWidget = NewObject<UImage>(Wrapper, UImage::StaticClass(), ImageName);
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
