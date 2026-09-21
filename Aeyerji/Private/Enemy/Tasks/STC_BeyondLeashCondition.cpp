// STC_BeyondLeashCondition.cpp

#include "Enemy/Tasks/STC_BeyondLeashCondition.h"
#include "Enemy/AeyerjiEnemyArchetypeComponent.h"
#include "Enemy/AeyerjiLeashPolicy.h"
#include "Enemy/EnemyAIController.h"
#include "GameFramework/Pawn.h"
#include "StateTreeExecutionContext.h"

bool FSTC_BeyondLeashCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	const AEnemyAIController* EnemyAI = Cast<AEnemyAIController>(Context.GetOwner());
	const APawn* Pawn = EnemyAI ? EnemyAI->GetPawn() : nullptr;
	if (!EnemyAI || !Pawn)
	{
		return false;
	}

	float LeashDistance = 0.f;
	if (const UAeyerjiEnemyArchetypeComponent* Archetype = Pawn->FindComponentByClass<UAeyerjiEnemyArchetypeComponent>())
	{
		LeashDistance = Archetype->GetLeashDistance();
	}

	const float DistanceFromHome = FVector::Dist2D(Pawn->GetActorLocation(), EnemyAI->GetHomeLocation());
	return FAeyerjiLeashPolicy::IsBeyondLeash(DistanceFromHome, LeashDistance, EnemyAI->HasHomeLocation());
}
