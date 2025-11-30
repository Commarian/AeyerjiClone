// ExecCalc_ItemStats.h
#pragma once

#include "CoreMinimal.h"
#include "GameplayEffectExecutionCalculation.h"

#include "ExecCalc_ItemStats.generated.h"

/**
 * Custom execution that reads modifiers from an item instance and writes them to the target ASC.
 */
UCLASS()
class AEYERJI_API UExecCalc_ItemStats : public UGameplayEffectExecutionCalculation
{
	GENERATED_BODY()

public:
	UExecCalc_ItemStats();

	virtual void Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
	                                    FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const override;
};

