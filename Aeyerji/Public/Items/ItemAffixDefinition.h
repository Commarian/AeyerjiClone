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
	/** Stable identifier used for save data, comparisons, and debugging (not player-facing). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	FName AffixId;

	/** Player-facing name shown in item UI (for example in the rolled affix list/tooltip). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	FText DisplayName;

	/**
	 * Tags describing what this affix "is" (element, theme, family, etc.).
	 * Used by the generator to resolve exclusions and can also be used by gameplay/UI systems to query affix identity.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	FGameplayTagContainer AffixTags;

	/**
	 * Optional item-category gate for this affix.
	 * If empty, the affix can roll on any item category; otherwise the item must be in this list.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	TArray<EItemCategory> AllowedCategories;

	/**
	 * Optional equipment-slot gate for this affix.
	 * If empty, the affix can roll in any slot; otherwise the item must be equipped in an allowed slot.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	FEquipmentSlotFilter SlotFilter;

	/**
	 * Roll tiers for this affix, selected by weighted random.
	 * - Only tiers with (ItemLevel >= MinItemLevel) participate in the roll.
	 * - The chosen tier contributes a random UnitRoll in [MinRoll, MaxRoll].
	 * - UnitRoll is used by AttributeContributions (Magnitude = Scale * UnitRoll).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	TArray<FAffixTier> Tiers;

	/**
	 * List of attribute modifiers this affix contributes when rolled.
	 * For each entry, the final magnitude is computed as: (TierUnitRoll * Scale) and applied with Op.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	TArray<FAttributeRoll> AttributeContributions;

	/**
	 * Gameplay effects granted by this affix while the item is equipped/active.
	 * These are copied into the item instance when the affix is rolled (and may be scaled later by rarity rules).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	TArray<FItemGrantedEffect> GrantedEffects;

	/**
	 * Gameplay abilities granted by this affix while the item is equipped/active.
	 * These are copied into the item instance when the affix is rolled.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	TArray<FItemGrantedAbility> GrantedAbilities;

	/**
	 * Tags that should not co-exist with this affix on the same item.
	 * During generation, this affix is skipped if ExclusionTags overlap with any already-chosen AffixTags;
	 * likewise, it will prevent later affixes whose AffixTags overlap this set.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Affix")
	FGameplayTagContainer ExclusionTags;

	/** Returns the sum of tier weights that are eligible for ItemLevel (clamped to non-negative). */
	int32 GetTotalWeight(int32 ItemLevel) const;

	/** Selects a tier using the eligible weights for ItemLevel (returns null if nothing can roll). */
	const FAffixTier* RollTier(FRandomStream& RNG, int32 ItemLevel) const;

	/** Builds rolled attribute modifiers for the given tier (one modifier per AttributeContributions entry). */
	void BuildFinalModifiers(const FAffixTier& Tier, FRandomStream& RNG, TArray<FItemStatModifier>& OutMods) const;

	/** Returns whether this affix is eligible for the given item category and equipped slot gates. */
	bool IsAllowedFor(EItemCategory ItemCategory, EEquipmentSlot Slot) const;
};
