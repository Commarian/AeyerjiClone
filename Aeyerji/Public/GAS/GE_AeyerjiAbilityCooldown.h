#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GE_AeyerjiAbilityCooldown.generated.h"

/** Duration-only cooldown GE driven by SetByCaller.Cooldown.Seconds. */
UCLASS()
class AEYERJI_API UGE_AeyerjiAbilityCooldown : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGE_AeyerjiAbilityCooldown(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
