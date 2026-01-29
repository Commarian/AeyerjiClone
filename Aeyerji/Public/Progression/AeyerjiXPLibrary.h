// File: Source/Aeyerji/Public/Progression/AeyerjiXPLibrary.h
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AeyerjiXPLibrary.generated.h"

class UAbilitySystemComponent;

/**
 * XP helper functions: compute scaled rewards and query player level state.
 */
UCLASS()
class AEYERJI_API UAeyerjiXPLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Highest player Level present in the world. Returns 1 when unknown/none. */
    UFUNCTION(BlueprintPure, Category="Aeyerji|XP", meta=(WorldContext="WorldContextObject"))
    static int32 GetHighestPlayerLevel(const UObject* WorldContextObject);

    /** Get an actor's Base XP reward (from Reward AttributeSet). */
    UFUNCTION(BlueprintPure, Category="Aeyerji|XP")
    static float GetBaseXPFromActor(const AActor* Actor);

    /**
     * Scales BaseXP by highest player level using a simple linear factor:
     *   XP = BaseXP * (1 + (HighestLevel - 1) * PerLevelScalar)
     * Set PerLevelScalar to 0.0 for no scaling.
     */
    UFUNCTION(BlueprintPure, Category="Aeyerji|XP", meta=(WorldContext="WorldContextObject"))
    static float ComputeScaledXPReward(const UObject* WorldContextObject,
                                       float BaseXP,
                                       float PerLevelScalar = 0.5f);

    /** Convenience: Reads BaseXP from Actor, then scales by that actor's current level and optional difficulty multiplier. */
    UFUNCTION(BlueprintPure, Category="Aeyerji|XP", meta=(WorldContext="WorldContextObject"))
    static float GetScaledXPRewardForEnemy(const UObject* WorldContextObject,
                                           const AActor* EnemyActor,
                                           float PerLevelScalar = 0.5f,
                                           float DifficultyScale = 0.f,
                                           float DifficultyMinMultiplier = 1.f,
                                           float DifficultyMaxMultiplier = 1.f);

    /** Set the Base XP value on an actor's Reward AttributeSet. */
    UFUNCTION(BlueprintCallable, Category="Aeyerji|XP")
    static void SetBaseXPOnActor(AActor* Actor, float BaseXP);

    /** Exact same as SetBaseXPOnActor, but with explicit naming for clarity in BPs. */
    UFUNCTION(BlueprintCallable, Category="Aeyerji|XP", meta=(DisplayName="Set Base Reward XP On Actor"))
    static void SetBaseRewardXPOnActor(AActor* Actor, float BaseRewardXP) { SetBaseXPOnActor(Actor, BaseRewardXP); }

    /** Apply Base Reward XP (and future tunables) from a RewardTuning asset. */
    UFUNCTION(BlueprintCallable, Category="Aeyerji|XP")
    static void ApplyRewardTuningToActor(AActor* Actor, const class UAeyerjiRewardTuning* RewardTuning);

    /**
     * Award scaled XP to all active players when an enemy dies.
     * - Computes scaled XP from the enemy via GetScaledXPRewardForEnemy.
     * - Killer receives +KillerBonusPercent; others receive -KillerBonusPercent.
     * Server-only; no-ops on clients.
     */
    UFUNCTION(BlueprintCallable, Category="Aeyerji|XP", meta=(WorldContext="WorldContextObject"))
    static void AwardXPOnEnemyDeath(const UObject* WorldContextObject,
                                    const AActor* EnemyActor,
                                    AActor* Killer);

    /**
     * On player death, give XP to nearby enemies so they can level up.
     * - Scales by the dead player's level: XP = BaseXP * (1 + (PlayerLevel-1)*PerPlayerLevelScalar)
     * - Only enemies within Radius (units) receive XP.
     * - Killer enemy gets +KillerBonusPercent; others in radius get -KillerBonusPercent.
     * Server-only; no-ops on clients.
     */
    UFUNCTION(BlueprintCallable, Category="Aeyerji|XP", meta=(WorldContext="WorldContextObject"))
    static void AwardXPToEnemiesOnPlayerDeath(const UObject* WorldContextObject,
                                              const AActor* DeadPlayer,
                                              AActor* Killer,
                                              float Radius = 2500.f);
};
