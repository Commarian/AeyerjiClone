// Copyright (c) 2025 Aeyerji.
#include "Director/AeyerjiEncounterDirector.h"

#include "Director/AeyerjiSpawnerGroup.h"
#include "Director/AeyerjiSpawnRegion.h"
#include "Director/AeyerjiWorldSpawnProfile.h"
#include "Director/AeyerjiLevelDirector.h"
#include "Enemy/AeyerjiEnemyManagementBPFL.h"
#include "Enemy/EnemyParentNative.h"
#include "Enemy/AeyerjiEnemyArchetypeComponent.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "DrawDebugHelpers.h"
#include "Curves/CurveFloat.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Components/CapsuleComponent.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogEncounterDirector, Log, All);

namespace
{
	static TAutoConsoleVariable<float>& GetFixedPopulationBudgetScaleCVar()
	{
		// Intentionally leaked to avoid shutdown-order crashes when the console manager is destroyed.
		static TAutoConsoleVariable<float>* CVar = new TAutoConsoleVariable<float>(
			TEXT("aeyerji.FixedPopulation.BudgetScale"),
			1.0f,
			TEXT("Scales fixed world population target (0..1)."),
			ECVF_Default);
		return *CVar;
	}

	static TAutoConsoleVariable<int32>& GetFixedPopulationBudgetCapCVar()
	{
		// Intentionally leaked to avoid shutdown-order crashes when the console manager is destroyed.
		static TAutoConsoleVariable<int32>* CVar = new TAutoConsoleVariable<int32>(
			TEXT("aeyerji.FixedPopulation.BudgetCap"),
			0,
			TEXT("Hard cap on fixed world population target (0 disables)."),
			ECVF_Default);
		return *CVar;
	}
}

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
	PrimaryActorTick.TickInterval = TickIntervalSeconds;
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

	SetActorTickInterval(FMath::Max(0.f, TickIntervalSeconds));

	RefreshPlayerReference();
	RecentPlayerSamples.Reset();
	LastPathSampleTimestamp = -RecentPathSampleInterval;
	UpdateRecentPlayerPath();

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

void AAeyerjiEncounterDirector::RegisterExternalEnemy(AEnemyParentNative* Enemy, bool bEnterCombatState)
{
	if (GetNetMode() == NM_Client || !IsValid(Enemy))
	{
		return;
	}

	for (const TWeakObjectPtr<AActor>& Tracked : LiveEnemies)
	{
		if (Tracked.Get() == Enemy)
		{
			return;
		}
	}

	RegisterSpawnedEnemy(Enemy);

	if (UWorld* World = GetWorld())
	{
		LastEncounterTimestamp = World->GetTimeSeconds();
	}

	if (bEnterCombatState)
	{
		LastEncounterLocation = Enemy->GetActorLocation();
		EnterState(EEncounterDirectorState::InCombat);
	}
}

bool AAeyerjiEncounterDirector::StartFixedWorldPopulation(UAeyerjiWorldSpawnProfile* Profile, AAeyerjiSpawnerGroup* SpawnManager, AAeyerjiLevelDirector* LevelDirector)
{
	if (GetNetMode() == NM_Client)
	{
		return false;
	}

	if (!Profile)
	{
		UE_LOG(LogEncounterDirector, Warning, TEXT("Fixed population start skipped: missing profile on %s"), *GetNameSafe(this));
		return false;
	}

	StopFixedWorldPopulation();
	RefreshPlayerReference();

	FixedSpawnProfile = Profile;
	FixedPopulationSpawner = SpawnManager;
	FixedPopulationLevelDirector = LevelDirector;
	bSpawnedPopulationSpawner = false;

	if (!FixedPopulationSpawner.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			const FTransform SpawnTransform = GetActorTransform();
			AAeyerjiSpawnerGroup* Spawned = World->SpawnActorDeferred<AAeyerjiSpawnerGroup>(
				AAeyerjiSpawnerGroup::StaticClass(),
				SpawnTransform,
				this,
				nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

			if (Spawned)
			{
				Spawned->bDisableActivationVolume = true;
				Spawned->bDisableActivationEvent = true;
				Spawned->bSuppressDoorControl = true;
				Spawned->bAllowManualActivationWithoutWaves = true;
				UGameplayStatics::FinishSpawningActor(Spawned, SpawnTransform);
				FixedPopulationSpawner = Spawned;
				bSpawnedPopulationSpawner = true;
			}
		}
	}

	if (!FixedPopulationSpawner.IsValid())
	{
		UE_LOG(LogEncounterDirector, Warning, TEXT("Fixed population start on %s has no spawn manager; elites/scaling will be limited."), *GetNameSafe(this));
	}

	if (FixedPopulationSpawner.IsValid() && FixedPopulationLevelDirector.IsValid())
	{
		FixedPopulationSpawner->LevelDirector = FixedPopulationLevelDirector.Get();
	}

	if (FixedPopulationSpawner.IsValid())
	{
		FixedPopulationSpawner->ResetEncounter();
	}

	PendingSpawnRequests.Reset();
	BuildFixedPopulationPlan();

	if (FixedPopulationTarget <= 0 || FixedSpawnQueue.IsEmpty())
	{
		UE_LOG(LogEncounterDirector, Warning, TEXT("Fixed population start skipped: no spawn requests generated on %s"), *GetNameSafe(this));
		return false;
	}

	bFixedPopulationActive = true;
	bFixedPopulationComplete = false;
	EnterState(EEncounterDirectorState::InCombat);

	return true;
}

void AAeyerjiEncounterDirector::StopFixedWorldPopulation()
{
	if (GetNetMode() == NM_Client)
	{
		return;
	}

	for (TPair<int32, TArray<TWeakObjectPtr<AEnemyParentNative>>>& Pair : FixedClusterMembers)
	{
		for (const TWeakObjectPtr<AEnemyParentNative>& EnemyPtr : Pair.Value)
		{
			ApplyEnemySleepState(EnemyPtr.Get(), false);
		}
	}

	bFixedPopulationActive = false;
	bFixedPopulationComplete = false;
	FixedSpawnQueue.Reset();
	FixedClusterCenters.Reset();
	FixedClusters.Reset();
	FixedEnemyClusterMap.Reset();
	FixedClusterMembers.Reset();
	FixedPopulationTarget = 0;
	FixedPopulationSpawned = 0;
	FixedPopulationRemaining = 0;
	FixedClustersRemaining = 0;
	FixedSpawnSeed = 0;
	FixedSpawnProfile.Reset();
	FixedPopulationLevelDirector.Reset();

	if (bSpawnedPopulationSpawner)
	{
		if (AAeyerjiSpawnerGroup* Spawned = FixedPopulationSpawner.Get())
		{
			Spawned->Destroy();
		}
	}

	FixedPopulationSpawner.Reset();
	bSpawnedPopulationSpawner = false;
	EnterState(EEncounterDirectorState::Idle);
}

void AAeyerjiEncounterDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (GetNetMode() == NM_Client)
	{
		return;
	}

	RefreshPlayerReference();
	UpdateRecentPlayerPath();
	CleanupInactiveEnemies();
	UpdateKillWindow();
	ProcessFixedSpawnQueue();
	UpdateEnemyLOD(DeltaSeconds);

	if (bFixedPopulationActive)
	{
		return;
	}

	ProcessSpawnQueue();

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
		if (ActiveEnemyCount <= 0 && PendingSpawnRequests.IsEmpty())
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

