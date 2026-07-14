#include "Enemy/EnemyAIController.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Enemy/EnemyParentNative.h"
#include "GameFramework/Pawn.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "GameFramework/PlayerController.h"
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
	constexpr double DefenseDamageThreatMemorySeconds = 4.0;

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

	const FGameplayTag& EnemyAIDeadStateTag()
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

		const FGameplayTag DeadTag = EnemyAIDeadStateTag();
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
		if (IsValid(DefenseObjectiveTarget.Get()))
		{
			RefreshDefenseObjectiveTarget();
			return;
		}

        TryAcquireTarget(Actor, /*bBroadcastAllyAlert=*/true);
    }
	else if (Actor == CurrentTarget && (!bSensed || bIsDead))        // ← added tests
	{
		// Rift pursuit is authority-driven by the owning spawner. Losing perception must not
		// send a living target back through patrol/leash fallback before the next retarget tick.
		if (bPermanentRiftPursuit && !bIsDead)
		{
			RememberTargetLocation(Actor);
			return;
		}
		if (!bIsDead)
		{
			RememberTargetLocation(Actor);
		}
		else
		{
			ClearLastKnownTarget();
		}

		if (IsValid(DefenseObjectiveTarget.Get()) && !HasDeadStateTag(DefenseObjectiveTarget.Get()))
		{
			RefreshDefenseObjectiveTarget();
			return;
		}

		AssignCurrentTarget(nullptr, EAeyerjiEnemyTargetSource::None, /*bSendTargetAcquiredEvent=*/false, /*bStopCurrentMovement=*/false);
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
			if (IsValid(DefenseObjectiveTarget.Get()) && !HasDeadStateTag(DefenseObjectiveTarget.Get()))
			{
				RefreshDefenseObjectiveTarget();
				break;
			}

			AssignCurrentTarget(nullptr, EAeyerjiEnemyTargetSource::None, /*bSendTargetAcquiredEvent=*/false, /*bStopCurrentMovement=*/false);
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

	AssignCurrentTarget(NewTarget, EAeyerjiEnemyTargetSource::HostileActor, /*bSendTargetAcquiredEvent=*/true, /*bStopCurrentMovement=*/true);

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

void AEnemyAIController::SetTargetActor(AActor* NewTarget)
{
	const EAeyerjiEnemyTargetSource NewSource = IsValid(NewTarget) && NewTarget == DefenseObjectiveTarget.Get()
		? EAeyerjiEnemyTargetSource::DefenseObjective
		: (IsValid(NewTarget) ? EAeyerjiEnemyTargetSource::HostileActor : EAeyerjiEnemyTargetSource::None);

	AssignCurrentTarget(NewTarget, NewSource, /*bSendTargetAcquiredEvent=*/false, /*bStopCurrentMovement=*/false);
}

void AEnemyAIController::SetDefenseObjectiveTargetActor(AActor* NewTarget)
{
	if (!IsValid(NewTarget) && CurrentTarget == DefenseObjectiveTarget)
	{
		CurrentTarget = nullptr;
		CurrentTargetSource = EAeyerjiEnemyTargetSource::None;
	}

	DefenseObjectiveTarget = NewTarget;
}

void AEnemyAIController::ConfigureDefenseObjectiveTargeting(AActor* NewTarget, const FAeyerjiDefenseTargetingSettings& TargetingSettings)
{
	DefenseObjectiveTarget = NewTarget;
	DefenseTargetingSettings = TargetingSettings;
	DefenseTargetingSettings.PlayerThreatAcquireRadius = FMath::Max(0.f, DefenseTargetingSettings.PlayerThreatAcquireRadius);
	DefenseTargetingSettings.PlayerThreatReleaseRadius = FMath::Max(
		DefenseTargetingSettings.PlayerThreatAcquireRadius,
		DefenseTargetingSettings.PlayerThreatReleaseRadius);
	DefenseTargetingSettings.PlayerThreatObjectiveAcquireRadius = DefenseTargetingSettings.PlayerThreatObjectiveAcquireRadius > 0.f
		? DefenseTargetingSettings.PlayerThreatObjectiveAcquireRadius
		: FAeyerjiDefenseTargetingSettings().PlayerThreatObjectiveAcquireRadius;
	DefenseTargetingSettings.PlayerThreatObjectiveReleaseRadius = DefenseTargetingSettings.PlayerThreatObjectiveReleaseRadius > 0.f
		? DefenseTargetingSettings.PlayerThreatObjectiveReleaseRadius
		: FAeyerjiDefenseTargetingSettings().PlayerThreatObjectiveReleaseRadius;
	DefenseTargetingSettings.PlayerThreatObjectiveReleaseRadius = FMath::Max(
		DefenseTargetingSettings.PlayerThreatObjectiveAcquireRadius,
		DefenseTargetingSettings.PlayerThreatObjectiveReleaseRadius);
	DefenseTargetingSettings.PlayerDistanceBias = FMath::Max(0.f, DefenseTargetingSettings.PlayerDistanceBias);

	RefreshDefenseObjectiveTarget();
}

bool AEnemyAIController::ShouldAcquireTargetWithDefenseObjective(AActor* Candidate) const
{
	if (!IsValid(Candidate))
	{
		return false;
	}

	if (HasDeadStateTag(Candidate))
	{
		return false;
	}

	AActor* Objective = DefenseObjectiveTarget.Get();
	if (!IsValid(Objective) || HasDeadStateTag(Objective))
	{
		return true;
	}

	if (Candidate == Objective)
	{
		return true;
	}

	const APawn* ControlledPawn = GetPawn();
	if (!IsValid(ControlledPawn))
	{
		return false;
	}

	const FVector SelfLocation = ControlledPawn->GetActorLocation();
	const FVector CandidateLocation = Candidate->GetActorLocation();
	const FVector ObjectiveLocation = Objective->GetActorLocation();
	const float CandidateDistance2D = FVector::Dist2D(SelfLocation, CandidateLocation);
	const float ObjectiveDistance2D = FVector::Dist2D(SelfLocation, ObjectiveLocation);
	const bool bAlreadyTargetingCandidate = Candidate == CurrentTarget.Get();
	const float Radius = bAlreadyTargetingCandidate
		? DefenseTargetingSettings.PlayerThreatReleaseRadius
		: DefenseTargetingSettings.PlayerThreatAcquireRadius;

	if (CandidateDistance2D > FMath::Max(0.f, Radius))
	{
		return false;
	}

	const float ObjectiveThreatRadius = bAlreadyTargetingCandidate
		? DefenseTargetingSettings.PlayerThreatObjectiveReleaseRadius
		: DefenseTargetingSettings.PlayerThreatObjectiveAcquireRadius;
	if (FVector::Dist2D(CandidateLocation, ObjectiveLocation) > ObjectiveThreatRadius)
	{
		return false;
	}

	if (DefenseTargetingSettings.bRequirePlayerCloserThanObjective
		&& CandidateDistance2D + FMath::Max(0.f, DefenseTargetingSettings.PlayerDistanceBias) > ObjectiveDistance2D)
	{
		return false;
	}

	return true;
}

bool AEnemyAIController::RefreshDefenseObjectiveTarget(const bool bSendTargetAcquiredEvent, const bool bStopCurrentMovement)
{
	AActor* Objective = DefenseObjectiveTarget.Get();
	if (!IsValid(Objective) || HasDeadStateTag(Objective))
	{
		return false;
	}

	AActor* DesiredTarget = FindBestDefenseThreatTarget();
	EAeyerjiEnemyTargetSource DesiredSource = EAeyerjiEnemyTargetSource::HostileActor;
	if (!DesiredTarget)
	{
		DesiredTarget = Objective;
		DesiredSource = EAeyerjiEnemyTargetSource::DefenseObjective;
	}

	if (CurrentTarget.Get() == DesiredTarget)
	{
		CurrentTargetSource = DesiredSource;
		return false;
	}

	AssignCurrentTarget(DesiredTarget, DesiredSource, bSendTargetAcquiredEvent, bStopCurrentMovement);
	return true;
}

void AEnemyAIController::NotifyDamagedBy(AActor* DamageInstigator)
{
	if (!HasAuthority())
	{
		return;
	}

	AActor* ThreatActor = DamageInstigator;
	if (const AController* InstigatorController = Cast<AController>(ThreatActor))
	{
		ThreatActor = InstigatorController->GetPawn();
	}

	const APawn* ControlledPawn = GetPawn();
	if (!IsValid(ThreatActor)
		|| !IsValid(ControlledPawn)
		|| ThreatActor == ControlledPawn
		|| HasDeadStateTag(ThreatActor)
		|| GetTeamAttitudeTowards(*ThreatActor) != ETeamAttitude::Hostile)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	RecentDamageThreat = ThreatActor;
	RecentDamageThreatExpiryTime = World->GetTimeSeconds() + DefenseDamageThreatMemorySeconds;
	RememberTargetLocation(ThreatActor);

	if (IsValid(DefenseObjectiveTarget.Get()) && !HasDeadStateTag(DefenseObjectiveTarget.Get()))
	{
		AssignCurrentTarget(ThreatActor, EAeyerjiEnemyTargetSource::HostileActor, /*bSendTargetAcquiredEvent=*/false, /*bStopCurrentMovement=*/true);
	}
	else
	{
		AssignCurrentTarget(ThreatActor, EAeyerjiEnemyTargetSource::HostileActor, /*bSendTargetAcquiredEvent=*/true, /*bStopCurrentMovement=*/true);
	}
}

void AEnemyAIController::SendAICrowdControlEvent(const FGameplayTag& EventTag)
{
	if (!HasAuthority() || !EventTag.IsValid() || !StateTreeComponent)
	{
		return;
	}

	StateTreeComponent->SendStateTreeEvent(FStateTreeEvent(EventTag));
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

	return GetTeamAttitudeTowards(*Candidate) == ETeamAttitude::Hostile
		&& ShouldAcquireTargetWithDefenseObjective(Candidate);
}

AActor* AEnemyAIController::FindBestDefenseThreatTarget() const
{
	const APawn* ControlledPawn = GetPawn();
	const UWorld* World = ControlledPawn ? ControlledPawn->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}

	if (IsRecentDamageThreatValid())
	{
		return RecentDamageThreat.Get();
	}

	AActor* BestTarget = nullptr;
	float BestDistanceSq = TNumericLimits<float>::Max();
	const FVector SelfLocation = ControlledPawn->GetActorLocation();

	TArray<AActor*> PerceivedActors;
	if (Perception)
	{
		Perception->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), PerceivedActors);
	}

	for (AActor* Candidate : PerceivedActors)
	{
		if (!IsTargetValidForAcquisition(Candidate))
		{
			continue;
		}

		const float DistanceSq = FVector::DistSquared2D(SelfLocation, Candidate->GetActorLocation());
		if (!BestTarget || DistanceSq < BestDistanceSq)
		{
			BestTarget = Candidate;
			BestDistanceSq = DistanceSq;
		}
	}

	return BestTarget;
}

