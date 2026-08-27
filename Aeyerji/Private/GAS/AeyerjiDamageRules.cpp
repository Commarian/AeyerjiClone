#include "GAS/AeyerjiDamageRules.h"

#include "AeyerjiGameplayTags.h"
#include "GameplayEffect.h"

void FAeyerjiDamageRuleConfig::ApplyToSpec(FGameplayEffectSpec& Spec) const
{
	auto AddRule = [&Spec](const bool bEnabled, const FGameplayTag& RuleTag)
	{
		if (bEnabled && RuleTag.IsValid())
		{
			Spec.AddDynamicAssetTag(RuleTag);
		}
	};

	AddRule(bUseVariance, AeyerjiTags::DamageRule_UseVariance);
	AddRule(bCanCrit, AeyerjiTags::DamageRule_CanCrit);
	AddRule(bCanBeDodged, AeyerjiTags::DamageRule_CanBeDodged);
	AddRule(bCanLifeSteal, AeyerjiTags::DamageRule_CanLifeSteal);
	AddRule(bCanTriggerOnHit, AeyerjiTags::DamageRule_CanTriggerOnHit);
	AddRule(bCanStagger, AeyerjiTags::DamageRule_CanStagger);

	if (bUseVariance && FMath::IsFinite(VarianceOverride) && VarianceOverride >= 0.f)
	{
		Spec.SetSetByCallerMagnitude(AeyerjiTags::SBC_Damage_Variance, FMath::Clamp(VarianceOverride, 0.f, 0.95f));
	}
	if (bCanCrit && FMath::IsFinite(CriticalMultiplierOverride) && CriticalMultiplierOverride > 0.f)
	{
		Spec.SetSetByCallerMagnitude(AeyerjiTags::SBC_Damage_CriticalMultiplier, CriticalMultiplierOverride);
	}
	if (FMath::IsFinite(ArmorShred) && ArmorShred > 0.f)
	{
		Spec.SetSetByCallerMagnitude(AeyerjiTags::SBC_ArmorShred, ArmorShred);
	}
	if (FMath::IsFinite(ArmorPenetration) && ArmorPenetration > 0.f)
	{
		Spec.SetSetByCallerMagnitude(AeyerjiTags::SBC_ArmorPenetration, FMath::Clamp(ArmorPenetration, 0.f, 1.f));
	}
	if (bCanStagger && FMath::IsFinite(StaggerMultiplier) && !FMath::IsNearlyEqual(StaggerMultiplier, 1.f))
	{
		Spec.SetSetByCallerMagnitude(AeyerjiTags::SBC_Damage_StaggerMultiplier, FMath::Max(0.f, StaggerMultiplier));
	}
}
