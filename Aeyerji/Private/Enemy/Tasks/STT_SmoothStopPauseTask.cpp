// STT_SmoothStopPauseTask.cpp

#include "Enemy/Tasks/STT_SmoothStopPauseTask.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "AIController.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "GameFramework/Character.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "GameFramework/CharacterMovementComponent.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "StateTreeExecutionContext.h"

USTT_SmoothStopPauseTask::USTT_SmoothStopPauseTask(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    bShouldCallTick = true;
}

EStateTreeRunStatus USTT_SmoothStopPauseTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult&)
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

	OriginalMaxWalkSpeed = CachedMove->MaxWalkSpeed;
	if (OriginalMaxWalkSpeed <= 1.f)
	{
		const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Char, /*LookForComponent=*/true);
		const float AttributeSpeed = ASC ? ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetWalkSpeedAttribute()) : 0.f;
		OriginalMaxWalkSpeed = FMath::Max(50.f, AttributeSpeed);
	}
    DecelStartTime = Char->GetWorld() ? Char->GetWorld()->GetTimeSeconds() : 0.f;
    bReachedZero = false;
    PauseStartTime = 0.f;

    // Ensure we start decelerating immediately. Do not snap yet; Tick will ramp down.
    return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus USTT_SmoothStopPauseTask::Tick(FStateTreeExecutionContext& Context, float)
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

    if (!bReachedZero)
    {
        const float T = FMath::Max(0.f, DecelerationTime);
        const float Elapsed = Now - DecelStartTime;
        const float Alpha = (T > 0.f) ? FMath::Clamp(Elapsed / T, 0.f, 1.f) : 1.f;
        const float Eased = (EasingExponent > 0.f) ? FMath::Pow(Alpha, EasingExponent) : Alpha;
        const float NewSpeed = FMath::Lerp(OriginalMaxWalkSpeed, 0.f, Eased);
        CachedMove->MaxWalkSpeed = FMath::Max(0.f, NewSpeed);

        // If we’re effectively stopped, clamp velocity as well and start the pause
        if (Alpha >= 1.f || CachedMove->Velocity.Size2D() <= StopSpeedThreshold)
        {
            // Stop residual movement
            CachedMove->StopMovementImmediately();
            CachedMove->MaxWalkSpeed = 0.f;
            bReachedZero = true;
            PauseStartTime = Now;
        }
        return EStateTreeRunStatus::Running;
    }

    // Hold still for PauseTime seconds
    if ((Now - PauseStartTime) < FMath::Max(0.f, PauseTime))
    {
        return EStateTreeRunStatus::Running;
    }

    return EStateTreeRunStatus::Succeeded;
}

void USTT_SmoothStopPauseTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult&)
{
    if (!CachedMove.IsValid())
    {
        return;
    }
	// Always restore a usable speed. Serialized Blueprint defaults may still contain the old false flag.
	CachedMove->MaxWalkSpeed = FMath::Max(50.f, OriginalMaxWalkSpeed);
}
