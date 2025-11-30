// File: Source/Aeyerji/Public/Attributes/GE_SecondaryStatsFromPrimaries.h
#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GE_SecondaryStatsFromPrimaries.generated.h"

/**
 * Passive infinite GE that expects SetByCaller magnitudes for secondary stats
 * derived from primary attributes. The StatEngine component computes the
 * magnitudes and applies this effect.
 */
UCLASS()
class AEYERJI_API UGE_SecondaryStatsFromPrimaries : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UGE_SecondaryStatsFromPrimaries();
};
