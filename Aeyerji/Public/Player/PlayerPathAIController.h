// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "AIController.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "PlayerPathAIController.generated.h"
// PlayerPathAIController.h
UCLASS()
class AEYERJI_API APlayerPathAIController : public AAIController
{
	GENERATED_BODY()
public:
	APlayerPathAIController();

	/* Expose a simple wrapper so the PlayerController can forward orders */
	UFUNCTION(Server, Reliable)
	void ServerSimpleMove(const FVector& Goal);
};
