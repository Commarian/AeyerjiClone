// Copyright Epic Games, Inc. All Rights Reserved.

#include "AeyerjiGameMode.h"
#include "AeyerjiPlayerController.h"
#include "AeyerjiGameState.h"
#include "GameFramework/PlayerController.h"
#include "Player/PlayerParentNative.h"
#include "UObject/ConstructorHelpers.h"
#include "Avoidance/AeyerjiAvoidanceProfile.h"

AAeyerjiGameMode::AAeyerjiGameMode()
{
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
}

void AAeyerjiGameMode::Logout(AController* Exiting)
{
	/*auto* PS   = Exiting->GetPlayerState<AAeyerjiPlayerState>();
	if (!PS)
	{
		UE_LOG(LogTemp, Error, TEXT("AAeyerjiGameMode: No PlayerState for %s"), *Exiting->GetName());
	}
	const FString Slot = UCharacterStatsLibrary::MakeStableCharSlotName(PS);
	auto* Data = Cast<UAeyerjiSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UAeyerjiSaveGame::StaticClass()));
	
	UE_LOG(LogTemp, Log, TEXT("Saving from LOGOUT"));

	UCharacterStatsLibrary::FillCharSave(Data, PS, Slot);*/
	Super::Logout(Exiting);
}
