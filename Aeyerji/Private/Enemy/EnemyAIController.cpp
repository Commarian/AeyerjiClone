#include "Enemy/EnemyAIController.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Enemy/EnemyParentNative.h"
#include "AeyerjiCharacterMovementComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Logging/AeyerjiLog.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "GameFramework/PlayerController.h"
#include "StateTree.h"
#include "GameplayTagContainer.h"

#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "Navigation/CrowdManager.h"
#if WITH_EDITOR
#include "UObject/UnrealType.h"
#endif

namespace
{
	constexpr double DefenseDamageThreatMemorySeconds = 4.0;
	constexpr float MaxPerceptionRange = 100000.f;
	constexpr float MaxDefenseThreatRange = 100000.f;

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

	// Perception updates arrive per sense. A hearing stimulus can expire while sight still
	// tracks the same actor (or vice versa), so target loss must consider every configured sense.
	bool IsActorStillPerceivedByAnySense(UAIPerceptionComponent* PerceptionComponent, AActor* Actor)
	{
		if (!PerceptionComponent || !IsValid(Actor))
		{
			return false;
		}

		FActorPerceptionBlueprintInfo PerceptionInfo;
		if (!PerceptionComponent->GetActorsPerception(Actor, PerceptionInfo))
		{
			return false;
		}

		return PerceptionInfo.LastSensedStimuli.ContainsByPredicate(
			[](const FAIStimulus& SenseStimulus)
			{
				return SenseStimulus.WasSuccessfullySensed();
			});
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
	// This controller starts its StateTree explicitly after applying pooled/Blueprint state.
	bStartAILogicOnPossess = false;

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

void AEnemyAIController::ResetChaseSprintCadence()
{
	bChaseSprintActive = false;
	ChaseSprintEndTime = -1.0;
	ChaseSprintRecoveryEndTime = -1.0;
}

void AEnemyAIController::BeginChaseSprintRecovery(const double CurrentTimeSeconds)
{
	const double SafeCurrentTime = FMath::IsFinite(CurrentTimeSeconds)
		? FMath::Max(0.0, CurrentTimeSeconds)
		: 0.0;
	const double SafeRecoveryDuration = FMath::IsFinite(ChaseSprintRecoverySeconds)
		? FMath::Max(0.0, static_cast<double>(ChaseSprintRecoverySeconds))
		: 5.0;

	bChaseSprintActive = false;
	ChaseSprintEndTime = -1.0;
	ChaseSprintRecoveryEndTime = SafeCurrentTime + SafeRecoveryDuration;
}

float AEnemyAIController::ResolveChaseCadenceSpeed(
	const float DistanceToTarget,
	const float AttackRange,
	const double CurrentTimeSeconds,
	const float WalkSpeed,
	const float RunSpeed)
{
	const float SafeWalkSpeed = FMath::IsFinite(WalkSpeed) ? FMath::Max(50.f, WalkSpeed) : 50.f;
	const float SafeRunSpeed = FMath::IsFinite(RunSpeed) ? FMath::Max(50.f, RunSpeed) : SafeWalkSpeed;
	if (!bEnableChaseSprintCadence)
	{
		ResetChaseSprintCadence();
		return SafeRunSpeed;
	}

	const double SafeCurrentTime = FMath::IsFinite(CurrentTimeSeconds)
		? FMath::Max(0.0, CurrentTimeSeconds)
		: 0.0;
	const float SafeAttackRange = FMath::IsFinite(AttackRange) ? FMath::Max(0.f, AttackRange) : 0.f;
	const float SafeReengageDistance = FMath::IsFinite(ChaseSprintReengageDistance)
		? FMath::Max(0.f, ChaseSprintReengageDistance)
		: 0.f;
	const float SprintThreshold = SafeAttackRange + SafeReengageDistance;
	const bool bTargetIsFar = FMath::IsFinite(DistanceToTarget) && DistanceToTarget > SprintThreshold;

	const double SafeSprintDuration = FMath::IsFinite(ChaseSprintDurationSeconds)
		? FMath::Max(0.05, static_cast<double>(ChaseSprintDurationSeconds))
		: 1.5;
	if (bChaseSprintActive
		&& (!bTargetIsFar || SafeCurrentTime >= ChaseSprintEndTime))
	{
		BeginChaseSprintRecovery(SafeCurrentTime);
	}

	if (!bChaseSprintActive
		&& bTargetIsFar
		&& SafeCurrentTime >= ChaseSprintRecoveryEndTime)
	{
		bChaseSprintActive = true;
		ChaseSprintEndTime = SafeCurrentTime + SafeSprintDuration;
	}

	return bChaseSprintActive ? SafeRunSpeed : SafeWalkSpeed;
}

void AEnemyAIController::UpdateChaseSprintCadence(
	const float DistanceToTarget,
	const float AttackRange,
	const float DeltaTime,
	const bool bForceImmediateSpeed)
{
	if (!HasAuthority() || !bEnableChaseSprintCadence)
	{
		return;
	}

	ACharacter* CharacterPawn = Cast<ACharacter>(GetPawn());
	UCharacterMovementComponent* Movement = CharacterPawn ? CharacterPawn->GetCharacterMovement() : nullptr;
	UWorld* World = CharacterPawn ? CharacterPawn->GetWorld() : nullptr;
	if (!CharacterPawn || !CharacterPawn->HasAuthority() || !Movement || !World)
	{
		return;
	}

	const UAbilitySystemComponent* ASC =
		UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(CharacterPawn, /*LookForComponent=*/true);
	const float CurrentSpeed = FMath::IsFinite(Movement->MaxWalkSpeed)
		? FMath::Max(50.f, Movement->MaxWalkSpeed)
		: 50.f;
	const float AttributeWalkSpeed = ASC
		? ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetWalkSpeedAttribute())
		: CurrentSpeed;
	const float WalkSpeed = FMath::IsFinite(AttributeWalkSpeed) && AttributeWalkSpeed > KINDA_SMALL_NUMBER
		? AttributeWalkSpeed
		: CurrentSpeed;
	const float AttributeRunSpeed = ASC
		? ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetRunSpeedAttribute())
		: WalkSpeed;
	const float RunSpeed = FMath::IsFinite(AttributeRunSpeed) && AttributeRunSpeed > KINDA_SMALL_NUMBER
		? AttributeRunSpeed
		: WalkSpeed;

