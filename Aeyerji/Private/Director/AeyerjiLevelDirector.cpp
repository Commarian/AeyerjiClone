#include "Director/AeyerjiLevelDirector.h"

#include "Director/AeyerjiSpawnerGroup.h"
#include "Director/AeyerjiEncounterDirector.h"
#include "Director/AeyerjiWorldSpawnProfile.h"
#include "AbilitySystemGlobals.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Progression/AeyerjiLevelingComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Enemy/AeyerjiEnemyManagementBPFL.h"
#include "Enemy/EnemyParentNative.h"
#include "../AeyerjiGameInstance.h"

namespace
{
	constexpr float RunTimerInterval = 0.1f;
}

AAeyerjiLevelDirector::AAeyerjiLevelDirector()
{
	PrimaryActorTick.bCanEverTick = false;
}

void AAeyerjiLevelDirector::BeginPlay()
{
	Super::BeginPlay();

	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Checkpoint = PlayerPawn->GetActorTransform();
	}

	if (UAeyerjiGameInstance* AeyerjiGI = Cast<UAeyerjiGameInstance>(GetGameInstance()))
	{
		if (AeyerjiGI->HasDifficultySelection())
		{
			DifficultySlider = AeyerjiGI->GetDifficultySlider();
		}
	}

	for (AAeyerjiSpawnerGroup* Spawner : SpawnerSequence)
	{
		BindSpawner(Spawner);
	}

	if (BossSpawner)
	{
		BindSpawner(BossSpawner);
	}

	BindPlayerLevelingComponent();

	if (SpawnMode == EAeyerjiLevelSpawnMode::FixedWorldPopulation)
	{
		if (WorldPopulationSpawner)
		{
			WorldPopulationSpawner->LevelDirector = this;
		}

		if (!CachedEncounterDirector.IsValid())
		{
			for (TActorIterator<AAeyerjiEncounterDirector> It(GetWorld()); It; ++It)
			{
				BindEncounterDirector(*It);
				break;
			}
		}
	}
}

void AAeyerjiLevelDirector::BindSpawner(AAeyerjiSpawnerGroup* Spawner)
{
	if (!IsValid(Spawner))
	{
		return;
	}

	if (!Spawner->LevelDirector)
	{
		Spawner->LevelDirector = this;
	}

	Spawner->OnEncounterStarted.RemoveDynamic(this, &AAeyerjiLevelDirector::HandleSpawnerStarted);
	Spawner->OnEncounterStarted.AddDynamic(this, &AAeyerjiLevelDirector::HandleSpawnerStarted);
	Spawner->OnEncounterCleared.RemoveDynamic(this, &AAeyerjiLevelDirector::HandleSpawnerCleared);
	Spawner->OnEncounterCleared.AddDynamic(this, &AAeyerjiLevelDirector::HandleSpawnerCleared);
}

void AAeyerjiLevelDirector::BindEncounterDirector(AAeyerjiEncounterDirector* Director)
{
	if (!IsValid(Director))
	{
		return;
	}

	CachedEncounterDirector = Director;

	Director->OnFixedClusterCleared.RemoveDynamic(this, &AAeyerjiLevelDirector::HandleFixedClusterCleared);
	Director->OnFixedClusterCleared.AddDynamic(this, &AAeyerjiLevelDirector::HandleFixedClusterCleared);
	Director->OnFixedPopulationCleared.RemoveDynamic(this, &AAeyerjiLevelDirector::HandleFixedPopulationCleared);
	Director->OnFixedPopulationCleared.AddDynamic(this, &AAeyerjiLevelDirector::HandleFixedPopulationCleared);
}

void AAeyerjiLevelDirector::StartRun()
{
	if (bRunActive)
	{
		return;
	}

	bRunActive = true;
	AccumulatedRunSeconds = 0.f;

	BindPlayerLevelingComponent();

	// Reset shard count for a fresh run.
	const int32 OldShards = ShardCount;
	ShardCount = 0;
	if (ShardCount != OldShards)
	{
		OnShardsChanged.Broadcast(ShardCount);
	}

	// Restore boss gate to locked state at run start.
	if (BossGateActor)
	{
		BossGateActor->SetActorHiddenInGame(false);
		BossGateActor->SetActorEnableCollision(true);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(RunTimerHandle, this, &AAeyerjiLevelDirector::TickRunTimer, RunTimerInterval, true);
	}

	OnRunStateChanged.Broadcast(true);

	FixedPopulationClustersCleared = 0;

	if (bForceEnemyLevelToPlayerLevel && bResyncEnemyLevelsOnRunStart)
	{
		RefreshEnemyLevelsToCurrentPlayer();
	}

	if (SpawnMode == EAeyerjiLevelSpawnMode::Sequence && bAutoStartFirstRoom)
	{
		APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
		AController* PlayerController = PlayerPawn ? PlayerPawn->GetController() : nullptr;

		for (int32 Index = 0; Index < SpawnerSequence.Num(); ++Index)
		{
			if (AAeyerjiSpawnerGroup* Spawner = SpawnerSequence[Index])
			{
				if (!Spawner->IsCleared())
				{
					CurrentIndex = Index;
					Spawner->ActivateEncounter(PlayerPawn, PlayerController);
					break;
				}
			}
		}
	}

	if (SpawnMode == EAeyerjiLevelSpawnMode::FixedWorldPopulation)
	{
		if (!CachedEncounterDirector.IsValid())
		{
			for (TActorIterator<AAeyerjiEncounterDirector> It(GetWorld()); It; ++It)
			{
				BindEncounterDirector(*It);
				break;
			}
		}

		if (AAeyerjiEncounterDirector* Director = CachedEncounterDirector.Get())
		{
			Director->StartFixedWorldPopulation(WorldSpawnProfile, WorldPopulationSpawner, this);
		}
	}
}

