//  ProjectileAimLibrary.cpp
#include "ProjectileAimLibrary.h"
#include "GameFramework/Actor.h"
#include "GameFramework/ProjectileMovementComponent.h"

namespace
{
	constexpr double MaximumPredictionSeconds = 60.0;

	FVector SafeDirection(const FVector& Direction, const FVector& Fallback = FVector::ForwardVector)
	{
		if (Direction.ContainsNaN())
		{
			return Fallback;
		}
		const FVector Normalized = Direction.GetSafeNormal();
		return Normalized.IsNearlyZero() ? Fallback : Normalized;
	}
}

EAimResult UProjectileAimLibrary::GetLaunchVelocity(
		const UObject* /*WorldContextObject*/,
		const FVector& MuzzleLocation,
		const FVector& MuzzleForward,
		AActor*        TargetActor,
		float          ProjectileSpeed,
		float          ExtraLeadSeconds,
		FVector&       OutLaunchVelocity)
{
	ProjectileSpeed = FMath::IsFinite(ProjectileSpeed) ? FMath::Max(1.f, ProjectileSpeed) : 1.f;
	OutLaunchVelocity = FVector::ZeroVector;
	if (MuzzleLocation.ContainsNaN())
	{
		return EAimResult::StraightShot;
	}
	const FVector SafeForward = SafeDirection(MuzzleForward);

	/* ───── 1. No target → StraightShot ───── */
	if (!IsValid(TargetActor))
	{
		OutLaunchVelocity = SafeForward * ProjectileSpeed;
		return EAimResult::StraightShot;
	}

	/* ───── 2. Stationary or nearly-stationary target? ───── */
	const FVector TargetLocation = TargetActor->GetActorLocation();
	const FVector TargetVelocity = TargetActor->GetVelocity();   // world units / s
	if (TargetLocation.ContainsNaN() || TargetVelocity.ContainsNaN())
	{
		OutLaunchVelocity = SafeForward * ProjectileSpeed;
		return EAimResult::StraightShot;
	}

	if (TargetVelocity.IsNearlyZero(10.f))
	{
		OutLaunchVelocity = SafeDirection(TargetLocation - MuzzleLocation, SafeForward) * ProjectileSpeed;
		return EAimResult::StationaryHit;
	}

	/* ───── 3. Predict lead against moving target ───── */
	const FVector  S  = MuzzleLocation;
	const FVector  P0 = TargetLocation;
	const FVector  V  = TargetVelocity;

	const FVector  R  = P0 - S;                   // relative position
	const double a = static_cast<double>(FVector::DotProduct(V, V)) - FMath::Square(static_cast<double>(ProjectileSpeed));
	const double b = 2.0 * static_cast<double>(FVector::DotProduct(V, R));
	const double c = static_cast<double>(FVector::DotProduct(R, R));

	// Solve a t² + b t + c = 0  (smallest positive t is our time-to-impact)
	const double Discriminant = b * b - 4.0 * a * c;

	double ChosenTime = -1.0;
	if (FMath::IsFinite(Discriminant) && Discriminant >= 0.0 && !FMath::IsNearlyZero(a))
	{
		const double SqrtDisc = FMath::Sqrt(Discriminant);
		const double t1 = (-b + SqrtDisc) / (2.0 * a);
		const double t2 = (-b - SqrtDisc) / (2.0 * a);

		// pick smallest positive root
		if (t1 > 0.f && t2 > 0.f)        ChosenTime = FMath::Min(t1, t2);
		else if (t1 > 0.f)               ChosenTime = t1;
		else if (t2 > 0.f)               ChosenTime = t2;
	}

	if (ChosenTime < 0.f)   // no valid root → fallback to Stationary
	{
		OutLaunchVelocity = SafeDirection(TargetLocation - MuzzleLocation, SafeForward) * ProjectileSpeed;
		return EAimResult::StationaryHit;
	}

	const double SafeExtraLead = FMath::IsFinite(ExtraLeadSeconds) ? static_cast<double>(ExtraLeadSeconds) : 0.0;
	ChosenTime = FMath::Clamp(ChosenTime + SafeExtraLead, 0.0, MaximumPredictionSeconds);
	const FVector AimPoint = P0 + V * static_cast<float>(ChosenTime);
	OutLaunchVelocity = SafeDirection(AimPoint - MuzzleLocation, SafeForward) * ProjectileSpeed;

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
	if (!ProjectileMov || !ProjectileMov->UpdatedComponent)
	{
		return false;
	}

	const FVector Start = ProjectileMov->UpdatedComponent->GetComponentLocation();
	if (Start.ContainsNaN())
	{
		ProjectileMov->Velocity = FVector::ZeroVector;
		OutCalculatedAimPoint = FVector::ZeroVector;
		return false;
	}
	const float SafeSpeed = FMath::IsFinite(InitialSpeed) ? FMath::Max(1.f, InitialSpeed) : 1.f;
	const float SafeStationaryTolerance = FMath::IsFinite(StationaryTolerance)
		? FMath::Max(0.f, StationaryTolerance)
		: 0.f;
	const FVector Forward = SafeDirection(ProjectileMov->UpdatedComponent->GetForwardVector());

	// 1) decide where we *want* to shoot
	FVector AimPoint     = Start + Forward * 100.f;
	bool    bUsedLead    = false;

	if (IsValid(TargetActor))
	{
		const FVector TargetPos  = TargetActor->GetActorLocation();
		const FVector TargetVel  = TargetActor->GetVelocity();

		if (!TargetPos.ContainsNaN() && !TargetVel.ContainsNaN()
			&& TargetVel.Size() > SafeStationaryTolerance)
		{
			// Analytical lead-shot solve:  P + V*t  ==  S + Dir*MuzzleSpeed*t
			const FVector  ToTarget   = TargetPos - Start;
			const double a = static_cast<double>(FVector::DotProduct(TargetVel, TargetVel))
				- FMath::Square(static_cast<double>(SafeSpeed));
			const double b = 2.0 * static_cast<double>(FVector::DotProduct(TargetVel, ToTarget));
			const double c = static_cast<double>(FVector::DotProduct(ToTarget, ToTarget));

			const double Discriminant = b * b - 4.0 * a * c;
			if (FMath::IsFinite(Discriminant) && Discriminant >= 0.0 && FMath::Abs(a) > SMALL_NUMBER)
			{
				const double SqrtDiscriminant = FMath::Sqrt(Discriminant);
				const double T1 = (-b + SqrtDiscriminant) / (2.0 * a);
				const double T2 = (-b - SqrtDiscriminant) / (2.0 * a);
				double InterceptTime = -1.0;
				if (T1 > 0.0 && T2 > 0.0)
				{
					InterceptTime = FMath::Min(T1, T2);
				}
				else if (T1 > 0.0)
				{
					InterceptTime = T1;
				}
				else if (T2 > 0.0)
				{
					InterceptTime = T2;
				}

				if (InterceptTime > 0.0)
				{
					InterceptTime = FMath::Min(InterceptTime, MaximumPredictionSeconds);
					AimPoint = TargetPos + TargetVel * static_cast<float>(InterceptTime);
					bUsedLead  = true;
				}
			}
		}
		else if (!TargetPos.ContainsNaN())
		{
			AimPoint = TargetPos;
		}
	}
	if (AimPoint.ContainsNaN())
	{
		AimPoint = Start + Forward * 100.f;
		bUsedLead = false;
	}

	OutCalculatedAimPoint = AimPoint;

	// 2) set velocity *in world space*
	const FVector Dir = SafeDirection(AimPoint - Start, Forward);
	ProjectileMov->Velocity = Dir * SafeSpeed;

	// convenience: keep rotation aligned with flight
	ProjectileMov->bRotationFollowsVelocity = true;
	ProjectileMov->Activate(true);

	return bUsedLead;
}