	const float DesiredSpeed = ResolveChaseCadenceSpeed(
		DistanceToTarget,
		AttackRange,
		World->GetTimeSeconds(),
		WalkSpeed,
		RunSpeed);
	const float SafeDeltaTime = FMath::IsFinite(DeltaTime) ? FMath::Max(0.f, DeltaTime) : 0.f;
	const float SafeChangeRate = FMath::IsFinite(ChaseSpeedChangeRate)
		? FMath::Max(0.f, ChaseSpeedChangeRate)
		: 0.f;
	const float NewSpeed = bForceImmediateSpeed || SafeDeltaTime <= 0.f || SafeChangeRate <= 0.f
		? DesiredSpeed
		: FMath::FInterpConstantTo(CurrentSpeed, DesiredSpeed, SafeDeltaTime, SafeChangeRate);
	Movement->MaxWalkSpeed = FMath::Max(50.f, NewSpeed);
}

void AEnemyAIController::EndChaseSprintCadence()
{
	if (!HasAuthority() || !bEnableChaseSprintCadence)
	{
		return;
	}

	if (bChaseSprintActive)
	{
		const UWorld* World = GetWorld();
		BeginChaseSprintRecovery(World ? World->GetTimeSeconds() : 0.0);
	}

	ACharacter* CharacterPawn = Cast<ACharacter>(GetPawn());
	UCharacterMovementComponent* Movement = CharacterPawn ? CharacterPawn->GetCharacterMovement() : nullptr;
	const UAbilitySystemComponent* ASC = CharacterPawn
		? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(CharacterPawn, /*LookForComponent=*/true)
		: nullptr;
	if (!CharacterPawn || !CharacterPawn->HasAuthority() || !Movement || !ASC)
	{
		return;
	}

	const float AttributeWalkSpeed = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetWalkSpeedAttribute());
	if (FMath::IsFinite(AttributeWalkSpeed) && AttributeWalkSpeed > KINDA_SMALL_NUMBER)
	{
		Movement->MaxWalkSpeed = FMath::Max(50.f, AttributeWalkSpeed);
	}
}

#if WITH_DEV_AUTOMATION_TESTS
float AEnemyAIController::ResolveChaseCadenceSpeedForAutomation(
	const float DistanceToTarget,
	const float AttackRange,
	const double CurrentTimeSeconds,
	const float WalkSpeed,
	const float RunSpeed)
{
	return ResolveChaseCadenceSpeed(
		DistanceToTarget,
		AttackRange,
		CurrentTimeSeconds,
		WalkSpeed,
		RunSpeed);
}

