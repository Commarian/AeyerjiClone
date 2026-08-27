// ExecCalc_DamagePhysical.cpp

#include "GAS/ExecCalc_DamagePhysical.h"

#include "AeyerjiGameplayTags.h"
#include "Attributes/AeyerjiStatTuning.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Abilities/AbilityTeamUtils.h"
#include "GAS/AeyerjiGameplayEffectContext.h"
#include "GAS/GE_DamagePhysical.h"
#include "GameplayEffect.h"
#include "GameplayEffectExtension.h"
#include "GameplayTagContainer.h"
#include "Logging/LogMacros.h"

namespace
{
	struct FDamageStatics
	{
		DECLARE_ATTRIBUTE_CAPTUREDEF(Armor);
		DECLARE_ATTRIBUTE_CAPTUREDEF(AttackDamageVariance);
		DECLARE_ATTRIBUTE_CAPTUREDEF(CritChance);
		DECLARE_ATTRIBUTE_CAPTUREDEF(CriticalDamageMultiplier);
		DECLARE_ATTRIBUTE_CAPTUREDEF(DodgeChance);
		DECLARE_ATTRIBUTE_CAPTUREDEF(PhysicalDamageBonus);
		DECLARE_ATTRIBUTE_CAPTUREDEF(ArmorPenetration);
		DECLARE_ATTRIBUTE_CAPTUREDEF(LifeSteal);
		DECLARE_ATTRIBUTE_CAPTUREDEF(StaggerPower);
		DECLARE_ATTRIBUTE_CAPTUREDEF(StaggerResistance);

		FDamageStatics()
		{
			DEFINE_ATTRIBUTE_CAPTUREDEF(UAeyerjiAttributeSet, Armor, Target, false);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UAeyerjiAttributeSet, AttackDamageVariance, Source, false);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UAeyerjiAttributeSet, CritChance, Source, false);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UAeyerjiAttributeSet, CriticalDamageMultiplier, Source, false);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UAeyerjiAttributeSet, DodgeChance, Target, false);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UAeyerjiAttributeSet, PhysicalDamageBonus, Source, false);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UAeyerjiAttributeSet, ArmorPenetration, Source, false);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UAeyerjiAttributeSet, LifeSteal, Source, false);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UAeyerjiAttributeSet, StaggerPower, Source, false);
			DEFINE_ATTRIBUTE_CAPTUREDEF(UAeyerjiAttributeSet, StaggerResistance, Target, false);
		}
	};

	FDamageStatics& DamageStatics()
	{
		static FDamageStatics Statics;
		return Statics;
	}

	float FiniteOrDefault(const float Value, const float DefaultValue = 0.f)
	{
		return FMath::IsFinite(Value) ? Value : DefaultValue;
	}
}

DEFINE_LOG_CATEGORY_STATIC(LogDamagePhysicalCalc, Log, All);

UExecCalc_DamagePhysical::UExecCalc_DamagePhysical()
{
	// Capture armor at execution time so active buffs/debuffs affect mitigation.
	RelevantAttributesToCapture.Add(DamageStatics().ArmorDef);
	RelevantAttributesToCapture.Add(DamageStatics().AttackDamageVarianceDef);
	RelevantAttributesToCapture.Add(DamageStatics().CritChanceDef);
	RelevantAttributesToCapture.Add(DamageStatics().CriticalDamageMultiplierDef);
	RelevantAttributesToCapture.Add(DamageStatics().DodgeChanceDef);
	RelevantAttributesToCapture.Add(DamageStatics().PhysicalDamageBonusDef);
	RelevantAttributesToCapture.Add(DamageStatics().ArmorPenetrationDef);
	RelevantAttributesToCapture.Add(DamageStatics().LifeStealDef);
	RelevantAttributesToCapture.Add(DamageStatics().StaggerPowerDef);
	RelevantAttributesToCapture.Add(DamageStatics().StaggerResistanceDef);
}

