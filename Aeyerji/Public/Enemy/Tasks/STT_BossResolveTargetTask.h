#pragma once

#include "CoreMinimal.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "STT_BossResolveTargetTask.generated.h"

/**
 * Boss recovery task that resolves a usable combat target in one step.
 * It prefers the current target, then the controller's last-known target actor,
 * and finally falls back to a random hostile player pawn before teleporting
 * the boss near the resolved target on the nav mesh.
 */
UCLASS(Blueprintable, meta=(DisplayName="Boss Resolve Target"))
class AEYERJI_API USTT_BossResolveTargetTask : public UStateTreeTaskBlueprintBase
{
	GENERATED_BODY()

public:
	/** Optional actor tag preferred when selecting a random fallback player target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boss|Target")
	FName PreferredPlayerActorTag = TEXT("Friendly");

	/** When true, only players with PreferredPlayerActorTag are eligible random fallback targets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boss|Target")
	bool bRequirePreferredPlayerActorTag = false;

	/** When true, the task tries the controller's remembered target actor before random player fallback. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boss|Target")
	bool bUseLastKnownTargetActor = true;

	/** Desired distance from the resolved target when teleporting the boss. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boss|Teleport", meta=(ClampMin="0.0", Units="cm"))
	float TeleportDistanceFromTarget = 500.f;

	/** Number of random direction samples to try around the resolved target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boss|Teleport", meta=(ClampMin="1"))
	int32 TeleportSampleCount = 8;

	/** Projection extents used when snapping teleport destinations back onto the nav mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boss|Teleport")
	FVector NavProjectionExtent = FVector(200.f, 200.f, 500.f);

	virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) override;

private:
	/** Returns true when the actor should be treated as dead for boss targeting. */
	bool IsActorDead(const AActor* Actor) const;

	/** Returns true when the pawn is a live hostile player candidate for random fallback selection. */
	bool IsEligibleRandomPlayerTarget(class AEnemyAIController& EnemyAI, const class APawn* PlayerPawn) const;

	/** Chooses a random eligible hostile player pawn for the random fallback step. */
	APawn* ChooseRandomPlayerTarget(class AEnemyAIController& EnemyAI) const;

	/** Attempts to teleport the boss near the resolved target using nav-projected points. */
	bool TeleportBossNearTarget(class APawn& BossPawn, const AActor& TargetActor) const;
};
