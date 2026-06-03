// STC_BossEnsureTargetCondition.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/StateTreeConditionBlueprintBase.h"
#include "STC_BossEnsureTargetCondition.generated.h"

/**
 * Boss-only StateTree condition that guarantees a usable engage target.
 * It can keep the current live target or pick a fresh random hostile player,
 * then teleports the boss near that target on the nav mesh so bosses cannot
 * be permanently kited out of range.
 */
UCLASS(Blueprintable, meta=(DisplayName="Boss Ensure Target"))
class AEYERJI_API USTC_BossEnsureTargetCondition : public UStateTreeConditionBlueprintBase
{
	GENERATED_BODY()

public:
	/** Optional actor tag preferred when selecting a fallback player target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boss|Target")
	FName PreferredPlayerActorTag = TEXT("Friendly");

	/** When true, only players with PreferredPlayerActorTag are eligible fallback targets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boss|Target")
	bool bRequirePreferredPlayerActorTag = false;

	/** When true, a live current target is kept instead of forcing a new random player pick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boss|Target")
	bool bUseCurrentTargetIfValid = true;

	/** When true, teleport near the selected target even if the boss already had a valid current target. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boss|Teleport")
	bool bTeleportWhenCurrentTargetIsValid = true;

	/** Desired distance from the chosen player when teleporting the boss. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boss|Teleport", meta=(ClampMin="0.0", Units="cm"))
	float TeleportDistanceFromTarget = 500.f;

	/** Number of random direction samples to try around the chosen player. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boss|Teleport", meta=(ClampMin="1"))
	int32 TeleportSampleCount = 8;

	/** Projection extents used when snapping the teleport point back onto the nav mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Boss|Teleport")
	FVector NavProjectionExtent = FVector(200.f, 200.f, 500.f);

	virtual bool TestCondition(FStateTreeExecutionContext& Context) const override;

private:
	/** Returns true when the actor should be treated as dead for boss targeting. */
	bool IsActorDead(const AActor* Actor) const;

	/** Returns true when the actor is a valid hostile fallback target for the boss. */
	bool IsEligiblePlayerTarget(class AEnemyAIController& EnemyAI, const class APawn* PlayerPawn) const;

	/** Chooses a random eligible hostile player pawn for boss fallback targeting. */
	APawn* ChooseRandomPlayerTarget(class AEnemyAIController& EnemyAI) const;

	/** Attempts to teleport the boss near the chosen player using nav-projected positions. */
	bool TeleportBossNearTarget(class APawn& BossPawn, const AActor& TargetActor) const;
};
