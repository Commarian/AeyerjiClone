#include "Abilities/MagicAmpAura/DA_MagicAmpAura.h"

FAeyerjiAbilityCost UDA_MagicAmpAura::EvaluateCost(const UAbilitySystemComponent* ASC) const
{
	// Magic amp aura is passive; no mana cost or cooldown here.
	FAeyerjiAbilityCost TotalCost;
	TotalCost.ManaCost = 0.f;
	TotalCost.Cooldown = 0.f;
	(void)ASC;
	return TotalCost;
}
