// Copyright (c) 2025 Aeyerji.

#include "AeyerjiGameState.h"

#include "AeyerjiPlayerState.h"
#include "CharacterStatsLibrary.h"
#include "Director/AeyerjiLevelDirector.h"
#include "Director/AeyerjiSpawnerGroup.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Net/UnrealNetwork.h"

namespace
{
	const TCHAR* RunStateToString(EAeyerjiRunState State)
	{
		switch (State)
		{
		case EAeyerjiRunState::PreRun:
			return TEXT("PreRun");
		case EAeyerjiRunState::InRun:
			return TEXT("InRun");
		case EAeyerjiRunState::BossDefeated:
			return TEXT("BossDefeated");
		case EAeyerjiRunState::RunComplete:
			return TEXT("RunComplete");
		case EAeyerjiRunState::ReturnToMenu:
			return TEXT("ReturnToMenu");
		default:
			return TEXT("Unknown");
		}
	}
}

AAeyerjiGameState::AAeyerjiGameState()
{
	bReplicates = true;
}

void AAeyerjiGameState::BeginPlay()
{
	Super::BeginPlay();

	if (HasAuthority())
	{
		BindToLevelDirector();
		SetRunState(EAeyerjiRunState::PreRun);
	}
}

void AAeyerjiGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(AAeyerjiGameState, RunState, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(AAeyerjiGameState, RunResults, COND_None, REPNOTIFY_Always);
}

void AAeyerjiGameState::OnRep_RunState(EAeyerjiRunState OldState)
{
	HandleRunStateChanged(OldState);
}

void AAeyerjiGameState::OnRep_RunResults()
{
	MaybeBroadcastRunResults();
}

bool AAeyerjiGameState::Server_StartRun()
{
	if (!HasAuthority())
	{
		return false;
	}

	BindToLevelDirector();

	if (!SetRunState(EAeyerjiRunState::InRun))
	{
		return false;
	}

	if (AAeyerjiLevelDirector* LevelDirector = CachedLevelDirector.Get())
	{
		LevelDirector->StartRun();
	}

	return true;
}

bool AAeyerjiGameState::Server_NotifyBossDefeated()
{
	if (!HasAuthority())
	{
		return false;
	}

	if (!SetRunState(EAeyerjiRunState::BossDefeated))
	{
		return false;
	}

	if (AAeyerjiLevelDirector* LevelDirector = CachedLevelDirector.Get())
	{
		LevelDirector->EndRun();
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BossDefeatedDelayHandle);

		if (BossDefeatedToCompleteDelay <= 0.f)
		{
			HandleBossDefeatedDelayElapsed();
		}
		else
		{
			World->GetTimerManager().SetTimer(BossDefeatedDelayHandle, this, &AAeyerjiGameState::HandleBossDefeatedDelayElapsed, BossDefeatedToCompleteDelay, false);
		}
	}

	return true;
}

bool AAeyerjiGameState::Server_MarkRunComplete()
{
	if (!HasAuthority())
	{
		return false;
	}

	if (RunState == EAeyerjiRunState::RunComplete)
	{
		MaybeBroadcastRunResults();
		return true;
	}

	const bool bBossDefeated = (RunState == EAeyerjiRunState::BossDefeated);

	if (!SetRunState(EAeyerjiRunState::RunComplete))
	{
		return false;
	}

	SnapshotRunResults(bBossDefeated);

	if (AAeyerjiLevelDirector* LevelDirector = CachedLevelDirector.Get())
	{
		LevelDirector->EndRun();
	}

	if (RunResults.bBossDefeated && RunResults.RunTimeSeconds > 0.f)
	{
		for (APlayerState* PS : PlayerArray)
		{
			if (const AAeyerjiPlayerState* AeyerjiPS = Cast<AAeyerjiPlayerState>(PS))
			{
				UCharacterStatsLibrary::RecordBestRunTimeSecondsForDifficulty(AeyerjiPS, RunResults.RunTimeSeconds, RunResults.DifficultySlider);
			}
		}
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BossDefeatedDelayHandle);
		World->GetTimerManager().ClearTimer(AutoReturnDelayHandle);

		if (AutoReturnToMenuDelay > 0.f)
		{
			World->GetTimerManager().SetTimer(AutoReturnDelayHandle, this, &AAeyerjiGameState::HandleAutoReturnDelayElapsed, AutoReturnToMenuDelay, false);
		}
	}

	MaybeBroadcastRunResults();
	return true;
}

