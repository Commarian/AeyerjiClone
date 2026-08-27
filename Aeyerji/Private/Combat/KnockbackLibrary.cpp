// KnockbackLibrary.cpp
#include "Combat/KnockbackLibrary.h"
#include "Kismet/KismetMathLibrary.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/PrimitiveComponent.h"

namespace
{
	constexpr float MaxKnockbackForce = 1000000.f;

	float FiniteClamped(const float Value, const float DefaultValue, const float MinValue, const float MaxValue)
	{
		return FMath::Clamp(FMath::IsFinite(Value) ? Value : DefaultValue, MinValue, MaxValue);
	}

	bool IsFiniteVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	FVector SafeKnockbackDirection(const FVector& Direction, const bool bFlattenToGround)
	{
		FVector Result = IsFiniteVector(Direction) ? Direction : FVector::ForwardVector;
		if (bFlattenToGround)
		{
			Result.Z = 0.f;
		}

		return Result.GetSafeNormal(SMALL_NUMBER, FVector::ForwardVector);
	}
}

static FVector ProjectAndNormalizeIfNeeded(const FVector& V, bool bFlattenToGround)
{
	return SafeKnockbackDirection(V, bFlattenToGround);
}

FVector UKnockbackLibrary::RandomizedKnockbackDirection(const FVector& Forward, float YawJitterDeg, float PitchJitterDeg, bool bFlattenToGround)
{
	const FVector Dir = SafeKnockbackDirection(Forward, bFlattenToGround);

	// Use FMath::VRandCone with separate horizontal/vertical half angles (in radians).
	// Horizontal approximates yaw jitter; vertical approximates pitch jitter relative to Dir.
	const float HorzRad = FMath::DegreesToRadians(FiniteClamped(YawJitterDeg, 0.f, 0.f, 180.f));
	const float VertRad = FMath::DegreesToRadians(FiniteClamped(PitchJitterDeg, 0.f, 0.f, 180.f));
	const FVector Jittered = FMath::VRandCone(Dir, HorzRad, VertRad);

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
	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World || !IsValid(Source) || !IsValid(Target)
		|| Source == Target || Source->GetWorld() != World || Target->GetWorld() != World)
	{
		return;
	}

	// Authority check: perform knockback on the server so clients replicate correctly.
	// If you must trigger from clients, route the call to the server RPC first.
	if (!Source->HasAuthority() || !Target->HasAuthority())
	{
		// Early out on non-authority to avoid double forces in network play.
		return;
	}

	const FVector BaseForward = Source->GetActorForwardVector();
	const FVector Dir = RandomizedKnockbackDirection(BaseForward, YawJitterDeg, PitchJitterDeg, bFlattenToGround);

	const float SafeMinForce = FiniteClamped(MinForce, 0.f, 0.f, MaxKnockbackForce);
	const float SafeMaxForce = FiniteClamped(MaxForce, SafeMinForce, 0.f, MaxKnockbackForce);
	const float Force = FMath::FRandRange(
		FMath::Min(SafeMinForce, SafeMaxForce),
		FMath::Max(SafeMinForce, SafeMaxForce));
	FVector Velocity = Dir * Force;
	Velocity.Z += FiniteClamped(UpBoost, 0.f, -MaxKnockbackForce, MaxKnockbackForce);
	if (!IsFiniteVector(Velocity))
	{
		return;
	}

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