void AAeyerjiEncounterDirector::UpdateRecentPlayerPath()
{
	if (!bAvoidRecentPlayerPath || !CachedPlayerPawn.IsValid())
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	if ((Now - LastPathSampleTimestamp) < RecentPathSampleInterval)
	{
		return;
	}

	LastPathSampleTimestamp = Now;

	FRecentPlayerSample Sample;
	Sample.Location = CachedPlayerPawn->GetActorLocation();
	Sample.Timestamp = Now;
	RecentPlayerSamples.Add(Sample);

	const double Cutoff = Now - RecentPathSeconds;
	for (int32 Index = RecentPlayerSamples.Num() - 1; Index >= 0; --Index)
	{
		if (RecentPlayerSamples[Index].Timestamp < Cutoff)
		{
			RecentPlayerSamples.RemoveAt(Index);
		}
	}

	const int32 MaxSamples = FMath::Max(1, RecentPathMaxSamples);
	if (RecentPlayerSamples.Num() > MaxSamples)
	{
		const int32 TrimCount = RecentPlayerSamples.Num() - MaxSamples;
		RecentPlayerSamples.RemoveAt(0, TrimCount, EAllowShrinking::No);
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

void AAeyerjiEncounterDirector::UpdateEnemyLOD(float DeltaSeconds)
{
	if (!CachedPlayerPawn.IsValid())
	{
		return;
	}

	if (EnemyLODUpdateInterval > 0.f)
	{
		EnemyLODTimeAccumulator += DeltaSeconds;
		if (EnemyLODTimeAccumulator < EnemyLODUpdateInterval)
		{
			return;
		}
		EnemyLODTimeAccumulator = 0.f;
	}

	const FVector PlayerLocation = CachedPlayerPawn->GetActorLocation();
	UpdateFixedClusterLOD(PlayerLocation);

	if (!bEnableEnemyLODThrottling)
	{
		return;
	}

	const float NearDistance = FMath::Max(0.f, EnemyLODNearDistance);
	const float MidDistance = FMath::Max(NearDistance, EnemyLODMidDistance);
	const float FarDistance = FMath::Max(MidDistance, EnemyLODFarDistance);
	const float MidDistSq = FMath::Square(MidDistance);
	const float FarDistSq = FMath::Square(FarDistance);

	for (const TWeakObjectPtr<AActor>& Tracked : LiveEnemies)
	{
		AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(Tracked.Get());
		if (!IsValid(Enemy))
		{
			continue;
		}

		FEnemyLODState& State = GetOrCreateEnemyLODState(Enemy);
		if (State.bSleeping)
		{
			continue;
		}

		const float DistSq = FVector::DistSquared2D(Enemy->GetActorLocation(), PlayerLocation);
		uint8 NewBucket = 0;
		if (DistSq > FarDistSq)
		{
			NewBucket = 2;
		}
		else if (DistSq > MidDistSq)
		{
			NewBucket = 1;
		}

		if (State.LODBucket != NewBucket)
		{
			ApplyEnemyLODBucket(Enemy, State, NewBucket);
		}
	}
}

void AAeyerjiEncounterDirector::UpdateFixedClusterLOD(const FVector& PlayerLocation)
{
	if (!bEnableFixedClusterSleeping || !bFixedPopulationActive || FixedClusters.IsEmpty())
	{
		return;
	}

	const float SleepDistance = FMath::Max(FixedClusterSleepDistance, FixedClusterWakeDistance);
	const float WakeDistance = FMath::Max(0.f, FMath::Min(FixedClusterWakeDistance, SleepDistance));
	const float SleepDistSq = FMath::Square(SleepDistance);
	const float WakeDistSq = FMath::Square(WakeDistance);

	for (TPair<int32, FFixedSpawnCluster>& Pair : FixedClusters)
	{
		FFixedSpawnCluster& Cluster = Pair.Value;
		const float DistSq = FVector::DistSquared2D(PlayerLocation, Cluster.Center);

		if (!Cluster.bSleeping && DistSq >= SleepDistSq)
		{
			Cluster.bSleeping = true;
			ApplyFixedClusterSleepState(Pair.Key, true);
		}
		else if (Cluster.bSleeping && DistSq <= WakeDistSq)
		{
			Cluster.bSleeping = false;
			ApplyFixedClusterSleepState(Pair.Key, false);
		}
	}
}

void AAeyerjiEncounterDirector::ApplyFixedClusterSleepState(int32 ClusterId, bool bSleep)
{
	TArray<TWeakObjectPtr<AEnemyParentNative>>* Members = FixedClusterMembers.Find(ClusterId);
	if (!Members)
	{
		return;
	}

	for (int32 Index = Members->Num() - 1; Index >= 0; --Index)
	{
		AEnemyParentNative* Enemy = (*Members)[Index].Get();
		if (!IsValid(Enemy))
		{
			Members->RemoveAtSwap(Index);
			continue;
		}

		ApplyEnemySleepState(Enemy, bSleep);
	}

	if (Members->IsEmpty())
	{
		FixedClusterMembers.Remove(ClusterId);
	}
}

void AAeyerjiEncounterDirector::ApplyEnemySleepState(AEnemyParentNative* Enemy, bool bSleep)
{
	if (!IsValid(Enemy))
	{
		return;
	}

	FEnemyLODState& State = GetOrCreateEnemyLODState(Enemy);
	if (State.bSleeping == bSleep)
	{
		return;
	}

	State.bSleeping = bSleep;

	UCharacterMovementComponent* MoveComp = Enemy->GetCharacterMovement();
	USkeletalMeshComponent* MeshComp = Enemy->GetMesh();
	AAIController* AIController = Cast<AAIController>(Enemy->GetController());
	UBrainComponent* Brain = AIController ? AIController->BrainComponent : nullptr;
	UAIPerceptionComponent* Perception = AIController ? AIController->GetPerceptionComponent() : nullptr;

	if (bSleep)
	{
		if (AIController)
		{
			AIController->StopMovement();
		}

		if (Brain && !State.bPausedByLOD && !Brain->IsPaused())
		{
			Brain->PauseLogic(TEXT("EncounterDirectorLOD"));
			State.bPausedByLOD = true;
		}

		if (MoveComp)
		{
			MoveComp->SetComponentTickEnabled(false);
		}

		if (MeshComp)
		{
			MeshComp->SetComponentTickEnabled(false);
		}

		if (Perception)
		{
			Perception->SetComponentTickEnabled(false);
		}
	}
	else
	{
		if (MoveComp)
		{
			MoveComp->SetComponentTickEnabled(State.bMovementTickEnabled);
			if (State.bMovementTickEnabled)
			{
				MoveComp->SetComponentTickInterval(State.BaseMovementTickInterval);
			}
		}

		if (MeshComp)
		{
			MeshComp->SetComponentTickEnabled(State.bMeshTickEnabled);
			if (State.bMeshTickEnabled)
			{
				MeshComp->SetComponentTickInterval(State.BaseMeshTickInterval);
			}
		}

		if (Perception)
		{
			Perception->SetComponentTickEnabled(State.bPerceptionTickEnabled);
			if (State.bPerceptionTickEnabled)
			{
				Perception->SetComponentTickInterval(State.BasePerceptionTickInterval);
			}
		}

		if (Brain && State.bPausedByLOD)
		{
			Brain->ResumeLogic(TEXT("EncounterDirectorLOD"));
			State.bPausedByLOD = false;
		}

		State.LODBucket = 255;
	}
}

void AAeyerjiEncounterDirector::ApplyEnemyLODBucket(AEnemyParentNative* Enemy, FEnemyLODState& State, uint8 NewBucket)
{
	if (!IsValid(Enemy))
	{
		return;
	}

	const float DesiredInterval = (NewBucket == 1) ? EnemyLODMidTickInterval : (NewBucket == 2 ? EnemyLODFarTickInterval : 0.f);
	auto ApplyTickSettings = [NewBucket, DesiredInterval](UActorComponent* Component, bool bEnabled, float BaseInterval)
	{
		if (!Component)
		{
			return;
		}

		Component->SetComponentTickEnabled(bEnabled);
		if (bEnabled)
		{
			const float Interval = (NewBucket == 0) ? BaseInterval : FMath::Max(BaseInterval, DesiredInterval);
			Component->SetComponentTickInterval(Interval);
		}
	};

	ApplyTickSettings(Enemy->GetCharacterMovement(), State.bMovementTickEnabled, State.BaseMovementTickInterval);
	ApplyTickSettings(Enemy->GetMesh(), State.bMeshTickEnabled, State.BaseMeshTickInterval);

	if (AAIController* AIController = Cast<AAIController>(Enemy->GetController()))
	{
		if (UAIPerceptionComponent* Perception = AIController->GetPerceptionComponent())
		{
			ApplyTickSettings(Perception, State.bPerceptionTickEnabled, State.BasePerceptionTickInterval);
		}
	}

	State.LODBucket = NewBucket;
}

AAeyerjiEncounterDirector::FEnemyLODState& AAeyerjiEncounterDirector::GetOrCreateEnemyLODState(AEnemyParentNative* Enemy)
{
	FEnemyLODState& State = EnemyLODStates.FindOrAdd(Enemy);
	if (!IsValid(Enemy))
	{
		return State;
	}

	if (!State.bInitialized)
	{
		State.bInitialized = true;
		State.LODBucket = 255;
	}

	if (!State.bCachedMovement)
	{
		if (UCharacterMovementComponent* MoveComp = Enemy->GetCharacterMovement())
		{
			State.bCachedMovement = true;
			State.BaseMovementTickInterval = MoveComp->PrimaryComponentTick.TickInterval;
			State.bMovementTickEnabled = MoveComp->IsComponentTickEnabled();
		}
	}

	if (!State.bCachedMesh)
	{
		if (USkeletalMeshComponent* MeshComp = Enemy->GetMesh())
		{
			State.bCachedMesh = true;
			State.BaseMeshTickInterval = MeshComp->PrimaryComponentTick.TickInterval;
			State.bMeshTickEnabled = MeshComp->IsComponentTickEnabled();
		}
	}

	if (!State.bCachedPerception)
	{
		if (AAIController* AIController = Cast<AAIController>(Enemy->GetController()))
		{
			if (UAIPerceptionComponent* Perception = AIController->GetPerceptionComponent())
			{
				State.bCachedPerception = true;
				State.BasePerceptionTickInterval = Perception->PrimaryComponentTick.TickInterval;
				State.bPerceptionTickEnabled = Perception->IsComponentTickEnabled();
			}
		}
	}

	return State;
}

void AAeyerjiEncounterDirector::RemoveEnemyLODState(AActor* Enemy)
{
	AEnemyParentNative* TypedEnemy = Cast<AEnemyParentNative>(Enemy);
	if (!TypedEnemy)
	{
		return;
	}

	EnemyLODStates.Remove(TypedEnemy);
}

void AAeyerjiEncounterDirector::RemoveFixedClusterMember(int32 ClusterId, AActor* Enemy)
{
	TArray<TWeakObjectPtr<AEnemyParentNative>>* Members = FixedClusterMembers.Find(ClusterId);
	if (!Members)
	{
		return;
	}

	for (int32 Index = Members->Num() - 1; Index >= 0; --Index)
	{
		if (!Members->operator[](Index).IsValid() || Members->operator[](Index).Get() == Enemy)
		{
			Members->RemoveAtSwap(Index);
		}
	}

	if (Members->IsEmpty())
	{
		FixedClusterMembers.Remove(ClusterId);
	}
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

void AAeyerjiEncounterDirector::BuildFixedPopulationPlan()
{
	FixedSpawnQueue.Reset();
	FixedClusterCenters.Reset();
	FixedClusters.Reset();
	FixedEnemyClusterMap.Reset();
	FixedPopulationTarget = 0;
	FixedPopulationSpawned = 0;
	FixedPopulationRemaining = 0;
	FixedClustersRemaining = 0;

	const UAeyerjiWorldSpawnProfile* Profile = FixedSpawnProfile.Get();
	if (!Profile)
	{
		return;
	}

	TArray<FFixedSpawnGroupEntry> GroupEntries;
	if (!Profile->SpawnGroups.IsEmpty())
	{
		for (const FWeightedSpawnGroup& Weighted : Profile->SpawnGroups)
		{
			if (IsValid(Weighted.Group) && !Weighted.Group->EnemyTypes.IsEmpty() && Weighted.Weight > 0.f)
			{
				FFixedSpawnGroupEntry Entry;
				Entry.Group = Weighted.Group;
				Entry.Weight = Weighted.Weight;
				GroupEntries.Add(Entry);
			}
		}
	}
	else
	{
		for (const UEnemySpawnGroupDefinition* Group : SpawnGroups)
		{
			if (IsValid(Group) && !Group->EnemyTypes.IsEmpty())
			{
				FFixedSpawnGroupEntry Entry;
				Entry.Group = Group;
				Entry.Weight = 1.0f;
				GroupEntries.Add(Entry);
			}
		}
	}

	if (GroupEntries.IsEmpty())
	{
		UE_LOG(LogEncounterDirector, Warning, TEXT("Fixed population build skipped: no spawn groups on %s"), *GetNameSafe(this));
		return;
	}

	const int32 BaseMinEnemyCount = FMath::Max(0, Profile->MinimumEnemyCount);
	const int32 BaseTargetEnemyCount = FMath::Max(0, Profile->TargetEnemyCount);
	int32 BaseMaxEnemyCount = Profile->MaximumEnemyCount > 0 ? Profile->MaximumEnemyCount : BaseTargetEnemyCount;
	BaseMaxEnemyCount = FMath::Max(BaseMaxEnemyCount, BaseMinEnemyCount);

	int32 MaxDifficultyMinEnemyCount = Profile->MinimumEnemyCountAtMaxDifficulty > 0
		? Profile->MinimumEnemyCountAtMaxDifficulty
		: BaseMinEnemyCount;
	int32 MaxDifficultyMaxEnemyCount = Profile->MaximumEnemyCountAtMaxDifficulty > 0
		? Profile->MaximumEnemyCountAtMaxDifficulty
		: BaseMaxEnemyCount;
	MaxDifficultyMinEnemyCount = FMath::Max(0, MaxDifficultyMinEnemyCount);
	MaxDifficultyMaxEnemyCount = FMath::Max(MaxDifficultyMaxEnemyCount, MaxDifficultyMinEnemyCount);

	float BudgetAlpha = 0.f;
	if (Profile->bScaleBudgetByDifficulty && FixedPopulationLevelDirector.IsValid())
	{
		const float Difficulty = FixedPopulationLevelDirector->GetCurvedDifficulty();
		BudgetAlpha = FMath::Clamp(Profile->DifficultyBudgetFloor + (1.f - Profile->DifficultyBudgetFloor) * Difficulty, 0.f, 1.f);
	}
	const int32 MinEnemyCount = FMath::RoundToInt(FMath::Lerp(static_cast<float>(BaseMinEnemyCount), static_cast<float>(MaxDifficultyMinEnemyCount), BudgetAlpha));
	const int32 MaxEnemyCount = FMath::RoundToInt(FMath::Lerp(static_cast<float>(BaseMaxEnemyCount), static_cast<float>(MaxDifficultyMaxEnemyCount), BudgetAlpha));
	const int32 ClampedMaxEnemyCount = FMath::Max(MaxEnemyCount, MinEnemyCount);

	float TargetAlpha = 0.f;
	if (BaseMaxEnemyCount > BaseMinEnemyCount)
	{
		// Preserve the target's relative position within the base min/max range.
		TargetAlpha = static_cast<float>(BaseTargetEnemyCount - BaseMinEnemyCount) / static_cast<float>(BaseMaxEnemyCount - BaseMinEnemyCount);
		TargetAlpha = FMath::Clamp(TargetAlpha, 0.f, 1.f);
	}

	int32 ResolvedTarget = FMath::RoundToInt(FMath::Lerp(static_cast<float>(MinEnemyCount), static_cast<float>(ClampedMaxEnemyCount), TargetAlpha));
	ResolvedTarget = FMath::Clamp(ResolvedTarget, MinEnemyCount, ClampedMaxEnemyCount);

	// Allow runtime scaling/caps to keep fixed population counts playable.
	const float BudgetScale = FMath::Clamp(GetFixedPopulationBudgetScaleCVar().GetValueOnGameThread(), 0.f, 1.f);
	if (BudgetScale < 1.f)
	{
		ResolvedTarget = FMath::RoundToInt(ResolvedTarget * BudgetScale);
	}

	const int32 BudgetCap = GetFixedPopulationBudgetCapCVar().GetValueOnGameThread();
	if (BudgetCap > 0)
	{
		ResolvedTarget = FMath::Min(ResolvedTarget, BudgetCap);
	}

	if (ResolvedTarget <= 0)
	{
		return;
	}

	FixedSpawnQueue.Reserve(ResolvedTarget);

	FixedSpawnSeed = Profile->Seed != 0 ? Profile->Seed : FMath::Rand();
	FixedSpawnStream.Initialize(FixedSpawnSeed);

	const int32 MinClusterSize = FMath::Max(1, Profile->MinClusterSize);
	const int32 MaxClusterSize = FMath::Max(MinClusterSize, Profile->MaxClusterSize);
	const int32 MinClusterCount = FMath::Max(1, Profile->MinClusterCount);
	const int32 MaxClusterCount = FMath::Max(MinClusterCount, Profile->MaxClusterCount);
	const float AvgClusterSize = (Profile->MinClusterSize + Profile->MaxClusterSize) * 0.5f;
	int32 ClusterCount = AvgClusterSize > 0.f ? FMath::RoundToInt(ResolvedTarget / AvgClusterSize) : MinClusterCount;
	ClusterCount = FMath::Clamp(ClusterCount, MinClusterCount, MaxClusterCount);
	const int32 MaxPossibleClusters = FMath::Max(1, ResolvedTarget / MinClusterSize);
	ClusterCount = FMath::Min(ClusterCount, MaxPossibleClusters);

	TArray<FFixedSpawnRegionEntry> Regions;
	if (Profile->bUseSpawnRegions)
	{
		for (TActorIterator<AAeyerjiSpawnRegion> It(GetWorld()); It; ++It)
		{
			AAeyerjiSpawnRegion* Region = *It;
			if (!IsValid(Region))
			{
				continue;
			}

			const FBox Bounds = Region->GetRegionBounds();
			if (!Bounds.IsValid)
			{
				continue;
			}

			FFixedSpawnRegionEntry Entry;
			Entry.Region = Region;
			Entry.Bounds = Bounds;
			const FVector RegionSize = Bounds.GetSize();
			const float SizeScore = FMath::Max(0.f, RegionSize.X + RegionSize.Y);
			Entry.Weight = FMath::Max(0.f, Region->RegionWeight) * SizeScore;
			Entry.DensityScale = FMath::Max(0.f, Region->DensityScale);
			Entry.EliteChanceBonus = FMath::Max(0.f, Region->EliteChanceBonus);
			Entry.RadiusScale = FMath::Max(0.f, Region->ClusterRadiusScale);
			Entry.bAllowElites = Region->bAllowElites;

			if (Entry.Weight > 0.f)
			{
				Regions.Add(Entry);
			}
		}

		UE_LOG(LogEncounterDirector, Log, TEXT("SpawnDiag: %s found %d spawn regions (fallbackRadius=%.1f seed=%d)"),
			*GetNameSafe(this),
			Regions.Num(),
			Profile->FallbackSpawnRadius,
			FixedSpawnSeed);

		int32 RegionLogCount = 0;
		for (const FFixedSpawnRegionEntry& Entry : Regions)
		{
			if (RegionLogCount++ >= 5)
			{
				break;
			}

			const FVector RegionSize = Entry.Bounds.GetSize();
			UE_LOG(LogEncounterDirector, Log, TEXT("SpawnDiag: Region %s weight=%.2f sizeXY=(%.1f, %.1f) bounds=%s"),
				*GetNameSafe(Entry.Region.Get()),
				Entry.Weight,
				RegionSize.X,
				RegionSize.Y,
				*Entry.Bounds.ToString());
		}
	}

	if (Profile->bUseSpawnRegions && Regions.IsEmpty())
	{
		UE_LOG(LogEncounterDirector, Warning, TEXT("SpawnDiag: Fixed population build skipped: bUseSpawnRegions is true but no valid spawn regions were found on %s"), *GetNameSafe(this));
		return;
	}

	auto ChooseRegionIndex = [this](const TArray<FFixedSpawnRegionEntry>& Entries) -> int32
	{
		float TotalWeight = 0.f;
		for (const FFixedSpawnRegionEntry& Entry : Entries)
		{
			TotalWeight += FMath::Max(0.f, Entry.Weight);
		}

		if (TotalWeight <= KINDA_SMALL_NUMBER)
		{
			return INDEX_NONE;
		}

		float Roll = FixedSpawnStream.FRandRange(0.f, TotalWeight);
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			Roll -= FMath::Max(0.f, Entries[Index].Weight);
			if (Roll <= 0.f)
			{
				return Index;
			}
		}

		return Entries.Num() > 0 ? 0 : INDEX_NONE;
	};

	TArray<int32> RegionAssignments;
	if (!Regions.IsEmpty() && ClusterCount >= Regions.Num())
	{
		RegionAssignments.Reserve(ClusterCount);
		for (int32 Index = 0; Index < Regions.Num(); ++Index)
		{
			RegionAssignments.Add(Index);
		}

		for (int32 Index = Regions.Num(); Index < ClusterCount; ++Index)
		{
			const int32 RegionIndex = ChooseRegionIndex(Regions);
			RegionAssignments.Add(RegionIndex);
		}

		for (int32 Index = RegionAssignments.Num() - 1; Index > 0; --Index)
		{
			const int32 SwapIndex = FixedSpawnStream.RandRange(0, Index);
			RegionAssignments.Swap(Index, SwapIndex);
		}
	}

	TArray<FFixedSpawnCluster> ClusterList;
	ClusterList.Reserve(ClusterCount);

	TArray<float> DensitySamples;
	DensitySamples.Reserve(ClusterCount);

	int32 RemainingEnemies = ResolvedTarget;
	for (int32 ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)
	{
		const int32 ClustersLeft = ClusterCount - ClusterIndex;
		int32 MinAllowed = RemainingEnemies - ((ClustersLeft - 1) * MaxClusterSize);
		int32 MaxAllowed = RemainingEnemies - ((ClustersLeft - 1) * MinClusterSize);
		MinAllowed = FMath::Clamp(MinAllowed, MinClusterSize, MaxClusterSize);
		MaxAllowed = FMath::Clamp(MaxAllowed, MinClusterSize, MaxClusterSize);
		if (MaxAllowed < MinAllowed)
		{
			MaxAllowed = MinAllowed;
		}

		int32 RegionIndex = INDEX_NONE;
		if (RegionAssignments.IsValidIndex(ClusterIndex))
		{
			RegionIndex = RegionAssignments[ClusterIndex];
		}
		else if (!Regions.IsEmpty())
		{
			RegionIndex = ChooseRegionIndex(Regions);
		}

		const FFixedSpawnRegionEntry* RegionEntry = Regions.IsValidIndex(RegionIndex) ? &Regions[RegionIndex] : nullptr;

		float DensityAlpha = FixedSpawnStream.FRand();
		if (Profile->DensityCurve)
		{
			DensityAlpha = Profile->DensityCurve->GetFloatValue(DensityAlpha);
		}
		else
		{
			DensityAlpha = FMath::Pow(DensityAlpha, FMath::Max(0.01f, Profile->DensityExponent));
		}

		DensityAlpha = FMath::Clamp(DensityAlpha, 0.f, 1.f);
		if (RegionEntry)
		{
			DensityAlpha = FMath::Clamp(DensityAlpha * RegionEntry->DensityScale, 0.f, 1.f);
		}

		int32 ClusterSize = FMath::RoundToInt(FMath::Lerp(static_cast<float>(MinAllowed), static_cast<float>(MaxAllowed), DensityAlpha));
		ClusterSize = FMath::Clamp(ClusterSize, MinAllowed, MaxAllowed);

		float Radius = FMath::Lerp(Profile->ClusterRadiusMin, Profile->ClusterRadiusMax, DensityAlpha);
		if (RegionEntry)
		{
			Radius *= RegionEntry->RadiusScale;
		}
		Radius = FMath::Max(0.f, Radius);

		FVector ClusterCenter = GetActorLocation();
		if (!ResolveFixedClusterCenter(RegionEntry, FixedClusterCenters, Profile->MinClusterSpacing, ClusterCenter))
		{
			if (RegionEntry)
			{
				ClusterCenter = RegionEntry->Bounds.GetCenter();
			}
			else if (CachedPlayerPawn.IsValid())
			{
				ClusterCenter = CachedPlayerPawn->GetActorLocation();
			}
		}

		if (RegionEntry && RegionEntry->Bounds.IsValid)
		{
			const float MaxRadiusX = FMath::Min(ClusterCenter.X - RegionEntry->Bounds.Min.X, RegionEntry->Bounds.Max.X - ClusterCenter.X);
			const float MaxRadiusY = FMath::Min(ClusterCenter.Y - RegionEntry->Bounds.Min.Y, RegionEntry->Bounds.Max.Y - ClusterCenter.Y);
			const float MaxRadius = FMath::Max(0.f, FMath::Min(MaxRadiusX, MaxRadiusY));
			if (MaxRadius < Radius && ClusterIndex < 5)
			{
				UE_LOG(LogEncounterDirector, Log, TEXT("SpawnDiag: Cluster %d radius clamped %.1f -> %.1f to stay inside region bounds"),
					ClusterIndex,
					Radius,
					MaxRadius);
			}
			Radius = FMath::Min(Radius, MaxRadius);
		}

		if (ClusterIndex < 5)
		{
			UE_LOG(LogEncounterDirector, Log, TEXT("SpawnDiag: Cluster %d region=%s density=%.2f radius=%.1f center=%s"),
				ClusterIndex,
				*GetNameSafe(RegionEntry ? RegionEntry->Region.Get() : nullptr),
				DensityAlpha,
				Radius,
				*ClusterCenter.ToCompactString());
		}

		FixedClusterCenters.Add(ClusterCenter);

		FFixedSpawnCluster Cluster;
		Cluster.ClusterId = ClusterIndex;
		Cluster.Center = ClusterCenter;
		Cluster.Radius = Radius;
		Cluster.RegionBounds = RegionEntry ? RegionEntry->Bounds : FBox(EForceInit::ForceInit);
		Cluster.bHasRegion = RegionEntry != nullptr;
		Cluster.DensityAlpha = DensityAlpha;
		Cluster.EliteChanceBonus = RegionEntry ? RegionEntry->EliteChanceBonus : 0.f;
		Cluster.bAllowElites = RegionEntry ? RegionEntry->bAllowElites : true;
		Cluster.TotalEnemies = ClusterSize;
		Cluster.RemainingEnemies = ClusterSize;

		ClusterList.Add(Cluster);
		DensitySamples.Add(DensityAlpha);
		RemainingEnemies -= ClusterSize;
	}

	if (RemainingEnemies > 0 && !ClusterList.IsEmpty())
	{
		ClusterList.Last().TotalEnemies += RemainingEnemies;
		ClusterList.Last().RemainingEnemies += RemainingEnemies;
		RemainingEnemies = 0;
	}

	float DenseThreshold = 1.0f;
	const float DensePercentile = FMath::Clamp(Profile->DenseClusterPercentile, 0.f, 1.f);
	if (DensePercentile > 0.f && DensitySamples.Num() > 0)
	{
		DensitySamples.Sort();
		const int32 Index = FMath::Clamp(FMath::RoundToInt((1.f - DensePercentile) * static_cast<float>(DensitySamples.Num() - 1)), 0, DensitySamples.Num() - 1);
		DenseThreshold = DensitySamples[Index];
	}

	for (FFixedSpawnCluster& Cluster : ClusterList)
	{
		Cluster.bDenseCluster = DensePercentile > 0.f && Cluster.DensityAlpha >= DenseThreshold;
		FixedClusters.Add(Cluster.ClusterId, Cluster);
	}

	FixedClustersRemaining = FixedClusters.Num();

	for (const FFixedSpawnCluster& Cluster : ClusterList)
	{
		for (int32 SpawnIndex = 0; SpawnIndex < Cluster.TotalEnemies; ++SpawnIndex)
		{
			const UEnemySpawnGroupDefinition* Group = ChooseFixedSpawnGroup(GroupEntries);
			if (!Group)
			{
				continue;
			}

			FFixedSpawnRequest Request;
			Request.Group = Group;
			Request.ClusterId = Cluster.ClusterId;
			Request.ClusterCenter = Cluster.Center;
			Request.ClusterRadius = Cluster.Radius;
			Request.DensityAlpha = Cluster.DensityAlpha;
			Request.EliteChanceBonus = Cluster.EliteChanceBonus;
			Request.bDenseCluster = Cluster.bDenseCluster;
			Request.bAllowElites = Cluster.bAllowElites;
			FixedSpawnQueue.Add(Request);
		}
	}

	for (int32 Index = FixedSpawnQueue.Num() - 1; Index > 0; --Index)
	{
		const int32 SwapIndex = FixedSpawnStream.RandRange(0, Index);
		FixedSpawnQueue.Swap(Index, SwapIndex);
	}

	FixedPopulationTarget = FixedSpawnQueue.Num();
	FixedPopulationRemaining = FixedPopulationTarget;

	UE_LOG(LogEncounterDirector, Log, TEXT("Fixed population plan: %d clusters, %d enemies (seed=%d)."),
		FixedClusters.Num(),
		FixedPopulationTarget,
		FixedSpawnSeed);
}

void AAeyerjiEncounterDirector::ProcessFixedSpawnQueue()
{
	if (!bFixedPopulationActive)
	{
		return;
	}

	const UAeyerjiWorldSpawnProfile* Profile = FixedSpawnProfile.Get();
	if (!Profile || FixedSpawnQueue.IsEmpty())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const int32 SpawnBudget = Profile->MaxFixedSpawnsPerTickOverride > 0 ? Profile->MaxFixedSpawnsPerTickOverride : FMath::Max(1, MaxSpawnsPerTick);
	int32 SpawnedThisTick = 0;
	const auto HandleSpawnSkip = [this](int32 ClusterId)
	{
		HandleFixedPopulationClusterDecrement(ClusterId);
		FixedPopulationTarget = FMath::Max(0, FixedPopulationTarget - 1);
		FixedPopulationRemaining = FMath::Max(0, FixedPopulationTarget - FixedPopulationSpawned);
	};

	while (SpawnedThisTick < SpawnBudget && FixedSpawnQueue.Num() > 0)
	{
		const FFixedSpawnRequest Request = FixedSpawnQueue[0];
		FixedSpawnQueue.RemoveAtSwap(0);

		const UEnemySpawnGroupDefinition* Group = Request.Group.Get();
		if (!Group)
		{
			HandleSpawnSkip(Request.ClusterId);
			continue;
		}

		TSubclassOf<AEnemyParentNative> EnemyClass = Group->ResolveEnemyClass();
		if (!*EnemyClass)
		{
			HandleSpawnSkip(Request.ClusterId);
			continue;
		}

		const float HalfHeight = GetEnemyHalfHeight(EnemyClass);
		const FFixedSpawnCluster* Cluster = FixedClusters.Find(Request.ClusterId);
		const FBox RegionBounds = (Cluster && Cluster->bHasRegion) ? Cluster->RegionBounds : FBox(EForceInit::ForceInit);
		const bool bHasRegion = Cluster && Cluster->bHasRegion && RegionBounds.IsValid;
		const FVector SpawnLocation = ResolveFixedSpawnLocation(Request.ClusterCenter, Request.ClusterRadius, HalfHeight, RegionBounds, bHasRegion);
		const FRotator SpawnRotation = CachedPlayerPawn.IsValid()
			? (CachedPlayerPawn->GetActorLocation() - SpawnLocation).Rotation()
			: FRotator::ZeroRotator;

		FEnemySet EnemyTemplate;
		EnemyTemplate.EnemyClass = EnemyClass;
		EnemyTemplate.Count = 1;
		EnemyTemplate.SpawnInterval = 0.0f;
		EnemyTemplate.EnemyArchetypeTag = ResolveArchetypeTagFromClass(EnemyClass);

		bool bSpawnElite = false;
		int32 MinAffixes = Profile->BaseEliteMinAffixes;
		int32 MaxAffixes = Profile->BaseEliteMaxAffixes;

		if (Request.bAllowElites)
		{
			float EliteChance = Profile->BaseEliteChance + Request.EliteChanceBonus;
			EliteChance += FMath::Clamp(Request.DensityAlpha, 0.f, 1.f) * Profile->DensityEliteChanceScale;

			if (Request.bDenseCluster)
			{
				EliteChance += Profile->DenseEliteChanceBonus;
				MinAffixes = Profile->DenseEliteMinAffixes;
				MaxAffixes = Profile->DenseEliteMaxAffixes;
			}

			EliteChance = FMath::Clamp(EliteChance, 0.f, Profile->EliteChanceCap);
			bSpawnElite = FixedSpawnStream.FRand() <= EliteChance;
		}

		EnemyTemplate.bIsElite = bSpawnElite;
		if (bSpawnElite)
		{
			EnemyTemplate.MinEliteAffixes = FMath::Max(0, MinAffixes);
			EnemyTemplate.MaxEliteAffixes = FMath::Max(EnemyTemplate.MinEliteAffixes, MaxAffixes);
		}

		const bool bApplyAggro = Profile->bApplyAggroOnSpawn;
		AAeyerjiSpawnerGroup* Spawner = FixedPopulationSpawner.Get();
		const FTransform SpawnTransform(SpawnRotation, SpawnLocation);

		APawn* SpawnedPawn = UAeyerjiEnemyManagementBPFL::SpawnAndRegisterEnemyFromSet(
			this,
			EnemyTemplate,
			SpawnTransform,
			Spawner,
			/*Owner=*/this,
			CachedPlayerPawn.Get(),
			/*bApplyEliteSettings=*/true,
			bApplyAggro,
			/*bAutoActivate=*/true,
			/*bAutoActivateOnlyIfNoWaves=*/true,
			CachedPlayerPawn.Get(),
			CachedPlayerController.Get(),
			/*bSkipRandomEliteResolution=*/true);
		AEnemyParentNative* SpawnedEnemy = Cast<AEnemyParentNative>(SpawnedPawn);
		if (!SpawnedEnemy)
		{
			HandleSpawnSkip(Request.ClusterId);
			continue;
		}

		SnapActorToGround(SpawnedEnemy, HalfHeight);
		RegisterFixedClusterEnemy(SpawnedEnemy, Request.ClusterId);

		if (!Spawner && FixedPopulationSpawned == 0)
		{
			UE_LOG(LogEncounterDirector, Warning, TEXT("Fixed population on %s has no spawn manager; elite scaling will be skipped."), *GetNameSafe(this));
		}

		FixedPopulationSpawned++;
		FixedPopulationRemaining = FMath::Max(0, FixedPopulationTarget - FixedPopulationSpawned);
		SpawnedThisTick++;
	}
}

const UEnemySpawnGroupDefinition* AAeyerjiEncounterDirector::ChooseFixedSpawnGroup(const TArray<FFixedSpawnGroupEntry>& Groups)
{
	float TotalWeight = 0.f;
	for (const FFixedSpawnGroupEntry& Entry : Groups)
	{
		if (Entry.Group.IsValid())
		{
			TotalWeight += FMath::Max(0.f, Entry.Weight);
		}
	}

	if (TotalWeight <= KINDA_SMALL_NUMBER)
	{
		return nullptr;
	}

	float Roll = FixedSpawnStream.FRandRange(0.f, TotalWeight);
	for (const FFixedSpawnGroupEntry& Entry : Groups)
	{
		if (!Entry.Group.IsValid())
		{
			continue;
		}

		Roll -= FMath::Max(0.f, Entry.Weight);
		if (Roll <= 0.f)
		{
			return Entry.Group.Get();
		}
	}

	return Groups.Num() > 0 ? Groups[0].Group.Get() : nullptr;
}

bool AAeyerjiEncounterDirector::ResolveFixedClusterCenter(const FFixedSpawnRegionEntry* RegionEntry, const TArray<FVector>& ExistingCenters, float MinSpacing, FVector& OutCenter)
{
	const UAeyerjiWorldSpawnProfile* Profile = FixedSpawnProfile.Get();
	if (!Profile)
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const int32 Attempts = FMath::Max(1, Profile->ClusterCenterSearchAttempts);
	const float MinSpacingSq = MinSpacing > 0.f ? FMath::Square(MinSpacing) : 0.f;
	const FVector ProjectionExtent(Profile->NavProjectionExtent);
	const FVector FallbackOrigin = CachedPlayerPawn.IsValid() ? CachedPlayerPawn->GetActorLocation() : GetActorLocation();
	const float FallbackRadius = FMath::Max(0.f, Profile->FallbackSpawnRadius);
	const bool bHasRegion = RegionEntry != nullptr;

	static int32 FallbackLogCount = 0;
	if (!bHasRegion && FallbackLogCount < 10)
	{
		UE_LOG(LogEncounterDirector, Log, TEXT("SpawnDiag: No region assigned. Using fallback origin=%s radius=%.1f"),
			*FallbackOrigin.ToString(),
			FallbackRadius);
		++FallbackLogCount;
	}

	for (int32 Attempt = 0; Attempt < Attempts; ++Attempt)
	{
		FVector Candidate = FallbackOrigin;

		if (RegionEntry)
		{
			const FBox& Bounds = RegionEntry->Bounds;
			Candidate.X = FixedSpawnStream.FRandRange(Bounds.Min.X, Bounds.Max.X);
			Candidate.Y = FixedSpawnStream.FRandRange(Bounds.Min.Y, Bounds.Max.Y);
			Candidate.Z = FixedSpawnStream.FRandRange(Bounds.Min.Z, Bounds.Max.Z);
		}
		else if (FallbackRadius > 0.f)
		{
			const float Angle = FixedSpawnStream.FRandRange(0.f, 2.f * PI);
			const float Distance = FMath::Sqrt(FixedSpawnStream.FRand()) * FallbackRadius;
			Candidate += FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 0.f);
		}

		if (NavSys)
		{
			FNavLocation Projected;
			if (NavSys->ProjectPointToNavigation(Candidate, Projected, ProjectionExtent))
			{
				Candidate = Projected.Location;
			}
			else if (!bHasRegion && FallbackRadius > 0.f)
			{
				FNavLocation Reachable;
				if (NavSys->GetRandomReachablePointInRadius(Candidate, FallbackRadius, Reachable))
				{
					Candidate = Reachable.Location;
				}
			}
		}

		if (bHasRegion && !RegionEntry->Bounds.IsInsideXY(Candidate))
		{
			continue;
		}

		if (MinSpacingSq > 0.f)
		{
			bool bTooClose = false;
			for (const FVector& Center : ExistingCenters)
			{
				if (FVector::DistSquared2D(Candidate, Center) < MinSpacingSq)
				{
					bTooClose = true;
					break;
				}
			}

			if (bTooClose)
			{
				continue;
			}
		}

		OutCenter = Candidate;
		return true;
	}

	return false;
}

FVector AAeyerjiEncounterDirector::ResolveFixedSpawnLocation(const FVector& ClusterCenter, float Radius, float HalfHeight, const FBox& RegionBounds, bool bHasRegion)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return ClusterCenter + FVector(0.f, 0.f, HalfHeight);
	}

	const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const int32 Attempts = FMath::Max(1, SpawnLocationSearchAttempts);
	const bool bUseRegionBounds = bHasRegion && RegionBounds.IsValid;
	float SafeRadius = FMath::Max(0.f, Radius);
	if (bUseRegionBounds)
	{
		const float MaxRadiusX = FMath::Min(ClusterCenter.X - RegionBounds.Min.X, RegionBounds.Max.X - ClusterCenter.X);
		const float MaxRadiusY = FMath::Min(ClusterCenter.Y - RegionBounds.Min.Y, RegionBounds.Max.Y - ClusterCenter.Y);
		const float MaxRadius = FMath::Max(0.f, FMath::Min(MaxRadiusX, MaxRadiusY));
		SafeRadius = FMath::Min(SafeRadius, MaxRadius);
	}
	const float MinDistance = FMath::Max(0.f, MinSpawnDistanceFromPlayer);
	static int32 OutOfBoundsLogCount = 0;
	static int32 FallbackLogCount = 0;

	for (int32 Attempt = 0; Attempt < Attempts; ++Attempt)
	{
		FVector Candidate = ClusterCenter;
		bool bFoundNav = false;

		if (NavSys && SafeRadius > 0.f)
		{
			FNavLocation Result;
			if (NavSys->GetRandomReachablePointInRadius(ClusterCenter, SafeRadius, Result))
			{
				Candidate = Result.Location;
				bFoundNav = true;
			}
		}

		if (!bFoundNav && SafeRadius > 0.f)
		{
			const float Angle = FixedSpawnStream.FRandRange(0.f, 2.f * PI);
			const float Distance = FMath::Sqrt(FixedSpawnStream.FRand()) * SafeRadius;
			Candidate += FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 0.f);
		}

		Candidate.Z += HalfHeight;

		if (bUseRegionBounds && !RegionBounds.IsInsideXY(Candidate))
		{
			if (OutOfBoundsLogCount < 10)
			{
				UE_LOG(LogEncounterDirector, Log, TEXT("SpawnDiag: Reject fixed spawn outside region (center=%s candidate=%s boundsMin=%s boundsMax=%s)"),
					*ClusterCenter.ToCompactString(),
					*Candidate.ToCompactString(),
					*RegionBounds.Min.ToCompactString(),
					*RegionBounds.Max.ToCompactString());
				OutOfBoundsLogCount++;
			}
			continue;
		}

		const FVector TraceStart = Candidate + FVector(0.f, 0.f, GroundTraceUpOffset);
		const FVector TraceEnd = Candidate - FVector(0.f, 0.f, GroundTraceDownDistance);

		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(EncounterDirector_FixedGroundTrace), false, CachedPlayerPawn.Get());
		if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params))
		{
			Candidate.Z = Hit.ImpactPoint.Z + SpawnGroundOffset + HalfHeight;
		}

		if (IsSpawnCandidateAllowed(Candidate, MinDistance))
		{
			return Candidate;
		}
	}

	if (bUseRegionBounds && FallbackLogCount < 5)
	{
		UE_LOG(LogEncounterDirector, Log, TEXT("SpawnDiag: Fixed spawn fallback to cluster center (center=%s boundsMin=%s boundsMax=%s radius=%.1f)"),
			*ClusterCenter.ToCompactString(),
			*RegionBounds.Min.ToCompactString(),
			*RegionBounds.Max.ToCompactString(),
			SafeRadius);
		FallbackLogCount++;
	}

	return ClusterCenter + FVector(0.f, 0.f, HalfHeight);
}