bool AEnemyAIController::IsRecentDamageThreatValid() const
{
	const AActor* ThreatActor = RecentDamageThreat.Get();
	const APawn* ControlledPawn = GetPawn();
	const UWorld* World = ControlledPawn ? ControlledPawn->GetWorld() : nullptr;
	if (!World
		|| !IsValid(ThreatActor)
		|| !IsValid(ControlledPawn)
		|| ThreatActor == ControlledPawn
		|| HasDeadStateTag(ThreatActor)
		|| World->GetTimeSeconds() > RecentDamageThreatExpiryTime)
	{
		return false;
	}

	return GetTeamAttitudeTowards(*ThreatActor) == ETeamAttitude::Hostile;
}

void AEnemyAIController::AssignCurrentTarget(
	AActor* NewTarget,
	const EAeyerjiEnemyTargetSource NewSource,
	const bool bSendTargetAcquiredEvent,
	const bool bStopCurrentMovement)
{
	CurrentTarget = NewTarget;
	CurrentTargetSource = IsValid(NewTarget) ? NewSource : EAeyerjiEnemyTargetSource::None;

	if (IsValid(NewTarget))
	{
		SetFocus(NewTarget, EAIFocusPriority::Gameplay);
	}
	else
	{
		ClearFocus(EAIFocusPriority::Gameplay);
	}

	if (IsValid(NewTarget) && NewSource == EAeyerjiEnemyTargetSource::HostileActor)
	{
		RememberTargetLocation(NewTarget);
	}

	if (bStopCurrentMovement)
	{
		StopMovement();
	}

	if (bSendTargetAcquiredEvent && StateTreeComponent)
	{
		StateTreeComponent->SendStateTreeEvent(FStateTreeEvent(TargetAcquiredTag()));
	}
}

