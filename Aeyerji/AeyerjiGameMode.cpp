// Copyright Epic Games, Inc. All Rights Reserved.

#include "AeyerjiGameMode.h"
#include "AeyerjiPlayerController.h"
#include "AeyerjiGameState.h"
#include "AeyerjiPlayerState.h"
#include "Engine/GameInstance.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PlayerController.h"
#include "Player/PlayerParentNative.h"
#include "UObject/ConstructorHelpers.h"
#include "Avoidance/AeyerjiAvoidanceProfile.h"
#include "Systems/AeyerjiStreamingSubsystem.h"
#include "HAL/IConsoleManager.h"

AAeyerjiGameMode::AAeyerjiGameMode()
{
	// Keep connected players/controllers alive across restart/menu server travel.
	bUseSeamlessTravel = true;

#if WITH_EDITOR
	// PIE disables seamless travel by default, which breaks retry/menu server travel in dedicated-server PIE.
	if (IConsoleVariable* AllowPIESeamlessTravel = IConsoleManager::Get().FindConsoleVariable(TEXT("net.AllowPIESeamlessTravel")))
	{
		AllowPIESeamlessTravel->Set(1, ECVF_SetByCode);
	}
#endif

	// Prefer the BP override so designers can tweak defaults without recompiling.
	static ConstructorHelpers::FClassFinder<AAeyerjiGameState> BP_GameState(TEXT("/Game/Systems/BP_AeyerjiGameState.BP_AeyerjiGameState_C"));
	if (BP_GameState.Succeeded())
	{
		GameStateClass = BP_GameState.Class;
	}
	else
	{
		GameStateClass = AAeyerjiGameState::StaticClass();
	}

	// use our custom PlayerController class
	PlayerControllerClass = AAeyerjiPlayerController::StaticClass();
	// set default pawn class to our Blueprinted character
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(
	TEXT("/Game/Player/PlayerParent"));

	if (PlayerPawnBPClass.Class)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}
	
}

AActor* AAeyerjiGameMode::ChoosePlayerStart_Implementation(AController* Player)
{
	AAeyerjiGameState* GS = GetGameState<AAeyerjiGameState>();
	if (!GS)
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	if (GS->IsBossArenaRespawnActive())
	{
		const FName BossArenaTag = GS->GetBossArenaRespawnPlayerStartTag();
		if (!BossArenaTag.IsNone())
		{
			if (UWorld* World = GetWorld())
			{
				for (TActorIterator<APlayerStart> It(World); It; ++It)
				{
					if (APlayerStart* PlayerStart = *It; PlayerStart && PlayerStart->PlayerStartTag == BossArenaTag)
					{
						return PlayerStart;
					}
				}
			}
			UE_LOG(LogTemp, Error,
				TEXT("[RiftRun][Respawn] Boss phase active but PlayerStart tag %s is missing; falling back to zone start."),
				*BossArenaTag.ToString());
		}
	}

	if (GS->GetActiveZoneId().IsNone())
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	UAeyerjiStreamingSubsystem* StreamingSubsystem = nullptr;
	if (UGameInstance* GI = GetGameInstance())
	{
		StreamingSubsystem = GI->GetSubsystem<UAeyerjiStreamingSubsystem>();
	}

	if (!StreamingSubsystem)
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	FZoneDef ZoneDef;
	if (!StreamingSubsystem->GetZoneDefinition(GS->GetActiveZoneId(), ZoneDef))
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	if (ZoneDef.EntryPlayerStartTag.IsNone())
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return Super::ChoosePlayerStart_Implementation(Player);
	}

	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		if (APlayerStart* PlayerStart = *It)
		{
			if (PlayerStart->PlayerStartTag == ZoneDef.EntryPlayerStartTag)
			{
				return PlayerStart;
			}
		}
	}

	return Super::ChoosePlayerStart_Implementation(Player);
}

void AAeyerjiGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	if (!bGateAutoSpawnByWorldFlow)
	{
		Super::HandleStartingNewPlayer_Implementation(NewPlayer);
		return;
	}

	UAeyerjiStreamingSubsystem* StreamingSubsystem = nullptr;
	if (UGameInstance* GI = GetGameInstance())
	{
		StreamingSubsystem = GI->GetSubsystem<UAeyerjiStreamingSubsystem>();
	}

	if (!StreamingSubsystem)
	{
		Super::HandleStartingNewPlayer_Implementation(NewPlayer);
		return;
	}

	const AAeyerjiGameState* GS = GetGameState<AAeyerjiGameState>();
	if (!GS || GS->GetWorldFlowPhase() == EAeyerjiWorldFlowPhase::Gameplay)
	{
		Super::HandleStartingNewPlayer_Implementation(NewPlayer);
		return;
	}
}

void AAeyerjiGameMode::PreLogin(const FString& Options, const FString& Address,
	const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
	if (!ErrorMessage.IsEmpty())
	{
		return;
	}
	if (const AAeyerjiGameState* GS = GetGameState<AAeyerjiGameState>())
	{
		if (!GS->IsFrontendLobbyAcceptingConnections())
		{
			ErrorMessage = GS->GetLobbySnapshot().Members.Num() >= 4
				? TEXT("AEYERJI_LOBBY_FULL") : TEXT("AEYERJI_LOBBY_IN_PROGRESS");
			UE_LOG(LogTemp, Warning, TEXT("[Lobby] ConnectionRejected Address=%s Reason=%s"), *Address, *ErrorMessage);
		}
	}
}

void AAeyerjiGameMode::PostLogin(APlayerController* NewPC)
{
    Super::PostLogin(NewPC);
    if (DefaultAvoidanceProfile)
    {
        if (AAeyerjiPlayerController* PC = Cast<AAeyerjiPlayerController>(NewPC))
        {
            PC->ApplyAvoidanceProfile(DefaultAvoidanceProfile);
        }
    }
	if (AAeyerjiGameState* GS = GetGameState<AAeyerjiGameState>())
	{
		GS->Server_NotifyFrontendRosterChanged();
	}
}

void AAeyerjiGameMode::Logout(AController* Exiting)
{
	if (AAeyerjiPlayerState* PS = Exiting ? Exiting->GetPlayerState<AAeyerjiPlayerState>() : nullptr)
	{
		PS->CommitCheckpointProfileFromPawn(
			EAeyerjiSaveCheckpointReason::LogoutOrShutdown,
			Exiting ? Exiting->GetPawn() : nullptr,
			/*bBumpRevision=*/true);
	}

	Super::Logout(Exiting);
	if (AAeyerjiGameState* GS = GetGameState<AAeyerjiGameState>())
	{
		GS->Server_NotifyFrontendRosterChanged();
	}
}

void AAeyerjiGameMode::HandleSeamlessTravelPlayer(AController*& Controller)
{
	Super::HandleSeamlessTravelPlayer(Controller);
	if (AAeyerjiGameState* GS = GetGameState<AAeyerjiGameState>())
	{
		GS->Server_NotifyFrontendRosterChanged();
	}
}
