// Fill out your copyright notice in the Description page of Project Settings.


#include "Enemy/Tasks/STT_MoveToAttackRangeTask.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/MovementComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "AbilitySystemComponent.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "StateTreeExecutionContext.h"
#include "Enemy/EnemyAIController.h"

#define MOVE_TO_ATTACK_RANGE_LOGGING 0

#if MOVE_TO_ATTACK_RANGE_LOGGING
DEFINE_LOG_CATEGORY_STATIC(LogMoveToAttackRangeTask, Log, All);

static const TCHAR* MoveToAttackRangeTask_GetNetModeString(const UWorld* World)
{
	if (!World)
	{
		return TEXT("NoWorld");
	}

	switch (World->GetNetMode())
	{
	case NM_Standalone:
		return TEXT("Standalone");
	case NM_DedicatedServer:
		return TEXT("DedicatedServer");
	case NM_ListenServer:
		return TEXT("ListenServer");
	case NM_Client:
		return TEXT("Client");
	default:
		return TEXT("Unknown");
	}
}

static const TCHAR* MoveToAttackRangeTask_GetPathStatusString(const EPathFollowingStatus::Type Status)
{
	switch (Status)
	{
	case EPathFollowingStatus::Idle:
		return TEXT("Idle");
	case EPathFollowingStatus::Waiting:
		return TEXT("Waiting");
	case EPathFollowingStatus::Paused:
		return TEXT("Paused");
	case EPathFollowingStatus::Moving:
		return TEXT("Moving");
	default:
		return TEXT("Unknown");
	}
}

static const TCHAR* MoveToAttackRangeTask_GetMoveRequestResultString(const EPathFollowingRequestResult::Type Result)
{
	switch (Result)
	{
	case EPathFollowingRequestResult::Failed:
		return TEXT("Failed");
	case EPathFollowingRequestResult::AlreadyAtGoal:
		return TEXT("AlreadyAtGoal");
	case EPathFollowingRequestResult::RequestSuccessful:
		return TEXT("RequestSuccessful");
	default:
		return TEXT("Unknown");
	}
}