void AEnemyAIController::ClearLastKnownTarget()
{
	LastKnownTargetActor.Reset();
	LastKnownTargetLocation = FVector::ZeroVector;
	LastKnownTargetTime = -1.0;
	bHasLastKnownTarget = false;
}

void AEnemyAIController::ResetForPooledReuse(const FVector& NewHomeLocation)
{
	StopMovement();
	ClearFocus(EAIFocusPriority::Gameplay);
	CurrentTarget = nullptr;
	CurrentTargetSource = EAeyerjiEnemyTargetSource::None;
	DefenseObjectiveTarget = nullptr;
	DefenseTargetingSettings = FAeyerjiDefenseTargetingSettings();
	RecentDamageThreat.Reset();
	RecentDamageThreatExpiryTime = -1.0;
	bPermanentRiftPursuit = false;
	HomeLocation = NewHomeLocation;
	ClearLastKnownTarget();

	if (Perception)
	{
		Perception->SetComponentTickEnabled(true);
		Perception->ForgetAll();
	}

	ApplyPerceptionSettings();

	if (StateTreeComponent && DefaultStateTree)
	{
		StateTreeComponent->StopLogic(TEXT("PooledReuse"));
		StateTreeComponent->SetStateTree(DefaultStateTree);
		StateTreeComponent->StartLogic();
	}
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