FAeyerjiDamageRollResult UExecCalc_DamagePhysical::ResolveDamageRoll(const float AverageDamage,
                                                                      const float VarianceFraction,
                                                                      const float CritChanceFraction,
                                                                      const float CriticalMultiplier,
                                                                      const bool bUseVariance,
                                                                      const bool bCanCrit,
                                                                      const float DamageRollAlpha,
                                                                      const float CritRollAlpha)
{
	FAeyerjiDamageRollResult Result;

	const float SafeAverage = FMath::Max(0.f, FiniteOrDefault(AverageDamage));
	const float SafeVariance = FMath::Clamp(FiniteOrDefault(VarianceFraction), 0.f, 0.95f);

	Result.DamageBeforeMitigation = SafeAverage;
	if (bUseVariance)
	{
		const float MinimumMultiplier = 1.f - SafeVariance;
		const float MaximumMultiplier = 1.f + SafeVariance;
		Result.DamageBeforeMitigation *= FMath::Lerp(
			MinimumMultiplier,
			MaximumMultiplier,
			FMath::Clamp(FiniteOrDefault(DamageRollAlpha), 0.f, 1.f));
	}

	Result.bWasCritical = bCanCrit
		&& FMath::Clamp(FiniteOrDefault(CritRollAlpha), 0.f, 1.f)
			< FMath::Clamp(FiniteOrDefault(CritChanceFraction), 0.f, 1.f);
	if (Result.bWasCritical)
	{
		Result.DamageBeforeMitigation *= FMath::Max(1.f, FiniteOrDefault(CriticalMultiplier, 1.f));
		if (!FMath::IsFinite(Result.DamageBeforeMitigation))
		{
			Result.DamageBeforeMitigation = 0.f;
			Result.bWasCritical = false;
		}
	}

	return Result;
}

bool UExecCalc_DamagePhysical::ResolveDodge(const bool bCanBeDodged, const float DodgeChanceFraction, const float DodgeRollAlpha)
{
	return bCanBeDodged
		&& FMath::Clamp(FiniteOrDefault(DodgeRollAlpha), 0.f, 1.f)
			< FMath::Clamp(FiniteOrDefault(DodgeChanceFraction), 0.f, 1.f);
}

float UExecCalc_DamagePhysical::ResolveArmorPenetration(
	const float SourcePenetration,
	const float SpecPenetration,
	const float MaximumPenetration)
{
	return FMath::Clamp(
		FMath::Max(0.f, FiniteOrDefault(SourcePenetration)) + FMath::Max(0.f, FiniteOrDefault(SpecPenetration)),
		0.f,
		FMath::Max(0.f, FiniteOrDefault(MaximumPenetration)));
}

float UExecCalc_DamagePhysical::ResolveLifeSteal(
	const float ActualDamage,
	const float LifeStealFraction,
	const float MissingHealth,
	const bool bCanLifeSteal)
{
	if (!bCanLifeSteal)
	{
		return 0.f;
	}
	return FMath::Min(
		FMath::Max(0.f, FiniteOrDefault(ActualDamage)) * FMath::Max(0.f, FiniteOrDefault(LifeStealFraction)),
		FMath::Max(0.f, FiniteOrDefault(MissingHealth)));
}

float UExecCalc_DamagePhysical::ResolveArmorDamageReduction(
	const float Armor,
	const float ArmorK,
	const float ArmorSoftCap,
	const float ArmorTailSlope,
	const float ArmorTailCap)
{
	const float SafeArmor = FMath::Max(0.f, FiniteOrDefault(Armor));
	const float SafeK = FMath::Max(FiniteOrDefault(ArmorK, 1000.f), KINDA_SMALL_NUMBER);
	const float SafeSoftCap = FMath::Max(0.f, FiniteOrDefault(ArmorSoftCap, 1000.f));
	const float SoftCapDR = FMath::Clamp(SafeSoftCap / (SafeSoftCap + SafeK), 0.f, 1.f);
	if (SafeArmor <= SafeSoftCap)
	{
		return FMath::Clamp(SafeArmor / (SafeArmor + SafeK), 0.f, 1.f);
	}

	const float SafeTailSlope = FMath::Max(0.f, FiniteOrDefault(ArmorTailSlope));
	const float SafeTailCap = FMath::Clamp(
		FMath::Max(SoftCapDR, FiniteOrDefault(ArmorTailCap, SoftCapDR)),
		0.f,
		1.f);
	const float TailDR = SoftCapDR + (SafeArmor - SafeSoftCap) * SafeTailSlope;
	return FMath::Clamp(FMath::Min(FiniteOrDefault(TailDR, SafeTailCap), SafeTailCap), 0.f, 1.f);
}

