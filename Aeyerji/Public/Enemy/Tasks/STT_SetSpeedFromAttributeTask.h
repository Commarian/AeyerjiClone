
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "GameplayEffectTypes.h"  // for FGameplayAttribute
#include "STT_SetSpeedFromAttributeTask.generated.h"

/**
 * StateTree Task that sets the character's MaxWalkSpeed from a Gameplay Attribute.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Set Speed from Attribute"))
class AEYERJI_API USTT_SetSpeedFromAttributeTask : public UStateTreeTaskBlueprintBase
{
	GENERATED_BODY()

public:
	/** Gameplay Attribute read when setting movement speed; defaults to WalkSpeed. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed")
	FGameplayAttribute SpeedAttribute;

	USTT_SetSpeedFromAttributeTask(const FObjectInitializer& ObjectInitializer);

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) override;
};
