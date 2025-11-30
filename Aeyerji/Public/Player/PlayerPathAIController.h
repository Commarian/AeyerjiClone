// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "AIController.h"
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