// STC_BossEnsureTargetCondition.cpp

#include "Enemy/Tasks/STC_BossEnsureTargetCondition.h"

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
	const FGameplayTag& DeadStateTag()
	{
		static const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TEXT("State.Dead"), /*ErrorIfNotFound=*/false);
		return Tag;
	}
}

bool USTC_BossEnsureTargetCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	AEnemyAIController* EnemyAI = Cast<AEnemyAIController>(Context.GetOwner());
	APawn* BossPawn = EnemyAI ? EnemyAI->GetPawn() : nullptr;
	if (!EnemyAI || !BossPawn || !BossPawn->HasAuthority())
	{
		return false;
	}

	AActor* CurrentTarget = EnemyAI->GetTargetActor();
	if (CurrentTarget && IsActorDead(CurrentTarget))
	{
		EnemyAI->SetTargetActor(nullptr);
		CurrentTarget = nullptr;
	}

	APawn* SelectedTarget = nullptr;
	const bool bHasUsableCurrentTarget = IsEligiblePlayerTarget(*EnemyAI, Cast<APawn>(CurrentTarget));
	if (bHasUsableCurrentTarget && bUseCurrentTargetIfValid)
	{
		SelectedTarget = CastChecked<APawn>(CurrentTarget);
	}
	else
	{
		SelectedTarget = ChooseRandomPlayerTarget(*EnemyAI);
	}

	if (!SelectedTarget)
	{
		return false;
	}

	const bool bAcquiredTarget = EnemyAI->TryAcquireTarget(SelectedTarget, /*bBroadcastAllyAlert=*/false)
		|| EnemyAI->GetTargetActor() == SelectedTarget;
	if (!bAcquiredTarget)
	{
		return false;
	}

	const bool bShouldTeleport = !bHasUsableCurrentTarget || !bUseCurrentTargetIfValid || bTeleportWhenCurrentTargetIsValid;
	if (bShouldTeleport && TeleportDistanceFromTarget > 0.f)
	{
		return TeleportBossNearTarget(*BossPawn, *SelectedTarget);
	}

	return true;
}

bool USTC_BossEnsureTargetCondition::IsActorDead(const AActor* Actor) const
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

bool USTC_BossEnsureTargetCondition::IsEligiblePlayerTarget(AEnemyAIController& EnemyAI, const APawn* PlayerPawn) const
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

APawn* USTC_BossEnsureTargetCondition::ChooseRandomPlayerTarget(AEnemyAIController& EnemyAI) const
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
		if (!IsEligiblePlayerTarget(EnemyAI, PlayerPawn))
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

bool USTC_BossEnsureTargetCondition::TeleportBossNearTarget(APawn& BossPawn, const AActor& TargetActor) const
{
	if (TeleportDistanceFromTarget <= 0.f)
	{
		return false;
	}

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
