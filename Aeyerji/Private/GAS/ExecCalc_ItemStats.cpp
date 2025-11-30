// ExecCalc_ItemStats.cpp

#include "GAS/ExecCalc_ItemStats.h"

#include "Items/ItemInstance.h"

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
	UObject* SourceObject = Spec.GetContext().GetSourceObject();
	const UAeyerjiItemInstance* ItemInstance = Cast<UAeyerjiItemInstance>(SourceObject);
	if (!ItemInstance)
	{
		return;
	}

	for (const FItemStatModifier& Modifier : ItemInstance->FinalAggregatedModifiers)
	{
		if (!Modifier.Attribute.IsValid())
		{
			continue;
		}

		const EGameplayModOp::Type GameplayOp = ConvertItemOp(Modifier.Op);
		FGameplayModifierEvaluatedData EvaluatedData(Modifier.Attribute, GameplayOp, Modifier.Magnitude);
		OutExecutionOutput.AddOutputModifier(EvaluatedData);
	}
}