void AAeyerjiEncounterDirector::RegisterFixedClusterEnemy(AEnemyParentNative* Enemy, int32 ClusterId)
{
	if (!IsValid(Enemy))
	{
		return;
	}

	RegisterSpawnedEnemy(Enemy);

	if (ClusterId != INDEX_NONE)
	{
		FixedEnemyClusterMap.Add(Enemy, ClusterId);
		FixedClusterMembers.FindOrAdd(ClusterId).Add(Enemy);

		if (FFixedSpawnCluster* Cluster = FixedClusters.Find(ClusterId))
		{
			if (Cluster->bSleeping)
			{
				ApplyEnemySleepState(Enemy, true);
			}
		}
	}
}

void AAeyerjiEncounterDirector::HandleFixedPopulationEnemyRemoved(AActor* Enemy)
{
	if (!Enemy)
	{
		return;
	}

	int32* ClusterId = FixedEnemyClusterMap.Find(Enemy);
	if (!ClusterId)
	{
		return;
	}

	const int32 ResolvedClusterId = *ClusterId;
	FixedEnemyClusterMap.Remove(Enemy);
	RemoveFixedClusterMember(ResolvedClusterId, Enemy);

	HandleFixedPopulationClusterDecrement(ResolvedClusterId);
}

void AAeyerjiEncounterDirector::HandleFixedPopulationClusterDecrement(int32 ClusterId)
{
	if (ClusterId == INDEX_NONE)
	{
		return;
	}

	FFixedSpawnCluster* Cluster = FixedClusters.Find(ClusterId);
	if (!Cluster)
	{
		return;
	}

	Cluster->RemainingEnemies = FMath::Max(0, Cluster->RemainingEnemies - 1);
	if (Cluster->RemainingEnemies > 0)
	{
		return;
	}

	const float DensityAlpha = Cluster->DensityAlpha;
	const bool bDenseCluster = Cluster->bDenseCluster;

	FixedClusters.Remove(ClusterId);
	FixedClustersRemaining = FMath::Max(0, FixedClustersRemaining - 1);
	FixedClusterMembers.Remove(ClusterId);

	OnFixedClusterCleared.Broadcast(ClusterId, DensityAlpha, bDenseCluster);

	if (FixedClustersRemaining <= 0 && !bFixedPopulationComplete)
	{
		bFixedPopulationComplete = true;
		OnFixedPopulationCleared.Broadcast();
	}
}

