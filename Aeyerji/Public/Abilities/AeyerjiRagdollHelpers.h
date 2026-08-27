// AeyerjiRagdollHelpers.h
#pragma once

#include "CoreMinimal.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "GameFramework/Character.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS

struct AEYERJI_API FAeyerjiRagdollHelpers
{
	/** Starts a local ragdoll on a character mesh. The caller must invoke it on every machine that needs presentation. */
	// If BoneName is NAME_None, impulse applies to the pelvis/root body.
	static void StartRagdoll(ACharacter* Char, const FVector& Impulse = FVector::ZeroVector, const FVector& ImpulseWorldLocation = FVector::ZeroVector, FName BoneName = NAME_None);

	/** Stops corpse physics after presentation; the caller owns any further pooled-character state restoration. */
	static void TeardownAfterRagdoll(ACharacter* Char);
};
