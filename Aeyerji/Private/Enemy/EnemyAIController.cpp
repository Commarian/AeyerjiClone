#include "Enemy/EnemyAIController.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Enemy/EnemyParentNative.h"
#include "GameFramework/Pawn.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "StateTree.h"
#include "GameplayTagContainer.h"

#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Navigation/CrowdFollowingComponent.h"
#if WITH_EDITOR
#include "UObject/UnrealType.h"
#endif

namespace
{
	const FGameplayTag& TargetAcquiredTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Event.TargetAcquired"));
		return Tag;
	}

	const FGameplayTag& TargetLostTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("Event.TargetLost"));
		return Tag;
	}

	const FGameplayTag& DeadStateTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("State.Dead"), /*ErrorIfNotFound=*/false);
		return Tag;
	}

	bool HasDeadStateTag(const AActor* Actor)
	{
		if (!IsValid(Actor))
		{
			return true;
		}

		const FGameplayTag DeadTag = DeadStateTag();
		if (DeadTag.IsValid() && Actor->Tags.Contains(DeadTag.GetTagName()))
		{
			return true;
		}

		if (DeadTag.IsValid())
		{
			if (const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor, /*LookForComponent=*/true))
			{
				return ASC->HasMatchingGameplayTag(DeadTag);
			}
		}

		return false;
	}
}

AEnemyAIController::AEnemyAIController(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer.SetDefaultSubobjectClass<UCrowdFollowingComponent>(TEXT("PathFollowingComponent")))
{
	//  Make the StateTreeComponent the brain component so the controller ticks it.
	StateTreeComponent = CreateDefaultSubobject<UStateTreeComponent>(TEXT("StateTreeComponent"));
	BrainComponent     = StateTreeComponent;

	// Initialize AI Perception Component with sight configuration
    Perception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("Perception"));
    SightSenseConfig   = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
    HearingSenseConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("HearingConfig"));

    if (SightSenseConfig)
    {
        Perception->ConfigureSense(*SightSenseConfig);
        Perception->SetDominantSense(*SightSenseConfig->GetSenseImplementation());
    }
    if (HearingSenseConfig)
    {
        Perception->ConfigureSense(*HearingSenseConfig);
    }
	// Team ID setup if needed (assuming TeamId is a FGenericTeamId property in this class or inherited)
	SetGenericTeamId(TeamId);

	// Bind perception delegates
	if (Perception)
	{
		Perception->OnTargetPerceptionUpdated.AddDynamic(this, &AEnemyAIController::OnTargetPerception);
		Perception->OnPerceptionUpdated.AddDynamic(this, &AEnemyAIController::OnPerceptionUpdated);
	}

    // Ensure configs reflect any BP overrides
    ApplyPerceptionSettings();
}

void AEnemyAIController::PostLoad()
{
	Super::PostLoad();

	ApplyLegacyPerceptionPropertyOverrides();
	ApplyPerceptionSettings();
}

#if WITH_EDITOR
void AEnemyAIController::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	ApplyPerceptionSettings();
}
#endif