static void MoveToAttackRangeTask_LogMovementSnapshot(const TCHAR* Phase, const AAIController* AI, const APawn* Pawn, const AActor* TargetActor, const float AttackRange, const float AttackRangeReduction, const float AcceptMoveRadius)
{
	UWorld* World = Pawn ? Pawn->GetWorld() : (AI ? AI->GetWorld() : nullptr);

	const FVector PawnLocation = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
	const FVector PawnVelocity = Pawn ? Pawn->GetVelocity() : FVector::ZeroVector;
	const float PawnSpeed = PawnVelocity.Size();
	const bool bPawnHasAuthority = Pawn ? Pawn->HasAuthority() : false;
	const ENetRole PawnRole = Pawn ? Pawn->GetLocalRole() : ROLE_None;

	const FVector TargetLocation = TargetActor ? TargetActor->GetActorLocation() : FVector::ZeroVector;
	const float Distance = (Pawn && TargetActor) ? FVector::Dist(PawnLocation, TargetLocation) : -1.f;

	const UMovementComponent* MovementComponent = Pawn ? Pawn->GetMovementComponent() : nullptr;
	const bool bMovementComponentActive = MovementComponent ? MovementComponent->IsActive() : false;
	const USceneComponent* UpdatedComponent = MovementComponent ? MovementComponent->UpdatedComponent : nullptr;

	const ACharacter* CharacterPawn = Cast<ACharacter>(Pawn);
	const UCharacterMovementComponent* CharacterMovement = CharacterPawn ? CharacterPawn->GetCharacterMovement() : nullptr;
	const float MaxWalkSpeed = CharacterMovement ? CharacterMovement->MaxWalkSpeed : -1.f;
	const bool bCharacterMovementActive = CharacterMovement ? CharacterMovement->IsActive() : false;
	const bool bRootComponentSimulatingPhysics = (Pawn && Pawn->GetRootComponent()) ? Pawn->GetRootComponent()->IsSimulatingPhysics() : false;

	UPathFollowingComponent* PFC = AI ? AI->GetPathFollowingComponent() : nullptr;
	const EPathFollowingStatus::Type PathStatus = PFC ? PFC->GetStatus() : EPathFollowingStatus::Idle;
	const FNavPathSharedPtr NavPath = PFC ? PFC->GetPath() : nullptr;
	const int32 NavPathPointCount = NavPath.IsValid() ? NavPath->GetPathPoints().Num() : -1;
	const bool bNavPathIsPartial = NavPath.IsValid() ? NavPath->IsPartial() : false;

	const UNavigationSystemV1* NavSys = World ? FNavigationSystem::GetCurrent<UNavigationSystemV1>(World) : nullptr;

	UE_LOG(
		LogMoveToAttackRangeTask,
		Log,
		TEXT("MoveToAttackRangeTask: [%s] World=%s NetMode=%s AI=%s Pawn=%s Role=%d Authority=%d PawnLoc=%s PawnVel=%s Speed=%.1f MoveComp=%s Active=%d Updated=%s CharMove=%s Active=%d MaxWalkSpeed=%.1f RootPhysics=%d Target=%s TargetLoc=%s Dist=%.1f AttackRange=%.1f Reduction=%.1f AcceptRadius=%.1f PathStatus=%s PathPoints=%d Partial=%d NavSys=%s"),
		Phase,
		World ? *World->GetName() : TEXT("None"),
		MoveToAttackRangeTask_GetNetModeString(World),
		AI ? *AI->GetName() : TEXT("None"),
		Pawn ? *Pawn->GetName() : TEXT("None"),
		static_cast<int32>(PawnRole),
		bPawnHasAuthority ? 1 : 0,
		*PawnLocation.ToString(),
		*PawnVelocity.ToString(),
		PawnSpeed,
		MovementComponent ? *MovementComponent->GetClass()->GetName() : TEXT("None"),
		bMovementComponentActive ? 1 : 0,
		UpdatedComponent ? *UpdatedComponent->GetName() : TEXT("None"),
		CharacterMovement ? *CharacterMovement->GetClass()->GetName() : TEXT("None"),
		bCharacterMovementActive ? 1 : 0,
		MaxWalkSpeed,
		bRootComponentSimulatingPhysics ? 1 : 0,
		TargetActor ? *TargetActor->GetName() : TEXT("None"),
		*TargetLocation.ToString(),
		Distance,
		AttackRange,
		AttackRangeReduction,
		AcceptMoveRadius,
		MoveToAttackRangeTask_GetPathStatusString(PathStatus),
		NavPathPointCount,
		bNavPathIsPartial ? 1 : 0,
		NavSys ? TEXT("Valid") : TEXT("None"));
}
#define MOVE_LOG(Verbosity, Format, ...) UE_LOG(LogMoveToAttackRangeTask, Verbosity, Format, ##__VA_ARGS__)
#define MOVE_SNAPSHOT(Phase, AI, Pawn, TargetActor, AttackRange, AttackRangeReduction, AcceptMoveRadius) MoveToAttackRangeTask_LogMovementSnapshot(Phase, AI, Pawn, TargetActor, AttackRange, AttackRangeReduction, AcceptMoveRadius)
#else
#define MOVE_LOG(Verbosity, Format, ...)
#define MOVE_SNAPSHOT(Phase, AI, Pawn, TargetActor, AttackRange, AttackRangeReduction, AcceptMoveRadius)
#endif

USTT_MoveToAttackRangeTask::USTT_MoveToAttackRangeTask(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bShouldCallTick = true;
}

