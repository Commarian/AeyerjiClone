#pragma once

#include "CoreMinimal.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "Inventory/AeyerjiInventoryBPFL.h"
#include "Systems/LootService.h"
#include "STT_RequestLootDropTask.generated.h"

/**
 * StateTree task that routes a reward event through the existing loot service and pickup spawning path.
 */
UCLASS(Blueprintable, meta=(DisplayName="Request Loot Drop"))
class AEYERJI_API USTT_RequestLootDropTask : public UStateTreeTaskBlueprintBase
{
	GENERATED_BODY()

public:
	/** Base roll context passed to ULootService. SourceTag, level, tier, and rarity rules should be set here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loot")
	FLootContext LootContext;

	/** Optional multi-drop configuration. When disabled, the task rolls one item. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loot")
	bool bUseMultiDropConfig = false;

	/** Multi-drop buckets used when bUseMultiDropConfig is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loot", meta=(EditCondition="bUseMultiDropConfig"))
	FLootMultiDropConfig MultiDropConfig;

	/** Distribution mode passed to the pickup spawning helper. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loot")
	EItemDropDistributionMode DropMode = EItemDropDistributionMode::DropOnlyForInstigator;

	/** Uses the StateTree owner actor as PlayerActor when LootContext.PlayerActor is unset. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loot")
	bool bUseOwnerAsPlayerActor = true;

	/** Uses the StateTree owner actor as pickup instigator when no explicit instigator is provided. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loot")
	bool bUseOwnerAsInstigator = true;

	/** Explicit instigator/recipient anchor. Leave empty to use the StateTree owner actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Loot")
	TObjectPtr<AActor> InstigatorActor = nullptr;

	/** Uses the StateTree owner actor's transform as the drop origin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn")
	bool bUseOwnerLocation = true;

	/** Drop location used when bUseOwnerLocation is false. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn", meta=(EditCondition="!bUseOwnerLocation"))
	FVector WorldLocation = FVector::ZeroVector;

	/** Offset applied after resolving the drop origin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn")
	FVector LocationOffset = FVector::ZeroVector;

	/** Rotation used for spawned pickups. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Spawn")
	FRotator WorldRotation = FRotator::ZeroRotator;

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) override;
};
