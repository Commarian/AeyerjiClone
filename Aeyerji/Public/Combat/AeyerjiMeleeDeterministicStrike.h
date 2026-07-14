#pragma once

#include "CoreMinimal.h"

/**
 * Pure policy helpers for deterministic melee strike timing and lifecycle decisions.
 * Keeping these rules outside the ability makes the high attack-speed edge cases testable.
 */
struct AEYERJI_API FAeyerjiMeleeDeterministicStrikePolicy
{
	/** Returns the server impact delay after attack-speed scaling. */
	static float CalculateImpactDelay(float WindupDuration, float StrikeDelay, float MontagePlayRate)
	{
		const float SafePlayRate = FMath::Max(MontagePlayRate, KINDA_SMALL_NUMBER);
		const float RawDelay = FMath::Max(0.f, WindupDuration) + FMath::Max(0.f, StrikeDelay);
		return FMath::Max(0.f, RawDelay / SafePlayRate);
	}

	/** Returns true when normal montage completion should flush a pending strike before recovery. */
	static bool ShouldResolveOnMontageFinish(bool bWasCancelled, bool bStrikePending, bool bStrikeResolved)
	{
		return !bWasCancelled && bStrikePending && !bStrikeResolved;
	}

	/** Returns true when a hard cancel should discard a pending unresolved strike. */
	static bool ShouldCancelOnHardCancel(bool bStrikePending, bool bStrikeResolved)
	{
		return bStrikePending && !bStrikeResolved;
	}

	/** Resolves the authored melee cone angle. A real zero means single-target, not a fallback cone. */
	static float ResolveAttackAngle(float AttributeAngle, bool bHasAttribute, float FallbackAngle)
	{
		if (!bHasAttribute)
		{
			return FMath::Clamp(FallbackAngle, 0.f, 360.f);
		}

		return FMath::Clamp(AttributeAngle, 0.f, 360.f);
	}

	/** Returns true when an angle should cleave additional targets. */
	static bool IsCleaveAngle(float AngleDegrees)
	{
		return AngleDegrees > KINDA_SMALL_NUMBER;
	}

	/** Returns the grace range for a target that was valid when the swing started. */
	static float CalculateLockedTargetGraceRange(float AttackRange, float GraceRangeMultiplier)
	{
		return FMath::Max(0.f, AttackRange) * FMath::Max(0.f, GraceRangeMultiplier);
	}

	/** Returns true when the target has moved far enough off facing that the attacker should re-face it. */
	static bool ShouldRefaceLockedTarget(float DotToTarget, float ThresholdDegrees)
	{
		const float ClampedThreshold = FMath::Clamp(ThresholdDegrees, 0.f, 180.f);
		const float CosThreshold = FMath::Cos(FMath::DegreesToRadians(ClampedThreshold));
		return DotToTarget < CosThreshold;
	}
};
