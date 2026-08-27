#pragma once

#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"

/**
 * Shared helpers for resolving and comparing team affiliation across ability code.
 */
namespace AbilityTeamUtils
{
	inline FGenericTeamId ResolveTeamId(const AActor* Actor)
	{
		if (!Actor)
		{
			return FGenericTeamId::NoTeam;
		}

		if (const IGenericTeamAgentInterface* TeamAgent = Cast<const IGenericTeamAgentInterface>(Actor))
		{
			return TeamAgent->GetGenericTeamId();
		}

		if (const APawn* Pawn = Cast<const APawn>(Actor))
		{
			if (const IGenericTeamAgentInterface* ControllerAgent = Cast<const IGenericTeamAgentInterface>(Pawn->GetController()))
			{
				return ControllerAgent->GetGenericTeamId();
			}
		}

		if (const AController* Controller = Cast<const AController>(Actor))
		{
			if (const IGenericTeamAgentInterface* ControllerAgent = Cast<const IGenericTeamAgentInterface>(Controller))
			{
				return ControllerAgent->GetGenericTeamId();
			}
		}

		return FGenericTeamId::NoTeam;
	}

	inline bool AreOnSameTeam(const AActor* A, const AActor* B)
	{
		if (!A || !B)
		{
			return false;
		}
		if (A == B)
		{
			return true;
		}

		const FGenericTeamId TeamA = ResolveTeamId(A);
		const FGenericTeamId TeamB = ResolveTeamId(B);
		// NoTeam means unknown affiliation, not a shared friendly faction.
		return TeamA != FGenericTeamId::NoTeam
			&& TeamB != FGenericTeamId::NoTeam
			&& TeamA == TeamB;
	}
}
