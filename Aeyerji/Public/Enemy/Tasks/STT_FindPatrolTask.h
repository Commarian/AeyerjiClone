
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "STT_FindPatrolTask.generated.h"
#pragma once


/**
 * StateTree Task that finds a random patrol location near the AI's home location.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Find Patrol Point"))
class AEYERJI_API USTT_FindPatrolTask : public UStateTreeTaskBlueprintBase
{
	GENERATED_BODY()

public:
	// The home/base location around which to patrol (if not set, defaults to the pawn's spawn location).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patrol")
	FVector HomeLocation;

	// The radius (distance) within which to find a patrol point (will use pawn's attribute if available).
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Patrol", meta=(ClampMin="0.0"))
	float OverridePatrolRadius;

	// Outputs the chosen patrol location.
	UPROPERTY(VisibleAnywhere, BlueprintReadWrite, Category = "Output")
	FVector PatrolLocation;

	USTT_FindPatrolTask(const FObjectInitializer& ObjectInitializer);

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) override;
};