EStateTreeRunStatus USTT_MoveToAttackRangeTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition)
{
	AAIController* AI = Cast<AAIController>(Context.GetOwner());
	APawn* Pawn = AI ? AI->GetPawn() : nullptr;
	if (!AI || !Pawn)
	{
		MOVE_LOG(Warning, TEXT("MoveToAttackRangeTask: [EnterState] Missing AI/Pawn. Owner=%s AI=%s Pawn=%s"),
			*GetNameSafe(Context.GetOwner()),
			*GetNameSafe(AI),
			*GetNameSafe(Pawn));
		return EStateTreeRunStatus::Failed;
	}

	AActor* TargetActor = nullptr;
	if (AI->IsA<AEnemyAIController>())
	{
		TargetActor = Cast<AEnemyAIController>(AI)->GetTargetActor();
	}
	if (!TargetActor)
	{
		MOVE_LOG(Warning, TEXT("MoveToAttackRangeTask: [EnterState] Missing TargetActor. AI=%s Pawn=%s AIClass=%s"),
			*GetNameSafe(AI),
			*GetNameSafe(Pawn),
			AI ? *AI->GetClass()->GetName() : TEXT("None"));
		return EStateTreeRunStatus::Failed;
	}

	// Query attack range from the pawn's Ability System (GAS AttributeSet)
	float AttackRange = 0.f;
	if (UAbilitySystemComponent* ASC = Pawn->FindComponentByClass<UAbilitySystemComponent>())
	{
		AttackRange = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetAttackRangeAttribute());
		MOVE_LOG(Log, TEXT("MoveToAttackRangeTask: [EnterState] Found ASC=%s AttackRangeAttribute=%.2f"),
			*GetNameSafe(ASC),
			AttackRange);
	}
	else
	{
		MOVE_LOG(Warning, TEXT("MoveToAttackRangeTask: [EnterState] No ASC found on Pawn=%s; using fallback attack range."),
			*GetNameSafe(Pawn));
	}
	if (AttackRange <= 0.f)
	{
		AttackRange = 150.0f;
		MOVE_LOG(Warning, TEXT("MoveToAttackRangeTask: [EnterState] AttackRange <= 0; fallback AttackRange=%.1f"), AttackRange);
	}

	const float AcceptMoveRadius = FMath::Max(0.f, AttackRange - AttackRangeReduction);
	MOVE_SNAPSHOT(TEXT("EnterState-BeforeChecks"), AI, Pawn, TargetActor, AttackRange, AttackRangeReduction, AcceptMoveRadius);
	if (const UWorld* World = Pawn->GetWorld())
	{
		if (World->GetNetMode() == NM_Client || !Pawn->HasAuthority())
		{
			MOVE_LOG(Warning, TEXT("MoveToAttackRangeTask: [EnterState] Task executing on non-authority context (NetMode=%s Authority=%d). AI movement typically must run on the server."),
				MoveToAttackRangeTask_GetNetModeString(World),
				Pawn->HasAuthority() ? 1 : 0);
		}
	}

	// Already in range? succeed and let the parent transition to Attack
	const float EnterDistance = FVector::Dist(Pawn->GetActorLocation(), TargetActor->GetActorLocation());
	if (EnterDistance <= AcceptMoveRadius)
	{
		MOVE_LOG(Log, TEXT("MoveToAttackRangeTask: [EnterState] Already within radius. Distance=%.1f AcceptRadius=%.1f -> Succeeded"),
			EnterDistance,
			AcceptMoveRadius);
		return EStateTreeRunStatus::Succeeded;
	}

	// Aim to stop just inside attack range
	// Aim to stop just inside attack range, measuring center-to-center only.
	const EPathFollowingRequestResult::Type Result = AI->MoveToActor(
		TargetActor,
		AcceptMoveRadius,
		/*bStopOnOverlap=*/false /* do not add agent/goal radii */);

	MOVE_LOG(Log, TEXT("MoveToAttackRangeTask: [EnterState] MoveToActor(Target=%s, AcceptRadius=%.1f, StopOnOverlap=false) -> %s"),
		*GetNameSafe(TargetActor),
		AcceptMoveRadius,
		MoveToAttackRangeTask_GetMoveRequestResultString(Result));
	MOVE_SNAPSHOT(TEXT("EnterState-AfterMoveTo"), AI, Pawn, TargetActor, AttackRange, AttackRangeReduction, AcceptMoveRadius);
	if (Result == EPathFollowingRequestResult::Failed)
	{
		MOVE_LOG(Warning, TEXT("MoveToAttackRangeTask: [EnterState] MoveToActor FAILED. Pawn=%s Target=%s"), *GetNameSafe(Pawn), *GetNameSafe(TargetActor));
		return EStateTreeRunStatus::Failed;
	}
	if (Result == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		MOVE_LOG(Warning, TEXT("MoveToAttackRangeTask: [EnterState] MoveToActor returned AlreadyAtGoal (Distance=%.1f AcceptRadius=%.1f). This typically means acceptance radius is too large or goal location is considered reached."),
			EnterDistance,
			AcceptMoveRadius);
	}

	return EStateTreeRunStatus::Running;
}

