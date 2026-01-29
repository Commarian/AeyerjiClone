// GE_DamagePhysical.h
#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"

#include "GE_DamagePhysical.generated.h"

/**
 * Gameplay effect wrapper that executes UExecCalc_DamagePhysical for physical damage.
 */
UCLASS()
class AEYERJI_API UGE_DamagePhysical : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGE_DamagePhysical(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
