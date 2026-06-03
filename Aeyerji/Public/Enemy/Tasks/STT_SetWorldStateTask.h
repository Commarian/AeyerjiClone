#pragma once

#include "CoreMinimal.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "Systems/AeyerjiWorldStateTypes.h"
#include "STT_SetWorldStateTask.generated.h"

/**
 * StateTree task that writes, increments, or clears a central world-state entry.
 */
UCLASS(Blueprintable, meta=(DisplayName="Set World State"))
class AEYERJI_API USTT_SetWorldStateTask : public UStateTreeTaskBlueprintBase
{
	GENERATED_BODY()

public:
	/** World-state tag to mutate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World State")
	FGameplayTag StateTag;

	/** Optional instance id for a unique registered object or fact. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World State")
	FName InstanceId = NAME_None;

	/** Optional owner id for character/profile-scoped facts. Leave empty for global and run facts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World State")
	FName OwnerId = NAME_None;

	/** Value written when the task is not clearing or incrementing. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World State", meta=(EditCondition="!bClearEntry && !bIncrementInt"))
	FAeyerjiWorldStateValue Value;

	/** Clears the entry instead of setting Value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World State")
	bool bClearEntry = false;

	/** Increments an integer entry instead of setting Value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World State", meta=(EditCondition="!bClearEntry"))
	bool bIncrementInt = false;

	/** Delta used when bIncrementInt is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World State", meta=(EditCondition="bIncrementInt && !bClearEntry"))
	int32 IntDelta = 1;

	/** Persistence policy for write operations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World State", meta=(EditCondition="!bClearEntry"))
	EAeyerjiWorldStatePersistence Persistence = EAeyerjiWorldStatePersistence::Persistent;

	/** Replication policy for write operations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World State", meta=(EditCondition="!bClearEntry"))
	EAeyerjiWorldStateReplication Replication = EAeyerjiWorldStateReplication::ServerOnly;

	/** Lifetime and owner lane for write operations. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="World State", meta=(EditCondition="!bClearEntry"))
	EAeyerjiWorldStateScope Scope = EAeyerjiWorldStateScope::Global;

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) override;
};
