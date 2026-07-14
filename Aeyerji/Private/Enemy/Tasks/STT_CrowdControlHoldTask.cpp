#include "Enemy/Tasks/STT_CrowdControlHoldTask.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "AIController.h"
#include "GameFramework/Character.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "GameFramework/MovementComponent.h"
#include "GameFramework/Pawn.h"
#include "StateTreeExecutionContext.h"

namespace
{
	APawn* ResolveCrowdControlHoldPawn(FStateTreeExecutionContext& Context, AAIController*& OutAI)
	{
		OutAI = Cast<AAIController>(Context.GetOwner());
		if (OutAI)
		{
			return OutAI->GetPawn();
		}

		APawn* Pawn = Cast<APawn>(Context.GetOwner());
		OutAI = Pawn ? Cast<AAIController>(Pawn->GetController()) : nullptr;
		return Pawn;
	}
}

USTT_CrowdControlHoldTask::USTT_CrowdControlHoldTask(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bShouldCallTick = true;
}

EStateTreeRunStatus USTT_CrowdControlHoldTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition)
{
	AAIController* AI = nullptr;
	APawn* Pawn = ResolveCrowdControlHoldPawn(Context, AI);
	if (!Pawn)
	{
		return EStateTreeRunStatus::Failed;
	}

	CachedAI = AI;
	CachedPawn = Pawn;

	UAbilitySystemComponent* ASC = ResolveAbilitySystemComponent();
	if (!ASC || ActiveTags.IsEmpty())
	{
		return EStateTreeRunStatus::Failed;
	}

	if (!HasActiveCrowdControlTags(*ASC))
	{
		return EStateTreeRunStatus::Succeeded;
	}

	if (bCancelAbilitiesOnAuthority && Pawn->HasAuthority())
	{
		ASC->CancelAbilities(nullptr, nullptr, nullptr);
	}

	if (bStopMovementOnEnter || bClearFocusOnEnter)
	{
		StopMovementAndFocus(bClearFocusOnEnter);
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus USTT_CrowdControlHoldTask::Tick(FStateTreeExecutionContext& Context, float DeltaTime)
{
	UAbilitySystemComponent* ASC = ResolveAbilitySystemComponent();
	if (!ASC)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (!HasActiveCrowdControlTags(*ASC))
	{
		return EStateTreeRunStatus::Succeeded;
	}

	if (bKeepMovementStoppedOnTick)
	{
		StopMovementAndFocus(/*bClearFocus=*/true);
	}

	return EStateTreeRunStatus::Running;
}

void USTT_CrowdControlHoldTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition)
{
	if (AAIController* AI = CachedAI.Get())
	{
		AI->ClearFocus(EAIFocusPriority::Gameplay);
		AI->ClearFocus(EAIFocusPriority::Move);
	}

	CachedAI.Reset();
	CachedPawn.Reset();
	Super::ExitState(Context, Transition);
}

UAbilitySystemComponent* USTT_CrowdControlHoldTask::ResolveAbilitySystemComponent() const
{
	APawn* Pawn = CachedPawn.Get();
	return Pawn ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn, /*LookForComponent=*/true) : nullptr;
}

bool USTT_CrowdControlHoldTask::HasActiveCrowdControlTags(const UAbilitySystemComponent& ASC) const
{
	FGameplayTagContainer OwnedTags;
	ASC.GetOwnedGameplayTags(OwnedTags);

	if (bMatchExactly)
	{
		return bMatchAll ? OwnedTags.HasAllExact(ActiveTags) : OwnedTags.HasAnyExact(ActiveTags);
	}

	return bMatchAll ? OwnedTags.HasAll(ActiveTags) : OwnedTags.HasAny(ActiveTags);
}

void USTT_CrowdControlHoldTask::StopMovementAndFocus(bool bClearFocus) const
{
	if (AAIController* AI = CachedAI.Get())
	{
		AI->StopMovement();
		if (bClearFocus)
		{
			AI->ClearFocus(EAIFocusPriority::Gameplay);
			AI->ClearFocus(EAIFocusPriority::Move);
		}
	}

	if (APawn* Pawn = CachedPawn.Get())
	{
		if (UMovementComponent* MovementComponent = Pawn->GetMovementComponent())
		{
			MovementComponent->StopMovementImmediately();
		}
	}
}