FGameplayTag AAeyerjiEncounterDirector::ResolveArchetypeTagFromClass(TSubclassOf<AEnemyParentNative> EnemyClass) const
{
	if (!*EnemyClass)
	{
		return FGameplayTag();
	}

	const AEnemyParentNative* CDO = EnemyClass->GetDefaultObject<AEnemyParentNative>();
	if (!CDO)
	{
		return FGameplayTag();
	}

	const UAeyerjiEnemyArchetypeComponent* ArchetypeComp = CDO->FindComponentByClass<UAeyerjiEnemyArchetypeComponent>();
	if (!ArchetypeComp || !ArchetypeComp->HasArchetypeData())
	{
		return FGameplayTag();
	}

	return ArchetypeComp->GetArchetypeTag();
}

bool AAeyerjiEncounterDirector::IsSpawnCandidateAllowed(const FVector& Candidate, float MinDistance) const
{
	if (!CachedPlayerPawn.IsValid())
	{
		return true;
	}

	if (MinDistance > 0.f)
	{
		const float MinDistanceSq = FMath::Square(MinDistance);
		if (FVector::DistSquared2D(Candidate, CachedPlayerPawn->GetActorLocation()) < MinDistanceSq)
		{
			return false;
		}
	}

	if (IsNearRecentPlayerPath(Candidate))
	{
		return false;
	}

	if (IsSpawnLocationVisible(Candidate))
	{
		return false;
	}

	return true;
}

