// GE_ItemStats.cpp

#include "GAS/GE_ItemStats.h"

#include "GAS/ExecCalc_ItemStats.h"

UGE_ItemStats::UGE_ItemStats()
{
	DurationPolicy = EGameplayEffectDurationType::Infinite;

	FGameplayEffectExecutionDefinition ExecDef;
	ExecDef.CalculationClass = UExecCalc_ItemStats::StaticClass();
	Executions.Add(ExecDef);
}

