// Fill out your copyright notice in the Description page of Project Settings.


#include "Player/PlayerPathAIController.h"

#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "GameFramework/Pawn.h"
#include "Navigation/AeyerjiNavSafetyLibrary.h"

// PlayerPathAIController.cpp
APlayerPathAIController::APlayerPathAIController()
{
	bReplicates = true;                  // replicate to the owning client
}

void APlayerPathAIController::ServerSimpleMove_Implementation(const FVector& Goal)
{
	APawn* ControlledPawn = GetPawn();
	if (!ControlledPawn)
	{
		return;
	}

	FAeyerjiNavSafetyResolveParams Params;
	Params.ProjectionExtent = FVector(200.f, 200.f, 500.f);

	FVector SafePawnLocation = ControlledPawn->GetActorLocation();
	if (!UAeyerjiNavSafetyLibrary::EnsurePawnOnSafeNav(ControlledPawn, Params, /*bRecoverIfOffNav=*/true, SafePawnLocation))
	{
		return;
	}

	FAeyerjiNavSafetyResult GoalResult;
	if (!UAeyerjiNavSafetyLibrary::ResolveSafeNavLocationForPawn(this, Goal, ControlledPawn, Params, GoalResult))
	{
		return;
	}

	UAIBlueprintHelperLibrary::SimpleMoveToLocation(this, GoalResult.NavLocation);
}
