// Copyright (c) 2025 Aeyerji.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "GameplayTagContainer.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Delegates/Delegate.h"
#include "Delegates/DelegateCombinations.h"
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

	/** Blueprint-friendly helper that broadcasts an event through the subsystem. */
	UFUNCTION(BlueprintCallable, Category="Gameplay Events", meta=(WorldContext="WorldContextObject"))
	static void BroadcastGameplayEvent(UObject* WorldContextObject, FGameplayTag EventTag, const FGameplayEventData& Payload);

private:
	FAeyerjiGameplayEventNativeSignature& FindOrAddDelegate(const FGameplayTag& EventTag);

private:
	TMap<FGameplayTag, FAeyerjiGameplayEventNativeSignature> EventDelegates;
};
