#include "Enemy/Tasks/STT_SetWorldStateTask.h"

#include "Systems/AeyerjiWorldStateSubsystem.h"
#include "StateTreeExecutionContext.h"

EStateTreeRunStatus USTT_SetWorldStateTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition)
{
	(void)Transition;

	UObject* Owner = Context.GetOwner();
	UAeyerjiWorldStateSubsystem* WorldStateSubsystem = UAeyerjiWorldStateSubsystem::Get(Owner);
	if (!WorldStateSubsystem)
	{
		return EStateTreeRunStatus::Failed;
	}

	const FAeyerjiWorldStateKey Key(StateTag, InstanceId, OwnerId);
	if (!Key.IsValid())
	{
		return EStateTreeRunStatus::Failed;
	}

	if (bClearEntry)
	{
		return WorldStateSubsystem->ClearValue(Key) ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
	}

	if (bIncrementInt)
	{
		int32 NewValue = 0;
		return WorldStateSubsystem->IncrementInt(Key, IntDelta, NewValue, Persistence, Replication, Scope)
			? EStateTreeRunStatus::Succeeded
			: EStateTreeRunStatus::Failed;
	}

	return WorldStateSubsystem->SetValue(Key, Value, Persistence, Replication, Scope)
		? EStateTreeRunStatus::Succeeded
		: EStateTreeRunStatus::Failed;
}
