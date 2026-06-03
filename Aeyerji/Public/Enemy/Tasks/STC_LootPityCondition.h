#pragma once

#include "CoreMinimal.h"
#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "Systems/LootService.h"
#include "STC_LootPityCondition.generated.h"

/** Checks exposed by StateTree for high-level loot memory decisions. */
UENUM(BlueprintType)
enum class EAeyerjiLootPityConditionMode : uint8
{
	DropsSinceLastLegendaryAtLeast UMETA(DisplayName="Drops Since Last Legendary At Least"),
	LegendaryChanceAtLeast UMETA(DisplayName="Legendary Chance At Least"),
	HasNeverPickedUpItem UMETA(DisplayName="Has Never Picked Up Item"),
	PityGroupAttemptsSinceSuccessAtLeast UMETA(DisplayName="Pity Group Attempts Since Success At Least"),
	PityGroupSuccessesAtLeast UMETA(DisplayName="Pity Group Successes At Least")
};

/**
 * StateTree condition that queries existing player loot stats without making StateTree own item memory.
 */
UCLASS(Blueprintable, meta=(DisplayName="Loot Pity Condition"))
class AEYERJI_API USTC_LootPityCondition : public UStateTreeConditionBlueprintBase
{
	GENERATED_BODY()

public:
	/** The check to perform against the current player loot stats. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loot")
	EAeyerjiLootPityConditionMode Mode = EAeyerjiLootPityConditionMode::DropsSinceLastLegendaryAtLeast;

	/** Actor whose PlayerStatsTrackingComponent should be queried. Leave empty to use the StateTree owner actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loot")
	TObjectPtr<AActor> PlayerActor = nullptr;

	/** Uses the StateTree owner actor when PlayerActor is unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loot")
	bool bUseOwnerAsPlayerActor = true;

	/** Threshold for DropsSinceLastLegendaryAtLeast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loot", meta=(ClampMin="0", EditCondition="Mode == EAeyerjiLootPityConditionMode::DropsSinceLastLegendaryAtLeast"))
	int32 MinDropsSinceLastLegendary = 20;

	/** Context used when computing the current legendary chance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loot", meta=(EditCondition="Mode == EAeyerjiLootPityConditionMode::LegendaryChanceAtLeast"))
	FLootContext LootContext;

	/** Threshold for LegendaryChanceAtLeast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loot", meta=(ClampMin="0.0", ClampMax="1.0", EditCondition="Mode == EAeyerjiLootPityConditionMode::LegendaryChanceAtLeast"))
	float MinLegendaryChance = 0.25f;

	/** Definition key checked by HasNeverPickedUpItem. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loot", meta=(EditCondition="Mode == EAeyerjiLootPityConditionMode::HasNeverPickedUpItem"))
	FName ItemDefinitionKey = NAME_None;

	/** Named pity group checked by PityGroup* modes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loot|Pity", meta=(EditCondition="Mode == EAeyerjiLootPityConditionMode::PityGroupAttemptsSinceSuccessAtLeast || Mode == EAeyerjiLootPityConditionMode::PityGroupSuccessesAtLeast"))
	FGameplayTag PityGroup;

	/** Threshold for PityGroupAttemptsSinceSuccessAtLeast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loot|Pity", meta=(ClampMin="0", EditCondition="Mode == EAeyerjiLootPityConditionMode::PityGroupAttemptsSinceSuccessAtLeast"))
	int32 MinPityAttemptsSinceSuccess = 1;

	/** Threshold for PityGroupSuccessesAtLeast. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loot|Pity", meta=(ClampMin="0", EditCondition="Mode == EAeyerjiLootPityConditionMode::PityGroupSuccessesAtLeast"))
	int32 MinPitySuccesses = 1;

	/** Inverts the final condition result. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loot")
	bool bInvert = false;

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;
};
