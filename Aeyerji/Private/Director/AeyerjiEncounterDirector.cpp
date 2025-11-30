// Copyright (c) 2025 Aeyerji.
#include "Director/AeyerjiEncounterDirector.h"

#include "Enemy/EnemyParentNative.h"
#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"

TSubclassOf<AEnemyParentNative> UEnemySpawnGroupDefinition::ResolveEnemyClass() const
{
	if (EnemyTypes.IsEmpty())
	{
		return nullptr;
	}

	const int32 Index = FMath::RandHelper(EnemyTypes.Num());
	return EnemyTypes[Index];
}

AAeyerjiEncounterDirector::AAeyerjiEncounterDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void AAeyerjiEncounterDirector::BeginPlay()
{
	Super::BeginPlay();

	// Encounter spawning must run on the authority side only; otherwise each client will
	// spawn its own non-replicated enemies, causing invalid NetGUID references.
	if (GetNetMode() == NM_Client)
	{
		SetActorTickEnabled(false);
		return;
	}

	RefreshPlayerReference();

	const double Now = GetWorld()->GetTimeSeconds();
	LastEncounterTimestamp = Now;
	LastKillTimestamp = Now;

	if (CachedPlayerPawn.IsValid())
	{
		LastEncounterLocation = CachedPlayerPawn->GetActorLocation();
	}
	else
	{
		LastEncounterLocation = GetActorLocation();
	}
}

void AAeyerjiEncounterDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (GetNetMode() == NM_Client)
	{
		return;
	}

	RefreshPlayerReference();
	CleanupInactiveEnemies();
	UpdateKillWindow();

	if (!CachedPlayerPawn.IsValid())
	{
		return;
	}

	DistanceFromLastEncounter = FVector::Dist(CachedPlayerPawn->GetActorLocation(), LastEncounterLocation);

	switch (DirectorState)
	{
	case EEncounterDirectorState::Idle:
		if (ShouldTriggerEncounter())
		{
			TriggerEncounter();
		}
		break;

	case EEncounterDirectorState::InCombat:
		if (ActiveEnemyCount <= 0)
		{
			EnterState(EEncounterDirectorState::PostCombat);
		}
		break;

	case EEncounterDirectorState::PostCombat:
		PostCombatTimeRemaining -= DeltaSeconds;
		if (PostCombatTimeRemaining <= 0.f)
		{
			EnterState(EEncounterDirectorState::Idle);
		}
		break;
	}
}

void AAeyerjiEncounterDirector::RefreshPlayerReference()
{
	if (!CachedPlayerPawn.IsValid())
	{
		if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			CachedPlayerPawn = Pawn;
		}
	}

	if (!CachedPlayerController.IsValid())
	{
		if (APawn* Pawn = CachedPlayerPawn.Get())
		{
			CachedPlayerController = Pawn->GetController();
		}
	}
}

void AAeyerjiEncounterDirector::CleanupInactiveEnemies()
{
	for (int32 Index = LiveEnemies.Num() - 1; Index >= 0; --Index)
	{
		if (!LiveEnemies[Index].IsValid())
		{
			LiveEnemies.RemoveAtSwap(Index);
		}
	}

	ActiveEnemyCount = LiveEnemies.Num();
}

