// ItemDefinition.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Items/ItemTypes.h"

#include "ItemDefinition.generated.h"

class UItemAffixDefinition;
class UTexture2D;
class UMaterialInterface;
class UStaticMesh;
class USkeletalMesh;
class AAeyerjiWeaponActor;

USTRUCT(BlueprintType)
struct AEYERJI_API FItemRarityAffixRange
{
	GENERATED_BODY()

	FItemRarityAffixRange() = default;

	FItemRarityAffixRange(EItemRarity InRarity, int32 InMin, int32 InMax)
		: Rarity(InRarity)
		, MinAffixes(InMin)
		, MaxAffixes(InMax)
	{
	}

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	EItemRarity Rarity = EItemRarity::Common;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	int32 MinAffixes = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	int32 MaxAffixes = 0;
};

/**
 * Base definition for an equippable or lootable item.
 */
UCLASS(BlueprintType)
class AEYERJI_API UItemDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UItemDefinition();

	// UObject
	virtual void PostLoad() override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	FName ItemId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	FText DisplayName;

	/** Short flavor/functional description shown in UI tooltips. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item", meta = (MultiLine = "true"))
	FText Description;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	EItemCategory ItemCategory = EItemCategory::Offense;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	EEquipmentSlot DefaultSlot = EEquipmentSlot::Offense;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item")
	FGameplayTagContainer ItemTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Affixes")
	TArray<FItemRarityAffixRange> RarityAffixRanges;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Stats")
	TArray<FItemStatModifier> BaseModifiers;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Affixes")
	TArray<TObjectPtr<UItemAffixDefinition>> AffixPool;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Effects")
	TArray<FItemGrantedEffect> GrantedEffects;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Effects")
	TArray<FItemGrantedAbility> GrantedAbilities;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Inventory")
	FIntPoint InventorySize = FIntPoint(1, 1);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Display")
	TObjectPtr<UTexture2D> Icon;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Display")
	TObjectPtr<UStaticMesh> WorldMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Display")
	TObjectPtr<USkeletalMesh> WorldSkeletalMesh;

	/** Optional relative offset applied to the world/preview mesh when dropped. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Display")
	FVector WorldMeshOffset = FVector::ZeroVector;

	/** Optional relative rotation applied to the world/preview mesh when dropped. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Display")
	FRotator WorldMeshRotation = FRotator::ZeroRotator;

	/** Optional relative scale applied to the world/preview mesh when dropped. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Display")
	FVector WorldMeshScale = FVector(1.f, 1.f, 1.f);

	/** Configures cosmetic FX to play on the player when this item is looted. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Display")
	FAeyerjiPickupVisualConfig PickupVisuals;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Synergy")
	bool bEnableEquipSynergy = false;

	/** Optional Niagara variable name to override when synergy is active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Synergy", meta = (EditCondition = "bEnableEquipSynergy"))
	FName EquipSynergyColorParameter = NAME_None;

	/**
	 * Optional per-stack color overrides. If empty, a global default mapping is used.
	 * Example: 2 -> red, 3 -> gold, 4 -> cyan, 5 -> custom.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Synergy", meta = (EditCondition = "bEnableEquipSynergy"))
	TArray<FItemEquipSynergyColor> EquipSynergyColors;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Weapon", meta = (EditCondition = "ItemCategory == EItemCategory::Offense"))
	TSubclassOf<AAeyerjiWeaponActor> WeaponActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Item|Weapon", meta = (EditCondition = "ItemCategory == EItemCategory::Offense"))
	FWeaponEquipmentConfig WeaponConfig;

	void GetAffixCountRange(EItemRarity Rarity, int32& OutMin, int32& OutMax) const;

	/** Returns true if a synergy color exists for the given stack count and fills color + param. */
	bool TryGetEquipSynergyColor(int32 StackCount, FLinearColor& OutColor, FName& OutColorParam) const;

	/** Returns the hard-coded preview material to use for a given item rarity (fallback: null). */
	static UMaterialInterface* ResolvePreviewMaterial(EItemRarity Rarity);
};
