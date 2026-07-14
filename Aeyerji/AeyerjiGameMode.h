// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "AeyerjiGameMode.generated.h"

UCLASS(minimalapi)
class AAeyerjiGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AAeyerjiGameMode();
	AActor* ChoosePlayerStart_Implementation(AController* Player) override;
	void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
    void PostLogin(APlayerController* NewPC) override;
    void Logout(AController* Exiting) override;
	void HandleSeamlessTravelPlayer(AController*& Controller) override;
	void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

    /** Map-configurable avoidance profile applied to controllers/pawns on login. */
    UPROPERTY(EditDefaultsOnly, Category="Aeyerji|Movement|Avoidance")
    TObjectPtr<class UAeyerjiAvoidanceProfile> DefaultAvoidanceProfile;

	/** If true, login-time auto-spawn is deferred until world-flow phase reaches gameplay. */
	UPROPERTY(EditDefaultsOnly, Category="Aeyerji|Streaming")
	bool bGateAutoSpawnByWorldFlow = true;
};



