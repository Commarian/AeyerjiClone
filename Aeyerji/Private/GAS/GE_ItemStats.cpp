// GE_ItemStats.cpp

#include "GAS/GE_ItemStats.h"

#include "GAS/ExecCalc_ItemStats.h"
#include "Logging/AeyerjiLog.h"

namespace
{
	const TCHAR* GetDurationPolicyName(EGameplayEffectDurationType Policy)
	{
		switch (Policy)
		{
		case EGameplayEffectDurationType::Instant:
			return TEXT("Instant");
		case EGameplayEffectDurationType::Infinite:
			return TEXT("Infinite");
		case EGameplayEffectDurationType::HasDuration:
			return TEXT("HasDuration");
		default:
			return TEXT("Unknown");
		}
	}
}

UGE_ItemStats::UGE_ItemStats()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;

	FGameplayEffectExecutionDefinition ExecDef;
	ExecDef.CalculationClass = UExecCalc_ItemStats::StaticClass();
	Executions.Add(ExecDef);
}

void UGE_ItemStats::PostInitProperties()
{
	Super::PostInitProperties();

	UE_LOG(LogAeyerji, Verbose,
		TEXT("[ItemStatsDebug] GE_ItemStats PostInitProperties Name=%s CDO=%d Duration=%s Period=%.3f Execs=%d"),
		*GetNameSafe(this),
		HasAnyFlags(RF_ClassDefaultObject) ? 1 : 0,
		GetDurationPolicyName(DurationPolicy),
		Period.GetValue(),
		Executions.Num());

	for (int32 Index = 0; Index < Executions.Num(); ++Index)
	{
		const FGameplayEffectExecutionDefinition& ExecDef = Executions[Index];
		UE_LOG(LogAeyerji, Verbose,
			TEXT("[ItemStatsDebug] GE_ItemStats Exec[%d] Calc=%s"),
			Index,
			*GetNameSafe(ExecDef.CalculationClass));
	}
}

void UGE_ItemStats::PostLoad()
{
	Super::PostLoad();

	UE_LOG(LogAeyerji, Verbose,
		TEXT("[ItemStatsDebug] GE_ItemStats PostLoad Name=%s CDO=%d Duration=%s Period=%.3f Execs=%d"),
		*GetNameSafe(this),
		HasAnyFlags(RF_ClassDefaultObject) ? 1 : 0,
		GetDurationPolicyName(DurationPolicy),
		Period.GetValue(),
		Executions.Num());

	for (int32 Index = 0; Index < Executions.Num(); ++Index)
	{
		const FGameplayEffectExecutionDefinition& ExecDef = Executions[Index];
		UE_LOG(LogAeyerji, Verbose,
			TEXT("[ItemStatsDebug] GE_ItemStats Exec[%d] Calc=%s"),
			Index,
			*GetNameSafe(ExecDef.CalculationClass));
	}
}
