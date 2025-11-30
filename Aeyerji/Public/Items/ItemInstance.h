// ItemInstance.h
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Items/ItemTypes.h"
#include "Delegates/DelegateCombinations.h"

#include "ItemInstance.generated.h"

class UItemDefinition;
class UItemAffixDefinition;
struct FAffixTier;

DECLARE_MULTICAST_DELEGATE(FOnItemInstanceChanged);

/**
 * Runtime instance of an item containing rolled affixes and aggregated modifiers.
 */
UCLASS(BlueprintType, DefaultToInstanced, EditInlineNew)
class AEYERJI_API UAeyerjiItemInstance : public UObject
{
	GENERATED_BODY()

public:
	UAeyerjiItemInstance();

	// UObject
	virtual bool IsSupportedForNetworking() const override { return true; }
	virtual bool IsNameStableForNetworking() const override { return true; }
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(ReplicatedUsing = OnRep_Definition, EditAnywhere, BlueprintReadOnly, Category = "Item")
	TObjectPtr<UItemDefinition> Definition;

	UPROPERTY(ReplicatedUsing = OnRep_Rarity, VisibleAnywhere, BlueprintReadOnly, Category = "Item")
	EItemRarity Rarity = EItemRarity::Common;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Item")
	int32 ItemLevel = 1;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Item")
	FGuid UniqueId;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Item")
	int32 Seed = 0;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Item")
	TArray<FRolledAffix> RolledAffixes;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Item")
	TArray<FItemStatModifier> FinalAggregatedModifiers;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Item")
	TArray<FItemGrantedEffect> GrantedEffects;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Item")
	TArray<FItemGrantedAbility> GrantedAbilities;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Item")
	EEquipmentSlot EquippedSlot = EEquipmentSlot::Offense;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Item")
	int32 EquippedSlotIndex = INDEX_NONE;

	UPROPERTY(ReplicatedUsing = OnRep_InventorySize, VisibleAnywhere, BlueprintReadOnly, Category = "Item")
	FIntPoint InventorySize = FIntPoint(1, 1);

	UFUNCTION(BlueprintCallable, Category = "Item")
	FText GetDisplayName() const;

	UFUNCTION(BlueprintCallable, Category = "Item")
	void RebuildAggregation();

	UFUNCTION(BlueprintPure, Category = "Item")
	const TArray<FItemGrantedEffect>& GetGrantedEffects() const { return GrantedEffects; }
	UFUNCTION(BlueprintPure, Category = "Item")
	const TArray<FItemGrantedAbility>& GetGrantedAbilities() const { return GrantedAbilities; }
	UFUNCTION(BlueprintPure, Category = "Item")
	const TArray<FItemStatModifier>& GetFinalAggregatedModifiers() const { return FinalAggregatedModifiers; }

	/** Returns the pickup visual config associated with this instance's definition (defaults when unavailable). */
	UFUNCTION(BlueprintPure, Category = "Item|Display")
	FAeyerjiPickupVisualConfig GetPickupVisualConfig() const;

	UFUNCTION(BlueprintPure, Category = "Item")
	EItemCategory GetItemCategory() const;

	/** Ensures listeners get a refresh even if no replicated field changed (e.g., ownership transfer). */
	void ForceItemChangedForUI();

	/** Marks the instance as safe for network replication references when dynamically created. */
	void SetNetAddressable();

	void InitializeFromDefinition(
		UItemDefinition* Definition,
		EItemRarity InRarity,
		int32 InItemLevel,
		int32 InSeed,
		EEquipmentSlot InSlot,
		const TArray<UItemAffixDefinition*>& ChosenAffixes,
		const TArray<const FAffixTier*>& ChosenTiers);

	/** Fired whenever replicated visuals/stat state changes (definition, rarity, size, etc). */
	FOnItemInstanceChanged& GetOnItemChangedDelegate() { return OnItemChanged; }

	// UObject
	virtual void PostNetReceive() override;

protected:
	FOnItemInstanceChanged OnItemChanged;

	void NotifyItemChanged();

	UFUNCTION()
	void OnRep_Definition();

	UFUNCTION()
	void OnRep_Rarity();

	UFUNCTION()
	void OnRep_InventorySize();
};
