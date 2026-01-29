// STT_CycleOwnedAbilitiesTask.cpp

#include "Enemy/Tasks/STT_CycleOwnedAbilitiesTask.h"

#include "AbilitySystemComponent.h"
#include "AIController.h"
#include "Algo/RandomShuffle.h"
#include "Enemy/EnemyAIController.h"
#include "GameFramework/Pawn.h"
#include "GameplayAbilitySpec.h"
#include "StateTreeExecutionContext.h"
#include "Abilities/GameplayAbility.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "GameplayTagContainer.h"
#include "Logging/AeyerjiLog.h"

USTT_CycleOwnedAbilitiesTask::USTT_CycleOwnedAbilitiesTask(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// Task ticks so we can wait for the active ability to finish.
	bShouldCallTick = true;

	// Default ignore: death ability tag so we don't try to activate cleanup abilities.
	if (const FGameplayTag DeathTag = FGameplayTag::RequestGameplayTag(TEXT("Ability.Death"), /*ErrorIfNotFound=*/false); DeathTag.IsValid())
	{
		IgnoreAbilityTags.AddTag(DeathTag);
	}
}

EStateTreeRunStatus USTT_CycleOwnedAbilitiesTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition)
{
	PendingAbilityOrder.Reset();
	ActiveAbilityHandle = FGameplayAbilitySpecHandle();
	PendingEndedHandle = FGameplayAbilitySpecHandle();
	bHasPendingEndData = false;
	bPendingEndDataWasCancelled = false;
	AJ_LOG(this, TEXT("CycleAbilities: EnterState reset (randomize=%s)"),
		bRandomizeAbilityOrder ? TEXT("true") : TEXT("false"));
	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus USTT_CycleOwnedAbilitiesTask::Tick(FStateTreeExecutionContext& Context, float DeltaTime)
{
	AAIController* AI = Cast<AAIController>(Context.GetOwner());
	APawn* Pawn = AI ? AI->GetPawn() : nullptr;
	if (!Pawn)
	{
		AJ_LOG(this, TEXT("CycleAbilities: no pawn/owner - failing"));
		return EStateTreeRunStatus::Failed;
	}

	UAbilitySystemComponent* ASC = Pawn->FindComponentByClass<UAbilitySystemComponent>();
	if (!ASC)
	{
		AJ_LOG(this, TEXT("CycleAbilities: no ASC on %s - failing"), *GetNameSafe(Pawn));
		return EStateTreeRunStatus::Failed;
	}

	RegisterASC(*ASC);

	// If an ability is already running, wait for it to complete.
	if (ActiveAbilityHandle.IsValid())
	{
		if (IsSpecActive(*ASC, ActiveAbilityHandle))
		{
			return EStateTreeRunStatus::Running;
		}

		const bool bEndedForUs = bHasPendingEndData && PendingEndedHandle == ActiveAbilityHandle;
		const bool bEndedCancelled = bEndedForUs ? bPendingEndDataWasCancelled : false;
		if (bEndedForUs)
		{
			bHasPendingEndData = false;
			PendingEndedHandle = FGameplayAbilitySpecHandle();
		}

		FGameplayAbilitySpecHandle CompletedHandle = ActiveAbilityHandle;
		ActiveAbilityHandle = FGameplayAbilitySpecHandle();

		if (!bEndedCancelled)
		{
			LastSuccessfulAbilityHandle = CompletedHandle;
			return EStateTreeRunStatus::Succeeded;
		}

		// Cancelled/failed -> try the next ability.
	}

	if (PendingAbilityOrder.Num() == 0)
	{
		BuildAbilityQueue(*ASC);
		if (PendingAbilityOrder.Num() == 0)
		{
			AJ_LOG(this, TEXT("CycleAbilities: no abilities after building queue"));
			return bFailWhenNoAbilityCouldActivate ? EStateTreeRunStatus::Failed : EStateTreeRunStatus::Succeeded;
		}
	}

	AActor* TargetActor = nullptr;
	if (AEnemyAIController* EnemyAI = Cast<AEnemyAIController>(AI))
	{
		TargetActor = EnemyAI->GetTargetActor();
	}
	if (bPassTargetActorAsEventData && !TargetActor)
	{
		AJ_LOG(this, TEXT("CycleAbilities: requires target but none set - failing"));
		return EStateTreeRunStatus::Failed;
	}

	while (PendingAbilityOrder.Num() > 0)
	{
		const FGameplayAbilitySpecHandle NextHandle = PendingAbilityOrder[0];
		PendingAbilityOrder.RemoveAt(0, 1, EAllowShrinking::No);

		FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(NextHandle);
		if (!Spec || !Spec->Ability)
		{
			AJ_LOG(this, TEXT("CycleAbilities: skipping invalid spec handle"));
			continue;
		}

		if (!DoesSpecPassFilters(*Spec))
		{
			AJ_LOG(this, TEXT("CycleAbilities: filtered out %s"), *Spec->Ability->GetName());
			continue;
		}

		if (!TryActivateSpec(*ASC, NextHandle, Pawn, TargetActor))
		{
			AJ_LOG(this, TEXT("CycleAbilities: activation failed for %s"), *Spec->Ability->GetName());
			continue; // try the next one
		}

		// Success path: either running (wait) or ended instantly (treat as success).
		if (IsSpecActive(*ASC, NextHandle))
		{
			AJ_LOG(this, TEXT("CycleAbilities: activated %s (running)"), *Spec->Ability->GetName());
			ActiveAbilityHandle = NextHandle;
			return EStateTreeRunStatus::Running;
		}

		AJ_LOG(this, TEXT("CycleAbilities: activated %s (instant end)"), *Spec->Ability->GetName());
		LastSuccessfulAbilityHandle = NextHandle;
		return EStateTreeRunStatus::Succeeded;
	}

	// Exhausted the pool this tick.
	AJ_LOG(this, TEXT("CycleAbilities: exhausted pool (count=%d) - %s"), PendingAbilityOrder.Num(), bFailWhenNoAbilityCouldActivate ? TEXT("failing") : TEXT("succeeding"));
	return bFailWhenNoAbilityCouldActivate ? EStateTreeRunStatus::Failed : EStateTreeRunStatus::Succeeded;
}

void USTT_CycleOwnedAbilitiesTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition)
{
	UnregisterASC();
	PendingAbilityOrder.Reset();
	ActiveAbilityHandle = FGameplayAbilitySpecHandle();
	PendingEndedHandle = FGameplayAbilitySpecHandle();
	bHasPendingEndData = false;
	bPendingEndDataWasCancelled = false;
	Super::ExitState(Context, Transition);
}

