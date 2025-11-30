// Fill out your copyright notice in the Description page of Project Settings.


#include "Abilities/Blink/GCN_Blink_In.h"
// GCN_Blink_Out.cpp
#include "NiagaraFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"

bool UGCN_Blink_In::OnExecute_Implementation(
	AActor* Target,
	const FGameplayCueParameters& Parameters) const
{
	if (!Target || !NiagaraTemplate) return false;

	const FVector Loc = Parameters.Location.IsNearlyZero()
					  ? Target->GetActorLocation()
					  : FVector(Parameters.Location);

	UNiagaraFunctionLibrary::SpawnSystemAtLocation(
		Target->GetWorld(), NiagaraTemplate, Loc, Target->GetActorRotation());

	UGameplayStatics::PlaySoundAtLocation(Target, DissolveSFX, Loc);
	return true;
}