// Copyright (c) 2025 Aeyerji.
#pragma once

#include "CoreMinimal.h"
#include "AeyerjiObjectiveTypes.h"
#include "Blueprint/UserWidget.h"
#include "W_PlayerStatusHUD.generated.h"

/**
 * Native player HUD contract used by the controller-owned objective presentation flow.
 * Blueprint subclasses should keep gameplay discovery out of the widget and render only from ApplyObjectiveState().
 */
UCLASS()
class AEYERJI_API UW_PlayerStatusHUD : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Caches and forwards the latest replicated objective snapshot to Blueprint presentation logic. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|HUD|Objective")
	void ApplyObjectiveState(const FAeyerjiObjectiveState& InObjectiveState);

	/** Caches and forwards the latest replicated survival-round snapshot to Blueprint presentation logic. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|HUD|Survival")
	void ApplySurvivalRoundState(const FAeyerjiSurvivalRoundState& InSurvivalState);

	/** Returns the last snapshot applied to this widget. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|HUD|Objective")
	const FAeyerjiObjectiveState& GetCachedObjectiveState() const { return CachedObjectiveState; }

	/** Returns the last survival-round snapshot applied to this widget. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|HUD|Survival")
	const FAeyerjiSurvivalRoundState& GetCachedSurvivalRoundState() const { return CachedSurvivalRoundState; }

protected:
	/** Blueprint presentation hook fired whenever ApplyObjectiveState() receives a new snapshot. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Objective", meta=(DisplayName="Handle Objective State Applied"))
	void BP_HandleObjectiveStateApplied(const FAeyerjiObjectiveState& InObjectiveState);

	/** Blueprint presentation hook fired whenever ApplySurvivalRoundState() receives a new snapshot. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|HUD|Survival", meta=(DisplayName="Handle Survival Round State Applied"))
	void BP_HandleSurvivalRoundStateApplied(const FAeyerjiSurvivalRoundState& InSurvivalState);

private:
	/** Cached objective snapshot for immediate refreshes after widget reattachment. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|HUD|Objective", meta=(AllowPrivateAccess="true"))
	FAeyerjiObjectiveState CachedObjectiveState;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|HUD|Survival", meta=(AllowPrivateAccess="true"))
	FAeyerjiSurvivalRoundState CachedSurvivalRoundState;
};