bool AAeyerjiEncounterDirector::IsNearRecentPlayerPath(const FVector& Candidate) const
{
	if (!bAvoidRecentPlayerPath || RecentPlayerSamples.IsEmpty())
	{
		return false;
	}

	const float Radius = FMath::Max(0.f, RecentPathAvoidRadius);
	if (Radius <= 0.f)
	{
		return false;
	}

	const float RadiusSq = FMath::Square(Radius);
	for (const FRecentPlayerSample& Sample : RecentPlayerSamples)
	{
		if (FVector::DistSquared2D(Candidate, Sample.Location) <= RadiusSq)
		{
			return true;
		}
	}

	return false;
}

bool AAeyerjiEncounterDirector::IsSpawnLocationVisible(const FVector& Candidate) const
{
	if (!bAvoidPlayerForwardSpawnCone || !CachedPlayerPawn.IsValid())
	{
		return false;
	}

	if (ForwardSpawnConeDegrees <= 0.f)
	{
		return false;
	}

	const FVector PlayerLocation = CachedPlayerPawn->GetActorLocation();
	const FVector Forward2D = CachedPlayerPawn->GetActorForwardVector().GetSafeNormal2D();
	FVector ToCandidate2D = Candidate - PlayerLocation;
	ToCandidate2D.Z = 0.f;

	if (ToCandidate2D.IsNearlyZero() || Forward2D.IsNearlyZero())
	{
		return true;
	}

	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(ForwardSpawnConeDegrees * 0.5f));
	const float Dot = FVector::DotProduct(Forward2D, ToCandidate2D.GetSafeNormal());

	if (Dot < CosHalfAngle)
	{
		return false;
	}

	if (!bUseLineOfSightForForwardCone)
	{
		return true;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	CachedPlayerPawn->GetActorEyesViewPoint(ViewLocation, ViewRotation);

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EncounterDirector_SpawnLOS), false, CachedPlayerPawn.Get());
	const bool bHit = World->LineTraceSingleByChannel(Hit, ViewLocation, Candidate, ECC_Visibility, Params);

	return !bHit;
}

