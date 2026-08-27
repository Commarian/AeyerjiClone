// Fill out your copyright notice in the Description page of Project Settings.

#include "AeyerjiCharacterMovementComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "GameFramework/Character.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "GameFramework/PlayerState.h"
#include "GameFramework/Pawn.h"
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
	NetworkMaxSmoothUpdateDistance = 120.f;  // Maximum distance over which clients smooth server corrections.
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
	// A zero timestamp would not force a refresh during the first cache interval after world start.
	LastRootedCheckTime = -FMath::Max(0.f, RootedStateCheckInterval);
	GetCachedRootedState();
}

UAbilitySystemComponent* UAeyerjiCharacterMovementComponent::GetAbilitySystemComponent() const
{
	if (APawn* OwningPawn = GetPawnOwner())
	{
		if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(OwningPawn))
		{
			return ASC;
		}
		
		// Player pawns may keep their ASC on PlayerState so it survives respawn.
		if (APlayerState* PlayerState = OwningPawn->GetPlayerState())
		{
			if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PlayerState))
			{
				return ASC;
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

void UAeyerjiCharacterMovementComponent::SmoothCorrection(const FVector& OldLocation, const FQuat& OldRotation, const FVector& NewLocation, const FQuat& NewRotation)
{
	const float CorrectionDistance = FVector::Dist(OldLocation, NewLocation);
	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const bool bCanLog = bLogMovementCorrections
		&& CorrectionDistance >= FMath::Max(0.f, MovementCorrectionLogThresholdCm)
		&& (LastMovementCorrectionLogTime < 0.0 || MovementCorrectionLogInterval <= 0.f || (Now - LastMovementCorrectionLogTime) >= MovementCorrectionLogInterval);

	if (bCanLog)
	{
		const APawn* OwningPawn = GetPawnOwner();
		const AController* Controller = OwningPawn ? OwningPawn->GetController() : nullptr;
		const UPathFollowingComponent* PathFollowing = Controller ? Controller->FindComponentByClass<UPathFollowingComponent>() : nullptr;
		const EPathFollowingStatus::Type PathStatus = PathFollowing ? PathFollowing->GetStatus() : EPathFollowingStatus::Idle;
		AJ_LOG_VERY_VERBOSE(this, TEXT("[MoveDiag] SmoothCorrection Dist=%.2f Old=%s New=%s Pawn=%s Role=%d Velocity=%s PathStatus=%d"),
			CorrectionDistance,
			*OldLocation.ToCompactString(),
			*NewLocation.ToCompactString(),
			*GetNameSafe(OwningPawn),
			OwningPawn ? static_cast<int32>(OwningPawn->GetLocalRole()) : -1,
			*Velocity.ToCompactString(),
			static_cast<int32>(PathStatus));
		LastMovementCorrectionLogTime = Now;
	}

	Super::SmoothCorrection(OldLocation, OldRotation, NewLocation, NewRotation);
}
