#include "Enemy/Tasks/STT_ActivatePrimaryAttackTask.h"
#include "AIController.h"
#include "AbilitySystemComponent.h"
#include "AeyerjiGameplayTags.h"
#include "CharacterStatsLibrary.h"
#include "GameplayAbilitySpec.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "GameFramework/Pawn.h"
#include "StateTreeExecutionContext.h"
#include "Logging/AeyerjiLog.h"


USTT_ActivatePrimaryAttackTask::USTT_ActivatePrimaryAttackTask(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    /** Enable Tick for this task (UE 5.6) */
    bShouldCallTick = true;
}

EStateTreeRunStatus USTT_ActivatePrimaryAttackTask::EnterState(FStateTreeExecutionContext& Context,
                                                               const FStateTreeTransitionResult& Transition)
{
    UnregisterCompletionListener();
    bRequestedActivation = false;
    bPrimaryAttackCompleted = false;
    NextRetryTime = 0.f;
    return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus USTT_ActivatePrimaryAttackTask::Tick(FStateTreeExecutionContext& Context, float)
{
    AAIController* AI = Cast<AAIController>(Context.GetOwner());
    APawn* Pawn = AI ? AI->GetPawn() : nullptr;
    if (!Pawn) return EStateTreeRunStatus::Failed;

    UAbilitySystemComponent* ASC = Pawn->FindComponentByClass<UAbilitySystemComponent>();
    if (!ASC) return EStateTreeRunStatus::Failed;

    // ASC must be fully initialized (Owner/Avatar) before activation attempts.
    if (!ASC->AbilityActorInfo.IsValid()
        || !ASC->AbilityActorInfo->OwnerActor.IsValid()
        || !ASC->AbilityActorInfo->AvatarActor.IsValid())
    {
        return EStateTreeRunStatus::Running;
    }

    RegisterCompletionListener(ASC);

    // 1) Resolve the character-specific primary leaf (e.g., Ranged.Basic / Melee.Basic)
    const FGameplayTag Leaf = UCharacterStatsLibrary::GetLeafTagFromBranchTag(ASC, AeyerjiTags::Ability_Primary);

    // 2) Build tolerant search: [Leaf, its parents ... , Ability.Primary]
    FGameplayTagContainer SearchTags;
    SearchTags.AddTag(Leaf);
    {
        FString Name = Leaf.ToString();
        while (true)
        {
            int32 Dot = INDEX_NONE;
            if (!Name.FindLastChar('.', Dot)) break;
            Name = Name.Left(Dot);
            const FGameplayTag Parent = FGameplayTag::RequestGameplayTag(*Name);
            SearchTags.AddTag(Parent);
            if (Parent == AeyerjiTags::Ability_Primary) break;
        }
    }

    // 3) Is a matching ability currently active? If so, keep running.
    bool bActiveNow = false;
    {
        TArray<FGameplayAbilitySpec*> Specs;
        ASC->GetActivatableGameplayAbilitySpecsByAllMatchingTags(SearchTags, Specs, /*OnlyAbilitiesThatSatisfyTagRequirements=*/true);
        for (FGameplayAbilitySpec* Spec : Specs)
        {
            if (Spec && Spec->IsActive())
            {
                bActiveNow = true;
                break;
            }
        }
    }

    if (bActiveNow)
    {
        return EStateTreeRunStatus::Running;
    }

    // 4) Require an explicit completion event (so animation/cues can decide when the swing is done).
    if (bPrimaryAttackCompleted)
    {
        return EStateTreeRunStatus::Succeeded;
    }

    // 5) Not active yet - throttle activation attempts to avoid spamming each tick.
    const float Now = Pawn->GetWorld() ? Pawn->GetWorld()->GetTimeSeconds() : 0.f;
    if (!bRequestedActivation || Now >= NextRetryTime)
    {
        bRequestedActivation = true;
        NextRetryTime = Now + 0.15f; // small retry delay
        (void)ASC->TryActivateAbilitiesByTag(SearchTags, /*bAllowRemoteActivation=*/true);
    }

    // 6) If any matching spec is on cooldown, keep ticking so we can fire once it's ready.
    {
        TArray<FGameplayAbilitySpec*> Specs;
        ASC->GetActivatableGameplayAbilitySpecsByAllMatchingTags(SearchTags, Specs, /*OnlyAbilitiesThatSatisfyTagRequirements=*/true);
        for (FGameplayAbilitySpec* Spec : Specs)
        {
            if (Spec && Spec->Ability)
            {
                FGameplayTagContainer CooldownFail;
                const bool bCooldownOK = Spec->Ability->CheckCooldown(Spec->Handle, ASC->AbilityActorInfo.Get(), &CooldownFail);
                if (!bCooldownOK)
                {
                    return EStateTreeRunStatus::Running;
                }
            }
        }
    }

    // 7) Neither active nor cooldown-gated: keep running (parent state remains Attack), we'll retry shortly.
    return EStateTreeRunStatus::Running;
}

void USTT_ActivatePrimaryAttackTask::ExitState(FStateTreeExecutionContext& Context,
    const FStateTreeTransitionResult& Transition)
{
    UnregisterCompletionListener();
    Super::ExitState(Context, Transition);
}

void USTT_ActivatePrimaryAttackTask::HandlePrimaryAttackCompleted(const FGameplayEventData*)
{
    bPrimaryAttackCompleted = true;
}

void USTT_ActivatePrimaryAttackTask::RegisterCompletionListener(UAbilitySystemComponent* ASC)
{
    if (!ASC)
    {
        return;
    }

    if (bRegisteredCompletionDelegate && CachedASC.Get() == ASC)
    {
        return; // already listening on this ASC
    }

    UnregisterCompletionListener();

    CachedASC = ASC;
    FGameplayEventMulticastDelegate& Delegate = ASC->GenericGameplayEventCallbacks.FindOrAdd(AeyerjiTags::Event_PrimaryAttack_Completed);
    PrimaryAttackCompletedHandle = Delegate.AddUObject(this, &USTT_ActivatePrimaryAttackTask::HandlePrimaryAttackCompleted);
    bRegisteredCompletionDelegate = true;
}

void USTT_ActivatePrimaryAttackTask::UnregisterCompletionListener()
{
    if (UAbilitySystemComponent* ASC = CachedASC.Get())
    {
        if (FGameplayEventMulticastDelegate* Delegate = ASC->GenericGameplayEventCallbacks.Find(AeyerjiTags::Event_PrimaryAttack_Completed))
        {
            Delegate->Remove(PrimaryAttackCompletedHandle);
        }
    }

    PrimaryAttackCompletedHandle.Reset();
    CachedASC.Reset();
    bRegisteredCompletionDelegate = false;
}
