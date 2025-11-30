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
    void PostLogin(APlayerController* NewPC);
    void Logout(AController* Exiting);

    /** Map-configurable avoidance profile applied to controllers/pawns on login. */
    UPROPERTY(EditDefaultsOnly, Category="Aeyerji|Movement|Avoidance")
    TObjectPtr<class UAeyerjiAvoidanceProfile> DefaultAvoidanceProfile;
};



