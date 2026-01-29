// AeyerjiEnemyManagementBPFL.cpp
#include "Enemy/AeyerjiEnemyManagementBPFL.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Logging/AeyerjiLog.h"

APawn* UAeyerjiEnemyManagementBPFL::SpawnAndRegisterEnemyFromSet(
	UObject* WorldContextObject,
	const FEnemySet& EnemySet,
	const FTransform& SpawnTransform,
	AAeyerjiSpawnerGroup* Spawner,
	AActor* Owner,
	APawn* InstigatorPawn,
	bool bApplyEliteSettings,
	bool bApplyAggro,
	bool bAutoActivate,
	bool bAutoActivateOnlyIfNoWaves,
	AActor* ActivationInstigator,
	AController* ActivationController,
	bool bSkipRandomEliteResolution)
{
	if (!WorldContextObject || !EnemySet.EnemyClass)
	{
		return nullptr;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	Params.Owner = Owner;
	Params.Instigator = InstigatorPawn;

	APawn* SpawnedPawn = World->SpawnActor<APawn>(EnemySet.EnemyClass, SpawnTransform, Params);
	if (!SpawnedPawn)
	{
		return nullptr;
	}

	if (Spawner)
	{
		Spawner->RegisterExternalEnemy(
			SpawnedPawn,
			EnemySet,
			bApplyEliteSettings,
			bApplyAggro,
			bAutoActivate,
			bAutoActivateOnlyIfNoWaves,
			ActivationInstigator,
			ActivationController,
			bSkipRandomEliteResolution);
	}
	else
	{
		AJ_LOG(WorldContextObject, TEXT("Enemy spawn %s skipped spawner registration; scaling/aggro may be missing."),
			*GetNameSafe(SpawnedPawn));
	}

	return SpawnedPawn;
}