void AAeyerjiEncounterDirector::UpdateKillWindow()
{
	if (KillTimestampHistory.IsEmpty())
	{
		CurrentKillVelocity = 0.f;
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	const double WindowStart = Now - KillVelocityWindowSeconds;

	for (int32 Index = KillTimestampHistory.Num() - 1; Index >= 0; --Index)
	{
		if (KillTimestampHistory[Index] < WindowStart)
		{
			KillTimestampHistory.RemoveAtSwap(Index);
		}
	}

	if (KillVelocityWindowSeconds <= 0.f)
	{
		CurrentKillVelocity = KillTimestampHistory.Num();
	}
	else
	{
		CurrentKillVelocity = static_cast<float>(KillTimestampHistory.Num()) / KillVelocityWindowSeconds;
	}
}

bool AAeyerjiEncounterDirector::ShouldTriggerEncounter() const
{
	if (DirectorState != EEncounterDirectorState::Idle)
	{
		return false;
	}

	if (!CachedPlayerPawn.IsValid() || SpawnGroups.IsEmpty())
	{
		return false;
	}

	if (LiveEnemies.Num() > 0)
	{
		return false;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const double Now = World->GetTimeSeconds();
	const double TimeSinceEncounter = Now - LastEncounterTimestamp;
	const double TimeSinceKill = (LastKillTimestamp > 0.0) ? (Now - LastKillTimestamp) : TimeSinceEncounter;

	const bool bDistanceRequirement = DistanceFromLastEncounter >= MinDistanceBetweenEncounters;
	const bool bForceDistance = ForceSpawnDistance > 0.f && DistanceFromLastEncounter >= ForceSpawnDistance;
	const bool bDowntimeWindow = TimeSinceKill >= MinDowntimeSeconds;
	const bool bForceDowntime = ForceSpawnDowntimeSeconds > 0.f && TimeSinceEncounter >= ForceSpawnDowntimeSeconds;
	const bool bKillVelocitySatisfied = CurrentKillVelocity >= KillVelocityThreshold;

	if (bDistanceRequirement && bDowntimeWindow && (bKillVelocitySatisfied || bForceDistance))
	{
		return true;
	}

	if (bForceDowntime)
	{
		return true;
	}

	return false;
}

void AAeyerjiEncounterDirector::TriggerEncounter()
{
	if (GetNetMode() == NM_Client)
	{
		return;
	}

	const UEnemySpawnGroupDefinition* Group = ChooseSpawnGroup();
	if (!Group || !CachedPlayerPawn.IsValid())
	{
		return;
	}

	LastEncounterLocation = CachedPlayerPawn->GetActorLocation();
	LastEncounterTimestamp = GetWorld()->GetTimeSeconds();
	LastSpawnedGroup = Group;

	if (bDrawDebug)
	{
		DrawDebugSphere(GetWorld(), LastEncounterLocation, 64.f, 12, FColor::Orange, false, 2.f);
	}

	SpawnFromGroup(Group);
	EnterState(EEncounterDirectorState::InCombat);
}

const UEnemySpawnGroupDefinition* AAeyerjiEncounterDirector::ChooseSpawnGroup() const
{
	const UEnemySpawnGroupDefinition* Fallback = nullptr;

	for (const UEnemySpawnGroupDefinition* Group : SpawnGroups)
	{
		if (IsValid(Group) && !Group->EnemyTypes.IsEmpty())
		{
			Fallback = Group;
			break;
		}
	}

	if (!Fallback)
	{
		return nullptr;
	}

	const int32 Attempts = SpawnGroups.Num() * 2;
	for (int32 Attempt = 0; Attempt < Attempts; ++Attempt)
	{
		const int32 Index = FMath::RandHelper(SpawnGroups.Num());
		const UEnemySpawnGroupDefinition* Candidate = SpawnGroups[Index];

		if (!IsValid(Candidate) || Candidate->EnemyTypes.IsEmpty())
		{
			continue;
		}

		if (!Candidate->bAllowBackToBackSelection && Candidate == LastSpawnedGroup.Get())
		{
			continue;
		}

		return Candidate;
	}

	return Fallback;
}

void AAeyerjiEncounterDirector::SpawnFromGroup(const UEnemySpawnGroupDefinition* Group)
{
	if (GetNetMode() == NM_Client)
	{
		return;
	}

	if (!Group || !CachedPlayerPawn.IsValid())
	{
		return;
	}

	const int32 SpawnCount = Group->ResolveSpawnCount();
	if (SpawnCount <= 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (int32 Index = 0; Index < SpawnCount; ++Index)
	{
		TSubclassOf<AEnemyParentNative> EnemyClass = Group->ResolveEnemyClass();
		if (!*EnemyClass)
		{
			continue;
		}

		const float HalfHeight = GetEnemyHalfHeight(EnemyClass);
		const FVector SpawnLocation = ResolveSpawnLocation(Group->SpawnRadius, HalfHeight);
		const FRotator SpawnRotation = (CachedPlayerPawn->GetActorLocation() - SpawnLocation).Rotation();

		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

		AEnemyParentNative* SpawnedEnemy = World->SpawnActor<AEnemyParentNative>(*EnemyClass, SpawnLocation, SpawnRotation, Params);
		if (SpawnedEnemy)
		{
			SnapActorToGround(SpawnedEnemy, HalfHeight);
			RegisterSpawnedEnemy(SpawnedEnemy);
		}
	}

	ActiveEnemyCount = LiveEnemies.Num();
}

FVector AAeyerjiEncounterDirector::ResolveSpawnLocation(float Radius, float HalfHeight) const
{
	if (!CachedPlayerPawn.IsValid())
	{
		return GetActorLocation();
	}

	const FVector PlayerLocation = CachedPlayerPawn->GetActorLocation();
	if (Radius <= 0.f)
	{
		return PlayerLocation + FVector(0.f, 0.f, HalfHeight);
	}

	if (const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		FNavLocation Result;
		if (NavSys->GetRandomReachablePointInRadius(PlayerLocation, Radius, Result))
		{
			FVector Candidate = Result.Location;

			if (UWorld* World = GetWorld())
			{
				const FVector TraceStart = Candidate + FVector(0.f, 0.f, GroundTraceUpOffset);
				const FVector TraceEnd = Candidate - FVector(0.f, 0.f, GroundTraceDownDistance);

				FHitResult Hit;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(EncounterDirector_GroundTrace), false, CachedPlayerPawn.Get());

				if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params))
				{
					Candidate.Z = Hit.ImpactPoint.Z + SpawnGroundOffset + HalfHeight;
					return Candidate;
				}
			}

			Candidate.Z += HalfHeight;
			return Candidate;
		}
	}

	const FVector2D Offset2D = FMath::RandPointInCircle(Radius);
	FVector Location = PlayerLocation + FVector(Offset2D.X, Offset2D.Y, 0.f);
	Location.Z = PlayerLocation.Z + HalfHeight;
	return Location;
}