// EnemyAIController.cpp  (UE 5.6)
void AEnemyAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

    if (const IGenericTeamAgentInterface* TeamAgent = Cast<IGenericTeamAgentInterface>(InPawn))
    {
        SetGenericTeamId(TeamAgent->GetGenericTeamId());
    }

    // Re-apply perception values after BP overrides are initialized
    ApplyPerceptionSettings();

    if (!DefaultStateTree)          // make sure a BP assigned something
    {
        UE_LOG(LogTemp, Error,
               TEXT("%s: DefaultStateTree is NOT set!"), *GetName());
        return;                     // bail safely – no crash
    }

    // Use the default subobject created in the constructor and assigned as BrainComponent
    if (!ensure(StateTreeComponent))
    {
        return;
    }
    // Cache home location for patrol logic.
    if (InPawn)
    {
        HomeLocation = InPawn->GetActorLocation();
    }
    StateTreeComponent->SetStateTree(DefaultStateTree);
    StateTreeComponent->StartLogic();

    // Immediately evaluate current perception to avoid "patrol-first" delay on spawn.
    if (Perception)
    {
        TArray<AActor*> SeenActors;
        Perception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), SeenActors);
        AActor* Best = nullptr;
        float BestDistSq = 0.f;
        bool bHaveBest = false;
        const FVector MyLoc = InPawn ? InPawn->GetActorLocation() : GetPawn() ? GetPawn()->GetActorLocation() : FVector::ZeroVector;
        for (AActor* Actor : SeenActors)
        {
            if (!IsTargetValidForAcquisition(Actor))
            {
                continue;
            }
            const float DistSq = FVector::DistSquared(MyLoc, Actor->GetActorLocation());
            if (!bHaveBest || DistSq < BestDistSq)
            {
                BestDistSq = DistSq;
                Best = Actor;
                bHaveBest = true;
            }
        }
        if (Best)
        {
            TryAcquireTarget(Best, /*bBroadcastAllyAlert=*/true);
        }
    }
}

void AEnemyAIController::OnTargetPerception(AActor* Actor, FAIStimulus Stimulus)
{
	if (!Actor)
		return;

	const bool bSensed = Stimulus.WasSuccessfullySensed();
	const bool bIsDead = HasDeadStateTag(Actor);

    if (bSensed && !bIsDead)
    {
        TryAcquireTarget(Actor, /*bBroadcastAllyAlert=*/true);
    }
	else if (Actor == CurrentTarget && (!bSensed || bIsDead))        // ← added tests
	{
		if (!bIsDead)
		{
			RememberTargetLocation(Actor);
		}
		else
		{
			ClearLastKnownTarget();
		}

		CurrentTarget = nullptr;
		if (StateTreeComponent)
		{
			StateTreeComponent->SendStateTreeEvent(FStateTreeEvent(TargetLostTag()));
		}
		UE_LOG(LogTemp, Display, TEXT("Target lost: %s"), *Actor->GetName());
	}
}

void AEnemyAIController::OnPerceptionUpdated(const TArray<AActor*>& UpdatedActors)
{
	if (!CurrentTarget)
	{
		return;
	}

	// If the current target is updated and is now dead, clear it
	for (AActor* UpdatedActor : UpdatedActors)
	{
		if (UpdatedActor && UpdatedActor == CurrentTarget && HasDeadStateTag(UpdatedActor))
		{
			ClearLastKnownTarget();
			CurrentTarget = nullptr;
			UE_LOG(LogTemp, Display, TEXT("TargetLostEvent!"));
			if (StateTreeComponent)
			{
				StateTreeComponent->SendStateTreeEvent(FStateTreeEvent(TargetLostTag()));
			}
			break;
		}
	}
}

bool AEnemyAIController::TryAcquireTarget(AActor* NewTarget, const bool bBroadcastAllyAlert)
{
	if (!HasAuthority() || !IsTargetValidForAcquisition(NewTarget))
	{
		return false;
	}

	RememberTargetLocation(NewTarget);

	if (CurrentTarget == NewTarget)
	{
		return false;
	}

	CurrentTarget = NewTarget;
	StopMovement();

	if (StateTreeComponent)
	{
		StateTreeComponent->SendStateTreeEvent(FStateTreeEvent(TargetAcquiredTag()));
	}

	UE_LOG(LogTemp, Log, TEXT("Target acquired: %s"), *GetNameSafe(NewTarget));

	if (bBroadcastAllyAlert)
	{
		if (AEnemyParentNative* EnemyPawn = Cast<AEnemyParentNative>(GetPawn()))
		{
			EnemyPawn->NotifyNearbyAlliesOfTarget(NewTarget);
		}
	}

	return true;
}

