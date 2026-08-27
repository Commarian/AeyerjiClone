#include "World/AeyerjiLinkedTeleporter.h"

#include "Aeyerji/AeyerjiPlayerController.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Navigation/AeyerjiNavSafetyLibrary.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

namespace
{
	constexpr uint8 EndpointAIndex = 0;
	constexpr uint8 EndpointBIndex = 1;
	constexpr float MaxTeleporterDistance = 1000000.f;
	constexpr float MaxTeleporterCooldownSeconds = 86400.f;

	void ConfigureInteractionSphere(USphereComponent* Sphere)
	{
		if (!Sphere)
		{
			return;
		}

		Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Sphere->SetCollisionObjectType(ECC_WorldDynamic);
		Sphere->SetCollisionResponseToAllChannels(ECR_Ignore);
		Sphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		Sphere->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
		Sphere->SetGenerateOverlapEvents(true);
		Sphere->SetCanEverAffectNavigation(false);
	}

	void ConfigurePortalMesh(UStaticMeshComponent* MeshComponent)
	{
		if (!MeshComponent)
		{
			return;
		}

		MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
		MeshComponent->SetGenerateOverlapEvents(false);
		MeshComponent->SetCanEverAffectNavigation(false);
	}
}

AAeyerjiLinkedTeleporter::AAeyerjiLinkedTeleporter()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bAlwaysRelevant = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	EndpointA = CreateDefaultSubobject<USceneComponent>(TEXT("EndpointA"));
	EndpointA->SetupAttachment(SceneRoot);

	EndpointB = CreateDefaultSubobject<USceneComponent>(TEXT("EndpointB"));
	EndpointB->SetupAttachment(SceneRoot);

	EndpointAMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EndpointAMesh"));
	EndpointAMesh->SetupAttachment(EndpointA);
	ConfigurePortalMesh(EndpointAMesh);

	EndpointBMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EndpointBMesh"));
	EndpointBMesh->SetupAttachment(EndpointB);
	ConfigurePortalMesh(EndpointBMesh);

	EndpointAInteraction = CreateDefaultSubobject<USphereComponent>(TEXT("EndpointAInteraction"));
	EndpointAInteraction->SetupAttachment(EndpointA);
	ConfigureInteractionSphere(EndpointAInteraction);

	EndpointBInteraction = CreateDefaultSubobject<USphereComponent>(TEXT("EndpointBInteraction"));
	EndpointBInteraction->SetupAttachment(EndpointB);
	ConfigureInteractionSphere(EndpointBInteraction);

	ApplyEndpointConfiguration();
}

void AAeyerjiLinkedTeleporter::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ApplyEndpointConfiguration();
}

void AAeyerjiLinkedTeleporter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		for (TPair<FObjectKey, FTimerHandle>& Cooldown : ControllerCooldownTimers)
		{
			World->GetTimerManager().ClearTimer(Cooldown.Value);
		}
	}

	ControllerCooldownTimers.Empty();

	Super::EndPlay(EndPlayReason);
}

void AAeyerjiLinkedTeleporter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AAeyerjiLinkedTeleporter, EndpointBRelativeTransform);
	DOREPLIFETIME(AAeyerjiLinkedTeleporter, bAllowEndpointAToB);
	DOREPLIFETIME(AAeyerjiLinkedTeleporter, bAllowEndpointBToA);
}

bool AAeyerjiLinkedTeleporter::ResolveEndpointFromComponent(const UPrimitiveComponent* Component, uint8& OutEndpointIndex) const
{
	if (!Component)
	{
		return false;
	}

	if (Component == EndpointAInteraction.Get())
	{
		OutEndpointIndex = EndpointAIndex;
		return true;
	}

	if (Component == EndpointBInteraction.Get())
	{
		OutEndpointIndex = EndpointBIndex;
		return true;
	}

	return false;
}

FVector AAeyerjiLinkedTeleporter::GetEndpointLocation(const uint8 EndpointIndex) const
{
	return GetEndpointTransform(EndpointIndex).GetLocation();
}

void AAeyerjiLinkedTeleporter::SetEndpointBWorldTransform(const FTransform& WorldTransform)
{
	if (!HasAuthority() || WorldTransform.ContainsNaN() || GetActorTransform().ContainsNaN())
	{
		return;
	}
	EndpointBRelativeTransform = WorldTransform.GetRelativeTransform(GetActorTransform());
	if (EndpointBRelativeTransform.ContainsNaN())
	{
		EndpointBRelativeTransform = FTransform::Identity;
		return;
	}
	ApplyEndpointConfiguration();
	ForceNetUpdate();
}

float AAeyerjiLinkedTeleporter::GetEndpointInteractionRadius() const
{
	return FMath::IsFinite(InteractionRadius)
		? FMath::Clamp(InteractionRadius, 1.f, MaxTeleporterDistance)
		: 140.f;
}