void AEnemyAIController::ResetChaseSprintCadenceForAutomation()
{
	ResetChaseSprintCadence();
}

void AEnemyAIController::EndChaseSprintCadenceForAutomation(const double CurrentTimeSeconds)
{
	if (bChaseSprintActive)
	{
		BeginChaseSprintRecovery(CurrentTimeSeconds);
	}
}
#endif

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

void AEnemyAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);
	ResetChaseSprintCadence();
	ApplyStableCrowdFacingPolicy();
	SetPathFollowingGameplayEnabled(true, TEXT("Possess"));

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
		const FVector PawnLocation = InPawn->GetActorLocation();
		HomeLocation = PawnLocation.ContainsNaN() ? FVector::ZeroVector : PawnLocation;
    }
	RestartConfiguredStateTree(TEXT("Possess"));

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
			const FVector ActorLocation = Actor->GetActorLocation();
			if (MyLoc.ContainsNaN() || ActorLocation.ContainsNaN())
			{
				continue;
			}
			const float DistSq = FVector::DistSquared(MyLoc, ActorLocation);
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

void AEnemyAIController::ApplyStableCrowdFacingPolicy()
{
	if (!bStabilizeCrowdFacing)
	{
		return;
	}

	ACharacter* CharacterPawn = Cast<ACharacter>(GetPawn());
	UCharacterMovementComponent* Movement = CharacterPawn ? CharacterPawn->GetCharacterMovement() : nullptr;
	if (!CharacterPawn || !Movement)
	{
		return;
	}

	// Detour Crowd may alternate its avoidance velocity from side to side when agents
	// block one another. Keep that velocity for navigation, but do not use it as visual yaw.
	CharacterPawn->bUseControllerRotationYaw = false;
	Movement->bOrientRotationToMovement = false;
	Movement->bUseControllerDesiredRotation = true;
	Movement->RotationRate.Yaw = FMath::IsFinite(StableFacingRotationRate)
		? FMath::Max(0.f, StableFacingRotationRate)
		: 540.f;
}

void AEnemyAIController::OnUnPossess()
{
	ResetChaseSprintCadence();
	if (StateTreeComponent && StateTreeComponent->IsRunning())
	{
		StateTreeComponent->StopLogic(TEXT("UnPossess"));
	}
	Super::OnUnPossess();
}

void AEnemyAIController::RestartConfiguredStateTree(const TCHAR* StopReason)
{
	if (!StateTreeComponent || !DefaultStateTree)
	{
		return;
	}

	if (StateTreeComponent->IsRunning())
	{
		StateTreeComponent->StopLogic(StopReason);
	}
	StateTreeComponent->SetStateTree(DefaultStateTree);
	StateTreeComponent->StartLogic();
	if (StateTreeComponent->IsRunning() && StateTreeComponent->IsPaused())
	{
		// UE 5.8 StartTree does not clear the pause bit preserved by a prior PauseLogic -> StopLogic cycle.
		StateTreeComponent->ResumeLogic(TEXT("ConfiguredStateTreeRestart"));
	}
}

void AEnemyAIController::OnTargetPerception(AActor* Actor, FAIStimulus Stimulus)
{
	if (!HasAuthority() || !Actor)
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
		if (!bIsDead && IsActorStillPerceivedByAnySense(Perception, Actor))
		{
			// One sense was lost, but another still owns perception of this target.
			RememberTargetLocation(Actor);
			return;
		}

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
		UE_LOG(LogTemp, Verbose, TEXT("Target lost: %s"), *Actor->GetName());
	}
}

void AEnemyAIController::OnPerceptionUpdated(const TArray<AActor*>& UpdatedActors)
{
	if (!HasAuthority() || !CurrentTarget)
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
			UE_LOG(LogTemp, Verbose, TEXT("Target lost after perception update."));
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
	if (!HasAuthority())
	{
		return;
	}

	const EAeyerjiEnemyTargetSource NewSource = IsValid(NewTarget) && NewTarget == DefenseObjectiveTarget.Get()
		? EAeyerjiEnemyTargetSource::DefenseObjective
		: (IsValid(NewTarget) ? EAeyerjiEnemyTargetSource::HostileActor : EAeyerjiEnemyTargetSource::None);
	if (NewSource == EAeyerjiEnemyTargetSource::HostileActor && !IsTargetValidForAcquisition(NewTarget))
	{
		return;
	}

	AssignCurrentTarget(NewTarget, NewSource, /*bSendTargetAcquiredEvent=*/false, /*bStopCurrentMovement=*/false);
}

