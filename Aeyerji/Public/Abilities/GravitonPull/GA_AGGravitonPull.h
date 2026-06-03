#pragma once

#include "CoreMinimal.h"
#include "Abilities/GA_AeyerjiTargetedEffectBase.h"
#include "GA_AGGravitonPull.generated.h"

/** Table-driven Graviton Pull entry point. Runtime tuning comes from the global ability table. */
UCLASS()
class AEYERJI_API UGA_AGGravitonPull : public UGA_AeyerjiTargetedEffectBase
{
	GENERATED_BODY()

public:
	UGA_AGGravitonPull();
};