bool AEnemyAIController::IsTargetValidForAcquisition(AActor* Candidate) const
{
	if (!HasAuthority() || !IsValid(Candidate))
	{
		return false;
	}

	const APawn* ControlledPawn = GetPawn();
	if (!IsValid(ControlledPawn) || Candidate == ControlledPawn)
	{
		return false;
	}

	if (HasDeadStateTag(Candidate))
	{
		return false;
	}

	return GetTeamAttitudeTowards(*Candidate) == ETeamAttitude::Hostile;
}

void AEnemyAIController::ClearLastKnownTarget()
{
	LastKnownTargetActor.Reset();
	LastKnownTargetLocation = FVector::ZeroVector;
	LastKnownTargetTime = -1.0;
	bHasLastKnownTarget = false;
}

void AEnemyAIController::RememberTargetLocation(AActor* Target)
{
	if (!IsValid(Target))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	LastKnownTargetActor = Target;
	LastKnownTargetLocation = Target->GetActorLocation();
	LastKnownTargetTime = World->GetTimeSeconds();
	bHasLastKnownTarget = true;
}

void AEnemyAIController::ApplyLegacyPerceptionPropertyOverrides()
{
	if (!bOverridePerceptionWithProperties)
	{
		return;
	}

	if (SightSenseConfig)
	{
		SightSenseConfig->SightRadius = SightRadius;
		SightSenseConfig->LoseSightRadius = LoseSightRadius;
		SightSenseConfig->PeripheralVisionAngleDegrees = PeripheralVisionAngleDegrees;
		SightSenseConfig->DetectionByAffiliation.bDetectEnemies = bDetectEnemies;
		SightSenseConfig->DetectionByAffiliation.bDetectFriendlies = bDetectFriendlies;
		SightSenseConfig->DetectionByAffiliation.bDetectNeutrals = bDetectNeutrals;
	}

	if (HearingSenseConfig)
	{
		HearingSenseConfig->HearingRange = HearingRange;
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		HearingSenseConfig->LoSHearingRange = LoSHearingRange;
		HearingSenseConfig->bUseLoSHearing = LoSHearingRange > HearingRange;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		HearingSenseConfig->DetectionByAffiliation.bDetectEnemies = bDetectEnemies;
		HearingSenseConfig->DetectionByAffiliation.bDetectFriendlies = bDetectFriendlies;
		HearingSenseConfig->DetectionByAffiliation.bDetectNeutrals = bDetectNeutrals;
	}

	// After migration the sense config subobjects are the only source of truth.
	bOverridePerceptionWithProperties = false;
}

void AEnemyAIController::SyncDeprecatedPerceptionPropertiesFromConfigs()
{
	if (SightSenseConfig)
	{
		SightRadius = SightSenseConfig->SightRadius;
		LoseSightRadius = SightSenseConfig->LoseSightRadius;
		PeripheralVisionAngleDegrees = SightSenseConfig->PeripheralVisionAngleDegrees;
		bDetectEnemies = SightSenseConfig->DetectionByAffiliation.bDetectEnemies;
		bDetectFriendlies = SightSenseConfig->DetectionByAffiliation.bDetectFriendlies;
		bDetectNeutrals = SightSenseConfig->DetectionByAffiliation.bDetectNeutrals;
	}

	if (HearingSenseConfig)
	{
		HearingRange = HearingSenseConfig->HearingRange;
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		LoSHearingRange = HearingSenseConfig->bUseLoSHearing
			? HearingSenseConfig->LoSHearingRange
			: HearingSenseConfig->HearingRange;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
	}
}

void AEnemyAIController::ApplyPerceptionSettings()
{
    if (!Perception)
    {
        return;
    }

	if (SightSenseConfig)
	{
		Perception->ConfigureSense(*SightSenseConfig);
		Perception->SetDominantSense(*SightSenseConfig->GetSenseImplementation());
	}

	if (HearingSenseConfig)
	{
		Perception->ConfigureSense(*HearingSenseConfig);
	}

	SyncDeprecatedPerceptionPropertiesFromConfigs();

    Perception->RequestStimuliListenerUpdate();
}
