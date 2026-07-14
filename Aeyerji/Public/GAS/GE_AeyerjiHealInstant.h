#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GE_AeyerjiHealInstant.generated.h"

/** Instant HP heal GE driven by SetByCaller.Heal.Instant. */
UCLASS()
class AEYERJI_API UGE_AeyerjiHealInstant : public UGameplayEffect
{
	GENERATED_BODY()

public:
	UGE_AeyerjiHealInstant(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());
};
