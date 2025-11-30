// File: Source/Aeyerji/Public/Attributes/GE_Regen_Periodic.h
#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GE_Regen_Periodic.generated.h"

/**
 * Infinite periodic regen effect that adds HP and Mana each second.
 * Magnitudes are AttributeBased: Source.HPRegen and Source.ManaRegen.
 */
UCLASS()
class AEYERJI_API UGE_Regen_Periodic : public UGameplayEffect
{
    GENERATED_BODY()
public:
    UGE_Regen_Periodic();
};

