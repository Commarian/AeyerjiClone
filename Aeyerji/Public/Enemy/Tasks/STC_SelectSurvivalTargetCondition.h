// Copyright (c) 2025 Aeyerji.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "AeyerjiObjectiveTypes.h"
#include "STC_SelectSurvivalTargetCondition.generated.h"

/** Selects a live nearby player when threatening, otherwise selects the survival defense objective. */
UCLASS(Blueprintable, meta=(DisplayName="Select Survival Target"))
class AEYERJI_API USTC_SelectSurvivalTargetCondition : public UStateTreeConditionBlueprintBase
{
	GENERATED_BODY()

public:
	/** Uses the per-controller settings pushed by LevelDirector/SpawnerGroup. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Targeting")
	bool bUseControllerSettings = true;

	/** Fallback settings used if controller settings are disabled or not configured. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Targeting")
	FAeyerjiDefenseTargetingSettings FallbackSettings;

	/** Ignores Z when comparing distances, matching ARPG ground-plane behavior. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Targeting")
	bool bUse2DDistance = true;

	/** When true, writes the chosen actor to AEnemyAIController::CurrentTarget. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Behavior")
	bool bSetTargetOnPass = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Behavior")
	bool bNegate = false;

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

private:
	float DistanceSq(const FVector& A, const FVector& B) const;
	bool IsLiveTarget(const AActor* Actor) const;
};
