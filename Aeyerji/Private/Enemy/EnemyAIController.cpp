#include "Enemy/EnemyAIController.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "StateTree.h"
#include "GameplayTagContainer.h"

#include "Perception/AISenseConfig_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Navigation/CrowdFollowingComponent.h"

// Name for perception event tags
static const FGameplayTag Tag_TargetAcquired = FGameplayTag::RequestGameplayTag(TEXT("Event.TargetAcquired"));
static const FGameplayTag Tag_TargetLost = FGameplayTag::RequestGameplayTag(TEXT("Event.TargetLost"));

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

// EnemyAIController.cpp  (UE 5.6)
void AEnemyAIController::OnPossess(APawn* InPawn)
{
    Super::OnPossess(InPawn);

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
            if (!IsValid(Actor) || Actor->Tags.Contains("State.Dead"))
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
            CurrentTarget = Best;
            StateTreeComponent->SendStateTreeEvent(FStateTreeEvent(Tag_TargetAcquired));
            UE_LOG(LogTemp, Verbose, TEXT("Initial target acquired on possess: %s"), *Best->GetName());
        }
    }
}

void AEnemyAIController::OnTargetPerception(AActor* Actor, FAIStimulus Stimulus)
{
	if (!StateTreeComponent || !Actor)
		return;

	const bool bSensed = Stimulus.WasSuccessfullySensed();
	const bool bIsDead = Actor->Tags.Contains("State.Dead");

    if (bSensed && !bIsDead)
    {
        if (CurrentTarget != Actor)
        {
            // Only announce when we actually swapped to a new target.
            CurrentTarget = Actor;
            StopMovement();
            StateTreeComponent->SendStateTreeEvent(FStateTreeEvent(Tag_TargetAcquired));
            UE_LOG(LogTemp, Log, TEXT("Target acquired: %s"), *Actor->GetName());
        }
    }
	else if (Actor == CurrentTarget && (!bSensed || bIsDead))        // ← added tests
	{
		CurrentTarget = nullptr;
		StateTreeComponent->SendStateTreeEvent(FStateTreeEvent(Tag_TargetLost));
		UE_LOG(LogTemp, Display, TEXT("Target lost: %s"), *Actor->GetName());
	}
}

void AEnemyAIController::OnPerceptionUpdated(const TArray<AActor*>& UpdatedActors)
{
	if (!StateTreeComponent || !CurrentTarget)
	{
		return;
	}

	// If the current target is updated and is now dead, clear it
	for (AActor* UpdatedActor : UpdatedActors)
	{
		if (UpdatedActor && UpdatedActor == CurrentTarget && UpdatedActor->Tags.Contains("State.Dead"))
		{
			CurrentTarget = nullptr;
			FStateTreeEvent Event(Tag_TargetLost);
			UE_LOG(LogTemp, Display, TEXT("TargetLostEvent!"));
			StateTreeComponent->SendStateTreeEvent(Event);
			break;
		}
	}
}

void AEnemyAIController::ApplyPerceptionSettings()
{
    if (!Perception)
    {
        return;
    }

    if (bOverridePerceptionWithProperties)
    {
        // Properties drive the configs
        if (SightSenseConfig)
        {
            SightSenseConfig->SightRadius = SightRadius;
            SightSenseConfig->LoseSightRadius = LoseSightRadius;
            SightSenseConfig->PeripheralVisionAngleDegrees = PeripheralVisionAngleDegrees;
            SightSenseConfig->DetectionByAffiliation.bDetectEnemies   = bDetectEnemies;
            SightSenseConfig->DetectionByAffiliation.bDetectFriendlies= bDetectFriendlies;
            SightSenseConfig->DetectionByAffiliation.bDetectNeutrals  = bDetectNeutrals;
            Perception->ConfigureSense(*SightSenseConfig);
            Perception->SetDominantSense(*SightSenseConfig->GetSenseImplementation());
        }
        if (HearingSenseConfig)
        {
            const float EffectiveHearingRange = FMath::Max(HearingRange, LoSHearingRange);
            HearingSenseConfig->HearingRange = EffectiveHearingRange;
            HearingSenseConfig->DetectionByAffiliation.bDetectEnemies    = bDetectEnemies;
            HearingSenseConfig->DetectionByAffiliation.bDetectFriendlies = bDetectFriendlies;
            HearingSenseConfig->DetectionByAffiliation.bDetectNeutrals   = bDetectNeutrals;
            Perception->ConfigureSense(*HearingSenseConfig);
        }
    }
    else
    {
        // Sense configs (BP subobjects) are the source of truth
        if (SightSenseConfig)
        {
            Perception->ConfigureSense(*SightSenseConfig);
            Perception->SetDominantSense(*SightSenseConfig->GetSenseImplementation());
        }
        if (HearingSenseConfig)
        {
            Perception->ConfigureSense(*HearingSenseConfig);
        }
        // Optionally reflect subobject values back to properties for clarity
        if (SightSenseConfig)
        {
            SightRadius = SightSenseConfig->SightRadius;
            LoseSightRadius = SightSenseConfig->LoseSightRadius;
            PeripheralVisionAngleDegrees = SightSenseConfig->PeripheralVisionAngleDegrees;
            bDetectEnemies    = SightSenseConfig->DetectionByAffiliation.bDetectEnemies;
            bDetectFriendlies = SightSenseConfig->DetectionByAffiliation.bDetectFriendlies;
            bDetectNeutrals   = SightSenseConfig->DetectionByAffiliation.bDetectNeutrals;
        }
        if (HearingSenseConfig)
        {
            HearingRange = HearingSenseConfig->HearingRange;
            LoSHearingRange = HearingSenseConfig->HearingRange;
        }
    }

    Perception->RequestStimuliListenerUpdate();
}
