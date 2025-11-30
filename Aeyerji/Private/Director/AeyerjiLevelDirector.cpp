#include "Director/AeyerjiLevelDirector.h"

#include "Director/AeyerjiSpawnerGroup.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

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

	for (AAeyerjiSpawnerGroup* Spawner : SpawnerSequence)
	{
		BindSpawner(Spawner);
	}

	if (BossSpawner)
	{
		BindSpawner(BossSpawner);
	}
}

void AAeyerjiLevelDirector::BindSpawner(AAeyerjiSpawnerGroup* Spawner)
{
	if (!IsValid(Spawner))
	{
		return;
	}

	Spawner->OnEncounterStarted.RemoveDynamic(this, &AAeyerjiLevelDirector::HandleSpawnerStarted);
	Spawner->OnEncounterStarted.AddDynamic(this, &AAeyerjiLevelDirector::HandleSpawnerStarted);
	Spawner->OnEncounterCleared.RemoveDynamic(this, &AAeyerjiLevelDirector::HandleSpawnerCleared);
	Spawner->OnEncounterCleared.AddDynamic(this, &AAeyerjiLevelDirector::HandleSpawnerCleared);
}

void AAeyerjiLevelDirector::StartRun()
{
	if (bRunActive)
	{
		return;
	}

	bRunActive = true;
	AccumulatedRunSeconds = 0.f;

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

	if (bAutoStartFirstRoom)
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
}

void AAeyerjiLevelDirector::TickRunTimer()
{
	if (bRunActive)
	{
		AccumulatedRunSeconds += RunTimerInterval;
	}
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
		// Boss encounter cleared – nothing else to auto trigger for now.
	}
}
