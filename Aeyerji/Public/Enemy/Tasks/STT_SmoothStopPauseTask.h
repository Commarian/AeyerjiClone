// STT_SmoothStopPauseTask.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "STT_SmoothStopPauseTask.generated.h"

/**
 * Smoothly decelerates the controlled character to a stop, then holds for a short pause.
 * Useful as a transitional state between high-tempo states (e.g., Attack -> Patrol)
 * to avoid snappy speed changes.
 */
UCLASS(Blueprintable, meta=(DisplayName="Smooth Stop + Pause"))
class AEYERJI_API USTT_SmoothStopPauseTask : public UStateTreeTaskBlueprintBase
{
    GENERATED_BODY()

public:
    USTT_SmoothStopPauseTask(const FObjectInitializer& ObjectInitializer);

    // Seconds to ramp MaxWalkSpeed down to zero.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SmoothStop")
    float DecelerationTime = 0.35f;

    // Seconds to remain stopped after reaching zero speed.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SmoothStop")
    float PauseTime = 0.40f;

    // If true, restore original MaxWalkSpeed on ExitState (the next state may also set speed).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SmoothStop")
    bool bRestoreSpeedOnExit = false;

    // Optional easing exponent applied to the decel alpha (1.0 = linear, 2.0 = ease-in, 0.5 = ease-out)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SmoothStop")
    float EasingExponent = 1.5f;

    // Minimum speed treated as stopped (cm/s)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="SmoothStop")
    float StopSpeedThreshold = 10.f;

    virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) override;
    virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) override;
    virtual void ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) override;

private:
    // Cached movement to manipulate
    TWeakObjectPtr<class UCharacterMovementComponent> CachedMove;
    float OriginalMaxWalkSpeed = 0.f;
    float DecelStartTime = 0.f;
    bool bReachedZero = false;
    float PauseStartTime = 0.f;
};
