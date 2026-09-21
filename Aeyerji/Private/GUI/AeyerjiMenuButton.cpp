// Copyright (c) 2025 Aeyerji.

#include "GUI/AeyerjiMenuButton.h"

#include "GUI/AeyerjiUIStyleLibrary.h"
#include "Materials/MaterialInterface.h"

void UAeyerjiMenuButton::SynchronizeProperties()
{
	Super::SynchronizeProperties();
	// Runs in the Designer preview and at runtime; safe to run repeatedly.
	ApplyLivingStyle();
}

void UAeyerjiMenuButton::RefreshLivingStyle()
{
	ApplyLivingStyle();
}

void UAeyerjiMenuButton::ApplyLivingStyle() const
{
	if (!bAutoApplyLivingStyle || bApplyingLivingStyle)
	{
		return;
	}
	TGuardValue<bool> Guard(bApplyingLivingStyle, true);
	UMaterialInterface* Override = HoverMaterialOverride.IsNull() ? nullptr : HoverMaterialOverride.LoadSynchronous();
	UAeyerjiUIStyleLibrary::ApplyMenuButtonStyle(const_cast<UAeyerjiMenuButton*>(this), bUseAnimatedHover, Override);
}
