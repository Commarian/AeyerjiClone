// STT_SmoothRampDownTask.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "GameplayEffectTypes.h" // FGameplayAttribute
#include "STT_SmoothRampDownTask.generated.h"

/**
 * Smoothly ramps the character's MaxWalkSpeed down to a non-zero target speed
 * (e.g., an "alert" walk) over a short duration.
 */
UCLASS(Blueprintable, meta=(DisplayName="Smooth Ramp Down Speed"))
class AEYERJI_API USTT_SmoothRampDownTask : public UStateTreeTaskBlueprintBase
{
    GENERATED_BODY()

public:
    USTT_SmoothRampDownTask(const FObjectInitializer& ObjectInitializer);

    // Read target speed from attribute if true; otherwise use TargetSpeed.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RampDown")
    bool bUseSpeedAttribute = true;

    // Attribute to read when bUseSpeedAttribute is true (e.g., WalkSpeed_Alert / CombatWalk).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RampDown", meta=(EditCondition="bUseSpeedAttribute"))
    FGameplayAttribute SpeedAttribute;

    // Explicit target speed if not using attribute (cm/s).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RampDown", meta=(EditCondition="!bUseSpeedAttribute"))
    float TargetSpeed = 300.f;

    // Start from current MaxWalkSpeed (recommended). If false, start from StartSpeedOverride.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RampDown")
    bool bStartFromCurrentSpeed = true;

    // Used only if bStartFromCurrentSpeed is false.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RampDown", meta=(EditCondition="!bStartFromCurrentSpeed"))
    float StartSpeedOverride = 600.f;

    // Total time to reach the target speed (seconds).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RampDown")
    float RampTime = 0.30f;

    // Easing exponent applied to Alpha (1=linear, 2=ease-in, 0.5=ease-out).
    // For deceleration a value around 1.2–1.6 reads nicely.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RampDown")
    float EasingExponent = 1.4f;

    // Consider finished if within this tolerance of DesiredSpeed (cm/s).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RampDown")
    float FinishTolerance = 5.f;

    virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) override;
    virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) override;

private:
    TWeakObjectPtr<class UCharacterMovementComponent> CachedMove;
    float StartSpeed = 0.f;
    float DesiredSpeed = 0.f;
    float StartTime = 0.f;
};

