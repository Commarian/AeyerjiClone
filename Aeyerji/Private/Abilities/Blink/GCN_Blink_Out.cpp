

#include "Abilities/Blink/GCN_Blink_Out.h"
#include "NiagaraFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"

bool UGCN_Blink_Out::OnExecute_Implementation(
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