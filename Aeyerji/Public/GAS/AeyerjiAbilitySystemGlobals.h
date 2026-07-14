#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemGlobals.h"

#include "AeyerjiAbilitySystemGlobals.generated.h"

/** Allocates Aeyerji's replicated gameplay-effect context for all outgoing specs. */
UCLASS(Config=Game)
class AEYERJI_API UAeyerjiAbilitySystemGlobals : public UAbilitySystemGlobals
{
	GENERATED_BODY()

public:
	virtual FGameplayEffectContext* AllocGameplayEffectContext() const override;
};
