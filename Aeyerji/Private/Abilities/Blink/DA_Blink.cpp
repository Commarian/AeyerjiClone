#include "Abilities/Blink/DA_Blink.h"

#include "AbilitySystemComponent.h"
#include "Attributes/AeyerjiAttributeSet.h"

FAeyerjiAbilityCost UDA_Blink::EvaluateCost(const UAbilitySystemComponent* ASC) const
{
	// Local variable to avoid shadowing the base class Cost property.
	FAeyerjiAbilityCost EvaluatedCost;
	EvaluatedCost.ManaCost = Tunables.ManaCost;
	EvaluatedCost.Cooldown = Tunables.Cooldown;

	// Optional: scale cost with range curve (mirrors legacy Blink behaviour).
	const FRichCurve* RangeCurve = Tunables.RangeByLevel.GetRichCurveConst();
	if (RangeCurve && RangeCurve->GetNumKeys() > 0 && ASC)
	{
		const float Level = ASC->HasAttributeSetForAttribute(UAeyerjiAttributeSet::GetLevelAttribute())
			                    ? ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetLevelAttribute())
			                    : 1.f;
		const float RangeBase = RangeCurve->Eval(Level, Tunables.MaxRange);
		const float RangeBaseLevel1 = RangeCurve->Eval(1.f, Tunables.MaxRange);
		if (RangeBaseLevel1 > KINDA_SMALL_NUMBER)
		{
			const float Scale = RangeBase / RangeBaseLevel1;
			EvaluatedCost.ManaCost *= Scale;
			EvaluatedCost.Cooldown *= 1.f / FMath::Max(Scale, 0.25f);
		}
	}

	return EvaluatedCost;
}