UExecCalc_DamagePhysical::FArmorTuning UExecCalc_DamagePhysical::ResolveArmorTuning()
{
	FArmorTuning Result;
	if (const UAeyerjiAttributeTuning* Tuning = UAeyerjiStatSettings::Get())
	{
		const FAeyerjiArmorMitigationTuning& ArmorTuning = Tuning->ArmorMitigation;
		Result.ArmorK = ArmorTuning.ArmorK;
		Result.ArmorSoftCap = ArmorTuning.ArmorSoftCap;
		Result.ArmorTailSlope = ArmorTuning.ArmorTailSlope;
		Result.ArmorTailCap = ArmorTuning.ArmorTailCap;
	}
	return Result;
}

void UExecCalc_DamagePhysical::Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
                                                      FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
	const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();

	const float RawMagnitude = Spec.GetSetByCallerMagnitude(
		AeyerjiTags::SBC_Damage_Instant,
		/*WarnIfNotFound=*/false,
		0.f);

	if (!FMath::IsFinite(RawMagnitude) || RawMagnitude <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogDamagePhysicalCalc, Warning,
			TEXT("Execute_Implementation: rejected non-positive or non-finite SetByCaller.Damage.Instant (%f)."),
			RawMagnitude);
		return;
	}

	const float BaseDamage = RawMagnitude;

	const UAbilitySystemComponent* TargetASC = ExecutionParams.GetTargetAbilitySystemComponent();
	const AActor* SourceActor = Spec.GetContext().GetOriginalInstigator();
	const AActor* TargetActor = TargetASC ? TargetASC->GetAvatarActor() : nullptr;
	if (!TargetActor && TargetASC)
	{
		TargetActor = TargetASC->GetOwnerActor();
	}

	if (AbilityTeamUtils::AreOnSameTeam(SourceActor, TargetActor))
	{
		UE_LOG(LogDamagePhysicalCalc, VeryVerbose,
			TEXT("DamageCalc: Friendly or self physical damage suppressed Source=%s Target=%s"),
			*GetNameSafe(SourceActor),
			*GetNameSafe(TargetActor));
		return;
	}

	FGameplayTagContainer AssetTags;
	Spec.GetAllAssetTags(AssetTags);
	const bool bTaggedPhysical = AssetTags.HasTagExact(AeyerjiTags::DamageType_Physical);
	const bool bUseVariance = AssetTags.HasTagExact(AeyerjiTags::DamageRule_UseVariance);
	const bool bCanCrit = AssetTags.HasTagExact(AeyerjiTags::DamageRule_CanCrit);
	const bool bCanBeDodged = AssetTags.HasTagExact(AeyerjiTags::DamageRule_CanBeDodged);
	const bool bCanLifeSteal = AssetTags.HasTagExact(AeyerjiTags::DamageRule_CanLifeSteal);
	const bool bCanTriggerOnHit = AssetTags.HasTagExact(AeyerjiTags::DamageRule_CanTriggerOnHit);
	const bool bCanStagger = AssetTags.HasTagExact(AeyerjiTags::DamageRule_CanStagger);
	// Accept subclasses so BP-derived damage effects still route through physical mitigation.
	const bool bClassIsPhysical = Spec.Def && Spec.Def->IsA(UGE_DamagePhysical::StaticClass());
	const bool bIsPhysical = bTaggedPhysical || bClassIsPhysical;

	FGameplayEffectContextHandle ContextHandle = Spec.GetContext();
	FAeyerjiGameplayEffectContext* AeyerjiContext = FAeyerjiGameplayEffectContext::ExtractMutable(ContextHandle);
	FAeyerjiDamageResult* Result = AeyerjiContext ? &AeyerjiContext->GetMutableDamageResult() : nullptr;
	if (Result)
	{
		*Result = FAeyerjiDamageResult();
		Result->BaseDamage = BaseDamage;
		Result->DamageType = bIsPhysical ? AeyerjiTags::DamageType_Physical : FGameplayTag();
		auto AddResultRule = [Result](const bool bEnabled, const FGameplayTag& RuleTag)
		{
			if (bEnabled)
			{
				Result->RuleTags.AddTag(RuleTag);
			}
		};
		AddResultRule(bUseVariance, AeyerjiTags::DamageRule_UseVariance);
		AddResultRule(bCanCrit, AeyerjiTags::DamageRule_CanCrit);
		AddResultRule(bCanBeDodged, AeyerjiTags::DamageRule_CanBeDodged);
		AddResultRule(bCanLifeSteal, AeyerjiTags::DamageRule_CanLifeSteal);
		AddResultRule(bCanTriggerOnHit, AeyerjiTags::DamageRule_CanTriggerOnHit);
		AddResultRule(bCanStagger, AeyerjiTags::DamageRule_CanStagger);
	}

	float FinalDamage = BaseDamage;
	float StaggerDamage = 0.f;
	if (bIsPhysical)
	{
		FAggregatorEvaluateParameters EvalParams;
		EvalParams.SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
		EvalParams.TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

		const FAeyerjiCombatLimitsTuning CombatLimits = UAeyerjiStatSettings::Get()
			? UAeyerjiStatSettings::Get()->CombatLimits
			: FAeyerjiCombatLimitsTuning();
		const float MaxDodgeChance = CombatLimits.GetSafeMaxDodgeChance();

		float DodgeChance = 0.f;
		if (bCanBeDodged)
		{
			ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DamageStatics().DodgeChanceDef, EvalParams, DodgeChance);
		DodgeChance = FMath::Clamp(FiniteOrDefault(DodgeChance), 0.f, MaxDodgeChance);
		}
		if (ResolveDodge(bCanBeDodged, DodgeChance, FMath::FRand()))
		{
			if (Result)
			{
				Result->bWasDodged = true;
			}
			UE_LOG(LogDamagePhysicalCalc, VeryVerbose,
				TEXT("DamageCalc: Base=%.2f DodgeChance=%.3f dodged=true Final=0.00"),
				BaseDamage,
				DodgeChance);
			OutExecutionOutput.AddOutputModifier(
				FGameplayModifierEvaluatedData(UAeyerjiAttributeSet::GetIncomingDodgeAttribute(), EGameplayModOp::Additive, 1.f));
			return;
		}

		float PhysicalDamageBonus = 0.f;
		ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DamageStatics().PhysicalDamageBonusDef, EvalParams, PhysicalDamageBonus);
		PhysicalDamageBonus = FiniteOrDefault(PhysicalDamageBonus);
		const float DamageAfterPhysicalBonus = BaseDamage * FMath::Max(0.f, 1.f + PhysicalDamageBonus);

		float AttackDamageVariance = 0.f;
		ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DamageStatics().AttackDamageVarianceDef, EvalParams, AttackDamageVariance);
		if (AeyerjiTags::SBC_Damage_Variance.GetTag().IsValid())
		{
			AttackDamageVariance = Spec.GetSetByCallerMagnitude(AeyerjiTags::SBC_Damage_Variance, /*WarnIfNotFound=*/false, AttackDamageVariance);
		}
		AttackDamageVariance = FMath::Clamp(FiniteOrDefault(AttackDamageVariance), 0.f, 0.95f);

		float CritChance = 0.f;
		ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DamageStatics().CritChanceDef, EvalParams, CritChance);
		CritChance = FMath::Clamp(
			FiniteOrDefault(CritChance),
			0.f,
			FMath::Max(0.f, FiniteOrDefault(CombatLimits.MaxCritChance)));

		float CriticalDamageMultiplier = 2.f;
		ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DamageStatics().CriticalDamageMultiplierDef, EvalParams, CriticalDamageMultiplier);
		if (AeyerjiTags::SBC_Damage_CriticalMultiplier.GetTag().IsValid())
		{
			CriticalDamageMultiplier = Spec.GetSetByCallerMagnitude(AeyerjiTags::SBC_Damage_CriticalMultiplier, /*WarnIfNotFound=*/false, CriticalDamageMultiplier);
		}
		CriticalDamageMultiplier = FMath::Clamp(
			FiniteOrDefault(CriticalDamageMultiplier, 1.f),
			1.f,
			FMath::Max(1.f, FiniteOrDefault(CombatLimits.MaxCriticalDamageMultiplier, 1.f)));

		const FAeyerjiDamageRollResult DamageRoll = ResolveDamageRoll(
			DamageAfterPhysicalBonus,
			AttackDamageVariance,
			CritChance,
			CriticalDamageMultiplier,
			bUseVariance,
			bCanCrit,
			FMath::FRand(),
			FMath::FRand());

		float ArmorValue = 0.f;
		ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DamageStatics().ArmorDef, EvalParams, ArmorValue);
		ArmorValue = FMath::Max(0.f, FiniteOrDefault(ArmorValue));

		const float ArmorShred = Spec.GetSetByCallerMagnitude(AeyerjiTags::SBC_ArmorShred, /*WarnIfNotFound=*/false, 0.f);
		ArmorValue = FMath::Max(0.f, ArmorValue - FMath::Max(0.f, FiniteOrDefault(ArmorShred)));

		float SourceArmorPenetration = 0.f;
		ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DamageStatics().ArmorPenetrationDef, EvalParams, SourceArmorPenetration);
		const float SpecArmorPenetration = Spec.GetSetByCallerMagnitude(AeyerjiTags::SBC_ArmorPenetration, /*WarnIfNotFound=*/false, 0.f);
		const float ArmorPenetration = ResolveArmorPenetration(
			SourceArmorPenetration,
			SpecArmorPenetration,
			FiniteOrDefault(CombatLimits.MaxArmorPenetration));

		const FArmorTuning ArmorTuning = ResolveArmorTuning();
		const float DamageReduction = ComputeArmorDR(ArmorValue, ArmorTuning);
		const float EffectiveDR = DamageReduction * (1.f - FMath::Clamp(ArmorPenetration, 0.f, 1.f));
		FinalDamage = FiniteOrDefault(DamageRoll.DamageBeforeMitigation * (1.f - EffectiveDR));

		float LifeSteal = 0.f;
		if (bCanLifeSteal)
		{
			ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DamageStatics().LifeStealDef, EvalParams, LifeSteal);
			LifeSteal = FMath::Clamp(
				FiniteOrDefault(LifeSteal),
				0.f,
				FMath::Max(0.f, FiniteOrDefault(CombatLimits.MaxLifeSteal)));
		}

		if (bCanStagger && FinalDamage > KINDA_SMALL_NUMBER)
		{
			float StaggerPower = 0.f;
			float StaggerResistance = 0.f;
			ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DamageStatics().StaggerPowerDef, EvalParams, StaggerPower);
			ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DamageStatics().StaggerResistanceDef, EvalParams, StaggerResistance);
			const float StaggerMultiplier = Spec.GetSetByCallerMagnitude(
				AeyerjiTags::SBC_Damage_StaggerMultiplier,
				/*WarnIfNotFound=*/false,
				1.f);
			StaggerDamage = FiniteOrDefault(
				FinalDamage
				* FMath::Max(0.f, FiniteOrDefault(StaggerPower))
				* FMath::Max(0.f, FiniteOrDefault(StaggerMultiplier))
				* (1.f - FMath::Clamp(
					FiniteOrDefault(StaggerResistance),
					0.f,
					FMath::Max(0.f, FiniteOrDefault(CombatLimits.MaxStaggerResistance)))));
		}

		if (Result)
		{
			Result->PreMitigationDamage = DamageRoll.DamageBeforeMitigation;
			Result->FinalDamage = FinalDamage;
			Result->MitigatedDamage = FMath::Max(0.f, DamageRoll.DamageBeforeMitigation - FinalDamage);
			Result->LifeStealFraction = LifeSteal;
			Result->StaggerDamage = StaggerDamage;
			Result->bWasCritical = DamageRoll.bWasCritical;
		}

		UE_LOG(LogDamagePhysicalCalc, VeryVerbose,
			TEXT("DamageCalc: Base=%.2f Variance=%.3f UseVariance=%d CritChance=%.3f CritMultiplier=%.3f CanCrit=%d Crit=%d DodgeChance=%.3f Armor=%.2f Shred=%.2f Pen=%.2f DR=%.4f Final=%.2f"),
			BaseDamage,
			AttackDamageVariance,
			bUseVariance ? 1 : 0,
			CritChance,
			CriticalDamageMultiplier,
			bCanCrit ? 1 : 0,
			DamageRoll.bWasCritical ? 1 : 0,
			DodgeChance,
			ArmorValue,
			ArmorShred,
			ArmorPenetration,
			EffectiveDR,
			FinalDamage);
	}

	if (FinalDamage > KINDA_SMALL_NUMBER)
	{
		OutExecutionOutput.AddOutputModifier(
			FGameplayModifierEvaluatedData(UAeyerjiAttributeSet::GetIncomingDamageAttribute(), EGameplayModOp::Additive, FinalDamage));
	}
	if (StaggerDamage > KINDA_SMALL_NUMBER)
	{
		OutExecutionOutput.AddOutputModifier(
			FGameplayModifierEvaluatedData(UAeyerjiAttributeSet::GetIncomingStaggerAttribute(), EGameplayModOp::Additive, StaggerDamage));
	}
}

float UExecCalc_DamagePhysical::ComputeArmorDR(float Armor, const FArmorTuning& Tuning)
{
	return ResolveArmorDamageReduction(
		Armor,
		Tuning.ArmorK,
		Tuning.ArmorSoftCap,
		Tuning.ArmorTailSlope,
		Tuning.ArmorTailCap);
}
