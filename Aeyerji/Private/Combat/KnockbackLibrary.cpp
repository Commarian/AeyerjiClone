// KnockbackLibrary.cpp
#include "Combat/KnockbackLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/PrimitiveComponent.h"

static FVector ProjectAndNormalizeIfNeeded(const FVector& V, bool bFlattenToGround)
{
	FVector Out = V;
	if (bFlattenToGround)
	{
		Out = FVector::VectorPlaneProject(Out, FVector::UpVector);
	}
	return Out.GetSafeNormal();
}

FVector UKnockbackLibrary::RandomizedKnockbackDirection(const FVector& Forward, float YawJitterDeg, float PitchJitterDeg, bool bFlattenToGround)
{
	const FVector Dir = Forward.GetSafeNormal();

	// Use FMath::VRandCone with separate horizontal/vertical half angles (in radians).
	// Horizontal ≈ yaw jitter, Vertical ≈ pitch jitter (relative to Dir).
	const float HorzRad  = FMath::DegreesToRadians(FMath::Max(0.f, YawJitterDeg));
	const float VertRad  = FMath::DegreesToRadians(FMath::Max(0.f, PitchJitterDeg));
	const FVector Jittered = FMath::VRandCone(Dir, VertRad, HorzRad);

	return ProjectAndNormalizeIfNeeded(Jittered, bFlattenToGround);
}

void UKnockbackLibrary::ApplyKnockback(UObject* WorldContextObject,
	AActor* Source,
	ACharacter* Target,
	float MinForce,
	float MaxForce,
	float YawJitterDeg,
	float PitchJitterDeg,
	bool bFlattenToGround,
	float UpBoost,
	bool bUseLaunchCharacter)
{
	if (!Source || !Target) return;

	// Authority check: perform knockback on the server so clients replicate correctly.
	// If you must trigger from clients, route the call to the server RPC first.
	if (AActor* AuthActor = Source; AuthActor && !AuthActor->HasAuthority())
	{
		// Early out on non-authority to avoid double forces in network play.
		return;
	}

	const FVector BaseForward = Source->GetActorForwardVector();
	const FVector Dir = RandomizedKnockbackDirection(BaseForward, YawJitterDeg, PitchJitterDeg, bFlattenToGround);

	const float Force = FMath::FRandRange(MinForce, MaxForce);
	FVector Velocity = Dir * Force;
	Velocity.Z += UpBoost;

	if (bUseLaunchCharacter)
	{
		// LaunchCharacter replaces velocity by default; set XY/Z override flags true.
		Target->LaunchCharacter(Velocity, /*bXYOverride*/ true, /*bZOverride*/ true);
	}
	else
	{
		// If you prefer physics impulse on a simulating root:
		if (UPrimitiveComponent* Prim = Cast<UPrimitiveComponent>(Target->GetRootComponent()))
		{
			if (Prim->IsSimulatingPhysics())
			{
				// Use mass-independent impulse for consistent feel.
				Prim->AddImpulse(Velocity, NAME_None, /*bVelChange*/ true);
			}
			else
			{
				// Fallback: directly set velocity on CharacterMovement (if present)
				if (UCharacterMovementComponent* Move = Target->GetCharacterMovement())
				{
					Move->Velocity = Velocity;
				}
			}
		}
	}
}