void AAeyerjiLevelDirector::RefreshEnemyLevelsToCurrentPlayer()
{
	if (!HasAuthority())
	{
		return;
	}

	const int32 PlayerLevel = GetCurrentPlayerLevel();
	const float DifficultyCurved = GetCurvedDifficulty();

	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AEnemyParentNative> It(World); It; ++It)
		{
			AEnemyParentNative* Enemy = *It;
			if (!IsValid(Enemy))
			{
				continue;
			}

			if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Enemy, /*LookForComponent*/ true))
			{
				ASC->SetNumericAttributeBase(UAeyerjiAttributeSet::GetLevelAttribute(), static_cast<float>(PlayerLevel));
			}

			Enemy->SetScalingSnapshot(PlayerLevel, DifficultyCurved, Enemy->GetScalingSourceTag());
		}
	}
}

void AAeyerjiLevelDirector::HandlePlayerLevelUp(int32 OldLevel, int32 NewLevel)
{
	static_cast<void>(OldLevel);
	static_cast<void>(NewLevel);

	if (bForceEnemyLevelToPlayerLevel && bResyncEnemyLevelsOnPlayerLevelUp)
	{
		RefreshEnemyLevelsToCurrentPlayer();
	}
}

void AAeyerjiLevelDirector::EndRun()
{
	if (!bRunActive)
	{
		return;
	}

	bRunActive = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RunTimerHandle);
	}

	OnRunStateChanged.Broadcast(false);

	if (SpawnMode == EAeyerjiLevelSpawnMode::FixedWorldPopulation)
	{
		if (AAeyerjiEncounterDirector* Director = CachedEncounterDirector.Get())
		{
			Director->StopFixedWorldPopulation();
		}
	}
}

void AAeyerjiLevelDirector::TickRunTimer()
{
	if (bRunActive)
	{
		AccumulatedRunSeconds += RunTimerInterval;
	}
}

void AAeyerjiLevelDirector::BindPlayerLevelingComponent()
{
	if (!HasAuthority())
	{
		return;
	}

	if (CachedPlayerLeveling.IsValid())
	{
		return;
	}

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!PlayerPawn)
	{
		return;
	}

	UAeyerjiLevelingComponent* Leveling = PlayerPawn->FindComponentByClass<UAeyerjiLevelingComponent>();
	if (!Leveling)
	{
		return;
	}

	CachedPlayerLeveling = Leveling;
	Leveling->OnLevelUp.RemoveDynamic(this, &AAeyerjiLevelDirector::HandlePlayerLevelUp);
	Leveling->OnLevelUp.AddDynamic(this, &AAeyerjiLevelDirector::HandlePlayerLevelUp);
}

float AAeyerjiLevelDirector::GetDifficultyScale() const
{
	const float Normalized = DifficultySlider / 1000.f;
	return FMath::Clamp(Normalized, 0.f, 1.f);
}

float AAeyerjiLevelDirector::GetCurvedDifficulty() const
{
	const float Scale = GetDifficultyScale();
	return FMath::Pow(Scale, FMath::Max(0.1f, DifficultyExponent));
}

int32 AAeyerjiLevelDirector::GetCurrentPlayerLevel() const
{
	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PlayerPawn, /*LookForComponent*/ true))
		{
			const float Level = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetLevelAttribute());
			const int32 Rounded = FMath::Max(1, FMath::RoundToInt(Level));
			return Rounded;
		}
	}

	return 1;
}

void AAeyerjiLevelDirector::AddShard(int32 Amount)
{
	if (Amount == 0)
	{
		return;
	}

	const int32 OldCount = ShardCount;
	ShardCount = FMath::Max(0, ShardCount + Amount);

	if (ShardCount != OldCount)
	{
		OnShardsChanged.Broadcast(ShardCount);

		if (ShardCount >= ShardsNeeded)
		{
			OpenBossGate();
		}
	}
}

void AAeyerjiLevelDirector::OpenBossGate()
{
	if (BossGateActor)
	{
		BossGateActor->SetActorEnableCollision(false);
		BossGateActor->SetActorHiddenInGame(true);
	}

	if (IsValid(BossSpawner) && !BossSpawner->IsCleared())
	{
		APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
		AController* PlayerController = PlayerPawn ? PlayerPawn->GetController() : nullptr;
		BossSpawner->ActivateEncounter(PlayerPawn, PlayerController);
	}
}