void AAeyerjiEncounterDirector::SnapActorToGround(AActor* SpawnedActor, float HalfHeight) const
{
	if (!SpawnedActor || !CachedPlayerPawn.IsValid())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector Start = SpawnedActor->GetActorLocation() + FVector(0.f, 0.f, GroundTraceUpOffset);
	const FVector End = Start - FVector(0.f, 0.f, GroundTraceDownDistance);

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EncounterDirector_SnapTrace), false, CachedPlayerPawn.Get());
	Params.AddIgnoredActor(SpawnedActor);

	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		FVector Adjusted = SpawnedActor->GetActorLocation();
		Adjusted.Z = Hit.ImpactPoint.Z + SpawnGroundOffset + HalfHeight;
		SpawnedActor->SetActorLocation(Adjusted, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

float AAeyerjiEncounterDirector::GetEnemyHalfHeight(TSubclassOf<AEnemyParentNative> EnemyClass) const
{
	if (!*EnemyClass)
	{
		return 0.f;
	}

	if (const ACharacter* CDO = Cast<ACharacter>(EnemyClass->GetDefaultObject()))
	{
		if (const UCapsuleComponent* Capsule = CDO->GetCapsuleComponent())
		{
			return Capsule->GetScaledCapsuleHalfHeight();
		}
	}

	return 0.f;
}

void AAeyerjiEncounterDirector::EnterState(EEncounterDirectorState NewState)
{
	if (DirectorState == NewState)
	{
		return;
	}

	DirectorState = NewState;

	switch (DirectorState)
	{
	case EEncounterDirectorState::Idle:
		PostCombatTimeRemaining = 0.0;
		break;
	case EEncounterDirectorState::InCombat:
		PostCombatTimeRemaining = 0.0;
		break;
	case EEncounterDirectorState::PostCombat:
		PostCombatTimeRemaining = PostCombatDelaySeconds;
		break;
	}
}

void AAeyerjiEncounterDirector::RegisterSpawnedEnemy(AEnemyParentNative* Enemy)
{
	if (!IsValid(Enemy))
	{
		return;
	}

	LiveEnemies.Add(Enemy);
	ActiveEnemyCount = LiveEnemies.Num();

	Enemy->OnEnemyDied.RemoveDynamic(this, &AAeyerjiEncounterDirector::HandleTrackedEnemyDied);
	Enemy->OnEnemyDied.AddDynamic(this, &AAeyerjiEncounterDirector::HandleTrackedEnemyDied);
	Enemy->OnDestroyed.RemoveDynamic(this, &AAeyerjiEncounterDirector::HandleTrackedEnemyDestroyed);
	Enemy->OnDestroyed.AddDynamic(this, &AAeyerjiEncounterDirector::HandleTrackedEnemyDestroyed);
}

void AAeyerjiEncounterDirector::HandleTrackedEnemyDied(AActor* DeadEnemy)
{
	if (!DeadEnemy)
	{
		return;
	}

	for (int32 Index = LiveEnemies.Num() - 1; Index >= 0; --Index)
	{
		if (!LiveEnemies[Index].IsValid() || LiveEnemies[Index].Get() == DeadEnemy)
		{
			LiveEnemies.RemoveAtSwap(Index);
		}
	}

	RecordKillTimestamp();
	ActiveEnemyCount = LiveEnemies.Num();

	if (DirectorState == EEncounterDirectorState::InCombat && ActiveEnemyCount <= 0)
	{
		EnterState(EEncounterDirectorState::PostCombat);
	}
}

void AAeyerjiEncounterDirector::HandleTrackedEnemyDestroyed(AActor* DestroyedActor)
{
	if (!DestroyedActor)
	{
		return;
	}

	for (int32 Index = LiveEnemies.Num() - 1; Index >= 0; --Index)
	{
		if (!LiveEnemies[Index].IsValid() || LiveEnemies[Index].Get() == DestroyedActor)
		{
			LiveEnemies.RemoveAtSwap(Index);
		}
	}

	ActiveEnemyCount = LiveEnemies.Num();

	if (DirectorState == EEncounterDirectorState::InCombat && ActiveEnemyCount <= 0)
	{
		EnterState(EEncounterDirectorState::PostCombat);
	}
}

void AAeyerjiEncounterDirector::RecordKillTimestamp()
{
	if (UWorld* World = GetWorld())
	{
		const double Now = World->GetTimeSeconds();
		LastKillTimestamp = Now;
		KillTimestampHistory.Add(Now);
	}
}