bool AAeyerjiEncounterDirector::ShouldTriggerEncounter()
{
	if (bFixedPopulationActive)
	{
		return false;
	}

	if (DirectorState != EEncounterDirectorState::Idle)
	{
		return false;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const double Now = World->GetTimeSeconds();
	const bool bCanLog = bDrawDebug &&
		DebugLogIntervalSeconds > 0.f &&
		(Now - LastDebugLogTimestamp) >= DebugLogIntervalSeconds;
	bool bLogged = false;

	if (!CachedPlayerPawn.IsValid())
	{
		if (bCanLog && !bLogged)
		{
			bLogged = true;
			LastDebugLogTimestamp = Now;
			UE_LOG(LogEncounterDirector, Verbose, TEXT("Gate: no valid player pawn."));
		}
		return false;
	}

	if (SpawnGroups.IsEmpty())
	{
		if (bCanLog && !bLogged)
		{
			bLogged = true;
			LastDebugLogTimestamp = Now;
			UE_LOG(LogEncounterDirector, Verbose, TEXT("Gate: SpawnGroups is empty."));
		}
		return false;
	}

	if (LiveEnemies.Num() > 0)
	{
		if (bCanLog && !bLogged)
		{
			bLogged = true;
			LastDebugLogTimestamp = Now;
			UE_LOG(LogEncounterDirector, Verbose, TEXT("Gate: waiting on %d live enemies."), LiveEnemies.Num());
		}
		return false;
	}

	if (!PendingSpawnRequests.IsEmpty())
	{
		if (bCanLog && !bLogged)
		{
			bLogged = true;
			LastDebugLogTimestamp = Now;
			UE_LOG(LogEncounterDirector, Verbose, TEXT("Gate: pending spawn requests (%d remaining)."), PendingSpawnRequests.Num());
		}
		return false;
	}

	const double TimeSinceEncounter = Now - LastEncounterTimestamp;
	const double TimeSinceKill = (LastKillTimestamp > 0.0) ? (Now - LastKillTimestamp) : TimeSinceEncounter;

	// Only spawn when the player is killing quickly enough.
	if (CurrentKillVelocity < KillVelocitySpawnFloor)
	{
		if (bCanLog && !bLogged)
		{
			bLogged = true;
			LastDebugLogTimestamp = Now;
			UE_LOG(LogEncounterDirector, Verbose, TEXT("Gate: KillVelocity %.2f < Floor %.2f (Window=%.2fs)."),
				CurrentKillVelocity,
				KillVelocitySpawnFloor,
				KillVelocityWindowSeconds);
		}
		return false;
	}

	const float SpeedAlpha = GetKillSpeedAlpha();
	const float RequiredDistance = FMath::Lerp(MinDistanceAtSlow, MinDistanceAtFast, SpeedAlpha);
	const float RequiredDowntime = FMath::Lerp(MinDowntimeAtSlow, MinDowntimeAtFast, SpeedAlpha);

	const bool bDistanceRequirement = DistanceFromLastEncounter >= FMath::Max(RequiredDistance, MinDistanceBetweenEncounters);
	const bool bDowntimeWindow = TimeSinceKill >= RequiredDowntime;

	if (bCanLog && !bLogged)
	{
		bLogged = true;
		LastDebugLogTimestamp = Now;
		UE_LOG(LogEncounterDirector, Verbose, TEXT("Gate: Dist %.1f/%.1f (Min=%.1f) Downtime %.2f/%.2f KillVel %.2f (Floor=%.2f Ceil=%.2f)."),
			DistanceFromLastEncounter,
			RequiredDistance,
			MinDistanceBetweenEncounters,
			TimeSinceKill,
			RequiredDowntime,
			CurrentKillVelocity,
			KillVelocitySpawnFloor,
			KillVelocitySpawnCeil);
	}

	return bDistanceRequirement && bDowntimeWindow;
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
		if (bDrawDebug)
		{
			UE_LOG(LogEncounterDirector, Verbose, TEXT("Trigger failed: Group=%s PlayerPawnValid=%d"),
				*GetNameSafe(Group),
				CachedPlayerPawn.IsValid() ? 1 : 0);
		}
		return;
	}

	LastEncounterLocation = CachedPlayerPawn->GetActorLocation();
	LastEncounterTimestamp = GetWorld()->GetTimeSeconds();
	LastSpawnedGroup = Group;

	if (bDrawDebug)
	{
		DrawDebugSphere(GetWorld(), LastEncounterLocation, 64.f, 12, FColor::Orange, false, 2.f);
	}

	const float SpeedAlpha = GetKillSpeedAlpha();
	const int32 BurstCount = FMath::Clamp(1 + FMath::RoundToInt(SpeedAlpha * (MaxGroupsPerTrigger - 1)), 1, MaxGroupsPerTrigger);
	int32 Enqueued = 0;
	for (int32 Burst = 0; Burst < BurstCount; ++Burst)
	{
		const UEnemySpawnGroupDefinition* BurstGroup = (Burst == 0) ? Group : ChooseSpawnGroup();
		if (BurstGroup)
		{
			Enqueued += QueueSpawnsFromGroup(BurstGroup);
		}
	}

	if (Enqueued <= 0)
	{
		if (bDrawDebug)
		{
			UE_LOG(LogEncounterDirector, Verbose, TEXT("Trigger skipped: no spawns queued (BurstCount=%d)."), BurstCount);
		}
		return;
	}

	ProcessSpawnQueue();
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
	QueueSpawnsFromGroup(Group);
}

