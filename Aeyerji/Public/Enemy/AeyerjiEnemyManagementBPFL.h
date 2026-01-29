// AeyerjiEnemyManagementBPFL.h
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Director/AeyerjiSpawnerGroup.h"
#include "AeyerjiEnemyManagementBPFL.generated.h"

class AController;

/**
 * Centralized enemy spawning helpers that register with spawner groups for scaling and aggro.
 */
UCLASS()
class AEYERJI_API UAeyerjiEnemyManagementBPFL : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Spawns an enemy pawn from a set and registers it with the spawner group for scaling/elite/aggro. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Enemy", meta=(WorldContext="WorldContextObject"))
	static APawn* SpawnAndRegisterEnemyFromSet(
		UObject* WorldContextObject,
		const FEnemySet& EnemySet,
		const FTransform& SpawnTransform,
		AAeyerjiSpawnerGroup* Spawner,
		AActor* Owner = nullptr,
		APawn* InstigatorPawn = nullptr,
		bool bApplyEliteSettings = true,
		bool bApplyAggro = true,
		bool bAutoActivate = true,
		bool bAutoActivateOnlyIfNoWaves = true,
		AActor* ActivationInstigator = nullptr,
		AController* ActivationController = nullptr,
		bool bSkipRandomEliteResolution = false);
};