void AEnemyAIController::SetDefenseObjectiveTargetActor(AActor* NewTarget)
{
	if (!HasAuthority())
	{
		return;
	}
	if (IsValid(NewTarget) && (NewTarget->GetWorld() != GetWorld() || NewTarget == GetPawn()))
	{
		return;
	}

	if (!IsValid(NewTarget) && CurrentTarget == DefenseObjectiveTarget)
	{
		CurrentTarget = nullptr;
		CurrentTargetSource = EAeyerjiEnemyTargetSource::None;
	}

	DefenseObjectiveTarget = NewTarget;
}

void AEnemyAIController::ConfigureDefenseObjectiveTargeting(AActor* NewTarget, const FAeyerjiDefenseTargetingSettings& TargetingSettings)
{
	if (!HasAuthority())
	{
		return;
	}
	if (IsValid(NewTarget) && (NewTarget->GetWorld() != GetWorld() || NewTarget == GetPawn()))
	{
		return;
	}

	DefenseObjectiveTarget = NewTarget;
	DefenseTargetingSettings = TargetingSettings;
	DefenseTargetingSettings.PlayerThreatAcquireRadius = FMath::IsFinite(DefenseTargetingSettings.PlayerThreatAcquireRadius)
		? FMath::Clamp(DefenseTargetingSettings.PlayerThreatAcquireRadius, 0.f, MaxDefenseThreatRange)
		: FAeyerjiDefenseTargetingSettings().PlayerThreatAcquireRadius;
	DefenseTargetingSettings.PlayerThreatReleaseRadius = FMath::IsFinite(DefenseTargetingSettings.PlayerThreatReleaseRadius)
		? FMath::Clamp(DefenseTargetingSettings.PlayerThreatReleaseRadius, 0.f, MaxDefenseThreatRange)
		: FAeyerjiDefenseTargetingSettings().PlayerThreatReleaseRadius;
	DefenseTargetingSettings.PlayerThreatReleaseRadius = FMath::Max(
		DefenseTargetingSettings.PlayerThreatAcquireRadius,
		DefenseTargetingSettings.PlayerThreatReleaseRadius);
	DefenseTargetingSettings.PlayerThreatObjectiveAcquireRadius = FMath::IsFinite(DefenseTargetingSettings.PlayerThreatObjectiveAcquireRadius)
		&& DefenseTargetingSettings.PlayerThreatObjectiveAcquireRadius > 0.f
		? FMath::Min(DefenseTargetingSettings.PlayerThreatObjectiveAcquireRadius, MaxDefenseThreatRange)
		: FAeyerjiDefenseTargetingSettings().PlayerThreatObjectiveAcquireRadius;
	DefenseTargetingSettings.PlayerThreatObjectiveReleaseRadius = FMath::IsFinite(DefenseTargetingSettings.PlayerThreatObjectiveReleaseRadius)
		&& DefenseTargetingSettings.PlayerThreatObjectiveReleaseRadius > 0.f
		? FMath::Min(DefenseTargetingSettings.PlayerThreatObjectiveReleaseRadius, MaxDefenseThreatRange)
		: FAeyerjiDefenseTargetingSettings().PlayerThreatObjectiveReleaseRadius;
	DefenseTargetingSettings.PlayerThreatObjectiveReleaseRadius = FMath::Max(
		DefenseTargetingSettings.PlayerThreatObjectiveAcquireRadius,
		DefenseTargetingSettings.PlayerThreatObjectiveReleaseRadius);
	DefenseTargetingSettings.PlayerDistanceBias = FMath::IsFinite(DefenseTargetingSettings.PlayerDistanceBias)
		? FMath::Clamp(DefenseTargetingSettings.PlayerDistanceBias, 0.f, MaxDefenseThreatRange)
		: FAeyerjiDefenseTargetingSettings().PlayerDistanceBias;

	RefreshDefenseObjectiveTarget();
}

