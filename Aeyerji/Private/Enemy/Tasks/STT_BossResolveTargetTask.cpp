#include "Enemy/Tasks/STT_BossResolveTargetTask.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "AIController.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "Engine/World.h"
#include "Enemy/EnemyAIController.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "GameFramework/Character.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "MouseNavBlueprintLibrary.h"
#include "NavigationSystem.h"
#include "StateTreeExecutionContext.h"

namespace
{
	const FGameplayTag& BossResolveDeadStateTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("State.Dead"), /*ErrorIfNotFound=*/false);
		return Tag;
	}
}

EStateTreeRunStatus USTT_BossResolveTargetTask::EnterState(
	FStateTreeExecutionContext& Context,
	const FStateTreeTransitionResult& /*Transition*/)
{
	AEnemyAIController* EnemyAI = Cast<AEnemyAIController>(Context.GetOwner());
	APawn* BossPawn = EnemyAI ? EnemyAI->GetPawn() : nullptr;
	if (!EnemyAI || !BossPawn || !BossPawn->HasAuthority())
	{
		return EStateTreeRunStatus::Failed;
	}

	AActor* SelectedTarget = EnemyAI->GetTargetActor();
	if (SelectedTarget && IsActorDead(SelectedTarget))
	{
		EnemyAI->SetTargetActor(nullptr);
		SelectedTarget = nullptr;
	}

	if (SelectedTarget && EnemyAI->GetTeamAttitudeTowards(*SelectedTarget) == ETeamAttitude::Hostile)
	{
		return EStateTreeRunStatus::Succeeded;
	}

	if (bUseLastKnownTargetActor)
	{
		AActor* LastKnownTargetActor = EnemyAI->GetLastKnownTargetActor();
		if (LastKnownTargetActor
			&& !IsActorDead(LastKnownTargetActor)
			&& EnemyAI->GetTeamAttitudeTowards(*LastKnownTargetActor) == ETeamAttitude::Hostile)
		{
			const bool bAcquiredLastKnownTarget = EnemyAI->TryAcquireTarget(LastKnownTargetActor, /*bBroadcastAllyAlert=*/false)
				|| EnemyAI->GetTargetActor() == LastKnownTargetActor;
			if (bAcquiredLastKnownTarget)
			{
				if (TeleportDistanceFromTarget <= 0.f || TeleportBossNearTarget(*BossPawn, *LastKnownTargetActor))
				{
					return EStateTreeRunStatus::Succeeded;
				}
			}
		}
	}

	APawn* RandomPlayerTarget = ChooseRandomPlayerTarget(*EnemyAI);
	if (!RandomPlayerTarget)
	{
		return EStateTreeRunStatus::Failed;
	}

	const bool bAcquiredRandomTarget = EnemyAI->TryAcquireTarget(RandomPlayerTarget, /*bBroadcastAllyAlert=*/false)
		|| EnemyAI->GetTargetActor() == RandomPlayerTarget;
	if (!bAcquiredRandomTarget)
	{
		return EStateTreeRunStatus::Failed;
	}

	if (TeleportDistanceFromTarget > 0.f && !TeleportBossNearTarget(*BossPawn, *RandomPlayerTarget))
	{
		return EStateTreeRunStatus::Failed;
	}

	return EStateTreeRunStatus::Succeeded;
}

bool USTT_BossResolveTargetTask::IsActorDead(const AActor* Actor) const
{
	if (!IsValid(Actor))
	{
		return true;
	}

	const FGameplayTag DeadTag = BossResolveDeadStateTag();
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

bool USTT_BossResolveTargetTask::IsEligibleRandomPlayerTarget(AEnemyAIController& EnemyAI, const APawn* PlayerPawn) const
{
	if (!IsValid(PlayerPawn) || IsActorDead(PlayerPawn))
	{
		return false;
	}

	if (EnemyAI.GetTeamAttitudeTowards(*PlayerPawn) != ETeamAttitude::Hostile)
	{
		return false;
	}

	const bool bMatchesPreferredTag = PreferredPlayerActorTag.IsNone() || PlayerPawn->Tags.Contains(PreferredPlayerActorTag);
	return bMatchesPreferredTag || !bRequirePreferredPlayerActorTag;
}

APawn* USTT_BossResolveTargetTask::ChooseRandomPlayerTarget(AEnemyAIController& EnemyAI) const
{
	UWorld* World = EnemyAI.GetWorld();
	if (!World)
	{
		return nullptr;
	}

	TArray<APawn*> PreferredTargets;
	TArray<APawn*> FallbackTargets;

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PlayerController = It->Get();
		APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
		if (!IsEligibleRandomPlayerTarget(EnemyAI, PlayerPawn))
		{
			continue;
		}

		FallbackTargets.Add(PlayerPawn);

		if (!PreferredPlayerActorTag.IsNone() && PlayerPawn->Tags.Contains(PreferredPlayerActorTag))
		{
			PreferredTargets.Add(PlayerPawn);
		}
	}

	const TArray<APawn*>& Candidates = PreferredTargets.Num() > 0
		? PreferredTargets
		: (bRequirePreferredPlayerActorTag ? PreferredTargets : FallbackTargets);

	if (Candidates.Num() <= 0)
	{
		return nullptr;
	}

	return Candidates[FMath::RandRange(0, Candidates.Num() - 1)];
}

bool USTT_BossResolveTargetTask::TeleportBossNearTarget(APawn& BossPawn, const AActor& TargetActor) const
{
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(BossPawn.GetWorld());
	if (!NavSys)
	{
		return false;
	}

	const FVector TargetLocation = TargetActor.GetActorLocation();
	const int32 Samples = FMath::Max(1, TeleportSampleCount);

	for (int32 SampleIndex = 0; SampleIndex < Samples; ++SampleIndex)
	{
		const float AngleRadians = FMath::FRandRange(0.f, 2.f * PI);
		const FVector Direction = FVector(FMath::Cos(AngleRadians), FMath::Sin(AngleRadians), 0.f);
		FVector DesiredLocation = TargetLocation + Direction * TeleportDistanceFromTarget;
		DesiredLocation.Z = TargetLocation.Z;

		FNavLocation ProjectedLocation;
		if (!NavSys->ProjectPointToNavigation(DesiredLocation, ProjectedLocation, NavProjectionExtent))
		{
			continue;
		}

		FVector GroundedLocation = ProjectedLocation.Location;
		if (!UMouseNavBlueprintLibrary::ResolveGroundedTeleportLocation(
				&BossPawn,
				ProjectedLocation.Location,
				&BossPawn,
				GroundedLocation))
		{
			continue;
		}

		FVector FacingDirection = TargetLocation - GroundedLocation;
		FacingDirection.Z = 0.f;
		if (FacingDirection.IsNearlyZero())
		{
			FacingDirection = BossPawn.GetActorForwardVector();
			FacingDirection.Z = 0.f;
		}

		FRotator DesiredRotation = FacingDirection.Rotation();
		DesiredRotation.Pitch = 0.f;
		DesiredRotation.Roll = 0.f;
		if (BossPawn.TeleportTo(GroundedLocation, DesiredRotation))
		{
			if (AAIController* BossController = Cast<AAIController>(BossPawn.GetController()))
			{
				BossController->StopMovement();
			}

			if (ACharacter* BossCharacter = Cast<ACharacter>(&BossPawn))
			{
				if (UCharacterMovementComponent* Movement = BossCharacter->GetCharacterMovement())
				{
					Movement->StopMovementImmediately();
					Movement->UpdateComponentVelocity();
				}
			}

			return true;
		}
	}

	return false;
}
