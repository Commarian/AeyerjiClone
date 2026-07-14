#include "Enemy/Tasks/STT_ActivatePrimaryAttackTask.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "AIController.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AeyerjiCharacter.h"
#include "AeyerjiGameplayTags.h"
#include "CharacterStatsLibrary.h"
#include "GameplayAbilitySpec.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "GameFramework/MovementComponent.h"
#include "GameFramework/Pawn.h"
#include "StateTreeExecutionContext.h"
#include "Logging/AeyerjiLog.h"

namespace
{
    constexpr float BossPrimaryAttackStaleActiveTimeoutSeconds = 2.5f;

#define BOSS_PRIMARY_AJ_LOG(Verbosity, ObjPtr, Fmt, ...) \
    do { \
        const UObject* BossPrimaryLogObj = Cast<const UObject>(ObjPtr); \
        UE_LOG(LogAeyerji, Verbosity, TEXT("[%s] %s: " Fmt), \
            Aeyerji::Detail::GetSide(BossPrimaryLogObj), \
            *Aeyerji::Detail::GetClass(BossPrimaryLogObj), \
            ##__VA_ARGS__); \
    } while (0)

    /** Resolve the controlled pawn whether the StateTree owner is a controller or the pawn itself. */
    APawn* ResolveStateTreePawn(FStateTreeExecutionContext& Context)
    {
        if (AAIController* AI = Cast<AAIController>(Context.GetOwner()))
        {
            return AI->GetPawn();
        }

        return Cast<APawn>(Context.GetOwner());
    }

    /** Build the same tolerant primary-tag search used by player-side activation. */
    bool BuildPrimaryAttackTagSearch(const UAbilitySystemComponent& ASC, FGameplayTag& OutLeafTag, FGameplayTagContainer& OutTags)
    {
        OutLeafTag = UCharacterStatsLibrary::GetLeafTagFromBranchTag(&ASC, AeyerjiTags::Ability_Primary);
        if (!OutLeafTag.IsValid())
        {
            OutLeafTag = AeyerjiTags::Ability_Primary;
        }

        OutTags.Reset();
        if (!OutLeafTag.IsValid())
        {
            return false;
        }

        OutTags.AddTag(OutLeafTag);

        FString Name = OutLeafTag.ToString();
        while (true)
        {
            int32 Dot = INDEX_NONE;
            if (!Name.FindLastChar('.', Dot))
            {
                break;
            }

            Name = Name.Left(Dot);
            const FGameplayTag Parent = FGameplayTag::RequestGameplayTag(*Name, /*ErrorIfNotFound=*/false);
            if (!Parent.IsValid())
            {
                break;
            }

            OutTags.AddTag(Parent);
            if (Parent == AeyerjiTags::Ability_Primary)
            {
                break;
            }
        }

        OutTags.AddTag(AeyerjiTags::Ability_Primary.GetTag());

        return OutTags.Num() > 0;
    }

    /** Merge the spec's asset and dynamic tags so the task can match primary abilities directly. */
    void BuildSpecTags(const FGameplayAbilitySpec& Spec, FGameplayTagContainer& OutTags)
    {
        OutTags.Reset();
        OutTags.AppendTags(Spec.GetDynamicSpecSourceTags());

        if (Spec.Ability)
        {
            OutTags.AppendTags(Spec.Ability->GetAssetTags());
        }
    }
}


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
    bWasActive = false;
    bPrimaryAttackCompleted = false;
    bLoggedWaitingForActorInfo = false;
    bLoggedMissingPrimarySpec = false;
    bLoggedActivationFailure = false;
    bLoggedPrimarySpecSnapshot = false;
    ObservedPrimaryActiveStartTime = -1.f;
    ObservedPrimarySpecHandle = FGameplayAbilitySpecHandle();
    NextRetryTime = 0.f;
    BOSS_PRIMARY_AJ_LOG(Verbose, this, TEXT("[BossPrimaryAttack] ActivatePrimaryAttack: enter state owner=%s."), *GetNameSafe(Context.GetOwner()));
    return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus USTT_ActivatePrimaryAttackTask::Tick(FStateTreeExecutionContext& Context, float)
{
    APawn* Pawn = ResolveStateTreePawn(Context);
    if (!Pawn)
    {
        BOSS_PRIMARY_AJ_LOG(Verbose, this, TEXT("[BossPrimaryAttack] ActivatePrimaryAttack: no pawn resolved from owner %s - failing."), *GetNameSafe(Context.GetOwner()));
        return EStateTreeRunStatus::Failed;
    }

    if (const AAeyerjiCharacter* ControlledCharacter = Cast<AAeyerjiCharacter>(Pawn))
    {
        if (ControlledCharacter->IsCrowdControlled())
        {
            if (AAIController* AI = Cast<AAIController>(Pawn->GetController()))
            {
                AI->StopMovement();
                AI->ClearFocus(EAIFocusPriority::Gameplay);
                AI->ClearFocus(EAIFocusPriority::Move);
            }

            if (UMovementComponent* MovementComponent = Pawn->GetMovementComponent())
            {
                MovementComponent->StopMovementImmediately();
            }

            BOSS_PRIMARY_AJ_LOG(Verbose, this, TEXT("[BossPrimaryAttack] ActivatePrimaryAttack: pawn %s is crowd-controlled - failing task without activation."), *GetNameSafe(Pawn));
            return EStateTreeRunStatus::Failed;
        }
    }

    UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn, /*LookForComponent=*/true);
    if (!ASC)
    {
        BOSS_PRIMARY_AJ_LOG(Verbose, this, TEXT("[BossPrimaryAttack] ActivatePrimaryAttack: no ASC found on pawn %s - failing."), *GetNameSafe(Pawn));
        return EStateTreeRunStatus::Failed;
    }