bool AEnemyAIController::ShouldAcquireTargetWithDefenseObjective(AActor* Candidate) const
{
	if (!IsValid(Candidate) || Candidate->GetWorld() != GetWorld())
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
	if (SelfLocation.ContainsNaN())
	{
		return false;
	}
	const FVector CandidateLocation = Candidate->GetActorLocation();
	const FVector ObjectiveLocation = Objective->GetActorLocation();
	if (SelfLocation.ContainsNaN() || CandidateLocation.ContainsNaN() || ObjectiveLocation.ContainsNaN())
	{
		return false;
	}
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
	if (!HasAuthority())
	{
		return false;
	}

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
	if (!HasAuthority() || !IsValid(Candidate) || Candidate->GetWorld() != GetWorld())
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
	if (SelfLocation.ContainsNaN())
	{
		return nullptr;
	}

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

		const FVector CandidateLocation = Candidate->GetActorLocation();
		if (CandidateLocation.ContainsNaN())
		{
			continue;
		}
		const float DistanceSq = FVector::DistSquared2D(SelfLocation, CandidateLocation);
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
	if (!HasAuthority())
	{
		return;
	}

	ResetChaseSprintCadence();
	StopMovement();
	SetPathFollowingGameplayEnabled(true, TEXT("PooledReuse"));
	ClearFocus(EAIFocusPriority::Gameplay);
	CurrentTarget = nullptr;
	CurrentTargetSource = EAeyerjiEnemyTargetSource::None;
	DefenseObjectiveTarget = nullptr;
	DefenseTargetingSettings = FAeyerjiDefenseTargetingSettings();
	RecentDamageThreat.Reset();
	RecentDamageThreatExpiryTime = -1.0;
	bPermanentRiftPursuit = false;
	const APawn* ControlledPawn = GetPawn();
	HomeLocation = !NewHomeLocation.ContainsNaN()
		? NewHomeLocation
		: (ControlledPawn ? ControlledPawn->GetActorLocation() : FVector::ZeroVector);
	ClearLastKnownTarget();

	if (Perception)
	{
		Perception->SetComponentTickEnabled(true);
		Perception->ForgetAll();
	}

	ApplyPerceptionSettings();
	EnsureConfiguredStateTreeRunning(TEXT("PooledReuse"));
}

bool AEnemyAIController::SetPathFollowingGameplayEnabled(const bool bEnabled, const TCHAR* Reason)
{
	UCrowdFollowingComponent* CrowdFollowing =
		Cast<UCrowdFollowingComponent>(GetPathFollowingComponent());
	ACharacter* CharacterPawn = Cast<ACharacter>(GetPawn());
	UAeyerjiCharacterMovementComponent* Movement = CharacterPawn
		? Cast<UAeyerjiCharacterMovementComponent>(CharacterPawn->GetCharacterMovement())
		: nullptr;

	if (!CrowdFollowing)
	{
		if (Movement)
		{
			Movement->SetAvoidanceEnabled(bEnabled && Movement->bEnableRVOAvoidance);
		}
		return bEnabled;
	}

	if (!bEnabled)
	{
		// SetCrowdSimulationState only changes registration while the path follower is idle.
		StopMovement();
		CrowdFollowing->SetCrowdSimulationState(ECrowdSimulationState::Disabled);
		if (Movement)
		{
			Movement->SetAvoidanceEnabled(false);
		}
		return false;
	}

	CrowdFollowing->SetCrowdSimulationState(ECrowdSimulationState::Enabled);
	UCrowdManager* CrowdManager = UCrowdManager::GetCurrent(GetWorld());
	const bool bHasValidCrowdSlot =
		CrowdManager && CrowdManager->IsAgentValid(CrowdFollowing);
	if (!bHasValidCrowdSlot)
	{
		// Detour's default capacity is 50 agents. A registered component without a valid
		// Detour index remains "Moving" but never receives ApplyCrowdAgentVelocity.
		CrowdFollowing->SetCrowdSimulationState(ECrowdSimulationState::Disabled);
		if (Movement)
		{
			Movement->SetAvoidanceEnabled(Movement->bEnableRVOAvoidance);
		}
		AJ_LOG_VERBOSITY(Warning, this,
			TEXT("[EnemyPathFollowing] Crowd slot unavailable; using standard path following with RVO. Reason=%s Pawn=%s PathFollowing=%s"),
			Reason ? Reason : TEXT("Unknown"),
			*GetNameSafe(GetPawn()),
			*GetNameSafe(CrowdFollowing));
		return true;
	}

	// Detour Crowd and CharacterMovement RVO are alternative avoidance solvers. Running both
	// can rewrite the same requested velocity, so a valid Crowd agent owns avoidance here.
	if (Movement)
	{
		Movement->SetAvoidanceEnabled(false);
	}
	AJ_LOG_VERBOSITY(Verbose, this,
		TEXT("[EnemyPathFollowing] Crowd slot active. Reason=%s Pawn=%s PathFollowing=%s"),
		Reason ? Reason : TEXT("Unknown"),
		*GetNameSafe(GetPawn()),
		*GetNameSafe(CrowdFollowing));
	return true;
}

