// Fill out your copyright notice in the Description page of Project Settings.


#include "Player/PlayerPathAIController.h"

#include "Blueprint/AIBlueprintHelperLibrary.h"

// PlayerPathAIController.cpp
APlayerPathAIController::APlayerPathAIController()
{
	bReplicates = true;                  // replicate to the owning client
}

void APlayerPathAIController::ServerSimpleMove_Implementation(const FVector& Goal)
{
	UAIBlueprintHelperLibrary::SimpleMoveToLocation(this, Goal);
}