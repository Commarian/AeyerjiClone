#pragma once

#include "CoreMinimal.h"

/**
 * Pure leash rules shared by the enemy AI and automation tests.
 * A pawn leashes when it strays past its archetype's LeashDistance from the home
 * location recorded at checkout. Fail-closed: missing homes and non-positive or
 * non-finite ranges never leash, so unauthored pawns keep their current behavior.
 */
struct AEYERJI_API FAeyerjiLeashPolicy
{
	/** True when the pawn's distance from home exceeds its leash range. */
	static bool IsBeyondLeash(float DistanceFromHome, float LeashDistance, bool bHasHome)
	{
		if (!bHasHome)
		{
			return false;
		}
		if (!FMath::IsFinite(DistanceFromHome) || !FMath::IsFinite(LeashDistance))
		{
			return false;
		}
		if (LeashDistance <= 0.f)
		{
			return false;
		}
		return DistanceFromHome > LeashDistance;
	}
};