bool AAeyerjiGameState::Server_ReturnToMenu()
{
	if (!HasAuthority())
	{
		return false;
	}

	return SetRunState(EAeyerjiRunState::ReturnToMenu);
}

bool AAeyerjiGameState::Server_ForceEndRunAndReturnToMenu()
{
	if (!HasAuthority())
	{
		return false;
	}

	if (RunState == EAeyerjiRunState::ReturnToMenu)
	{
		return true;
	}

	if (RunState != EAeyerjiRunState::RunComplete)
	{
		if (!SetRunState(EAeyerjiRunState::RunComplete))
		{
			return false;
		}

		SnapshotRunResults(/*bBossDefeated=*/false);
		MaybeBroadcastRunResults();
	}

	return SetRunState(EAeyerjiRunState::ReturnToMenu);
}

void AAeyerjiGameState::BindToLevelDirector()
{
	if (!HasAuthority() || CachedLevelDirector.IsValid())
	{
		return;
	}

	if (!GetWorld())
	{
		return;
	}

	for (TActorIterator<AAeyerjiLevelDirector> It(GetWorld()); It; ++It)
	{
		CachedLevelDirector = *It;
		break;
	}

	AAeyerjiLevelDirector* LevelDirector = CachedLevelDirector.Get();
	if (!IsValid(LevelDirector))
	{
		return;
	}

	LevelDirector->OnRunStateChanged.RemoveDynamic(this, &AAeyerjiGameState::HandleLevelDirectorRunActiveChanged);
	LevelDirector->OnRunStateChanged.AddDynamic(this, &AAeyerjiGameState::HandleLevelDirectorRunActiveChanged);

	if (AAeyerjiSpawnerGroup* BossSpawner = LevelDirector->BossSpawner)
	{
		CachedBossSpawner = BossSpawner;
		BossSpawner->OnEncounterCleared.RemoveDynamic(this, &AAeyerjiGameState::HandleBossSpawnerCleared);
		BossSpawner->OnEncounterCleared.AddDynamic(this, &AAeyerjiGameState::HandleBossSpawnerCleared);
	}
}

void AAeyerjiGameState::SnapshotRunResults(bool bBossDefeated)
{
	FAeyerjiRunResults NewResults;
	NewResults.ResultsVersion = NextResultsVersion++;
	NewResults.bBossDefeated = bBossDefeated;

	if (AAeyerjiLevelDirector* LevelDirector = CachedLevelDirector.Get())
	{
		NewResults.RunTimeSeconds = LevelDirector->GetRunTimeSeconds();
		NewResults.DifficultySlider = LevelDirector->GetDifficultySlider();
		NewResults.ShardsCollected = LevelDirector->GetShardCount();
	}

	RunResults = NewResults;
	ForceNetUpdate();
}

void AAeyerjiGameState::MaybeBroadcastRunResults()
{
	if (RunState != EAeyerjiRunState::RunComplete)
	{
		return;
	}

	if (RunResults.ResultsVersion <= 0 || RunResults.ResultsVersion == LastBroadcastResultsVersion)
	{
		return;
	}

	LastBroadcastResultsVersion = RunResults.ResultsVersion;
	OnRunResultsReady.Broadcast(RunResults);
}

