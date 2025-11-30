// Copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Abilities/AeyerjiAbilitySlot.h"
#include "Items/InventoryComponent.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "AeyerjiSaveGame.generated.h"

USTRUCT(BlueprintType)
struct FAttrSnapshot
{
	GENERATED_BODY()

	UPROPERTY(SaveGame) float XP = 0.f;
	UPROPERTY(SaveGame) int32 Level = 1;
	// add other scalar attributes as needed
};

/**
 * 
 */
UCLASS()
class AEYERJI_API UAeyerjiSaveGame : public USaveGame
{
	GENERATED_BODY()
public:

	/*
	 * So this is where we define what should be serialized. In the case of Aeyerji
	 * we want to serialize only the actionBar and the Attributes the character is holding
	 * at the moment of a save/load.
	 */
	
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite)
	TArray<FAeyerjiAbilitySlot> ActionBar;
	//TODO later use this new struct to save and load XP instead of using the entire attribute thing, it looks like it doesnt serialize well.
	UPROPERTY(SaveGame) FAttrSnapshot Attributes;

	UPROPERTY(SaveGame)
	FAeyerjiInventorySaveData Inventory;

	/** Currently selected passive choice (FName identifier) */
	UPROPERTY(SaveGame)
	FName SelectedPassiveId;
};
