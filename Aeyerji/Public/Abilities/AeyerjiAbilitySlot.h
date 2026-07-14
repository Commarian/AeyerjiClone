#pragma once
#include "CoreMinimal.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayTagContainer.h"
#include "AeyerjiAbilityTypes.h"
#include "AeyerjiAbilitySlot.generated.h"

class UTexture2D;

/** Fixed-size ability-bar record (7 per player, including potion slot). */
USTRUCT(BlueprintType)
struct FAeyerjiAbilitySlot
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) FGameplayTagContainer Tag;
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) FName Description;
	/** Stable save identifier for the ability class. Filled from Class before save/RPC, resolved back on load. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) TSoftClassPtr<UGameplayAbility> SavedAbilityClass;
	/** Stable save identifier for the icon. Filled from Icon before save/RPC, resolved back on load. */
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) TSoftObjectPtr<UTexture2D> SavedIcon;
	/** Runtime UI pointer resolved from SavedIcon or provided by picker data. Not saved directly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UTexture2D> Icon = nullptr;
	/** Runtime ability class resolved from SavedAbilityClass or provided by picker data. Not saved directly. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TSubclassOf<UGameplayAbility> Class;
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) int32 Level = 1;
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite)
	EAeyerjiTargetMode TargetMode = EAeyerjiTargetMode::Instant;

	/** Returns true when this slot has persisted identity. Runtime Class/Icon alone must be captured first. */
	bool HasPersistentIdentity() const
	{
		return !SavedAbilityClass.IsNull()
			|| !Tag.IsEmpty()
			|| !Description.IsNone()
			|| !SavedIcon.IsNull();
	}

	/** Captures soft references from runtime pointers so the slot survives disk/cloud serialization. */
	void CaptureStableReferences()
	{
		if (Class)
		{
			SavedAbilityClass = TSoftClassPtr<UGameplayAbility>(Class);
		}

		if (Icon)
		{
			SavedIcon = TSoftObjectPtr<UTexture2D>(Icon);
		}
	}

	/** Resolves saved soft references into runtime pointers. */
	void ResolveSavedReferences()
	{
		Class = nullptr;
		Icon = nullptr;

		if (!SavedAbilityClass.IsNull())
		{
			Class = SavedAbilityClass.Get();
			if (!Class)
			{
				Class = SavedAbilityClass.LoadSynchronous();
			}
		}

		if (!SavedIcon.IsNull())
		{
			Icon = SavedIcon.Get();
			if (!Icon)
			{
				Icon = SavedIcon.LoadSynchronous();
			}
		}
	}
};

namespace AeyerjiAbilitySlotUtils
{
	/** Returns true when a slot has no runtime or saved ability identity. */
	FORCEINLINE bool IsAbilitySlotEmpty(const FAeyerjiAbilitySlot& Slot)
	{
		return Slot.Tag.IsEmpty()
			&& Slot.Class == nullptr
			&& Slot.SavedAbilityClass.IsNull();
	}

	/** The potion slot is the fixed final action-bar slot. */
	FORCEINLINE int32 GetPotionSlotIndex(const int32 ActionBarSize)
	{
		return ActionBarSize > 0 ? ActionBarSize - 1 : INDEX_NONE;
	}

	/** Returns true if SlotIndex points at the fixed final potion slot. */
	FORCEINLINE bool IsPotionSlotIndex(const int32 SlotIndex, const int32 ActionBarSize)
	{
		return SlotIndex != INDEX_NONE && SlotIndex == GetPotionSlotIndex(ActionBarSize);
	}

	/** Returns true for any Ability.Potion.* gameplay tag in the container. */
	FORCEINLINE bool IsPotionAbilityTagContainer(const FGameplayTagContainer& Tags)
	{
		static const FString PotionTagPrefix(TEXT("Ability.Potion"));
		for (const FGameplayTag& Tag : Tags)
		{
			if (Tag.IsValid() && Tag.ToString().StartsWith(PotionTagPrefix))
			{
				return true;
			}
		}

		return false;
	}

	/** Validates the slot's ability category against the fixed potion-slot rule. */
	FORCEINLINE bool IsSlotAllowedAtIndex(const FAeyerjiAbilitySlot& Slot, const int32 SlotIndex, const int32 ActionBarSize)
	{
		if (IsAbilitySlotEmpty(Slot))
		{
			return true;
		}

		const bool bIsPotionSlot = IsPotionSlotIndex(SlotIndex, ActionBarSize);
		const bool bIsPotionAbility = IsPotionAbilityTagContainer(Slot.Tag);
		return bIsPotionSlot == bIsPotionAbility;
	}
}
