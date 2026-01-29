// ExecCalc_ItemStats.cpp

#include "GAS/ExecCalc_ItemStats.h"

#include "AbilitySystemComponent.h"
#include "GameplayEffectExtension.h"
#include "Items/ItemDefinition.h"
#include "Items/ItemInstance.h"
#include "Logging/AeyerjiLog.h"

namespace
{
	EGameplayModOp::Type ConvertItemOp(EItemModOp Op)
	{
		switch (Op)
		{
		case EItemModOp::Multiplicative:
			return EGameplayModOp::MultiplyAdditive;
		case EItemModOp::Override:
			return EGameplayModOp::Override;
		default:
			return EGameplayModOp::Additive;
		}
	}
}

UExecCalc_ItemStats::UExecCalc_ItemStats()
{
}

void UExecCalc_ItemStats::Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
                                                 FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
	const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();
	const FGameplayEffectContextHandle& Context = Spec.GetContext();
	UObject* SourceObject = Context.GetSourceObject();
	UAbilitySystemComponent* TargetASC = ExecutionParams.GetTargetAbilitySystemComponent();
	UAbilitySystemComponent* SourceASC = ExecutionParams.GetSourceAbilitySystemComponent();
	const FGameplayTagContainer* SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	const FGameplayTagContainer* TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	const float Multiplier = Spec.GetSetByCallerMagnitude(FName(TEXT("ItemStatsMultiplier")), /*WarnIfNotFound=*/false, 1.f);

	UE_LOG(LogAeyerji, Verbose, TEXT("[ItemStatsDebug] ExecCalc: Begin Spec=%s Mult=%.2f; TargetASC=%s Owner=%s Avatar=%s; SourceASC=%s Owner=%s Avatar=%s; Instigator=%s Causer=%s SourceObject=%s Tags(Source=%s Target=%s)"),
		*GetNameSafe(Spec.Def),
		Multiplier,
		*GetNameSafe(TargetASC),
		*GetNameSafe(TargetASC ? TargetASC->GetOwner() : nullptr),
		*GetNameSafe(TargetASC ? TargetASC->GetAvatarActor() : nullptr),
		*GetNameSafe(SourceASC),
		*GetNameSafe(SourceASC ? SourceASC->GetOwner() : nullptr),
		*GetNameSafe(SourceASC ? SourceASC->GetAvatarActor() : nullptr),
		*GetNameSafe(Context.GetOriginalInstigator()),
		*GetNameSafe(Context.GetEffectCauser()),
		*GetNameSafe(SourceObject),
		SourceTags ? *SourceTags->ToString() : TEXT("None"),
		TargetTags ? *TargetTags->ToString() : TEXT("None"));

	const UAeyerjiItemInstance* ItemInstance = Cast<UAeyerjiItemInstance>(SourceObject);
	if (!ItemInstance)
	{
		UE_LOG(LogAeyerji, Verbose, TEXT("[ItemStatsDebug] ExecCalc: No ItemInstance. SourceObject=%s"),
			*GetNameSafe(SourceObject));
		return;
	}

	UE_LOG(LogAeyerji, Verbose, TEXT("[ItemStatsDebug] ExecCalc: Item=%s Def=%s Mods=%d"),
		*GetNameSafe(ItemInstance),
		*GetNameSafe(ItemInstance->Definition.Get()),
		ItemInstance->FinalAggregatedModifiers.Num());

	int32 OutputCount = 0;
	for (const FItemStatModifier& Modifier : ItemInstance->FinalAggregatedModifiers)
	{
		if (!Modifier.Attribute.IsValid())
		{
			UE_LOG(LogAeyerji, Verbose, TEXT("[ItemStatsDebug] ExecCalc: Skip invalid attr Mod Attr=%s Op=%d Mag=%.3f"),
				*Modifier.Attribute.GetName(),
				static_cast<int32>(Modifier.Op),
				Modifier.Magnitude);
			continue;
		}

		const EGameplayModOp::Type GameplayOp = ConvertItemOp(Modifier.Op);
		const float EffectiveMagnitude = Modifier.Magnitude * Multiplier;
		const bool bHasAttrSet = TargetASC ? TargetASC->HasAttributeSetForAttribute(Modifier.Attribute) : false;
		const float PreValue = (TargetASC && bHasAttrSet) ? TargetASC->GetNumericAttribute(Modifier.Attribute) : 0.f;
		FGameplayModifierEvaluatedData EvaluatedData(Modifier.Attribute, GameplayOp, EffectiveMagnitude);
		OutExecutionOutput.AddOutputModifier(EvaluatedData);
		UE_LOG(LogAeyerji, Verbose, TEXT("[ItemStatsDebug] ExecCalc: Output Attr=%s Set=%s HasSet=%d Pre=%.3f Op=%d Mag=%.3f Raw=%.3f"),
			*Modifier.Attribute.GetName(),
			*GetNameSafe(Modifier.Attribute.GetAttributeSetClass()),
			bHasAttrSet ? 1 : 0,
			PreValue,
			static_cast<int32>(GameplayOp),
			EffectiveMagnitude,
			Modifier.Magnitude);
		++OutputCount;
	}

	UE_LOG(LogAeyerji, Verbose, TEXT("[ItemStatsDebug] ExecCalc: OutputCount=%d"), OutputCount);
}
