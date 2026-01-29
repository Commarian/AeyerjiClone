// File: Source/Aeyerji/Public/Progression/AeyerjiRewardTuning.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AeyerjiRewardTuning.generated.h"

/**
 * Data-only definition of reward tuning for a unit type (enemy/player).
 * Supports a simple parent->child hierarchy with per-field override flags.
 */
UCLASS(BlueprintType)
class AEYERJI_API UAeyerjiRewardTuning : public UDataAsset
{
    GENERATED_BODY()
public:
    /** Optional parent; child inherits values where override flags are false. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Rewards")
    TObjectPtr<const UAeyerjiRewardTuning> Parent = nullptr;

    /* ---- Base XP (applies when this actor dies) ---- */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Rewards|Overrides")
    bool bOverride_BaseRewardXP = false;

    /** Base XP at player level 1. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Rewards", meta=(EditCondition="bOverride_BaseRewardXP"))
    float BaseRewardXP = 0.f;

    /* ---- Optional scalars (for convenience) ---- */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Rewards|Overrides")
    bool bOverride_PerLevelScalar = false;

    /** Scaling with the owner's level: XP = Base * (1 + (OwnerLevel-1)*PerLevelScalar) */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Rewards", meta=(ClampMin="0.0", EditCondition="bOverride_PerLevelScalar"))
    float PerLevelScalar = 0.5f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Rewards|Overrides")
    bool bOverride_DifficultyMultiplierRange = false;

    /** Difficulty multiplier applied via lerp(Min, Max, DifficultyScale) where DifficultyScale is curved 0..1. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Rewards", meta=(ClampMin="0.0", EditCondition="bOverride_DifficultyMultiplierRange"))
    float DifficultyMinMultiplier = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Rewards", meta=(ClampMin="0.0", EditCondition="bOverride_DifficultyMultiplierRange"))
    float DifficultyMaxMultiplier = 1.5f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Rewards|Overrides")
    bool bOverride_KillerBonusPercent = false;

    /** Killer gets +X%; others get -X% */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Rewards", meta=(ClampMin="0.0", EditCondition="bOverride_KillerBonusPercent"))
    float KillerBonusPercent = 1.0f;
};
