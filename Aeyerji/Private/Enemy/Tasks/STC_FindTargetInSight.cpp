// STC_FindTargetInSight.cpp

#include "Enemy/Tasks/STC_FindTargetInSight.h"
#include "Enemy/EnemyAIController.h"

#include "StateTreeExecutionContext.h"
#include "AIController.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISenseConfig_Sight.h"
#include "GenericTeamAgentInterface.h"
#include "Kismet/KismetSystemLibrary.h"

#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "Engine/EngineTypes.h"

static FORCEINLINE float DistSq2D(const FVector& A, const FVector& B)
{
	const float DX = A.X - B.X;
	const float DY = A.Y - B.Y;
	return DX*DX + DY*DY;
}

bool USTC_FindTargetInSight::TestCondition(FStateTreeExecutionContext& Context) const
{
	AAIController* AI = Cast<AAIController>(Context.GetOwner());
	APawn* Self = AI ? AI->GetPawn() : nullptr;

	if (!AI || !Self)
	{
		return bNegate ? true : false;
	}

	// Decide search radius
	float Radius = 1200.f; // default
	if (!bUse360Search)
	{
		if (UAIPerceptionComponent* Perc = AI->GetPerceptionComponent())
		{
			GetSightRadius(Perc, Radius);
		}
	}
	if (SearchRadius > 0.f) { Radius = SearchRadius; }
	if (MaxDistance > 0.f)  { Radius = FMath::Min(Radius, MaxDistance); }
	const float RadiusSq = Radius * Radius;

	AActor* Best = nullptr;

	if (bUse360Search)
	{
		Best = FindBySphere(AI, Self, RadiusSq);
	}
	else
	{
		if (UAIPerceptionComponent* Perc = AI->GetPerceptionComponent())
		{
			Best = FindByPerception(AI, Perc, Self, RadiusSq);
		}
	}

	// Side-effect: set as target if we found one
	if (Best)
	{
		if (AEnemyAIController* EnemyAI = Cast<AEnemyAIController>(AI))
		{
			EnemyAI->SetTargetActor(Best);
		}
	}

	const bool bFound = (Best != nullptr);
	const bool bPass = bOnlySetTargetDoNotPass ? false : bFound;
	return bNegate ? !bPass : bPass;
}

AActor* USTC_FindTargetInSight::FindByPerception(AAIController* AI, UAIPerceptionComponent* Perc, APawn* Self, float UseRadiusSq) const
{
	TArray<AActor*> Seen;
	Perc->GetCurrentlyPerceivedActors(UAISense_Sight::StaticClass(), Seen);

	AActor* Best = nullptr;
	float BestD2 = TNumericLimits<float>::Max();

	for (AActor* Candidate : Seen)
	{
		if (!Candidate || Candidate == Self)
			continue;

		if (InvalidTag != NAME_None && Candidate->Tags.Contains(InvalidTag))
			continue;

		if (!IsAliveAndHostile(AI, Candidate))
			continue;

		const float D2 = bUse2DDistance
			? DistSq2D(Candidate->GetActorLocation(), Self->GetActorLocation())
			: FVector::DistSquared(Candidate->GetActorLocation(), Self->GetActorLocation());

		if (D2 > UseRadiusSq)
			continue;

		if (bRequireLineOfSightTrace && !HasLOS(AI, Candidate))
			continue;

		if (D2 < BestD2)
		{
			BestD2 = D2;
			Best = Candidate;
		}
	}
	return Best;
}

AActor* USTC_FindTargetInSight::FindBySphere(AAIController* AI, APawn* Self, float UseRadiusSq) const
{
	if (!Self) return nullptr;

	const FVector Center = Self->GetActorLocation();
	const float Radius = FMath::Sqrt(UseRadiusSq);

	// We’ll query for Pawns (player & enemies). Add more object types if needed.
	TArray<TEnumAsByte<EObjectTypeQuery>> ObjectTypes;
	ObjectTypes.Add(UEngineTypes::ConvertToObjectType(ECC_Pawn));

	TArray<AActor*> Ignore;
	Ignore.Add(Self);

	TArray<AActor*> Hits;
	UKismetSystemLibrary::SphereOverlapActors(
		Self,                  // World context
		Center,                // Center
		Radius,                // Radius
		ObjectTypes,           // Object types to consider
		AActor::StaticClass(), // Class filter (any Actor)
		Ignore,                // Actors to ignore
		Hits                   // Out actors
	);

	AActor* Best = nullptr;
	float BestD2 = TNumericLimits<float>::Max();

	for (AActor* Candidate : Hits)
	{
		if (!Candidate || Candidate == Self)
			continue;

		if (InvalidTag != NAME_None && Candidate->Tags.Contains(InvalidTag))
			continue;

		if (!IsAliveAndHostile(AI, Candidate))
			continue;

		const float D2 = bUse2DDistance
			? (FVector2D(Candidate->GetActorLocation()) - FVector2D(Center)).SizeSquared()
			: FVector::DistSquared(Candidate->GetActorLocation(), Center);

		if (D2 > UseRadiusSq)
			continue;

		if (bRequireLineOfSightTrace && !HasLOS(AI, Candidate))
			continue;

		if (D2 < BestD2)
		{
			BestD2 = D2;
			Best = Candidate;
		}
	}

	return Best;
}


bool USTC_FindTargetInSight::GetSightRadius(UAIPerceptionComponent* Perc, float& OutSightRadius) const
{
	if (!Perc) return false;

	// UE 5.6 templated accessor
	if (const UAISenseConfig_Sight* Sight = Perc->GetSenseConfig<UAISenseConfig_Sight>())
	{
		OutSightRadius = Sight->SightRadius;
		return true;
	}
	return false;
}

bool USTC_FindTargetInSight::IsAliveAndHostile(AAIController* AI, const AActor* Candidate) const
{
	if (!AI || !Candidate) return false;

	if (InvalidTag != NAME_None && Candidate->Tags.Contains(InvalidTag))
		return false;

	const ETeamAttitude::Type Att = AI->GetTeamAttitudeTowards(*Candidate);
	return Att == ETeamAttitude::Hostile;
}

bool USTC_FindTargetInSight::HasLOS(const AAIController* AI, const AActor* Candidate) const
{
	return (AI && Candidate) ? AI->LineOfSightTo(Candidate) : false;
}