bool AAeyerjiLinkedTeleporter::IsPawnInInteractionRange(const APawn* Pawn, const uint8 EndpointIndex) const
{
	if (!IsValid(Pawn)
		|| Pawn->GetWorld() != GetWorld()
		|| !IsValidEndpointIndex(EndpointIndex))
	{
		return false;
	}

	const FVector EndpointLocation = GetEndpointLocation(EndpointIndex);
	if (EndpointLocation.ContainsNaN() || Pawn->GetActorLocation().ContainsNaN())
	{
		return false;
	}
	const float Radius = GetEndpointInteractionRadius();
	return FVector::DistSquared2D(Pawn->GetActorLocation(), EndpointLocation) <= FMath::Square(Radius);
}

bool AAeyerjiLinkedTeleporter::IsControllerOnCooldown(const AAeyerjiPlayerController* Controller) const
{
	if (!Controller)
	{
		return false;
	}

	return ControllerCooldownTimers.Contains(FObjectKey(const_cast<AAeyerjiPlayerController*>(Controller)));
}

bool AAeyerjiLinkedTeleporter::IsEndpointEnabledForUse(const uint8 EndpointIndex) const
{
	if (EndpointIndex == EndpointAIndex)
	{
		return bAllowEndpointAToB;
	}

	if (EndpointIndex == EndpointBIndex)
	{
		return bAllowEndpointBToA;
	}

	return false;
}

bool AAeyerjiLinkedTeleporter::TryTeleport(AAeyerjiPlayerController* Controller, const uint8 EndpointIndex)
{
	if (!HasAuthority()
		|| !IsValidEndpointIndex(EndpointIndex)
		|| !IsValid(Controller)
		|| Controller->GetWorld() != GetWorld())
	{
		return false;
	}

	APawn* Pawn = Controller->GetPawn();
	if (!IsValid(Pawn)
		|| Pawn->GetWorld() != GetWorld()
		|| Pawn->GetController() != Controller)
	{
		return false;
	}

	if (!IsEndpointEnabledForUse(EndpointIndex)
		|| IsControllerOnCooldown(Controller)
		|| !IsPawnInInteractionRange(Pawn, EndpointIndex))
	{
		BP_OnTeleportRejected(Pawn, EndpointIndex);
		return false;
	}

	const uint8 DestinationEndpointIndex = GetLinkedEndpointIndex(EndpointIndex);
	const FTransform DestinationTransform = GetEndpointTransform(DestinationEndpointIndex);
	const FVector SafeExitOffset = ExitOffsetLocal.ContainsNaN()
		? FVector(140.f, 0.f, 0.f)
		: ExitOffsetLocal.GetClampedToMaxSize(MaxTeleporterDistance);
	if (DestinationTransform.ContainsNaN())
	{
		BP_OnTeleportRejected(Pawn, EndpointIndex);
		return false;
	}
	const FVector DestinationLocation = DestinationTransform.TransformPosition(SafeExitOffset);
	const FRotator DestinationRotation = DestinationTransform.GetRotation().Rotator();
	if (DestinationLocation.ContainsNaN() || DestinationRotation.ContainsNaN())
	{
		BP_OnTeleportRejected(Pawn, EndpointIndex);
		return false;
	}

	FAeyerjiNavSafetyResolveParams NavParams;
	NavParams.ProjectionExtent = FVector(200.f, 200.f, 600.f);
	NavParams.SearchRadius = FMath::Clamp(GetEndpointInteractionRadius() + 400.f, 600.f, MaxTeleporterDistance);
	NavParams.SearchStep = 150.f;
	NavParams.GroundTraceHeight = 300.f;
	NavParams.GroundTraceDepth = 600.f;

	FAeyerjiNavSafetyResult NavResult;
	if (!UAeyerjiNavSafetyLibrary::ResolveSafeNavLocationForPawn(this, DestinationLocation, Pawn, NavParams, NavResult))
	{
		BP_OnTeleportRejected(Pawn, EndpointIndex);
		return false;
	}

	Controller->AbortMovement_Local();
	const bool bTeleported = Pawn->SetActorLocationAndRotation(
		NavResult.GroundedLocation,
		DestinationRotation,
		/*bSweep=*/false,
		nullptr,
		ETeleportType::TeleportPhysics);
	if (!bTeleported)
	{
		BP_OnTeleportRejected(Pawn, EndpointIndex);
		return false;
	}
	Pawn->ForceNetUpdate();
	Controller->ForceNetUpdate();

	StartCooldownForController(Controller);
	if (EndpointIndex == EndpointAIndex && Controller->PlayerState)
	{
		PlayersEnteredFromEndpointA.Add(Controller->PlayerState);
	}
	BP_OnTeleported(Pawn, EndpointIndex, DestinationEndpointIndex);

	return true;
}

