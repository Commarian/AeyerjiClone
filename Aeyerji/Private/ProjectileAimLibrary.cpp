//  ProjectileAimLibrary.cpp
#include "ProjectileAimLibrary.h"
#include "GameFramework/Actor.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Kismet/KismetMathLibrary.h"

EAimResult UProjectileAimLibrary::GetLaunchVelocity(
		const UObject* /*WorldContextObject*/,
		const FVector& MuzzleLocation,
		const FVector& MuzzleForward,
		AActor*        TargetActor,
		float          ProjectileSpeed,
		float          ExtraLeadSeconds,
		FVector&       OutLaunchVelocity)
{
	// Safety
	ProjectileSpeed = FMath::Max(1.f, ProjectileSpeed);          // avoid div/0
	OutLaunchVelocity = FVector::ZeroVector;

	/* ───── 1. No target → StraightShot ───── */
	if (!IsValid(TargetActor))
	{
		OutLaunchVelocity = MuzzleForward.GetSafeNormal() * ProjectileSpeed;
		return EAimResult::StraightShot;
	}

	/* ───── 2. Stationary or nearly-stationary target? ───── */
	const FVector TargetLocation = TargetActor->GetActorLocation();
	const FVector TargetVelocity = TargetActor->GetVelocity();   // world units / s

	if (TargetVelocity.IsNearlyZero(10.f))
	{
		OutLaunchVelocity = (TargetLocation - MuzzleLocation).GetSafeNormal() * ProjectileSpeed;
		return EAimResult::StationaryHit;
	}

	/* ───── 3. Predict lead against moving target ───── */
	const FVector  S  = MuzzleLocation;
	const FVector  P0 = TargetLocation;
	const FVector  V  = TargetVelocity;

	const FVector  R  = P0 - S;                   // relative position
	const float    a  = FVector::DotProduct(V,V) - FMath::Square(ProjectileSpeed);
	const float    b  = 2.f * FVector::DotProduct(V, R);
	const float    c  = FVector::DotProduct(R,R);

	// Solve a t² + b t + c = 0  (smallest positive t is our time-to-impact)
	float Discriminant = b*b - 4.f*a*c;

	float ChosenTime = -1.f;
	if (Discriminant >= 0.f && !FMath::IsNearlyZero(a))
	{
		const float SqrtDisc = FMath::Sqrt(Discriminant);
		const float t1 = (-b + SqrtDisc) / (2.f * a);
		const float t2 = (-b - SqrtDisc) / (2.f * a);

		// pick smallest positive root
		if (t1 > 0.f && t2 > 0.f)        ChosenTime = FMath::Min(t1, t2);
		else if (t1 > 0.f)               ChosenTime = t1;
		else if (t2 > 0.f)               ChosenTime = t2;
	}

	if (ChosenTime < 0.f)   // no valid root → fallback to Stationary
	{
		OutLaunchVelocity = (TargetLocation - MuzzleLocation).GetSafeNormal() * ProjectileSpeed;
		return EAimResult::StationaryHit;
	}

	ChosenTime += ExtraLeadSeconds;
	const FVector AimPoint = P0 + V * ChosenTime;
	OutLaunchVelocity = (AimPoint - MuzzleLocation).GetSafeNormal() * ProjectileSpeed;

	return EAimResult::PredictedHit;
}

bool UProjectileAimLibrary::LaunchProjectileTowards(
	UObject* WorldContextObject,
	UProjectileMovementComponent* ProjectileMov,
	const AActor* TargetActor,
	float InitialSpeed,
	float StationaryTolerance,
	FVector& OutCalculatedAimPoint)
{
	if (!ProjectileMov || !ProjectileMov->UpdatedComponent) { return false; }

	const FVector Start = ProjectileMov->UpdatedComponent->GetComponentLocation();

	// 1) decide where we *want* to shoot
	FVector AimPoint     = Start + ProjectileMov->UpdatedComponent->GetForwardVector() * 100.f;
	bool    bUsedLead    = false;

	if (TargetActor)
	{
		const FVector TargetPos  = TargetActor->GetActorLocation();
		const FVector TargetVel  = TargetActor->GetVelocity();

		if (TargetVel.Size() > StationaryTolerance)            // *** moving target  ***
		{
			const float  MuzzleSpeed = FMath::Max(InitialSpeed, KINDA_SMALL_NUMBER);

			// Analytical lead-shot solve:  P + V*t  ==  S + Dir*MuzzleSpeed*t
			const FVector  ToTarget   = TargetPos - Start;
			const float    a = FVector::DotProduct(TargetVel, TargetVel) - MuzzleSpeed*MuzzleSpeed;
			const float    b = 2.f * FVector::DotProduct(TargetVel, ToTarget);
			const float    c = FVector::DotProduct(ToTarget, ToTarget);

			float Discriminant = b*b - 4.f*a*c;
			if (Discriminant >= 0.f && FMath::Abs(a) > SMALL_NUMBER)
			{
				float t = (-b + FMath::Sqrt(Discriminant)) / (2.f*a);
				if (t < 0.f) { t = (-b - FMath::Sqrt(Discriminant)) / (2.f*a); }
				if (t > 0.f)
				{
					AimPoint   = TargetPos + TargetVel * t;
					bUsedLead  = true;
				}
			}
		}
		else                                                   // *** stationary target ***
		{
			AimPoint = TargetPos;
		}
	}

	OutCalculatedAimPoint = AimPoint;

	// 2) set velocity *in world space*
	const FVector Dir = (AimPoint - Start).GetSafeNormal();
	ProjectileMov->Velocity = Dir * InitialSpeed;

	// convenience: keep rotation aligned with flight
	ProjectileMov->bRotationFollowsVelocity = true;
	ProjectileMov->Activate(true);

	return bUsedLead;
}
