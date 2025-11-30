// STT_SmoothRampUpTask.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/StateTreeTaskBlueprintBase.h"
#include "GameplayEffectTypes.h" // FGameplayAttribute
#include "STT_SmoothRampUpTask.generated.h"

/**
 * Smoothly ramps the character's MaxWalkSpeed up to a target speed over time.
 * Target speed can be read from a Gameplay Attribute or specified as a fixed value.
 */
UCLASS(Blueprintable, meta=(DisplayName="Smooth Ramp Up Speed"))
class AEYERJI_API USTT_SmoothRampUpTask : public UStateTreeTaskBlueprintBase
{
    GENERATED_BODY()

public:
    USTT_SmoothRampUpTask(const FObjectInitializer& ObjectInitializer);

    // If true, DesiredSpeed is read from SpeedAttribute on the pawn's ASC.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RampUp")
    bool bUseSpeedAttribute = true;

    // Attribute to read when bUseSpeedAttribute is true (e.g., WalkSpeed).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RampUp", meta=(EditCondition="bUseSpeedAttribute"))
    FGameplayAttribute SpeedAttribute;

    // Fallback/explicit target speed if not using attribute (cm/s).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RampUp", meta=(EditCondition="!bUseSpeedAttribute"))
    float TargetSpeed = 500.f;

    // If true, start ramp from current MaxWalkSpeed; otherwise start at 0.
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RampUp")
    bool bStartFromCurrentSpeed = false;

    // Total time to reach the target speed (seconds).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RampUp")
    float RampTime = 0.35f;

    // Easing exponent applied to Alpha (1=linear, 2=ease-in, 0.5=ease-out).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RampUp")
    float EasingExponent = 1.5f;

    // Consider finished if within this tolerance of DesiredSpeed (cm/s).
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="RampUp")
    float FinishTolerance = 5.f;

    virtual EStateTreeRunStatus EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition) override;
    virtual EStateTreeRunStatus Tick(FStateTreeExecutionContext& Context, float DeltaTime) override;

private:
    TWeakObjectPtr<class UCharacterMovementComponent> CachedMove;
    float StartSpeed = 0.f;
    float DesiredSpeed = 0.f;
    float StartTime = 0.f;
};

