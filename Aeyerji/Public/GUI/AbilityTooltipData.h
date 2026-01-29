// AbilityTooltipData.h
#pragma once

#include "CoreMinimal.h"
#include "Abilities/AeyerjiAbilitySlot.h"

#include "AbilityTooltipData.generated.h"

class UAbilitySystemComponent;
class UGameplayAbility;
class UAeyerjiAbilityData;
class UTexture2D;

/** Source widget requesting the ability tooltip (action bar vs picker). */
UENUM(BlueprintType)
enum class EAbilityTooltipSource : uint8
{
	ActionBar,
	AbilityPicker
};

/**
 * Unified payload describing an ability for tooltip/pop-up displays.
 * This is intentionally data-only so designers can style the UI freely in UMG.
 */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiAbilityTooltipData
{
	GENERATED_BODY()

public:
	/** Original slot payload used to construct the tooltip. */
	UPROPERTY(BlueprintReadOnly, Category="Ability")
	FAeyerjiAbilitySlot Slot;

	UPROPERTY(BlueprintReadOnly, Category="Ability")
	TObjectPtr<UTexture2D> Icon = nullptr;

	UPROPERTY(BlueprintReadOnly, Category="Ability")
	FText DisplayName = FText::GetEmpty();

	UPROPERTY(BlueprintReadOnly, Category="Ability")
	FText Description = FText::GetEmpty();

	/** Current evaluated mana cost (may be ASC-dependent if scaled). */
	UPROPERTY(BlueprintReadOnly, Category="Ability|Cost")
	float ManaCost = 0.f;

	/** Current evaluated cooldown seconds (may be ASC-dependent if scaled). */
	UPROPERTY(BlueprintReadOnly, Category="Ability|Cost")
	float CooldownSeconds = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Ability|UI")
	EAbilityTooltipSource Source = EAbilityTooltipSource::ActionBar;

	/** Builds tooltip data from an action bar slot and optional ASC for cost scaling. */
	static FAeyerjiAbilityTooltipData FromSlot(
		const UAbilitySystemComponent* ASC,
		const FAeyerjiAbilitySlot& Slot,
		EAbilityTooltipSource Source = EAbilityTooltipSource::ActionBar);

	/** Resolves the first UAeyerjiAbilityData* configured on the ability CDO (if any). */
	static const UAeyerjiAbilityData* ResolveAbilityData(TSubclassOf<UGameplayAbility> AbilityClass);
};

