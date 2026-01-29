#include "Abilities/Potions/DA_Potions.h"

FAeyerjiAbilityCost UDA_Potions::EvaluateCost(const UAbilitySystemComponent* ASC) const
{
	// Potions only apply a cooldown; mana cost stays zero.
	FAeyerjiAbilityCost EvaluatedCost;
	EvaluatedCost.Cooldown = Tunables.Cooldown;
	(void)ASC;

	return EvaluatedCost;
}