    // ASC must be fully initialized (Owner/Avatar) before activation attempts.
    if (!ASC->AbilityActorInfo.IsValid()
        || !ASC->AbilityActorInfo->OwnerActor.IsValid()
        || !ASC->AbilityActorInfo->AvatarActor.IsValid())
    {
        if (!bLoggedWaitingForActorInfo)
        {
            BOSS_PRIMARY_AJ_LOG(Verbose, this, TEXT("[BossPrimaryAttack] ActivatePrimaryAttack: ASC actor info is not ready yet for pawn %s."), *GetNameSafe(Pawn));
            bLoggedWaitingForActorInfo = true;
        }
        return EStateTreeRunStatus::Running;
    }
    bLoggedWaitingForActorInfo = false;

    RegisterCompletionListener(ASC);

    // 1) Resolve the character-specific primary leaf (e.g., Ranged.Basic / Melee.Basic)
    FGameplayTag Leaf;
    FGameplayTagContainer SearchTags;
    if (!BuildPrimaryAttackTagSearch(*ASC, Leaf, SearchTags))
    {
        if (!bLoggedActivationFailure)
        {
            BOSS_PRIMARY_AJ_LOG(Verbose, this, TEXT("[BossPrimaryAttack] ActivatePrimaryAttack: failed to build primary tag search for pawn %s."), *GetNameSafe(Pawn));
            bLoggedActivationFailure = true;
        }
        return EStateTreeRunStatus::Running;
    }

    // 2) Find the best matching primary spec directly. This avoids requiring every parent tag to exist on the spec.
    bool bActiveNow = false;
    bool bAnyPrimaryOnCooldown = false;
    bool bFoundAnyPrimarySpec = false;
    bool bSelectedExactLeafSpec = false;
    FGameplayAbilitySpecHandle PrimarySpecHandle;
    const FGameplayAbilitySpec* PrimarySpec = nullptr;
    FGameplayTagContainer SpecTags;

    {
        const TArray<FGameplayAbilitySpec>& Specs = ASC->GetActivatableAbilities();
        for (const FGameplayAbilitySpec& Spec : Specs)
        {
            if (!Spec.Handle.IsValid() || !Spec.Ability)
            {
                continue;
            }

            BuildSpecTags(Spec, SpecTags);

            const bool bExactLeafMatch = Leaf.IsValid() && SpecTags.HasTagExact(Leaf);
            const bool bMatchesPrimary = bExactLeafMatch || SpecTags.HasAny(SearchTags);
            if (!bMatchesPrimary)
            {
                continue;
            }

            bFoundAnyPrimarySpec = true;

            if (Spec.IsActive())
            {
                bActiveNow = true;
            }

            FGameplayTagContainer CooldownFail;
            if (!Spec.Ability->CheckCooldown(Spec.Handle, ASC->AbilityActorInfo.Get(), &CooldownFail))
            {
                bAnyPrimaryOnCooldown = true;
            }

            if (!PrimarySpecHandle.IsValid() || (bExactLeafMatch && !bSelectedExactLeafSpec))
            {
                PrimarySpecHandle = Spec.Handle;
                PrimarySpec = &Spec;
                bSelectedExactLeafSpec = bExactLeafMatch;
            }
        }
    }

