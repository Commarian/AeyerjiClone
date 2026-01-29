#include "Enemy/Tasks/STC_IsMiniBossCondition.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AIController.h"
#include "StateTreeExecutionContext.h"
#include "Logging/AeyerjiLog.h"

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
		AJ_LOG(this, TEXT("STC_IsMiniBossCondition: no pawn (owner=%s)"), *GetNameSafe(OwnerActor));
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
	else if (MiniBossGameplayTag.IsValid())
	{
		AJ_LOG(this, TEXT("STC_IsMiniBossCondition: ASC missing tag %s (pawn=%s ASC=%s)"),
			*MiniBossGameplayTag.ToString(), *GetNameSafe(ControlledPawn), *GetNameSafe(ASC));
	}
	else
	{
		AJ_LOG(this, TEXT("STC_IsMiniBossCondition: no ASC tag configured (pawn=%s ASC=%s)"),
			*GetNameSafe(ControlledPawn), *GetNameSafe(ASC));
	}

	if (RequiredAbility)
	{
		const bool bHasAbility = ASC && ASC->FindAbilitySpecFromClass(RequiredAbility);
		if (bRequireAbility && !bHasAbility)
		{
			AJ_LOG(this, TEXT("STC_IsMiniBossCondition: required ability %s missing"), *GetNameSafe(RequiredAbility));
			return false;
		}

		bIsMiniBoss = bIsMiniBoss || bHasAbility;
		if (!bHasAbility && !bRequireAbility)
		{
			AJ_LOG(this, TEXT("STC_IsMiniBossCondition: ability %s not found but not required"), *GetNameSafe(RequiredAbility));
		}
	}

	if (!bIsMiniBoss)
	{
		AJ_LOG(this, TEXT("STC_IsMiniBossCondition: returning FALSE (tag=%s actorTag=%s)"),
			*MiniBossGameplayTag.ToString(), *MiniBossActorTag.ToString());
	}

	return bIsMiniBoss;
}