bool AAeyerjiGameState::SetRunState(EAeyerjiRunState NewState)
{
	if (!HasAuthority())
	{
		return false;
	}

	if (RunState == NewState)
	{
		return true;
	}

	if (!CanTransitionTo(NewState))
	{
		UE_LOG(LogTemp, Warning, TEXT("AAeyerjiGameState: invalid run state transition %s -> %s"),
		       RunStateToString(RunState),
		       RunStateToString(NewState));
		return false;
	}

	const EAeyerjiRunState OldState = RunState;
	RunState = NewState;
	HandleRunStateChanged(OldState);
	ForceNetUpdate();
	return true;
}

bool AAeyerjiGameState::CanTransitionTo(EAeyerjiRunState NewState) const
{
	if (RunState == NewState)
	{
		return true;
	}

	switch (RunState)
	{
	case EAeyerjiRunState::PreRun:
		return (NewState == EAeyerjiRunState::InRun) || (NewState == EAeyerjiRunState::RunComplete) || (NewState == EAeyerjiRunState::ReturnToMenu);
	case EAeyerjiRunState::InRun:
		return (NewState == EAeyerjiRunState::BossDefeated) || (NewState == EAeyerjiRunState::RunComplete);
	case EAeyerjiRunState::BossDefeated:
		return (NewState == EAeyerjiRunState::RunComplete);
	case EAeyerjiRunState::RunComplete:
		return (NewState == EAeyerjiRunState::ReturnToMenu);
	case EAeyerjiRunState::ReturnToMenu:
		return false;
	default:
		return false;
	}
}

void AAeyerjiGameState::HandleRunStateChanged(EAeyerjiRunState OldState)
{
	OnRunStateChanged.Broadcast(RunState, OldState);

	if (RunState == EAeyerjiRunState::InRun && HasAuthority())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(BossDefeatedDelayHandle);
			World->GetTimerManager().ClearTimer(AutoReturnDelayHandle);
		}

		RunResults = FAeyerjiRunResults();
		LastBroadcastResultsVersion = 0;
	}

	if (RunState == EAeyerjiRunState::ReturnToMenu)
	{
		if (HasAuthority())
		{
			if (UWorld* World = GetWorld())
			{
				World->GetTimerManager().ClearTimer(BossDefeatedDelayHandle);
				World->GetTimerManager().ClearTimer(AutoReturnDelayHandle);
			}
		}

		CleanupUIAndReturnToMenu();
	}

	MaybeBroadcastRunResults();
}

void AAeyerjiGameState::CleanupUIAndReturnToMenu()
{
	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	UWidgetLayoutLibrary::RemoveAllWidgets(this);

	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
	{
		return;
	}

	if (!MainMenuTravelURL.IsEmpty())
	{
		PC->ClientTravel(MainMenuTravelURL, TRAVEL_Absolute);
		return;
	}

	if (UGameInstance* GI = GetGameInstance())
	{
		GI->ReturnToMainMenu();
	}
}

void AAeyerjiGameState::HandleLevelDirectorRunActiveChanged(bool bIsRunning)
{
	if (!HasAuthority())
	{
		return;
	}

	if (bIsRunning)
	{
		SetRunState(EAeyerjiRunState::InRun);
		return;
	}

	// If the director ends while we were still in-run, treat it as an abort to RunComplete.
	if (RunState == EAeyerjiRunState::InRun)
	{
		Server_MarkRunComplete();
	}
}

void AAeyerjiGameState::HandleBossSpawnerCleared(AAeyerjiSpawnerGroup* Spawner)
{
	if (!HasAuthority())
	{
		return;
	}

	if (!CachedBossSpawner.IsValid() || Spawner != CachedBossSpawner.Get())
	{
		return;
	}

	if (RunState == EAeyerjiRunState::InRun)
	{
		Server_NotifyBossDefeated();
	}
}

void AAeyerjiGameState::HandleBossDefeatedDelayElapsed()
{
	if (HasAuthority())
	{
		Server_MarkRunComplete();
	}
}

void AAeyerjiGameState::HandleAutoReturnDelayElapsed()
{
	if (HasAuthority())
	{
		Server_ReturnToMenu();
	}
}
