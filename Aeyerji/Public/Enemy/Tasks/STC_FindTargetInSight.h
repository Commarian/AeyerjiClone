// STC_FindTargetInSight.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "GameplayTagContainer.h"
#include "STC_FindTargetInSight.generated.h"

/**
 * StateTree Condition (UE 5.6):
 * - Mode A (default): read AI Perception (Sight) and pick nearest hostile.
 * - Mode B (360 deg): sphere-overlap in world, ignore FOV, pick nearest hostile.
 * - If a target is found, calls AEnemyAIController::SetTargetActor(Target).
 */
UCLASS(Blueprintable, meta=(DisplayName="Find Target (Sight or 360 deg Radius)"))
class AEYERJI_API USTC_FindTargetInSight : public UStateTreeConditionBlueprintBase
{
	GENERATED_BODY()

public:
	USTC_FindTargetInSight(const FObjectInitializer& ObjectInitializer);
	/** Use a 360 deg sphere search (ignores FOV) instead of Perception.Sight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mode")
	bool bUse360Search = true;

	/** Radius for the 360 deg sphere search. If 0, falls back to Sight radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Mode", meta=(ClampMin="0.0"))
	float SearchRadius = 1200.f;

	/** Actors with this gameplay tag (GAS/ASC) are ignored (e.g. "State.Dead"). Empty = ignore none. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Filter")
	FGameplayTag InvalidTag;

	/** Extra trace-based LOS check (perception already implies sight). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Filter")
	bool bRequireLineOfSightTrace = false;

	/** Optional clamp for either mode. 0 = use mode's native radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Filter", meta=(ClampMin="0.0"))
	float MaxDistance = 0.f;

	/** Use 2D distance (ignores Z). Good for ARPGs. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Filter")
	bool bUse2DDistance = true;

	/** If true, will set the target but the condition returns false. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Behavior")
	bool bOnlySetTargetDoNotPass = false;

	/** Negate final result. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Behavior")
	bool bNegate = false;

	virtual bool TestCondition(struct FStateTreeExecutionContext& Context) const override;

protected:
	bool GetSightRadius(class UAIPerceptionComponent* Perc, float& OutSightRadius) const;
	bool IsAliveAndHostile(class AAIController* AI, const AActor* Candidate) const;
	bool HasLOS(const class AAIController* AI, const AActor* Candidate) const;

	AActor* FindByPerception(class AAIController* AI, class UAIPerceptionComponent* Perc, class APawn* Self, float UseRadiusSq) const;
	AActor* FindBySphere(class AAIController* AI, class APawn* Self, float UseRadiusSq) const;
};
