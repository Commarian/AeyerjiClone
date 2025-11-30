// AeyerjiRagdollHelpers.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"

struct AEYERJI_API FAeyerjiRagdollHelpers
{
	// Starts a network-safe ragdoll on a Character's Mesh, optionally with an impulse.
	// If BoneName is NAME_None, impulse applies to the pelvis/root body.
	static void StartRagdoll(ACharacter* Char, const FVector& Impulse = FVector::ZeroVector, const FVector& ImpulseWorldLocation = FVector::ZeroVector, FName BoneName = NAME_None);

	// Call once you want to clean the corpse (e.g., after dissolve or delay).
	static void TeardownAfterRagdoll(ACharacter* Char);
};
