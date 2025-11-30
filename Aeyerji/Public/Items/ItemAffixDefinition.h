// ItemAffixDefinition.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Items/ItemTypes.h"

#include "ItemAffixDefinition.generated.h"

/**
 * Data describing a single affix entry (prefix/suffix/etc.).
 */
UCLASS(BlueprintType)
class AEYERJI_API UItemAffixDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	FName AffixId;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	FText DisplayName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	FGameplayTagContainer AffixTags;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	TArray<EItemCategory> AllowedCategories;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	FEquipmentSlotFilter SlotFilter;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	TArray<FAffixTier> Tiers;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	TArray<FAttributeRoll> AttributeContributions;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	TArray<FItemGrantedEffect> GrantedEffects;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	TArray<FItemGrantedAbility> GrantedAbilities;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	FGameplayTagContainer ExclusionTags;

	int32 GetTotalWeight(int32 ItemLevel) const;

	const FAffixTier* RollTier(FRandomStream& RNG, int32 ItemLevel) const;

	void BuildFinalModifiers(const FAffixTier& Tier, FRandomStream& RNG, TArray<FItemStatModifier>& OutMods) const;

	bool IsAllowedFor(EItemCategory ItemCategory, EEquipmentSlot Slot) const;
};