void AAeyerjiLevelDirector::UpdateCheckpoint(const FTransform& NewCheckpoint)
{
	Checkpoint = NewCheckpoint;
}

void AAeyerjiLevelDirector::RespawnAtCheckpoint()
{
	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		PlayerPawn->SetActorTransform(Checkpoint, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void AAeyerjiLevelDirector::HandleSpawnerStarted(AAeyerjiSpawnerGroup* Spawner)
{
	// Hook for audio / UI events if needed later.
}

void AAeyerjiLevelDirector::HandleSpawnerCleared(AAeyerjiSpawnerGroup* Spawner)
{
	if (!IsValid(Spawner))
	{
		return;
	}

	if (SpawnMode == EAeyerjiLevelSpawnMode::FixedWorldPopulation)
	{
		if (Spawner == BossSpawner)
		{
			// Boss encounter cleared; fixed population mode does not auto-advance spawners.
		}
		return;
	}

	UpdateCheckpoint(Spawner->GetActorTransform());

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	AController* PlayerController = PlayerPawn ? PlayerPawn->GetController() : nullptr;

	const int32 ClearedIndex = SpawnerSequence.IndexOfByKey(Spawner);
	if (ClearedIndex != INDEX_NONE)
	{
		CurrentIndex = ClearedIndex + 1;

		if (ShardCount < ShardsNeeded)
		{
			AddShard(1);
		}

		for (int32 Index = CurrentIndex; Index < SpawnerSequence.Num(); ++Index)
		{
			if (AAeyerjiSpawnerGroup* Next = SpawnerSequence[Index])
			{
				if (!Next->IsCleared())
				{
					CurrentIndex = Index;
					Next->ActivateEncounter(PlayerPawn, PlayerController);
					return;
				}
			}
		}

		if (ShardCount >= ShardsNeeded && IsValid(BossSpawner) && !BossSpawner->IsCleared())
		{
			BossSpawner->ActivateEncounter(PlayerPawn, PlayerController);
		}
	}
	else if (Spawner == BossSpawner)
	{
		// Boss encounter cleared — nothing else to auto trigger for now.
	}
}

void AAeyerjiLevelDirector::HandleFixedClusterCleared(int32 ClusterId, float DensityAlpha, bool bDenseCluster)
{
	if (!bRunActive)
	{
		return;
	}

	static_cast<void>(ClusterId);
	static_cast<void>(DensityAlpha);
	static_cast<void>(bDenseCluster);

	FixedPopulationClustersCleared++;

	const int32 ClustersPerShard = WorldSpawnProfile ? FMath::Max(1, WorldSpawnProfile->ClustersPerShard) : 1;
	if (FixedPopulationClustersCleared % ClustersPerShard == 0 && ShardCount < ShardsNeeded)
	{
		AddShard(1);
	}
}

void AAeyerjiLevelDirector::HandleFixedPopulationCleared()
{
	if (!bRunActive)
	{
		return;
	}

	if (bOpenBossGateOnFixedPopulationCleared)
	{
		OpenBossGate();
	}
}

APawn* AAeyerjiLevelDirector::SpawnBossEncounter_Implementation(AAeyerjiEncounterDirector* EncounterDirector)
{
	// Blueprint is expected to drive boss spawning. Set bEnableNativeBossSpawn=true if you want this native fallback to run.
	if (!bEnableNativeBossSpawn)
	{
		return nullptr;
	}

	if (!HasAuthority() || !IsValid(BossSpawner) || !*BossPawnClass)
	{
		return nullptr;
	}

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	AController* PlayerController = PlayerPawn ? PlayerPawn->GetController() : nullptr;

	const FTransform SpawnTransform = BossSpawnMarker ? BossSpawnMarker->GetActorTransform() : BossSpawner->GetActorTransform();

	FEnemySet BossEnemySet;
	BossEnemySet.EnemyClass = BossPawnClass;
	BossEnemySet.Count = 1;
	BossEnemySet.SpawnInterval = 0.0f;
	BossEnemySet.bIsElite = true;
	BossEnemySet.bIsMiniBoss = true;
	BossEnemySet.bIsBoss = true;

	APawn* SpawnedBoss = UAeyerjiEnemyManagementBPFL::SpawnAndRegisterEnemyFromSet(
		this,
		BossEnemySet,
		SpawnTransform,
		BossSpawner,
		/*Owner=*/this,
		PlayerPawn,
		/*bApplyEliteSettings=*/true,
		/*bApplyAggro=*/true,
		/*bAutoActivate=*/true,
		/*bAutoActivateOnlyIfNoWaves=*/true,
		PlayerPawn,
		PlayerController,
		/*bSkipRandomEliteResolution=*/true);
	if (!SpawnedBoss)
	{
		return nullptr;
	}

	if (IsValid(EncounterDirector))
	{
		if (AEnemyParentNative* BossEnemy = Cast<AEnemyParentNative>(SpawnedBoss))
		{
			EncounterDirector->RegisterExternalEnemy(BossEnemy, true);
		}
	}

	return SpawnedBoss;
}

void AAeyerjiLevelDirector::SetBossSpawnMarker(AActor* NewMarker)
{
	BossSpawnMarker = NewMarker;
}
