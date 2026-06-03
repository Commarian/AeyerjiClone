#include "Systems/AeyerjiGameplayEventSubsystem.h"

#include "Systems/AeyerjiWorldStateSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

UAeyerjiGameplayEventSubsystem* UAeyerjiGameplayEventSubsystem::Get(const UObject* WorldContext)
{
	if (!WorldContext)
	{
		return nullptr;
	}

	const UWorld* World = WorldContext->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	if (UGameInstance* GameInstance = World->GetGameInstance())
	{
		return GameInstance->GetSubsystem<UAeyerjiGameplayEventSubsystem>();
	}

	return nullptr;
}

FDelegateHandle UAeyerjiGameplayEventSubsystem::RegisterListener(const FGameplayTag& EventTag, FAeyerjiGameplayEventNativeSignature::FDelegate&& Delegate)
{
	if (!EventTag.IsValid())
	{
		return {};
	}

	return FindOrAddDelegate(EventTag).Add(MoveTemp(Delegate));
}

void UAeyerjiGameplayEventSubsystem::UnregisterListener(const FGameplayTag& EventTag, FDelegateHandle& Handle)
{
	if (!Handle.IsValid() || !EventTag.IsValid())
	{
		return;
	}

	if (FAeyerjiGameplayEventNativeSignature* Delegate = EventDelegates.Find(EventTag))
	{
		Delegate->Remove(Handle);
	}

	Handle.Reset();
}

void UAeyerjiGameplayEventSubsystem::BroadcastEvent(const FGameplayTag& EventTag, const FGameplayEventData& Payload)
{
	if (!EventTag.IsValid())
	{
		return;
	}

	if (FAeyerjiGameplayEventNativeSignature* Delegate = EventDelegates.Find(EventTag))
	{
		Delegate->Broadcast(EventTag, Payload);
	}
}

void UAeyerjiGameplayEventSubsystem::RecordEvent(const FGameplayTag& EventTag, const FGameplayEventData& Payload, const EAeyerjiWorldStatePersistence Persistence, const EAeyerjiWorldStateReplication Replication)
{
	(void)Payload;

	if (!EventTag.IsValid())
	{
		return;
	}

	if (UAeyerjiWorldStateSubsystem* WorldStateSubsystem = UAeyerjiWorldStateSubsystem::Get(this))
	{
		WorldStateSubsystem->MarkEventHappened(EventTag, NAME_None, Persistence, Replication);
	}
}

void UAeyerjiGameplayEventSubsystem::BroadcastAndRecordEvent(const FGameplayTag& EventTag, const FGameplayEventData& Payload, const EAeyerjiWorldStatePersistence Persistence, const EAeyerjiWorldStateReplication Replication)
{
	BroadcastEvent(EventTag, Payload);
	RecordEvent(EventTag, Payload, Persistence, Replication);
}

void UAeyerjiGameplayEventSubsystem::BroadcastGameplayEvent(UObject* WorldContextObject, FGameplayTag EventTag, const FGameplayEventData& Payload)
{
	if (UAeyerjiGameplayEventSubsystem* Subsystem = Get(WorldContextObject))
	{
		Subsystem->BroadcastEvent(EventTag, Payload);
	}
}

void UAeyerjiGameplayEventSubsystem::RecordGameplayEvent(UObject* WorldContextObject, FGameplayTag EventTag, const FGameplayEventData& Payload, const EAeyerjiWorldStatePersistence Persistence, const EAeyerjiWorldStateReplication Replication)
{
	if (UAeyerjiGameplayEventSubsystem* Subsystem = Get(WorldContextObject))
	{
		Subsystem->RecordEvent(EventTag, Payload, Persistence, Replication);
	}
}

void UAeyerjiGameplayEventSubsystem::BroadcastAndRecordGameplayEvent(UObject* WorldContextObject, FGameplayTag EventTag, const FGameplayEventData& Payload, const EAeyerjiWorldStatePersistence Persistence, const EAeyerjiWorldStateReplication Replication)
{
	if (UAeyerjiGameplayEventSubsystem* Subsystem = Get(WorldContextObject))
	{
		Subsystem->BroadcastAndRecordEvent(EventTag, Payload, Persistence, Replication);
	}
}

UAeyerjiGameplayEventSubsystem::FAeyerjiGameplayEventNativeSignature& UAeyerjiGameplayEventSubsystem::FindOrAddDelegate(const FGameplayTag& EventTag)
{
	return EventDelegates.FindOrAdd(EventTag);
}
