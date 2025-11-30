// Fill out your copyright notice in the Description page of Project Settings.

#include "AeyerjiCharacterMovementComponent.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
#include "AbilitySystemInterface.h"
#include "Logging/AeyerjiLog.h"
#include "Navigation/PathFollowingComponent.h"


UAeyerjiCharacterMovementComponent::UAeyerjiCharacterMovementComponent()
{
	// Initialize the rooted tag
	RootedTag = FGameplayTag::RequestGameplayTag(FName("Player.States.Rooted"));

	// Set up default network smoothing values
	bSmoothClientPosition_AIMovement = true;
	MinNetUpdateFrequency = 100.0f;
	MaxNetUpdateFrequency = 100.0f;
	ClientPredictionFudgeFactor = 0.0f;
	NetworkSmoothingMode = ENetworkSmoothingMode::Linear;
	NetworkMaxSmoothUpdateDistance = 120.f;  // how far we’ll smooth server corrections
    NetworkNoSmoothUpdateDistance  = 250.f;  // snap if farther than this
}

void UAeyerjiCharacterMovementComponent::BeginPlay()
{
    Super::BeginPlay();

    // Optional RVO avoidance can be enabled per-BP via UPROPERTY toggles (set in header)
    if (bEnableRVOAvoidance)
    {
        bUseRVOAvoidance = true;
        AvoidanceConsiderationRadius = RVOConsiderationRadius;
        AvoidanceWeight = RVOAvoidanceWeight;
    }
}

bool UAeyerjiCharacterMovementComponent::GetCachedRootedState() const
{
	const float CurrentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;

	// Only refresh the cache periodically to avoid expensive ASC lookups every frame
	if (CurrentTime - LastRootedCheckTime >= RootedStateCheckInterval)
	{
		LastRootedCheckTime = CurrentTime;

		// Try to get the ASC and update cached state
		if (UAbilitySystemComponent* ASC = GetAbilitySystemComponent())
		{
			bCachedRootedState = ASC->HasMatchingGameplayTag(RootedTag);
		}
		else
		{
			// If ASC is not available (common on clients during replication), 
			// keep the previous cached state rather than defaulting to false
			// This prevents movement glitches during network delays
		}
	}

	return bCachedRootedState;
}

void UAeyerjiCharacterMovementComponent::ForceRootedStateRefresh()
{
	LastRootedCheckTime = 0.0f; // Force immediate refresh on next check
	GetCachedRootedState(); // Update the cache now
}

UAbilitySystemComponent* UAeyerjiCharacterMovementComponent::GetAbilitySystemComponent() const
{
	// Get the ASC from the specific pawn that owns this movement component
	if (const APawn* OwningPawn = GetPawnOwner())
	{
		// First try to get ASC directly from the pawn
		if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(OwningPawn))
		{
			if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
			{
				return ASC;
			}
		}
		
		// Fallback: check PlayerState for ASC (common pattern where ASC lives on PlayerState)
		if (const APlayerState* PS = OwningPawn->GetPlayerState())
		{
			if (const IAbilitySystemInterface* PSI = Cast<IAbilitySystemInterface>(PS))
			{
				if (UAbilitySystemComponent* ASC = PSI->GetAbilitySystemComponent())
				{
					return ASC;
				}
			}
		}
	}
	return nullptr;
}

void UAeyerjiCharacterMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	// If the character is rooted, prevent any movement by zeroing out velocity
	if (GetCachedRootedState())
	{
		// Stop all movement immediately when rooted
		Velocity = FVector::ZeroVector;
		return;
	}

	// Call parent implementation for normal movement
	Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
}

void UAeyerjiCharacterMovementComponent::UpdateFromCompressedFlags(uint8 Flags)
{
	// If rooted, don't process any movement input flags
	if (GetCachedRootedState())
	{
		return;
	}

	Super::UpdateFromCompressedFlags(Flags);
}

FVector UAeyerjiCharacterMovementComponent::ConsumeInputVector()
{
	// If rooted, don't consume any input vector (prevents movement input from being processed)
	if (GetCachedRootedState())
	{
		// Call super to clear the input but return zero to prevent movement
		Super::ConsumeInputVector();
		return FVector::ZeroVector;
	}

	return Super::ConsumeInputVector();
}

void UAeyerjiCharacterMovementComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	//TODO Optimize
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Clear velocity after the parent tick if rooted (double ensure no movement)
	if (GetCachedRootedState())
	{
		Velocity = FVector::ZeroVector;
		Acceleration = FVector::ZeroVector;
		
		// Clear any cached movement goals/paths when rooted
		if (const APawn* OwningPawn = GetPawnOwner())
		{
			if (const AController* Controller = OwningPawn->GetController())
			{
				if (UPathFollowingComponent* PathFollowing = Controller->FindComponentByClass<UPathFollowingComponent>())
				{
					PathFollowing->AbortMove(*this, FPathFollowingResultFlags::ForcedScript);
				}
			}
		}
	}
}
