// GE_DamagePhysical.cpp

#include "GAS/GE_DamagePhysical.h"

#include "GAS/ExecCalc_DamagePhysical.h"

UGE_DamagePhysical::UGE_DamagePhysical(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FGameplayEffectExecutionDefinition ExecDef;
	ExecDef.CalculationClass = UExecCalc_DamagePhysical::StaticClass();
	Executions.Add(ExecDef);
}
