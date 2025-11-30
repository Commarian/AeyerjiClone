// AeyerjiItemDragOperation.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/DragDropOperation.h"
#include "Items/ItemTypes.h"
#include "AeyerjiItemDragOperation.generated.h"

class UAeyerjiInventoryComponent;
class UAeyerjiItemInstance;

UENUM(BlueprintType)
enum class EAeyerjiItemDragSource : uint8
{
    Bag,
    Equipment
};

/**
 * Carries inventory placement details while dragging an item within the bag.
 */
UCLASS()
class AEYERJI_API UAeyerjiItemDragOperation : public UDragDropOperation
{
	GENERATED_BODY()

public:
	/** Item being dragged. */
	UPROPERTY(BlueprintReadOnly, Category = "Aeyerji|Inventory")
	FGuid ItemId;

	/** Size of the item in grid cells. */
	UPROPERTY(BlueprintReadOnly, Category = "Aeyerji|Inventory")
	FIntPoint ItemSize = FIntPoint(1, 1);

	/** Original location to ignore when validating destination. */
	UPROPERTY(BlueprintReadOnly, Category = "Aeyerji|Inventory")
	FIntPoint OriginalTopLeft = FIntPoint::ZeroValue;

	/** Mouse offset (in pixels) relative to the tile when drag started. */
	UPROPERTY(BlueprintReadOnly, Category = "Aeyerji|Inventory")
	FVector2D GrabOffsetPx = FVector2D::ZeroVector;

	/** Inventory component that initiated the drag. */
	UPROPERTY(BlueprintReadOnly, Category = "Aeyerji|Inventory")
	TWeakObjectPtr<UAeyerjiInventoryComponent> SourceInventory;

	UPROPERTY(BlueprintReadWrite, Category = "Aeyerji|Inventory")
    TObjectPtr<UAeyerjiItemInstance> ItemInstance;

    UPROPERTY(BlueprintReadWrite, Category = "Aeyerji|Inventory")
    FIntPoint SourceGridPos = FIntPoint(-1, -1);

    UPROPERTY(BlueprintReadWrite, Category = "Aeyerji|Inventory")
    EAeyerjiItemDragSource Source = EAeyerjiItemDragSource::Bag;

    UPROPERTY(BlueprintReadWrite, Category = "Aeyerji|Inventory")
    EEquipmentSlot SourceEquipmentSlot = EEquipmentSlot::Offense;

	UPROPERTY(BlueprintReadWrite, Category = "Aeyerji|Inventory")
	int32 SourceEquipmentSlotIndex = INDEX_NONE;
};