bool AEnemyAIController::EnsureConfiguredStateTreeRunning(const TCHAR* ActivationReason)
{
	if (!HasAuthority())
	{
		return false;
	}

	if (!StateTreeComponent || !DefaultStateTree)
	{
		AJ_LOG_VERBOSITY(Warning, this,
			TEXT("[EnemyActivation] Cannot run StateTree Reason=%s Pawn=%s Component=%s Asset=%s"),
			ActivationReason ? ActivationReason : TEXT("Unknown"),
			*GetNameSafe(GetPawn()),
			*GetNameSafe(StateTreeComponent),
			*GetNameSafe(DefaultStateTree));
		return false;
	}

	if (StateTreeComponent->IsRunning())
	{
		return true;
	}

	RestartConfiguredStateTree(ActivationReason ? ActivationReason : TEXT("EncounterActivation"));
	const bool bRunning = StateTreeComponent->IsRunning();
	const bool bPaused = StateTreeComponent->IsPaused();
	if (bRunning && !bPaused)
	{
		AJ_LOG_VERBOSITY(Verbose, this,
			TEXT("[EnemyActivation] StateTree wake complete Reason=%s Pawn=%s Asset=%s Running=1 Paused=0"),
			ActivationReason ? ActivationReason : TEXT("Unknown"),
			*GetNameSafe(GetPawn()),
			*GetNameSafe(DefaultStateTree));
	}
	else
	{
		AJ_LOG_VERBOSITY(Warning, this,
			TEXT("[EnemyActivation] StateTree wake failed Reason=%s Pawn=%s Asset=%s Running=%d Paused=%d"),
			ActivationReason ? ActivationReason : TEXT("Unknown"),
			*GetNameSafe(GetPawn()),
			*GetNameSafe(DefaultStateTree),
			bRunning ? 1 : 0,
			bPaused ? 1 : 0);
	}
	return bRunning && !bPaused;
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

	const FVector TargetLocation = Target->GetActorLocation();
	if (TargetLocation.ContainsNaN())
	{
		return;
	}

	LastKnownTargetActor = Target;
	LastKnownTargetLocation = TargetLocation;
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
		SightSenseConfig->SightRadius = FMath::IsFinite(SightSenseConfig->SightRadius)
			? FMath::Clamp(SightSenseConfig->SightRadius, 0.f, MaxPerceptionRange)
			: 1500.f;
		SightSenseConfig->LoseSightRadius = FMath::IsFinite(SightSenseConfig->LoseSightRadius)
			? FMath::Clamp(SightSenseConfig->LoseSightRadius, SightSenseConfig->SightRadius, MaxPerceptionRange)
			: FMath::Max(SightSenseConfig->SightRadius, 2500.f);
		SightSenseConfig->PeripheralVisionAngleDegrees = FMath::IsFinite(SightSenseConfig->PeripheralVisionAngleDegrees)
			? FMath::Clamp(SightSenseConfig->PeripheralVisionAngleDegrees, 0.f, 180.f)
			: 55.f;
		Perception->ConfigureSense(*SightSenseConfig);
		Perception->SetDominantSense(*SightSenseConfig->GetSenseImplementation());
	}

	if (HearingSenseConfig)
	{
		HearingSenseConfig->HearingRange = FMath::IsFinite(HearingSenseConfig->HearingRange)
			? FMath::Clamp(HearingSenseConfig->HearingRange, 0.f, MaxPerceptionRange)
			: 1800.f;
PRAGMA_DISABLE_DEPRECATION_WARNINGS
		HearingSenseConfig->LoSHearingRange = FMath::IsFinite(HearingSenseConfig->LoSHearingRange)
			? FMath::Clamp(HearingSenseConfig->LoSHearingRange, HearingSenseConfig->HearingRange, MaxPerceptionRange)
			: FMath::Max(HearingSenseConfig->HearingRange, 2400.f);
PRAGMA_ENABLE_DEPRECATION_WARNINGS
		Perception->ConfigureSense(*HearingSenseConfig);
	}

	SyncDeprecatedPerceptionPropertiesFromConfigs();

    Perception->RequestStimuliListenerUpdate();
}
