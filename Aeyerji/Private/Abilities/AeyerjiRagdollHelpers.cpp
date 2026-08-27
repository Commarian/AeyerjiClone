// Fill out your copyright notice in the Description page of Project Settings.


#include "Abilities/AeyerjiRagdollHelpers.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PhysicsEngine/BodyInstance.h"

namespace
{
	bool IsFiniteRagdollVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}
}

void FAeyerjiRagdollHelpers::StartRagdoll(ACharacter* Char, const FVector& Impulse, const FVector& ImpulseWorldLocation, FName BoneName)
{
	if (!IsValid(Char)) return;

	UCapsuleComponent* Capsule = Char->GetCapsuleComponent();
	USkeletalMeshComponent* Mesh = Char->GetMesh();
	if (!Capsule || !Mesh) return;

	// Capture momentum before StopMovementImmediately clears the movement component velocity.
	FVector PreRagdollVelocity = FVector::ZeroVector;
	if (const UCharacterMovementComponent* Move = Char->GetCharacterMovement())
	{
		PreRagdollVelocity = IsFiniteRagdollVector(Move->Velocity) ? Move->Velocity : FVector::ZeroVector;
	}

	// Stop normal movement and input.
	if (UCharacterMovementComponent* Move = Char->GetCharacterMovement())
	{
		Move->StopMovementImmediately();
		Move->DisableMovement();
	}
	// Order matters: set collision/profile first
	Mesh->SetCollisionProfileName(TEXT("Ragdoll"));
	Mesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	// Make physics drive the component transform
	Mesh->PhysicsTransformUpdateMode = EPhysicsTransformUpdateMode::SimulationUpatesComponentTransform;

	// Stop animation influence before enabling physics.
	if (UAnimInstance* AI = Mesh->GetAnimInstance())
	{
		AI->StopAllMontages(0.05f);
	}
	Mesh->bPauseAnims = true;

	Mesh->SetSimulatePhysics(true);

	// Ensure all bodies are simulating and not blending with animation
	Mesh->SetAllBodiesBelowPhysicsBlendWeight(NAME_None, 1.f, false, true);
	Mesh->SetAllBodiesSimulatePhysics(true);
	Mesh->WakeAllRigidBodies();

	// Capsule out of the way
	Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Preserve the character's momentum rather than starting the corpse from rest.
	Mesh->SetAllPhysicsLinearVelocity(PreRagdollVelocity);

	// 5) Optional impulse/knockback at the hit point
	if (IsFiniteRagdollVector(Impulse) && !Impulse.IsNearlyZero())
	{
		if (BoneName.IsNone() && IsFiniteRagdollVector(ImpulseWorldLocation))
		{
			Mesh->AddImpulseAtLocation(Impulse, ImpulseWorldLocation, NAME_None);
		}
		else
		{
			Mesh->AddImpulseToAllBodiesBelow(Impulse, BoneName, /*bVelChange=*/true);
		}
	}

	// 6) Keep the mesh from snapping back to the capsule transform
	Mesh->SetIgnoreBoundsForEditorFocus(true);
	Mesh->SetEnableGravity(true);

}

void FAeyerjiRagdollHelpers::TeardownAfterRagdoll(ACharacter* Char)
{
	if (!Char) return;
	if (USkeletalMeshComponent* Mesh = Char->GetMesh())
	{
		Mesh->SetAllBodiesSimulatePhysics(false);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	if (UCapsuleComponent* Capsule = Char->GetCapsuleComponent())
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	}
}
