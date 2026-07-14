#include "GAS/AeyerjiAbilitySystemGlobals.h"

#include "GAS/AeyerjiGameplayEffectContext.h"

FGameplayEffectContext* UAeyerjiAbilitySystemGlobals::AllocGameplayEffectContext() const
{
	return new FAeyerjiGameplayEffectContext();
}
