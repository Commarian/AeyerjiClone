// STC_IsAliveCondition.cpp

#include "Enemy/Tasks/STC_IsAliveCondition.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "AIController.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AeyerjiGameplayTags.h"
#include "GameFramework/Pawn.h"
#include "StateTreeExecutionContext.h"

bool FSTC_IsAliveCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const AAIController* AI = Cast<AAIController>(Context.GetOwner());
	const APawn* Pawn = AI ? AI->GetPawn() : nullptr;
	if (!IsValid(Pawn))
	{
		return false;
	}

	// Mirrors the dead-state definition in EnemyAIController (HasDeadStateTag):
	// State.Dead on actor tags or matching on the ASC means dead.
	const FGameplayTag& DeadTag = AeyerjiTags::State_Dead;
	if (!DeadTag.IsValid())
	{
		return true;
	}
	if (Pawn->Tags.Contains(DeadTag.GetTagName()))
	{
		return false;
	}
	if (const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn, /*LookForComponent=*/true))
	{
		return !ASC->HasMatchingGameplayTag(DeadTag);
	}
	return true;
}
