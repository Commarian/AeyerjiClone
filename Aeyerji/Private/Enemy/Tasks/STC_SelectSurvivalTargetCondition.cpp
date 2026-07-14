// Copyright (c) 2025 Aeyerji.

#include "Enemy/Tasks/STC_SelectSurvivalTargetCondition.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AeyerjiGameplayTags.h"
#include "Enemy/EnemyAIController.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "GenericTeamAgentInterface.h"
#include "StateTreeExecutionContext.h"

bool USTC_SelectSurvivalTargetCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	AEnemyAIController* EnemyAI = Cast<AEnemyAIController>(Context.GetOwner());
	APawn* SelfPawn = EnemyAI ? EnemyAI->GetPawn() : nullptr;
	UWorld* World = SelfPawn ? SelfPawn->GetWorld() : nullptr;
	AActor* Objective = EnemyAI ? EnemyAI->GetDefenseObjectiveTargetActor() : nullptr;

	if (!EnemyAI || !SelfPawn || !World || !SelfPawn->HasAuthority() || !IsLiveTarget(Objective))
	{
		return bNegate ? true : false;
	}

	if (bUseControllerSettings)
	{
		EnemyAI->RefreshDefenseObjectiveTarget();
		const bool bSelected = IsLiveTarget(EnemyAI->GetTargetActor());
		return bNegate ? !bSelected : bSelected;
	}

	const FAeyerjiDefenseTargetingSettings& ControllerSettings = EnemyAI->GetDefenseTargetingSettings();
	FAeyerjiDefenseTargetingSettings Settings = bUseControllerSettings ? ControllerSettings : FallbackSettings;
	Settings.PlayerThreatAcquireRadius = FMath::Max(0.f, Settings.PlayerThreatAcquireRadius);
	Settings.PlayerThreatReleaseRadius = FMath::Max(Settings.PlayerThreatAcquireRadius, Settings.PlayerThreatReleaseRadius);
	Settings.PlayerThreatObjectiveAcquireRadius = Settings.PlayerThreatObjectiveAcquireRadius > 0.f
		? Settings.PlayerThreatObjectiveAcquireRadius
		: FAeyerjiDefenseTargetingSettings().PlayerThreatObjectiveAcquireRadius;
	Settings.PlayerThreatObjectiveReleaseRadius = Settings.PlayerThreatObjectiveReleaseRadius > 0.f
		? Settings.PlayerThreatObjectiveReleaseRadius
		: FAeyerjiDefenseTargetingSettings().PlayerThreatObjectiveReleaseRadius;
	Settings.PlayerThreatObjectiveReleaseRadius = FMath::Max(Settings.PlayerThreatObjectiveAcquireRadius, Settings.PlayerThreatObjectiveReleaseRadius);
	Settings.PlayerDistanceBias = FMath::Max(0.f, Settings.PlayerDistanceBias);

	const FVector SelfLocation = SelfPawn->GetActorLocation();
	const float ObjectiveDistanceSq = DistanceSq(SelfLocation, Objective->GetActorLocation());
	AActor* CurrentTarget = EnemyAI->GetTargetActor();
	APawn* BestPlayer = nullptr;
	float BestPlayerDistanceSq = TNumericLimits<float>::Max();

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PlayerController = It->Get();
		APawn* Candidate = PlayerController ? PlayerController->GetPawn() : nullptr;
		if (!Candidate || Candidate == SelfPawn || !IsLiveTarget(Candidate))
		{
			continue;
		}

		if (EnemyAI->GetTeamAttitudeTowards(*Candidate) != ETeamAttitude::Hostile)
		{
			continue;
		}

		const float PlayerDistanceSq = DistanceSq(SelfLocation, Candidate->GetActorLocation());
		const float Radius = Candidate == CurrentTarget
			? Settings.PlayerThreatReleaseRadius
			: Settings.PlayerThreatAcquireRadius;
		if (PlayerDistanceSq > FMath::Square(Radius))
		{
			continue;
		}

		const float ObjectiveThreatRadius = Candidate == CurrentTarget
			? Settings.PlayerThreatObjectiveReleaseRadius
			: Settings.PlayerThreatObjectiveAcquireRadius;
		if (DistanceSq(Candidate->GetActorLocation(), Objective->GetActorLocation()) > FMath::Square(ObjectiveThreatRadius))
		{
			continue;
		}

		if (Settings.bRequirePlayerCloserThanObjective)
		{
			const float RequiredAdvantage = FMath::Max(0.f, Settings.PlayerDistanceBias);
			if (FMath::Sqrt(PlayerDistanceSq) + RequiredAdvantage > FMath::Sqrt(ObjectiveDistanceSq))
			{
				continue;
			}
		}

		if (PlayerDistanceSq < BestPlayerDistanceSq)
		{
			BestPlayerDistanceSq = PlayerDistanceSq;
			BestPlayer = Candidate;
		}
	}

	AActor* ChosenTarget = BestPlayer ? static_cast<AActor*>(BestPlayer) : Objective;
	const bool bSelected = IsLiveTarget(ChosenTarget);
	if (bSelected && bSetTargetOnPass)
	{
		if (!EnemyAI->TryAcquireTarget(ChosenTarget, /*bBroadcastAllyAlert=*/false) && EnemyAI->GetTargetActor() != ChosenTarget)
		{
			EnemyAI->SetTargetActor(ChosenTarget);
		}
	}

	return bNegate ? !bSelected : bSelected;
}

float USTC_SelectSurvivalTargetCondition::DistanceSq(const FVector& A, const FVector& B) const
{
	if (!bUse2DDistance)
	{
		return FVector::DistSquared(A, B);
	}

	const float DX = A.X - B.X;
	const float DY = A.Y - B.Y;
	return DX * DX + DY * DY;
}

bool USTC_SelectSurvivalTargetCondition::IsLiveTarget(const AActor* Actor) const
{
	if (!IsValid(Actor))
	{
		return false;
	}

	if (Actor->Tags.Contains(AeyerjiTags::State_Dead.GetTag().GetTagName()))
	{
		return false;
	}

	const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor, /*LookForComponent=*/true);
	return !ASC || !ASC->HasMatchingGameplayTag(AeyerjiTags::State_Dead);
}
