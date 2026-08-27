// Copyright (c) 2025 Aeyerji.

#include "GUI/AeyerjiMinimapMarkerComponent.h"

UAeyerjiMinimapMarkerComponent::UAeyerjiMinimapMarkerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}
