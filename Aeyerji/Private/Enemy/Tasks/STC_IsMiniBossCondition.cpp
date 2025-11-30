#include "Enemy/Tasks/STC_IsMiniBossCondition.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AIController.h"
#include "StateTreeExecutionContext.h"

bool USTC_IsMiniBossCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	AActor* OwnerActor = Cast<AActor>(Context.GetOwner());
	APawn* ControlledPawn = Cast<APawn>(OwnerActor);

	if (!ControlledPawn)
	{
		if (AAIController* AIController = Cast<AAIController>(OwnerActor))
		{
			ControlledPawn = AIController->GetPawn();
		}
	}

	if (!ControlledPawn)
	{
		return false;
	}

	bool bIsMiniBoss = false;

	if (!MiniBossActorTag.IsNone() && ControlledPawn->Tags.Contains(MiniBossActorTag))
	{
		bIsMiniBoss = true;
	}

	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(ControlledPawn);
	if (ASC && MiniBossGameplayTag.IsValid() && ASC->HasMatchingGameplayTag(MiniBossGameplayTag))
	{
		bIsMiniBoss = true;
	}

	if (RequiredAbility)
	{
		const bool bHasAbility = ASC && ASC->FindAbilitySpecFromClass(RequiredAbility);
		if (bRequireAbility && !bHasAbility)
		{
			return false;
		}

		bIsMiniBoss = bIsMiniBoss || bHasAbility;
	}

	return bIsMiniBoss;
}