void AAeyerjiLinkedTeleporter::SetAllowedDirections(const bool bAllowAToB, const bool bAllowBToA)
{
	if (!HasAuthority())
	{
		return;
	}
	bAllowEndpointAToB = bAllowAToB;
	bAllowEndpointBToA = bAllowBToA;
	ApplyEndpointConfiguration();
	ForceNetUpdate();
}

bool AAeyerjiLinkedTeleporter::HasPlayerEnteredFromEndpointA(const APlayerState* PlayerState) const
{
	return PlayerState && PlayersEnteredFromEndpointA.Contains(
		TWeakObjectPtr<APlayerState>(const_cast<APlayerState*>(PlayerState)));
}

void AAeyerjiLinkedTeleporter::ApplyEndpointConfiguration()
{
	if (EndpointA)
	{
		EndpointA->SetRelativeTransform(FTransform::Identity);
	}

	if (EndpointB)
	{
		EndpointB->SetRelativeTransform(
			EndpointBRelativeTransform.ContainsNaN() ? FTransform::Identity : EndpointBRelativeTransform);
	}

	if (EndpointAMesh)
	{
		EndpointAMesh->SetStaticMesh(PortalMesh);
	}

	if (EndpointBMesh)
	{
		EndpointBMesh->SetStaticMesh(PortalMesh);
	}

	const float ClampedRadius = GetEndpointInteractionRadius();
	if (EndpointAInteraction)
	{
		EndpointAInteraction->SetSphereRadius(ClampedRadius, true);
	}

	if (EndpointBInteraction)
	{
		EndpointBInteraction->SetSphereRadius(ClampedRadius, true);
	}
}

void AAeyerjiLinkedTeleporter::OnRep_EndpointBRelativeTransform()
{
	ApplyEndpointConfiguration();
}

void AAeyerjiLinkedTeleporter::OnRep_AllowedDirections()
{
	ApplyEndpointConfiguration();
}

USceneComponent* AAeyerjiLinkedTeleporter::GetEndpointScene(const uint8 EndpointIndex) const
{
	if (EndpointIndex == EndpointAIndex)
	{
		return EndpointA;
	}

	if (EndpointIndex == EndpointBIndex)
	{
		return EndpointB;
	}

	return nullptr;
}

USphereComponent* AAeyerjiLinkedTeleporter::GetEndpointSphere(const uint8 EndpointIndex) const
{
	if (EndpointIndex == EndpointAIndex)
	{
		return EndpointAInteraction;
	}

	if (EndpointIndex == EndpointBIndex)
	{
		return EndpointBInteraction;
	}

	return nullptr;
}

FTransform AAeyerjiLinkedTeleporter::GetEndpointTransform(const uint8 EndpointIndex) const
{
	if (const USceneComponent* EndpointScene = GetEndpointScene(EndpointIndex))
	{
		return EndpointScene->GetComponentTransform();
	}

	return GetActorTransform();
}

uint8 AAeyerjiLinkedTeleporter::GetLinkedEndpointIndex(const uint8 EndpointIndex) const
{
	return EndpointIndex == EndpointAIndex ? EndpointBIndex : EndpointAIndex;
}

bool AAeyerjiLinkedTeleporter::IsValidEndpointIndex(const uint8 EndpointIndex) const
{
	return EndpointIndex == EndpointAIndex || EndpointIndex == EndpointBIndex;
}

void AAeyerjiLinkedTeleporter::StartCooldownForController(AAeyerjiPlayerController* Controller)
{
	if (!HasAuthority() || !IsValid(Controller) || Controller->GetWorld() != GetWorld())
	{
		return;
	}
	const float SafeCooldownSeconds = FMath::IsFinite(CooldownSeconds)
		? FMath::Clamp(CooldownSeconds, 0.f, MaxTeleporterCooldownSeconds)
		: 2.f;
	if (SafeCooldownSeconds <= 0.f)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FObjectKey ControllerKey(Controller);
	if (FTimerHandle* ExistingTimer = ControllerCooldownTimers.Find(ControllerKey))
	{
		World->GetTimerManager().ClearTimer(*ExistingTimer);
	}

	FTimerHandle& CooldownTimer = ControllerCooldownTimers.FindOrAdd(ControllerKey);
	FTimerDelegate CooldownDelegate;
	CooldownDelegate.BindUObject(this, &AAeyerjiLinkedTeleporter::ClearCooldownForController, ControllerKey);
	World->GetTimerManager().SetTimer(
		CooldownTimer,
		CooldownDelegate,
		FMath::Max(0.01f, SafeCooldownSeconds),
		false);
}

void AAeyerjiLinkedTeleporter::ClearCooldownForController(const FObjectKey ControllerKey)
{
	ControllerCooldownTimers.Remove(ControllerKey);
}
