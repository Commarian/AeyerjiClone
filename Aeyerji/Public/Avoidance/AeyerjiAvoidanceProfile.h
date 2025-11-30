// AeyerjiAvoidanceProfile.h
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AeyerjiAvoidanceProfile.generated.h"

/**
 * Data asset holding tuning for short avoidance and RVO.
 * Create instances per map (Calm/Tight/Crowded) and assign in GameMode or PlayerController.
 */
UCLASS(BlueprintType)
class AEYERJI_API UAeyerjiAvoidanceProfile : public UDataAsset
{
    GENERATED_BODY()
public:
    // PlayerController short avoidance
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PC|Avoidance")
    bool  bEnableShortAvoidance = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PC|Avoidance")
    float ProbeDistance = 160.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PC|Avoidance")
    float SideStepDistance = 220.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PC|Avoidance")
    float ProbeRadiusScale = 1.05f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PC|Avoidance")
    float HoldTimeMin = 0.22f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PC|Avoidance")
    float HoldTimeMax = 0.40f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PC|Avoidance")
    bool  bSkipWhenBlockingIsCurrentTarget = false;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PC|Avoidance")
    bool  bBiasDetourAroundTargetTangent = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PC|Avoidance")
    bool  bProjectToNavmesh = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PC|Avoidance")
    float MinDistanceToGoal = 200.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PC|Avoidance")
    float MinSpeedCmPerSec = 60.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PC|Avoidance")
    float MinTimeBetweenTriggers = 0.35f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PC|Avoidance")
    float EarlyReleaseDistance = 120.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PC|Avoidance")
    float BlockedNudgeScale = 0.6f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="PC|Avoidance|Debug")
    bool  bDebugDraw = false;

    // RVO avoidance defaults
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement|RVO")
    bool  bEnableRVO_Player = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement|RVO")
    bool  bEnableRVO_Enemies = true;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement|RVO")
    float RVOConsiderationRadius = 280.f;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Movement|RVO")
    float RVOAvoidanceWeight = 0.55f;
};