void USTT_CycleOwnedAbilitiesTask::BuildAbilityQueue(UAbilitySystemComponent& ASC)
{
	PendingAbilityOrder.Reset();
	TArray<FGameplayAbilitySpecHandle> Handles;

	const TArray<FGameplayAbilitySpec>& Specs = ASC.GetActivatableAbilities();
	Handles.Reserve(Specs.Num());
	AJ_LOG(this, TEXT("CycleAbilities: building queue from %d specs"), Specs.Num());

	for (const FGameplayAbilitySpec& Spec : Specs)
	{
		if (!Spec.Handle.IsValid() || !Spec.Ability)
		{
			AJ_LOG(this, TEXT("CycleAbilities: ignoring spec with invalid handle/ability"));
			continue;
		}

		if (!DoesSpecPassFilters(Spec))
		{
			AJ_LOG(this, TEXT("CycleAbilities: filtered out %s during build"), *Spec.Ability->GetName());
			continue;
		}

		Handles.Add(Spec.Handle);
	}

	if (Handles.Num() <= 1)
	{
		AJ_LOG(this, TEXT("CycleAbilities: queue size %d (no rotation/randomization)"), Handles.Num());
		PendingAbilityOrder = MoveTemp(Handles);
		return;
	}

	if (bRandomizeAbilityOrder)
	{
		Algo::RandomShuffle(Handles);

		// Avoid repeating the previous successful ability first if possible.
		if (LastSuccessfulAbilityHandle.IsValid() && Handles.Num() > 1)
		{
			const int32 LastIdx = Handles.IndexOfByKey(LastSuccessfulAbilityHandle);
			if (LastIdx != INDEX_NONE && LastIdx != Handles.Num() - 1)
			{
				Handles.Swap(LastIdx, Handles.Num() - 1);
			}
		}
	}
	else if (LastSuccessfulAbilityHandle.IsValid())
	{
		const int32 LastIndex = Handles.IndexOfByKey(LastSuccessfulAbilityHandle);
		if (LastIndex != INDEX_NONE && Handles.Num() > 1)
		{
			TArray<FGameplayAbilitySpecHandle> Rotated;
			Rotated.Reserve(Handles.Num());

			for (int32 Offset = 1; Offset <= Handles.Num(); ++Offset)
			{
				const int32 Index = (LastIndex + Offset) % Handles.Num();
				Rotated.Add(Handles[Index]);
			}

			Handles = MoveTemp(Rotated);
		}
	}

	PendingAbilityOrder = MoveTemp(Handles);
	AJ_LOG(this, TEXT("CycleAbilities: final queue size %d"), PendingAbilityOrder.Num());
}

