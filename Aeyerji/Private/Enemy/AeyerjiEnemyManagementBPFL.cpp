// AeyerjiEnemyManagementBPFL.cpp
#include "Enemy/AeyerjiEnemyManagementBPFL.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Logging/AeyerjiLog.h"
#include "Navigation/AeyerjiNavSafetyLibrary.h"

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
		AJ_LOG(WorldContextObject, TEXT("SpawnAndRegisterEnemyFromSet aborted: invalid context/class. Context=%s Class=%s"),
			*GetNameSafe(WorldContextObject),
			*GetNameSafe(EnemySet.EnemyClass));
		return nullptr;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		AJ_LOG(WorldContextObject, TEXT("SpawnAndRegisterEnemyFromSet aborted: invalid world or client world. WorldValid=%d NetMode=%d Class=%s"),
			World ? 1 : 0,
			World ? static_cast<int32>(World->GetNetMode()) : -1,
			*GetNameSafe(EnemySet.EnemyClass));
		return nullptr;
	}

	AJ_LOG(WorldContextObject, TEXT("SpawnAndRegisterEnemyFromSet request: Class=%s Elite=%d SkipEliteAutoScaling=%d Spawner=%s Owner=%s Instigator=%s AutoActivate=%d ApplyElite=%d ApplyAggro=%d SkipRandomEliteResolution=%d Location=%s"),
		*GetNameSafe(EnemySet.EnemyClass),
		EnemySet.bIsElite ? 1 : 0,
		EnemySet.bSkipEliteAutoScaling ? 1 : 0,
		*GetNameSafe(Spawner),
		*GetNameSafe(Owner),
		*GetNameSafe(InstigatorPawn),
		bAutoActivate ? 1 : 0,
		bApplyEliteSettings ? 1 : 0,
		bApplyAggro ? 1 : 0,
		bSkipRandomEliteResolution ? 1 : 0,
		*SpawnTransform.GetLocation().ToCompactString());

	if (Spawner)
	{
		return Spawner->SpawnRegisteredEnemyFromSet(
			EnemySet,
			SpawnTransform,
			Owner,
			InstigatorPawn,
			bApplyEliteSettings,
			bApplyAggro,
			bAutoActivate,
			bAutoActivateOnlyIfNoWaves,
			ActivationInstigator,
			ActivationController,
			bSkipRandomEliteResolution);
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	Params.Owner = Owner;
	Params.Instigator = InstigatorPawn;

	APawn* SpawnedPawn = World->SpawnActor<APawn>(EnemySet.EnemyClass, SpawnTransform, Params);
	if (!SpawnedPawn)
	{
		AJ_LOG(WorldContextObject, TEXT("SpawnAndRegisterEnemyFromSet failed: SpawnActor returned null for Class=%s Location=%s"),
			*GetNameSafe(EnemySet.EnemyClass),
			*SpawnTransform.GetLocation().ToCompactString());
		return nullptr;
	}

	FAeyerjiNavSafetyResolveParams NavParams;
	NavParams.ProjectionExtent = FVector(500.f, 500.f, 1000.f);
	NavParams.SearchRadius = 600.f;
	NavParams.GroundTraceHeight = 400.f;
	NavParams.GroundTraceDepth = 1000.f;

	FAeyerjiNavSafetyResult SpawnNavResult;
	if (!UAeyerjiNavSafetyLibrary::ResolveSafeNavLocationForPawn(WorldContextObject, SpawnedPawn->GetActorLocation(), SpawnedPawn, NavParams, SpawnNavResult))
	{
		AJ_LOG(WorldContextObject, TEXT("SpawnAndRegisterEnemyFromSet rejected off-nav spawn: Pawn=%s Class=%s Location=%s Reason=%s"),
			*GetNameSafe(SpawnedPawn),
			*GetNameSafe(EnemySet.EnemyClass),
			*SpawnedPawn->GetActorLocation().ToCompactString(),
			*SpawnNavResult.FailureReason.ToString());
		SpawnedPawn->Destroy();
		return nullptr;
	}

	if (!SpawnedPawn->GetActorLocation().Equals(SpawnNavResult.GroundedLocation, 1.f))
	{
		SpawnedPawn->SetActorLocation(
			SpawnNavResult.GroundedLocation,
			/*bSweep=*/false,
			nullptr,
			ETeleportType::TeleportPhysics);
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
		AJ_LOG(WorldContextObject, TEXT("SpawnAndRegisterEnemyFromSet spawned+registered: Pawn=%s Spawner=%s"),
			*GetNameSafe(SpawnedPawn),
			*GetNameSafe(Spawner));
	}
	else
	{
		AJ_LOG(WorldContextObject, TEXT("Enemy spawn %s skipped spawner registration; scaling/aggro may be missing."),
			*GetNameSafe(SpawnedPawn));
	}

	return SpawnedPawn;
}
