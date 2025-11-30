// GE_ItemStats.h
#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"

#include "GE_ItemStats.generated.h"

/**
 * Gameplay effect wrapper that executes UExecCalc_ItemStats to apply item modifiers.
 */
UCLASS()
class AEYERJI_API UGE_ItemStats : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGE_ItemStats();
};

