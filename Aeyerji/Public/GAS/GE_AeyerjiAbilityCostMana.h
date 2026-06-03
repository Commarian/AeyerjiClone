#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GE_AeyerjiAbilityCostMana.generated.h"

/** Instant mana cost GE driven by SetByCaller.Cost.Mana. */
UCLASS()
class AEYERJI_API UGE_AeyerjiAbilityCostMana : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGE_AeyerjiAbilityCostMana(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
