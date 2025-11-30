// STT_SmoothRampUpTask.cpp

#include "Enemy/Tasks/STT_SmoothRampUpTask.h"
#include "AIController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AbilitySystemComponent.h"
#include "StateTreeExecutionContext.h"

USTT_SmoothRampUpTask::USTT_SmoothRampUpTask(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    bShouldCallTick = true;
}

EStateTreeRunStatus USTT_SmoothRampUpTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult&)
{
    AAIController* AI = Cast<AAIController>(Context.GetOwner());
    ACharacter* Char = AI ? Cast<ACharacter>(AI->GetPawn()) : nullptr;
    if (!Char)
    {
        return EStateTreeRunStatus::Failed;
    }

    CachedMove = Char->GetCharacterMovement();
    if (!CachedMove.IsValid())
    {
        return EStateTreeRunStatus::Failed;
    }

    // Determine DesiredSpeed
    DesiredSpeed = TargetSpeed;
    if (bUseSpeedAttribute)
    {
        if (UAbilitySystemComponent* ASC = Char->FindComponentByClass<UAbilitySystemComponent>())
        {
            const float AttrVal = ASC->GetNumericAttribute(SpeedAttribute);
            if (AttrVal > 0.f)
            {
                DesiredSpeed = AttrVal;
            }
        }
    }
    DesiredSpeed = FMath::Max(0.f, DesiredSpeed);

    // Choose start speed
    StartSpeed = bStartFromCurrentSpeed ? CachedMove->MaxWalkSpeed : 0.f;
    StartSpeed = FMath::Max(0.f, StartSpeed);

    // If we are already at/near target, finish immediately
    if (FMath::Abs(StartSpeed - DesiredSpeed) <= FinishTolerance)
    {
        CachedMove->MaxWalkSpeed = DesiredSpeed;
        return EStateTreeRunStatus::Succeeded;
    }

    // Set initial value if starting from 0 (optional)
    if (!bStartFromCurrentSpeed)
    {
        CachedMove->MaxWalkSpeed = StartSpeed;
    }

    StartTime = Char->GetWorld() ? Char->GetWorld()->GetTimeSeconds() : 0.f;
    return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus USTT_SmoothRampUpTask::Tick(FStateTreeExecutionContext& Context, float)
{
    if (!CachedMove.IsValid())
    {
        return EStateTreeRunStatus::Failed;
    }
    UWorld* World = CachedMove->GetWorld();
    if (!World)
    {
        return EStateTreeRunStatus::Failed;
    }

    const float Now = World->GetTimeSeconds();
    const float T = FMath::Max(0.f, RampTime);
    const float Elapsed = Now - StartTime;
    const float Alpha = (T > 0.f) ? FMath::Clamp(Elapsed / T, 0.f, 1.f) : 1.f;
    const float Eased = (EasingExponent > 0.f) ? FMath::Pow(Alpha, EasingExponent) : Alpha;

    const float NewSpeed = FMath::Lerp(StartSpeed, DesiredSpeed, Eased);
    CachedMove->MaxWalkSpeed = FMath::Max(0.f, NewSpeed);

    if (Alpha >= 1.f || FMath::Abs(CachedMove->MaxWalkSpeed - DesiredSpeed) <= FinishTolerance)
    {
        CachedMove->MaxWalkSpeed = DesiredSpeed;
        return EStateTreeRunStatus::Succeeded;
    }

    return EStateTreeRunStatus::Running;
}

