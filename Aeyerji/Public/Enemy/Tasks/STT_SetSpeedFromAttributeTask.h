
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "GameplayEffectTypes.h"  // for FGameplayAttribute
#include "STT_SetSpeedFromAttributeTask.generated.h"
#pragma once

/**
 * StateTree Task that sets the character's MaxWalkSpeed from a Gameplay Attribute.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Set Speed from Attribute"))
class AEYERJI_API USTT_SetSpeedFromAttributeTask : public UStateTreeTaskBlueprintBase
{
	GENERATED_BODY()

public:
	// Which Gameplay Attribute to read for the speed (default is WalkSpeed)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Speed")
	FGameplayAttribute SpeedAttribute;

	USTT_SetSpeedFromAttributeTask(const FObjectInitializer& ObjectInitializer);

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) override;
};
