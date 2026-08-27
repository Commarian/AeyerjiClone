// Fill out your copyright notice in the Description page of Project Settings.


#include "Enemy/Tasks/STT_SetSpeedFromAttributeTask.h"
#include "AbilitySystemGlobals.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "GameFramework/Character.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "GameFramework/CharacterMovementComponent.h"
#include "Attributes/AeyerjiAttributeSet.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "AIController.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "StateTreeExecutionContext.h"
#include "Logging/AeyerjiLog.h"

USTT_SetSpeedFromAttributeTask::USTT_SetSpeedFromAttributeTask(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	// Default to using the WalkSpeed attribute from the project's AttributeSet
	SpeedAttribute = UAeyerjiAttributeSet::GetWalkSpeedAttribute();
}

EStateTreeRunStatus USTT_SetSpeedFromAttributeTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& /*Transition*/)
{
	// Get the controlled character
	AAIController* AI = Cast<AAIController>(Context.GetOwner());
	ACharacter* Char = AI ? Cast<ACharacter>(AI->GetPawn()) : nullptr;
	if (!Char)
	{
		AJ_LOG(this, TEXT("STT_SetSpeedFromAttributeTask: No character to set speed from attribute on."));
		return EStateTreeRunStatus::Failed;
	}

	if (!SpeedAttribute.IsValid())
	{
		AJ_LOG(this, TEXT("STT_SetSpeedFromAttributeTask: SpeedAttribute is invalid."));
		return EStateTreeRunStatus::Failed;
	}

	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Char);
	if (!ASC)
	{
		AJ_LOG(this, TEXT("STT_SetSpeedFromAttributeTask: No ASC found."));
		return EStateTreeRunStatus::Failed;
	}

	const float NewSpeed = ASC->GetNumericAttribute(SpeedAttribute);
	if (UCharacterMovementComponent* MoveComp = Char->GetCharacterMovement())
	{
		MoveComp->MaxWalkSpeed = FMath::Max(50.f, NewSpeed);
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Failed;
}
