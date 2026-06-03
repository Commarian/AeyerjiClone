// Copyright (c) 2025 Aeyerji.

#include "GUI/W_PlayerStatusHUD.h"

void UW_PlayerStatusHUD::ApplyObjectiveState(const FAeyerjiObjectiveState& InObjectiveState)
{
	CachedObjectiveState = InObjectiveState;
	BP_HandleObjectiveStateApplied(CachedObjectiveState);
}

void UW_PlayerStatusHUD::ApplySurvivalRoundState(const FAeyerjiSurvivalRoundState& InSurvivalState)
{
	CachedSurvivalRoundState = InSurvivalState;
	BP_HandleSurvivalRoundStateApplied(CachedSurvivalRoundState);
}
