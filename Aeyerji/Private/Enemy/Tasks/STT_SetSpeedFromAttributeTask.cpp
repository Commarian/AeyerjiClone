// Fill out your copyright notice in the Description page of Project Settings.


#include "Enemy/Tasks/STT_SetSpeedFromAttributeTask.h"
#include "Enemy/Tasks/STT_SetSpeedFromAttributeTask.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "AbilitySystemComponent.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "AIController.h"
#include "StateTreeExecutionContext.h"
#include "Logging/AeyerjiLog.h"

USTT_SetSpeedFromAttributeTask::USTT_SetSpeedFromAttributeTask(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	// Default to using the WalkSpeed attribute from the project's AttributeSet
	SpeedAttribute = UAeyerjiAttributeSet::GetWalkSpeedAttribute();
}

EStateTreeRunStatus USTT_SetSpeedFromAttributeTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition)
{
	// Get the controlled character
	AAIController* AI = Cast<AAIController>(Context.GetOwner());
	ACharacter* Char = AI ? Cast<ACharacter>(AI->GetPawn()) : nullptr;
	if (!Char)
	{
		AJ_LOG(this, TEXT("STT_SetSpeedFromAttributeTask: No character to set speed from attribute on."));
		return EStateTreeRunStatus::Failed;
	}

	// Fetch the current value of the specified speed attribute from the character's ASC
	float NewSpeed = 0.f;
	if (UAbilitySystemComponent* ASC = Char->FindComponentByClass<UAbilitySystemComponent>())
	{
		NewSpeed = ASC->GetNumericAttribute(SpeedAttribute);
	} else
	{
		AJ_LOG(this, TEXT("STT_SetSpeedFromAttributeTask: No ASC found."));
	}

	// Safety clamp and apply to movement component if available
	if (UCharacterMovementComponent* MoveComp = Char->GetCharacterMovement())
	{
		MoveComp->MaxWalkSpeed = FMath::Max(50.f, NewSpeed);
		return EStateTreeRunStatus::Succeeded;
	}

	return EStateTreeRunStatus::Failed;
}