EStateTreeRunStatus USTT_MoveToAttackRangeTask::Tick(FStateTreeExecutionContext& Context, float DeltaTime)
{
	AAIController* AI = Cast<AAIController>(Context.GetOwner());
	APawn* Pawn = AI ? AI->GetPawn() : nullptr;
	if (!AI || !Pawn)
	{
		MOVE_LOG(Warning, TEXT("MoveToAttackRangeTask: [Tick] Missing AI/Pawn. Owner=%s AI=%s Pawn=%s"),
			*GetNameSafe(Context.GetOwner()),
			*GetNameSafe(AI),
			*GetNameSafe(Pawn));
		return EStateTreeRunStatus::Failed;
	}

	AActor* TargetActor = nullptr;
	if (AI->IsA<AEnemyAIController>())
	{
		TargetActor = Cast<AEnemyAIController>(AI)->GetTargetActor();
	}
	if (!TargetActor)
	{
		MOVE_LOG(Warning, TEXT("MoveToAttackRangeTask: [Tick] Missing TargetActor. AI=%s Pawn=%s"), *GetNameSafe(AI), *GetNameSafe(Pawn));
		return EStateTreeRunStatus::Failed;
	}

	float AttackRange = 0.f;
	if (UAbilitySystemComponent* ASC = Pawn->FindComponentByClass<UAbilitySystemComponent>())
	{
		AttackRange = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetAttackRangeAttribute());
	}
	if (AttackRange <= 0.f)
	{
		AttackRange = 150.f;
	}

	const float Distance = FVector::Dist(Pawn->GetActorLocation(), TargetActor->GetActorLocation());
	const float AcceptMoveRadius = FMath::Max(0.f, AttackRange - AttackRangeReduction);

	if (const UWorld* World = Pawn->GetWorld())
	{
		const float TimeSeconds = World->GetTimeSeconds();
		if (FMath::Fmod(TimeSeconds, 0.50f) < DeltaTime)
		{
			MOVE_SNAPSHOT(TEXT("Tick-Periodic"), AI, Pawn, TargetActor, AttackRange, AttackRangeReduction, AcceptMoveRadius);

			if (AcceptMoveRadius <= 0.f)
			{
				MOVE_LOG(Warning, TEXT("MoveToAttackRangeTask: [Tick] AcceptMoveRadius is <= 0 (AttackRange=%.1f Reduction=%.1f). This can make the AI try to reach the exact target location."),
					AttackRange,
					AttackRangeReduction);
			}

			UPathFollowingComponent* PFC = AI ? AI->GetPathFollowingComponent() : nullptr;
			const EPathFollowingStatus::Type Status = PFC ? PFC->GetStatus() : EPathFollowingStatus::Idle;
			const float Speed = Pawn->GetVelocity().Size();
			if (Status == EPathFollowingStatus::Moving && Speed < 1.f)
			{
				MOVE_LOG(Warning, TEXT("MoveToAttackRangeTask: [Tick] PathFollowing says Moving, but Pawn speed is ~0. Check movement component, collisions, physics simulation, or max speed."));
			}

			const UMovementComponent* MovementComponent = Pawn->GetMovementComponent();
			if (!MovementComponent)
			{
				MOVE_LOG(Warning, TEXT("MoveToAttackRangeTask: [Tick] Pawn has no MovementComponent. MoveToActor will not physically move it."));
			}
			else if (!MovementComponent->IsActive())
			{
				MOVE_LOG(Warning, TEXT("MoveToAttackRangeTask: [Tick] MovementComponent is inactive (%s)."), *MovementComponent->GetClass()->GetName());
			}

			if (const ACharacter* CharacterPawn = Cast<ACharacter>(Pawn))
			{
				if (const UCharacterMovementComponent* CharacterMovement = CharacterPawn->GetCharacterMovement())
				{
					if (CharacterMovement->MaxWalkSpeed <= 1.f)
					{
						MOVE_LOG(Warning, TEXT("MoveToAttackRangeTask: [Tick] CharacterMovement MaxWalkSpeed is very low (%.1f)."), CharacterMovement->MaxWalkSpeed);
					}
				}
			}
		}
	}
	if (Distance <= AttackRange)
	{
		MOVE_LOG(Log, TEXT("MoveToAttackRangeTask: [Tick] Within AttackRange. Distance=%.1f AttackRange=%.1f (AcceptRadius=%.1f) -> Succeeded"),
			Distance,
			AttackRange,
			AcceptMoveRadius);
		return EStateTreeRunStatus::Succeeded;
	}

	EPathFollowingStatus::Type MoveStatus = EPathFollowingStatus::Idle;
	if (AI->GetPathFollowingComponent())
	{
		MoveStatus = AI->GetPathFollowingComponent()->GetStatus();
	}

	if (MoveStatus == EPathFollowingStatus::Idle ||
		MoveStatus == EPathFollowingStatus::Paused ||
		MoveStatus == EPathFollowingStatus::Waiting)
	{
		MOVE_LOG(Warning, TEXT("MoveToAttackRangeTask: [Tick] PathFollowingStatus=%s; re-issuing MoveToActor. Distance=%.1f AttackRange=%.1f AcceptRadius=%.1f"),
			MoveToAttackRangeTask_GetPathStatusString(MoveStatus),
			Distance,
			AttackRange,
			AcceptMoveRadius);

		const EPathFollowingRequestResult::Type Result = AI->MoveToActor(
			TargetActor,
			AcceptMoveRadius,
			/*bStopOnOverlap=*/false);

		MOVE_LOG(Log, TEXT("MoveToAttackRangeTask: [Tick] MoveToActor(Target=%s, AcceptRadius=%.1f, StopOnOverlap=false) -> %s"),
			*GetNameSafe(TargetActor),
			AcceptMoveRadius,
			MoveToAttackRangeTask_GetMoveRequestResultString(Result));
		MOVE_SNAPSHOT(TEXT("Tick-AfterReissueMoveTo"), AI, Pawn, TargetActor, AttackRange, AttackRangeReduction, AcceptMoveRadius);
		if (Result == EPathFollowingRequestResult::Failed)
		{
			MOVE_LOG(Warning, TEXT("MoveToAttackRangeTask: [Tick] MoveToActor FAILED on re-issue. Pawn=%s Target=%s"), *GetNameSafe(Pawn), *GetNameSafe(TargetActor));
			return EStateTreeRunStatus::Failed;
		}
		return EStateTreeRunStatus::Running;
	}

	return EStateTreeRunStatus::Running;
}

void USTT_MoveToAttackRangeTask::ExitState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition)
{
	// Ensure movement is stopped when leaving this state (in case of abort or transition)
	if (AAIController* AI = Cast<AAIController>(Context.GetOwner()))
	{
		MOVE_LOG(Log, TEXT("MoveToAttackRangeTask: [ExitState] StopMovement. AI=%s Pawn=%s"), *GetNameSafe(AI), *GetNameSafe(AI->GetPawn()));
		AI->StopMovement();
	}
	else
	{
		MOVE_LOG(Warning, TEXT("MoveToAttackRangeTask: [ExitState] No AIController on Owner=%s"), *GetNameSafe(Context.GetOwner()));
	}
}
