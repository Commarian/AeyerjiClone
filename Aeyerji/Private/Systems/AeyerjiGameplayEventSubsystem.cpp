#include "Systems/AeyerjiGameplayEventSubsystem.h"

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

void UAeyerjiGameplayEventSubsystem::BroadcastGameplayEvent(UObject* WorldContextObject, FGameplayTag EventTag, const FGameplayEventData& Payload)
{
	if (UAeyerjiGameplayEventSubsystem* Subsystem = Get(WorldContextObject))
	{
		Subsystem->BroadcastEvent(EventTag, Payload);
	}
}

UAeyerjiGameplayEventSubsystem::FAeyerjiGameplayEventNativeSignature& UAeyerjiGameplayEventSubsystem::FindOrAddDelegate(const FGameplayTag& EventTag)
{
	return EventDelegates.FindOrAdd(EventTag);
}
