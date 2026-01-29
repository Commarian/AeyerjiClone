// AeyerjiEnemyTraitComponent.cpp
#include "Enemy/AeyerjiEnemyTraitComponent.h"

UAeyerjiEnemyTraitComponent::UAeyerjiEnemyTraitComponent()
{
	// Traits are usually event-driven; disable ticking unless a subclass enables it.
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}
