// Copyright (c) 2025 Aeyerji.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameplayTagContainer.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Delegates/Delegate.h"
#include "Delegates/DelegateCombinations.h"
#include "Systems/AeyerjiWorldStateTypes.h"
#include "AeyerjiGameplayEventSubsystem.generated.h"

/**
 * Lightweight dispatcher for gameplay-tag keyed events that need to fire outside of GAS.
 */
UCLASS()
class AEYERJI_API UAeyerjiGameplayEventSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	DECLARE_MULTICAST_DELEGATE_TwoParams(FAeyerjiGameplayEventNativeSignature, const FGameplayTag&, const FGameplayEventData&);

	static UAeyerjiGameplayEventSubsystem* Get(const UObject* WorldContext);

	/** Register a native listener for a gameplay event tag; returns a handle that must be stored for unregistration. */
	FDelegateHandle RegisterListener(const FGameplayTag& EventTag, FAeyerjiGameplayEventNativeSignature::FDelegate&& Delegate);

	/** Removes a previously registered listener. Safe to call even if the handle/tag pair is no longer registered. */
	void UnregisterListener(const FGameplayTag& EventTag, FDelegateHandle& Handle);

	/** Broadcast a gameplay event payload to all native listeners keyed to the given tag. */
	void BroadcastEvent(const FGameplayTag& EventTag, const FGameplayEventData& Payload);

	/** Records a gameplay event as happened in the central world-state registry without broadcasting it. */
	void RecordEvent(const FGameplayTag& EventTag, const FGameplayEventData& Payload, EAeyerjiWorldStatePersistence Persistence, EAeyerjiWorldStateReplication Replication);

	/** Broadcasts a gameplay event and records it as happened in the central world-state registry. */
	void BroadcastAndRecordEvent(const FGameplayTag& EventTag, const FGameplayEventData& Payload, EAeyerjiWorldStatePersistence Persistence, EAeyerjiWorldStateReplication Replication);

	/** Blueprint-friendly helper that broadcasts an event through the subsystem. */
	UFUNCTION(BlueprintCallable, Category="Gameplay Events", meta=(WorldContext="WorldContextObject"))
	static void BroadcastGameplayEvent(UObject* WorldContextObject, FGameplayTag EventTag, const FGameplayEventData& Payload);

	/** Blueprint-friendly helper that records an event through the world-state registry. */
	UFUNCTION(BlueprintCallable, Category="Gameplay Events", meta=(WorldContext="WorldContextObject"))
	static void RecordGameplayEvent(UObject* WorldContextObject, FGameplayTag EventTag, const FGameplayEventData& Payload, EAeyerjiWorldStatePersistence Persistence = EAeyerjiWorldStatePersistence::Persistent, EAeyerjiWorldStateReplication Replication = EAeyerjiWorldStateReplication::ServerOnly);

	/** Blueprint-friendly helper that broadcasts and records an event through explicit opt-in. */
	UFUNCTION(BlueprintCallable, Category="Gameplay Events", meta=(WorldContext="WorldContextObject"))
	static void BroadcastAndRecordGameplayEvent(UObject* WorldContextObject, FGameplayTag EventTag, const FGameplayEventData& Payload, EAeyerjiWorldStatePersistence Persistence = EAeyerjiWorldStatePersistence::Persistent, EAeyerjiWorldStateReplication Replication = EAeyerjiWorldStateReplication::ServerOnly);

private:
	FAeyerjiGameplayEventNativeSignature& FindOrAddDelegate(const FGameplayTag& EventTag);

private:
	TMap<FGameplayTag, FAeyerjiGameplayEventNativeSignature> EventDelegates;
};
