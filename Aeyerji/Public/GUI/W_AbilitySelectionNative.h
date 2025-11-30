// GUI/W_AbilitySelectionNative.h
#pragma once
#include "Blueprint/UserWidget.h"
#include "Abilities/AeyerjiAbilitySlot.h"
#include "W_AbilitySelectionNative.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
	FOnAbilityPicked,    /* delegate name           */
	int32,               SlotIndex,                /* which slot to fill   */
	FAeyerjiAbilitySlot, PickedData                /* the chosen ability   */
);

UCLASS()
class AEYERJI_API UW_AbilitySelectionNative : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Set by ActionBar right before AddToViewport(). */
	UPROPERTY(BlueprintReadOnly) int32 EditingSlotIndex = INDEX_NONE;

	/** Blueprint fires this when user clicks an icon. */
	UPROPERTY(BlueprintAssignable, Category="Events", BlueprintCallable)
	FOnAbilityPicked OnAbilityPicked;

	/** Helper BP call: the widget finished, close itself. */
	UFUNCTION(BlueprintCallable)
	void Close()
	{
		RemoveFromParent();
	}
};