bool USTT_CycleOwnedAbilitiesTask::DoesSpecPassFilters(const FGameplayAbilitySpec& Spec) const
{
	if (!Spec.Ability)
	{
		return false;
	}

	FGameplayTagContainer CombinedTags;
	CombinedTags.AppendTags(Spec.GetDynamicSpecSourceTags()); // runtime/dynamic tags
	CombinedTags.AppendTags(Spec.Ability->GetAssetTags());    // ability asset tags

	if (!AbilityTagQuery.IsEmpty() && !AbilityTagQuery.Matches(CombinedTags))
	{
		return false;
	}

	if (IgnoreAbilityTags.Num() > 0 && CombinedTags.HasAny(IgnoreAbilityTags))
	{
		return false;
	}

	return true;
}

bool USTT_CycleOwnedAbilitiesTask::IsSpecActive(const UAbilitySystemComponent& ASC, const FGameplayAbilitySpecHandle& Handle) const
{
	if (!Handle.IsValid())
	{
		return false;
	}

	if (const FGameplayAbilitySpec* Spec = ASC.FindAbilitySpecFromHandle(Handle))
	{
		return Spec->IsActive();
	}

	return false;
}

bool USTT_CycleOwnedAbilitiesTask::TryActivateSpec(UAbilitySystemComponent& ASC,
	const FGameplayAbilitySpecHandle& Handle,
	APawn* Avatar,
	AActor* TargetActor)
{
	(void)Avatar;

	if (bPassTargetActorAsEventData && TargetActor)
	{
		// Intentionally left as a hook: abilities can query controller/state tree for the current target.
	}

	// UE 5.6 TryActivateAbility does not take event data; abilities should pull target from controller/state tree.
	// If you need to supply TargetData/EventData, trigger a gameplay event inside the ability after activation.
	return ASC.TryActivateAbility(Handle, /*bAllowRemoteActivation=*/true);
}

void USTT_CycleOwnedAbilitiesTask::HandleAbilityEnded(const FAbilityEndedData& EndData)
{
	// Record end result for the active handle only; ignore unrelated ability completions.
	if (ActiveAbilityHandle.IsValid() && EndData.AbilitySpecHandle == ActiveAbilityHandle)
	{
		bHasPendingEndData = true;
		bPendingEndDataWasCancelled = EndData.bWasCancelled;
		PendingEndedHandle = EndData.AbilitySpecHandle;
	}
}

void USTT_CycleOwnedAbilitiesTask::RegisterASC(UAbilitySystemComponent& ASC)
{
	if (CachedASC.Get() == &ASC && AbilityEndedHandle.IsValid())
	{
		return;
	}

	UnregisterASC();
	CachedASC = &ASC;
	AbilityEndedHandle = ASC.OnAbilityEnded.AddUObject(this, &USTT_CycleOwnedAbilitiesTask::HandleAbilityEnded);
}

void USTT_CycleOwnedAbilitiesTask::UnregisterASC()
{
	if (UAbilitySystemComponent* ASC = CachedASC.Get())
	{
		if (AbilityEndedHandle.IsValid())
		{
			ASC->OnAbilityEnded.Remove(AbilityEndedHandle);
		}
	}

	AbilityEndedHandle.Reset();
	CachedASC.Reset();
}