    if (bActiveNow)
    {
        if (PrimarySpecHandle.IsValid() && (!ObservedPrimarySpecHandle.IsValid() || ObservedPrimarySpecHandle != PrimarySpecHandle))
        {
            ObservedPrimarySpecHandle = PrimarySpecHandle;
            ObservedPrimaryActiveStartTime = Pawn->GetWorld() ? Pawn->GetWorld()->GetTimeSeconds() : 0.f;
        }

        if (!bWasActive)
        {
            BOSS_PRIMARY_AJ_LOG(Verbose, this, TEXT("[BossPrimaryAttack] ActivatePrimaryAttack: primary spec is active for pawn %s (leaf=%s handle=%s)."),
                *GetNameSafe(Pawn),
                *Leaf.ToString(),
                *PrimarySpecHandle.ToString());
        }
        if (!bLoggedPrimarySpecSnapshot && PrimarySpec && PrimarySpec->Ability)
        {
            BOSS_PRIMARY_AJ_LOG(Verbose, this, TEXT("[BossPrimaryAttack] ActivatePrimaryAttack: active spec snapshot pawn=%s ability=%s handle=%s exactLeaf=%s cooldownBlocked=%s."),
                *GetNameSafe(Pawn),
                *GetNameSafe(PrimarySpec->Ability),
                *PrimarySpecHandle.ToString(),
                bSelectedExactLeafSpec ? TEXT("true") : TEXT("false"),
                bAnyPrimaryOnCooldown ? TEXT("true") : TEXT("false"));
            bLoggedPrimarySpecSnapshot = true;
        }

        const float ActiveNowTime = Pawn->GetWorld() ? Pawn->GetWorld()->GetTimeSeconds() : 0.f;
        if (ObservedPrimaryActiveStartTime >= 0.f
            && (ActiveNowTime - ObservedPrimaryActiveStartTime) >= BossPrimaryAttackStaleActiveTimeoutSeconds)
        {
            BOSS_PRIMARY_AJ_LOG(Verbose, this, TEXT("[BossPrimaryAttack] ActivatePrimaryAttack: cancelling stale active primary spec pawn=%s ability=%s handle=%s activeFor=%.2fs."),
                *GetNameSafe(Pawn),
                *GetNameSafe(PrimarySpec ? PrimarySpec->Ability : nullptr),
                *PrimarySpecHandle.ToString(),
                ActiveNowTime - ObservedPrimaryActiveStartTime);
            ASC->CancelAbilities(&SearchTags);
            ObservedPrimaryActiveStartTime = ActiveNowTime;
            bWasActive = false;
            bLoggedPrimarySpecSnapshot = false;
        }

        bWasActive = true;
        bLoggedActivationFailure = false;
        return EStateTreeRunStatus::Running;
    }
    bWasActive = false;
    bLoggedPrimarySpecSnapshot = false;
    ObservedPrimaryActiveStartTime = -1.f;

    // 4) Require an explicit completion event (so animation/cues can decide when the swing is done).
    if (bPrimaryAttackCompleted)
    {
        return EStateTreeRunStatus::Succeeded;
    }

    if (!bFoundAnyPrimarySpec)
    {
        if (!bLoggedMissingPrimarySpec)
        {
            BOSS_PRIMARY_AJ_LOG(Verbose, this, TEXT("[BossPrimaryAttack] ActivatePrimaryAttack: no primary ability spec found for pawn %s (leaf=%s)."),
                *GetNameSafe(Pawn),
                *Leaf.ToString());
            bLoggedMissingPrimarySpec = true;
        }
        return EStateTreeRunStatus::Running;
    }
    bLoggedMissingPrimarySpec = false;

