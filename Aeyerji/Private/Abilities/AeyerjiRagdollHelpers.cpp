// Fill out your copyright notice in the Description page of Project Settings.


#include "Abilities/AeyerjiRagdollHelpers.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PhysicsEngine/BodyInstance.h"

static void CopyVelocityToRagdoll(USkeletalMeshComponent* Mesh, const FVector& Vel)
{
	if (!Mesh) return;
	Mesh->SetAllPhysicsLinearVelocity(Vel);
}

void FAeyerjiRagdollHelpers::StartRagdoll(ACharacter* Char, const FVector& Impulse, const FVector& ImpulseWorldLocation, FName BoneName)
{
	if (!Char) return;

	UCapsuleComponent* Capsule = Char->GetCapsuleComponent();
	USkeletalMeshComponent* Mesh = Char->GetMesh();
	if (!Capsule || !Mesh) return;

	// 1) Stop normal movement & input
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

	// Stop any animation influence
	if (UAnimInstance* AI = Mesh->GetAnimInstance())
	{
		AI->StopAllMontages(0.05f);
	}
	Mesh->bPauseAnims = true;

	// 🔸 Flip the component flag too (this makes the Details "Simulate Physics" tick true)
	Mesh->SetSimulatePhysics(true);

	// Ensure all bodies are simulating and not blending with animation
	Mesh->SetAllBodiesBelowPhysicsBlendWeight(NAME_None, 1.f, false, true);
	Mesh->SetAllBodiesSimulatePhysics(true);
	Mesh->WakeAllRigidBodies();

	// Capsule out of the way
	Char->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	
	if (UAnimInstance* AI = Mesh->GetAnimInstance())
	{
		AI->StopAllMontages(0.05f);   // kill any slots
	}
	Mesh->bPauseAnims = true;

	// 4) Give the ragdoll the current movement velocity so it doesn't "freeze" on death
	if (const UCharacterMovementComponent* Move = Char->GetCharacterMovement())
	{
		CopyVelocityToRagdoll(Mesh, Move->Velocity);
	}

	// 5) Optional impulse/knockback at the hit point
	if (!Impulse.IsNearlyZero())
	{
		if (BoneName.IsNone())
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

	// 7) Optional: let the actor die after a while
	// Char->SetLifeSpan(10.f);
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
