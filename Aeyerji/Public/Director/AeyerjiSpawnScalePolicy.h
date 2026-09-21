#pragma once

#include "CoreMinimal.h"

/**
 * Pure spawn-scale rules shared by the encounter spawner and automation tests.
 * Spawn markers and spawner actors place enemies but must never resize them; actor scale
 * stays authored on the pawn, plus intentional elite multipliers applied later in the spawn flow.
 */
struct AEYERJI_API FAeyerjiSpawnScalePolicy
{
	/** Actor-scale magnitude above which a non-intentionally-scaled pawn is worth a warning. */
	static constexpr float OverscaleWarningThreshold = 5.f;

	/** Forces a spawn transform back to unit scale. Returns true when the scale was modified. */
	static bool StripNonUnitScale(FTransform& InOutTransform)
	{
		if (InOutTransform.GetScale3D().Equals(FVector::OneVector))
		{
			return false;
		}
		InOutTransform.SetScale3D(FVector::OneVector);
		return true;
	}

	/**
	 * True when a pawn's final actor scale is outside every intentional scale window.
	 * Non-finite scale is always anomalous; elites, mini-bosses, and bosses carry
	 * intentional multipliers and are excluded by the caller through bHasIntentionalScale.
	 */
	static bool IsAnomalousSpawnScale(const FVector& Scale, bool bHasIntentionalScale)
	{
		if (!FMath::IsFinite(Scale.X) || !FMath::IsFinite(Scale.Y) || !FMath::IsFinite(Scale.Z))
		{
			return true;
		}
		return !bHasIntentionalScale && Scale.GetMax() > OverscaleWarningThreshold;
	}
};