    // 5) Not active yet - throttle activation attempts to avoid spamming each tick.
    const float Now = Pawn->GetWorld() ? Pawn->GetWorld()->GetTimeSeconds() : 0.f;
    if (!bRequestedActivation || Now >= NextRetryTime)
    {
        bRequestedActivation = true;
        NextRetryTime = Now + 0.15f; // small retry delay
        BOSS_PRIMARY_AJ_LOG(VeryVerbose, this, TEXT("[BossPrimaryAttack] ActivatePrimaryAttack: attempting activation pawn=%s leaf=%s handle=%s cooldownBlocked=%s tags=[%s]."),
            *GetNameSafe(Pawn),
            *Leaf.ToString(),
            *PrimarySpecHandle.ToString(),
            bAnyPrimaryOnCooldown ? TEXT("true") : TEXT("false"),
            *SearchTags.ToStringSimple());
        const bool bActivated = ASC->TryActivateAbility(PrimarySpecHandle, /*bAllowRemoteActivation=*/true);
        if (bActivated)
        {
            ObservedPrimaryActiveStartTime = Now;
            ObservedPrimarySpecHandle = PrimarySpecHandle;
            BOSS_PRIMARY_AJ_LOG(Verbose, this, TEXT("[BossPrimaryAttack] ActivatePrimaryAttack: TryActivateAbility returned true for pawn %s handle=%s."),
                *GetNameSafe(Pawn),
                *PrimarySpecHandle.ToString());
            bLoggedActivationFailure = false;
        }
        else if (!bAnyPrimaryOnCooldown && !bLoggedActivationFailure)
        {
            BOSS_PRIMARY_AJ_LOG(Verbose, this, TEXT("[BossPrimaryAttack] ActivatePrimaryAttack: TryActivateAbility failed for pawn %s (leaf=%s cooldownBlocked=%s handle=%s)."),
                *GetNameSafe(Pawn),
                *Leaf.ToString(),
                bAnyPrimaryOnCooldown ? TEXT("true") : TEXT("false"),
                *PrimarySpecHandle.ToString());
            bLoggedActivationFailure = true;
        }
    }

    // 6) If a matching spec is on cooldown, keep ticking so we can fire once it's ready.
    if (bAnyPrimaryOnCooldown)
    {
        bLoggedActivationFailure = false;
        return EStateTreeRunStatus::Running;
    }

    // 7) Neither active nor cooldown-gated: keep running (parent state remains Attack), we'll retry shortly.
    return EStateTreeRunStatus::Running;
}

void USTT_ActivatePrimaryAttackTask::ExitState(FStateTreeExecutionContext& Context,
    const FStateTreeTransitionResult& Transition)
{
    BOSS_PRIMARY_AJ_LOG(Verbose, this, TEXT("[BossPrimaryAttack] ActivatePrimaryAttack: exit state transition=%d completed=%s requested=%s."),
        static_cast<int32>(Transition.CurrentRunStatus),
        bPrimaryAttackCompleted ? TEXT("true") : TEXT("false"),
        bRequestedActivation ? TEXT("true") : TEXT("false"));
    UnregisterCompletionListener();
    Super::ExitState(Context, Transition);
}

void USTT_ActivatePrimaryAttackTask::HandlePrimaryAttackCompleted(const FGameplayEventData*)
{
    bPrimaryAttackCompleted = true;
    BOSS_PRIMARY_AJ_LOG(Verbose, this, TEXT("[BossPrimaryAttack] ActivatePrimaryAttack: received primary attack completion event."));
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
    ObservedAbilityEndedHandle = ASC->OnAbilityEnded.AddUObject(this, &USTT_ActivatePrimaryAttackTask::HandleObservedAbilityEnded);
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

        if (ObservedAbilityEndedHandle.IsValid())
        {
            ASC->OnAbilityEnded.Remove(ObservedAbilityEndedHandle);
        }
    }

    PrimaryAttackCompletedHandle.Reset();
    ObservedAbilityEndedHandle.Reset();
    CachedASC.Reset();
    ObservedPrimaryActiveStartTime = -1.f;
    ObservedPrimarySpecHandle = FGameplayAbilitySpecHandle();
    bRegisteredCompletionDelegate = false;
}

void USTT_ActivatePrimaryAttackTask::HandleObservedAbilityEnded(const FAbilityEndedData& EndData)
{
    if (!ObservedPrimarySpecHandle.IsValid() || EndData.AbilitySpecHandle != ObservedPrimarySpecHandle)
    {
        return;
    }

    BOSS_PRIMARY_AJ_LOG(Verbose, this, TEXT("[BossPrimaryAttack] ActivatePrimaryAttack: observed primary handle ended handle=%s ability=%s cancelled=%s."),
        *EndData.AbilitySpecHandle.ToString(),
        *GetNameSafe(EndData.AbilityThatEnded),
        EndData.bWasCancelled ? TEXT("true") : TEXT("false"));
}
