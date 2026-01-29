// AeyerjiEnemyTraitComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AeyerjiEnemyTraitComponent.generated.h"

/**
 * Base class for modular enemy traits (flanker, kiter, gap-closer, etc.).
 * Blueprint subclasses implement the behavior and can be attached at runtime.
 */
UCLASS(Blueprintable, ClassGroup=(Aeyerji), meta=(BlueprintSpawnableComponent))
class AEYERJI_API UAeyerjiEnemyTraitComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Creates a non-ticking trait component; subclasses can opt in to ticking.
	UAeyerjiEnemyTraitComponent();
};