int32 AAeyerjiEncounterDirector::QueueSpawnsFromGroup(const UEnemySpawnGroupDefinition* Group)
{
	if (GetNetMode() == NM_Client || !Group)
	{
		return 0;
	}

	const int32 SpawnCount = Group->ResolveSpawnCount();
	if (SpawnCount <= 0)
	{
		return 0;
	}

	PendingSpawnRequests.Reserve(PendingSpawnRequests.Num() + SpawnCount);
	for (int32 Index = 0; Index < SpawnCount; ++Index)
	{
		PendingSpawnRequests.Add(Group);
	}

	return SpawnCount;
}

void AAeyerjiEncounterDirector::ProcessSpawnQueue()
{
	if (GetNetMode() == NM_Client)
	{
		return;
	}

	if (PendingSpawnRequests.IsEmpty())
	{
		return;
	}

	if (!CachedPlayerPawn.IsValid())
	{
		return;
	}

	const int32 SpawnBudget = FMath::Max(1, MaxSpawnsPerTick);
	int32 SpawnedThisTick = 0;

	while (SpawnedThisTick < SpawnBudget && PendingSpawnRequests.Num() > 0)
	{
		const TWeakObjectPtr<const UEnemySpawnGroupDefinition> GroupPtr = PendingSpawnRequests[0];
		PendingSpawnRequests.RemoveAtSwap(0);

		if (const UEnemySpawnGroupDefinition* Group = GroupPtr.Get())
		{
			if (SpawnSingleFromGroup(Group))
			{
				++SpawnedThisTick;
			}
		}
	}

	ActiveEnemyCount = LiveEnemies.Num();
}

