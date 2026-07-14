#include "Enemy/Tasks/STC_AscHasTagCondition.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "AIController.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "Logging/AeyerjiLog.h"
#include "StateTreeExecutionContext.h"

bool USTC_AscHasTagCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	AActor* OwnerActor = Cast<AActor>(Context.GetOwner());
	AActor* TargetActor = AscOwner;
	if (!TargetActor)
	{
		TargetActor = OwnerActor;
	}

	APawn* Pawn = Cast<APawn>(TargetActor);
	if (!Pawn)
	{
		if (AAIController* AI = Cast<AAIController>(TargetActor))
		{
			Pawn = AI->GetPawn();
		}
	}

	if (!Pawn)
	{
		AJ_LOG(this, TEXT("STC_AscHasTag: no pawn (owner=%s)"), *GetNameSafe(OwnerActor));
		return false;
	}

	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn);
	if (!ASC)
	{
		AJ_LOG(this, TEXT("STC_AscHasTag: missing ASC on pawn %s"), *GetNameSafe(Pawn));
		return false;
	}

	if (Tags.IsEmpty())
	{
		AJ_LOG(this, TEXT("STC_AscHasTag: empty tag container on pawn %s ASC=%s"), *GetNameSafe(Pawn), *GetNameSafe(ASC));
		return false;
	}

	FGameplayTagContainer Owned;
	ASC->GetOwnedGameplayTags(Owned);

	bool bMatch = false;
	if (bMatchExactly)
	{
		bMatch = bMatchAll ? Owned.HasAllExact(Tags) : Owned.HasAnyExact(Tags);
	}
	else if (bMatchAll)
	{
		bMatch = Owned.HasAll(Tags);
	}
	else
	{
		bMatch = Owned.HasAny(Tags);
	}
	return bMatch;
}
