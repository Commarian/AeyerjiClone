// Copyright (c) 2025 Aeyerji.
#pragma once

#include "CoreMinimal.h"
#include "TimerManager.h"
#include "GameFramework/Actor.h"
#include "AeyerjiEndRunPortal.generated.h"

class USceneComponent;
class USphereComponent;
class UPrimitiveComponent;
class APawn;

/**
 * Replicated extraction portal used to finalize a successful run after the player reaches it.
 */
UCLASS(Blueprintable)
class AEYERJI_API AAeyerjiEndRunPortal : public AActor
{
	GENERATED_BODY()

public:
	AAeyerjiEndRunPortal();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UFUNCTION()
	void HandlePortalOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex,
		bool bFromSweep,
		const FHitResult& SweepResult);

	UFUNCTION()
	void HandlePortalEndOverlap(
		UPrimitiveComponent* OverlappedComponent,
		AActor* OtherActor,
		UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

	/** Starts a server-authoritative extraction countdown for the overlapping player. */
	void BeginExtractionCountdown(APawn* OverlappingPawn);

	/** Clears the active extraction countdown and optionally notifies the portal BP. */
	void ResetExtractionCountdown(APawn* ExpectedPawn = nullptr, bool bNotifyBlueprintReset = true);

	/** Restarts the countdown for another player already standing inside the portal. */
	void TryBeginCountdownFromCurrentOverlaps();

	/** Finalizes extraction if the player remained inside the portal for the full countdown. */
	void CompleteExtractionCountdown();

	/** Optional BP hook for portal visuals when the countdown starts. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|Portal")
	void BP_OnExtractionCountdownStarted(APawn* ExtractingPawn, float DurationSeconds);

	/** Optional BP hook for portal visuals when the countdown is canceled by leaving the overlap. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|Portal")
	void BP_OnExtractionCountdownReset(APawn* PreviousPawn);

	/** Optional BP hook for portal visuals when extraction succeeds. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|Portal")
	void BP_OnExtractionCountdownCompleted(APawn* ExtractingPawn);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Portal")
	TObjectPtr<USceneComponent> SceneRoot = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Portal")
	TObjectPtr<USphereComponent> InteractionSphere = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Portal", meta=(ClampMin="0.1"))
	float ExtractionDurationSeconds = 3.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Aeyerji|Portal")
	TObjectPtr<APawn> PendingExtractingPawn = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Portal")
	bool bConsumed = false;

	FTimerHandle ExtractionCountdownTimerHandle;
};