bool AAeyerjiEncounterDirector::SpawnSingleFromGroup(const UEnemySpawnGroupDefinition* Group)
{
	if (!Group || !CachedPlayerPawn.IsValid())
	{
		return false;
	}

	TSubclassOf<AEnemyParentNative> EnemyClass = Group->ResolveEnemyClass();
	if (!*EnemyClass)
	{
		return false;
	}

	const float HalfHeight = GetEnemyHalfHeight(EnemyClass);
	const FVector SpawnLocation = ResolveSpawnLocation(Group->SpawnRadius, HalfHeight);
	const FRotator SpawnRotation = (CachedPlayerPawn->GetActorLocation() - SpawnLocation).Rotation();

	FEnemySet EnemyTemplate;
	EnemyTemplate.EnemyClass = EnemyClass;
	EnemyTemplate.Count = 1;
	EnemyTemplate.SpawnInterval = 0.0f;
	EnemyTemplate.EnemyArchetypeTag = ResolveArchetypeTagFromClass(EnemyClass);

	const FTransform SpawnTransform(SpawnRotation, SpawnLocation);
	APawn* SpawnedPawn = UAeyerjiEnemyManagementBPFL::SpawnAndRegisterEnemyFromSet(
		this,
		EnemyTemplate,
		SpawnTransform,
		/*Spawner=*/nullptr,
		/*Owner=*/this,
		CachedPlayerPawn.Get(),
		/*bApplyEliteSettings=*/true,
		/*bApplyAggro=*/true,
		/*bAutoActivate=*/false,
		/*bAutoActivateOnlyIfNoWaves=*/true,
		CachedPlayerPawn.Get(),
		CachedPlayerController.Get(),
		/*bSkipRandomEliteResolution=*/false);
	AEnemyParentNative* SpawnedEnemy = Cast<AEnemyParentNative>(SpawnedPawn);
	if (!SpawnedEnemy)
	{
		return false;
	}

	SnapActorToGround(SpawnedEnemy, HalfHeight);
	RegisterSpawnedEnemy(SpawnedEnemy);
	return true;
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

	UWorld* World = GetWorld();
	if (!World)
	{
		return PlayerLocation + FVector(0.f, 0.f, HalfHeight);
	}

	const float MinDistance = FMath::Clamp(MinSpawnDistanceFromPlayer, 0.f, Radius);
	const int32 Attempts = FMath::Max(1, SpawnLocationSearchAttempts);
	const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);

	for (int32 Attempt = 0; Attempt < Attempts; ++Attempt)
	{
		FVector Candidate = PlayerLocation;
		bool bFoundNav = false;

		if (NavSys)
		{
			FNavLocation Result;
			if (NavSys->GetRandomReachablePointInRadius(PlayerLocation, Radius, Result))
			{
				Candidate = Result.Location;
				bFoundNav = true;
			}
		}

		if (!bFoundNav)
		{
			const float Distance = (MinDistance > 0.f)
				? FMath::Lerp(MinDistance, Radius, FMath::FRand())
				: FMath::FRandRange(0.f, Radius);
			const FVector2D Offset2D = FMath::RandPointInCircle(Distance);
			Candidate += FVector(Offset2D.X, Offset2D.Y, 0.f);
		}

		Candidate.Z += HalfHeight;

		const FVector TraceStart = Candidate + FVector(0.f, 0.f, GroundTraceUpOffset);
		const FVector TraceEnd = Candidate - FVector(0.f, 0.f, GroundTraceDownDistance);

		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(EncounterDirector_GroundTrace), false, CachedPlayerPawn.Get());
		if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params))
		{
			Candidate.Z = Hit.ImpactPoint.Z + SpawnGroundOffset + HalfHeight;
		}

		if (IsSpawnCandidateAllowed(Candidate, MinDistance))
		{
			return Candidate;
		}
	}

	FVector Fallback = PlayerLocation + FVector(0.f, 0.f, HalfHeight);
	if (NavSys)
	{
		FNavLocation Result;
		if (NavSys->GetRandomReachablePointInRadius(PlayerLocation, Radius, Result))
		{
			Fallback = Result.Location + FVector(0.f, 0.f, HalfHeight);
		}
	}
	else
	{
		const FVector2D Offset2D = FMath::RandPointInCircle(Radius);
		Fallback = PlayerLocation + FVector(Offset2D.X, Offset2D.Y, HalfHeight);
	}

	const FVector TraceStart = Fallback + FVector(0.f, 0.f, GroundTraceUpOffset);
	const FVector TraceEnd = Fallback - FVector(0.f, 0.f, GroundTraceDownDistance);

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EncounterDirector_GroundTrace), false, CachedPlayerPawn.Get());
	if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params))
	{
		Fallback.Z = Hit.ImpactPoint.Z + SpawnGroundOffset + HalfHeight;
	}

	return Fallback;
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

float AAeyerjiEncounterDirector::GetKillSpeedAlpha() const
{
	if (KillVelocitySpawnCeil <= KillVelocitySpawnFloor)
	{
		return 0.f;
	}

	const float Range = KillVelocitySpawnCeil - KillVelocitySpawnFloor;
	const float Clamped = FMath::Clamp(CurrentKillVelocity - KillVelocitySpawnFloor, 0.f, Range);
	return Clamped / Range;
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

	GetOrCreateEnemyLODState(Enemy);
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

	HandleFixedPopulationEnemyRemoved(DeadEnemy);
	RemoveEnemyLODState(DeadEnemy);
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

	HandleFixedPopulationEnemyRemoved(DestroyedActor);
	RemoveEnemyLODState(DestroyedActor);
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
