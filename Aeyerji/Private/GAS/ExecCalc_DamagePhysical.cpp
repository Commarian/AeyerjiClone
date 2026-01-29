// ExecCalc_DamagePhysical.cpp

#include "GAS/ExecCalc_DamagePhysical.h"

#include "AeyerjiGameplayTags.h"
#include "Attributes/AeyerjiStatTuning.h"
#include "Attributes/AeyerjiAttributeSet.h"
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

		FDamageStatics()
		{
			DEFINE_ATTRIBUTE_CAPTUREDEF(UAeyerjiAttributeSet, Armor, Target, false);
		}
	};

	FDamageStatics& DamageStatics()
	{
		static FDamageStatics Statics;
		return Statics;
	}
}

DEFINE_LOG_CATEGORY_STATIC(LogDamagePhysicalCalc, Log, All);

UExecCalc_DamagePhysical::UExecCalc_DamagePhysical()
{
	// Capture armor at execution time so active buffs/debuffs affect mitigation.
	RelevantAttributesToCapture.Add(DamageStatics().ArmorDef);
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

	static const FGameplayTag DamageTag = FGameplayTag::RequestGameplayTag(TEXT("SetByCaller.Damage.Instant"), /*ErrorIfNotFound=*/false);
	const float RawMagnitude = DamageTag.IsValid()
		? Spec.GetSetByCallerMagnitude(DamageTag, /*WarnIfNotFound=*/false, 0.f)
		: 0.f;

	if (RawMagnitude < 0.f)
	{
		UE_LOG(LogDamagePhysicalCalc, Warning, TEXT("Execute_Implementation: SetByCaller.Damage.Instant is negative (%.2f). Expected positive; using abs."), RawMagnitude);
	}

	const float BaseDamage = FMath::Max(FMath::Abs(RawMagnitude), 0.f);
	if (BaseDamage <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	FGameplayTagContainer AssetTags;
	Spec.GetAllAssetTags(AssetTags);
	const bool bTaggedPhysical = AssetTags.HasTagExact(AeyerjiTags::DamageType_Physical);
	const bool bClassIsPhysical = Spec.Def && Spec.Def->GetClass() == UGE_DamagePhysical::StaticClass();
	const bool bIsPhysical = bTaggedPhysical || bClassIsPhysical;

	float FinalDamage = BaseDamage;
	if (bIsPhysical)
	{
		FAggregatorEvaluateParameters EvalParams;
		EvalParams.SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
		EvalParams.TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

		float ArmorValue = 0.f;
		ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(DamageStatics().ArmorDef, EvalParams, ArmorValue);
		ArmorValue = FMath::Max(0.f, ArmorValue);

		static const FGameplayTag ArmorShredTag = FGameplayTag::RequestGameplayTag(TEXT("SetByCaller.ArmorShred"), /*ErrorIfNotFound=*/false);
		static const FGameplayTag ArmorPenTag = FGameplayTag::RequestGameplayTag(TEXT("SetByCaller.ArmorPenetration"), /*ErrorIfNotFound=*/false);

		const float ArmorShred = ArmorShredTag.IsValid()
			? Spec.GetSetByCallerMagnitude(ArmorShredTag, /*WarnIfNotFound=*/false, 0.f)
			: 0.f;
		ArmorValue = FMath::Max(0.f, ArmorValue - FMath::Max(0.f, ArmorShred));

		const float ArmorPenetration = ArmorPenTag.IsValid()
			? Spec.GetSetByCallerMagnitude(ArmorPenTag, /*WarnIfNotFound=*/false, 0.f)
			: 0.f;

		const FArmorTuning ArmorTuning = ResolveArmorTuning();
		const float DamageReduction = ComputeArmorDR(ArmorValue, ArmorTuning);
		const float EffectiveDR = DamageReduction * (1.f - FMath::Clamp(ArmorPenetration, 0.f, 1.f));
		FinalDamage = BaseDamage * (1.f - EffectiveDR);

		UE_LOG(LogDamagePhysicalCalc, VeryVerbose,
			TEXT("DamageCalc: Base=%.2f Armor=%.2f Shred=%.2f Pen=%.2f DR=%.4f Final=%.2f"),
			BaseDamage, ArmorValue, ArmorShred, ArmorPenetration, EffectiveDR, FinalDamage);
	}

	OutExecutionOutput.AddOutputModifier(
		FGameplayModifierEvaluatedData(UAeyerjiAttributeSet::GetHPAttribute(), EGameplayModOp::Additive, -FinalDamage));
}

float UExecCalc_DamagePhysical::ComputeArmorDR(float Armor, const FArmorTuning& Tuning)
{
	const float ClampedArmor = FMath::Max(0.f, Armor);
	if (ClampedArmor <= Tuning.ArmorSoftCap)
	{
		const float Denominator = ClampedArmor + FMath::Max(Tuning.ArmorK, KINDA_SMALL_NUMBER);
		return FMath::Clamp(ClampedArmor / Denominator, 0.f, 1.f);
	}

	const float TailDR = 0.5f + (ClampedArmor - Tuning.ArmorSoftCap) * Tuning.ArmorTailSlope;
	return FMath::Clamp(FMath::Min(TailDR, Tuning.ArmorTailCap), 0.f, 1.f);
}
