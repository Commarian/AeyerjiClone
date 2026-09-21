#include "Abilities/MeteorStrike/AeyerjiMeteorStrike.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AAeyerjiMeteorStrike::AAeyerjiMeteorStrike()
{
	bReplicates = true;
	// Every client must see the telegraph and the landing, independent of the caster's relevance.
	bAlwaysRelevant = true;
	SetReplicateMovement(false);
	PrimaryActorTick.bCanEverTick = true;

	FallMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FallMesh"));
	SetRootComponent(FallMesh);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (Sphere.Succeeded())
	{
		FallMesh->SetStaticMesh(Sphere.Object);
	}
	FallMesh->SetMobility(EComponentMobility::Movable);
	FallMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FallMesh->SetGenerateOverlapEvents(false);
	FallMesh->SetCanEverAffectNavigation(false);
	FallMesh->SetCastShadow(false);
	FallMesh->SetRelativeScale3D(FVector(1.6f));
}

float AAeyerjiMeteorStrike::ClampFallDuration(float FallDuration)
{
	if (!FMath::IsFinite(FallDuration))
	{
		return 1.f;
	}
	return FMath::Clamp(FallDuration, MinFallDuration, MaxFallDuration);
}

float AAeyerjiMeteorStrike::ClampSpawnHeight(float SpawnHeight)
{
	if (!FMath::IsFinite(SpawnHeight))
	{
		return DefaultSpawnHeight;
	}
	return FMath::Clamp(SpawnHeight, MinSpawnHeight, MaxSpawnHeight);
}

FVector AAeyerjiMeteorStrike::ComputeSpawnLocation(const FVector& ImpactLocation, float SpawnHeight)
{
	return ImpactLocation + FVector(0.f, 0.f, SpawnHeight);
}

float AAeyerjiMeteorStrike::ComputeFallProgress(double FallStartServerTime, double NowServerTime, float FallDuration)
{
	if (!FMath::IsFinite(static_cast<float>(FallStartServerTime))
		|| !FMath::IsFinite(static_cast<float>(NowServerTime))
		|| !(FallDuration > 0.f))
	{
		return 0.f;
	}
	return FMath::Clamp(static_cast<float>((NowServerTime - FallStartServerTime) / FallDuration), 0.f, 1.f);
}

FVector AAeyerjiMeteorStrike::ComputeFallLocation(const FVector& ImpactLocation, float SpawnHeight, float FallProgress)
{
	return FMath::Lerp(ComputeSpawnLocation(ImpactLocation, SpawnHeight), ImpactLocation, FMath::Clamp(FallProgress, 0.f, 1.f));
}

bool AAeyerjiMeteorStrike::InitializeMeteor(const FVector& ImpactLocation, float FallDuration, float SpawnHeight)
{
	if (!HasAuthority() || State.Phase != EAeyerjiMeteorPhase::Inactive || !GetWorld())
	{
		return false;
	}
	if (ImpactLocation.ContainsNaN())
	{
		return false;
	}

	FVector GroundLocation = ImpactLocation;
	FCollisionQueryParams Trace(SCENE_QUERY_STAT(MeteorGroundSnap), true, this);
	FHitResult GroundHit;
	if (GetWorld()->LineTraceSingleByObjectType(GroundHit,
		ImpactLocation + FVector(0.f, 0.f, 500.f), ImpactLocation - FVector(0.f, 0.f, 2000.f),
		FCollisionObjectQueryParams(ECC_WorldStatic), Trace))
	{
		GroundLocation = GroundHit.ImpactPoint;
	}

	State.ImpactLocation = GroundLocation;
	State.SpawnHeight = ClampSpawnHeight(SpawnHeight);
	State.FallDuration = ClampFallDuration(FallDuration);
	State.FallStartServerTime = GetWorld()->GetTimeSeconds();
	State.Phase = EAeyerjiMeteorPhase::Falling;
	SetActorLocation(ComputeSpawnLocation(State.ImpactLocation, State.SpawnHeight));

	// Failsafe only: the owning ability resolves the impact at damage time. This timer covers
	// the case where the ability ended without resolving or cancelling us.
	GetWorldTimerManager().SetTimer(ImpactTimer, this, &ThisClass::ResolveImpact,
		State.FallDuration + ImpactFailsafeSlack, false);
	ForceNetUpdate();
	RefreshPresentation();
	return true;
}

void AAeyerjiMeteorStrike::ResolveImpact()
{
	if (!HasAuthority() || !IsFalling() || bResolvingImpact)
	{
		return;
	}
	// Impact presentation is atomic once started.
	TGuardValue<bool> ResolveGuard(bResolvingImpact, true);
	FinishMeteor(true);
}

void AAeyerjiMeteorStrike::CancelMeteor()
{
	if (HasAuthority() && IsFalling() && !bResolvingImpact)
	{
		FinishMeteor(false);
	}
}

void AAeyerjiMeteorStrike::FinishMeteor(bool bImpacted)
{
	GetWorldTimerManager().ClearTimer(ImpactTimer);
	State.Phase = bImpacted ? EAeyerjiMeteorPhase::Impacted : EAeyerjiMeteorPhase::Cancelled;
	if (bImpacted)
	{
		SetActorLocation(State.ImpactLocation);
		if (FractureActorClass && GetWorld())
		{
			FActorSpawnParameters Spawn;
			Spawn.Owner = GetOwner();
			Spawn.Instigator = GetInstigator();
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			GetWorld()->SpawnActor<AActor>(FractureActorClass, State.ImpactLocation, FRotator::ZeroRotator, Spawn);
		}
	}
	ForceNetUpdate();
	RefreshPresentation();
	OnResolved.Broadcast();
	OnResolved.Clear();
	// Retain the terminal snapshot briefly so clients observe impact/cancellation before destruction.
	SetLifeSpan(bImpacted ? 2.f : 0.5f);
}

void AAeyerjiMeteorStrike::BeginPlay()
{
	Super::BeginPlay();
	RefreshPresentation();
}

void AAeyerjiMeteorStrike::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	CancelMeteor();
	GetWorldTimerManager().ClearTimer(ImpactTimer);
	Super::EndPlay(EndPlayReason);
}

void AAeyerjiMeteorStrike::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (GetNetMode() == NM_DedicatedServer || !IsFalling() || !GetWorld())
	{
		return;
	}
	const AGameStateBase* GameState = GetWorld()->GetGameState();
	const double Now = GameState ? GameState->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
	const float Progress = ComputeFallProgress(State.FallStartServerTime, Now, State.FallDuration);
	SetActorLocation(ComputeFallLocation(State.ImpactLocation, State.SpawnHeight, Progress));
	BP_UpdateFall(Progress);
}

void AAeyerjiMeteorStrike::OnRep_State()
{
	RefreshPresentation();
}

void AAeyerjiMeteorStrike::RefreshPresentation()
{
	if (GetNetMode() == NM_DedicatedServer || !GetWorld())
	{
		return;
	}
	if (State.Phase == LastPresentedPhase)
	{
		return;
	}
	LastPresentedPhase = State.Phase;
	if (State.Phase == EAeyerjiMeteorPhase::Impacted)
	{
		SetActorLocation(State.ImpactLocation);
		FallMesh->SetVisibility(false, true);
		BP_OnMeteorImpacted(State);
	}
	else if (State.Phase == EAeyerjiMeteorPhase::Cancelled)
	{
		FallMesh->SetVisibility(false, true);
	}
	BP_OnMeteorStateChanged(State);
}

void AAeyerjiMeteorStrike::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AAeyerjiMeteorStrike, State);
}
