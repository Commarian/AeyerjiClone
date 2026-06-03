#include "Enemy/Tasks/STC_LootPityCondition.h"

#include "CharacterStatsLibrary.h"
#include "Player/PlayerStatsTrackingComponent.h"
#include "StateTreeExecutionContext.h"

namespace
{
	AActor* ResolveConditionOwnerActor(UObject* Owner)
	{
		if (AActor* Actor = Cast<AActor>(Owner))
		{
			return Actor;
		}

		if (UActorComponent* Component = Cast<UActorComponent>(Owner))
		{
			return Component->GetOwner();
		}

		return nullptr;
	}
}

bool USTC_LootPityCondition::TestCondition(FStateTreeExecutionContext& Context) const
{
	UObject* Owner = Context.GetOwner();
	AActor* StatsActor = PlayerActor.Get();
	if (!StatsActor && bUseOwnerAsPlayerActor)
	{
		StatsActor = ResolveConditionOwnerActor(Owner);
	}

	const UPlayerStatsTrackingComponent* StatsComp = UCharacterStatsLibrary::GetPlayerStatsTracking(StatsActor);
	if (!StatsComp)
	{
		return bInvert;
	}

	const FPlayerLootStats& Stats = StatsComp->GetLootStats();
	bool bResult = false;

	switch (Mode)
	{
	case EAeyerjiLootPityConditionMode::DropsSinceLastLegendaryAtLeast:
		bResult = Stats.DropsSinceLastLegendary >= MinDropsSinceLastLegendary;
		break;
	case EAeyerjiLootPityConditionMode::LegendaryChanceAtLeast:
	{
		if (ULootService* LootService = UCharacterStatsLibrary::GetLootService(Owner))
		{
			FLootContext RuntimeContext = LootContext;
			if (!RuntimeContext.PlayerActor.IsValid())
			{
				RuntimeContext.PlayerActor = StatsActor;
			}

			bResult = LootService->ComputeLegendaryChance(RuntimeContext, Stats) >= MinLegendaryChance;
		}
		break;
	}
	case EAeyerjiLootPityConditionMode::HasNeverPickedUpItem:
		bResult = !ItemDefinitionKey.IsNone() && !StatsComp->HasPickedUpItemId(ItemDefinitionKey);
		break;
	case EAeyerjiLootPityConditionMode::PityGroupAttemptsSinceSuccessAtLeast:
	{
		const FAeyerjiLootPityMemory* Memory = Stats.FindPityMemory(PityGroup);
		bResult = Memory && Memory->AttemptsSinceLastSuccess >= MinPityAttemptsSinceSuccess;
		break;
	}
	case EAeyerjiLootPityConditionMode::PityGroupSuccessesAtLeast:
	{
		const FAeyerjiLootPityMemory* Memory = Stats.FindPityMemory(PityGroup);
		bResult = Memory && Memory->TotalSuccesses >= MinPitySuccesses;
		break;
	}
	default:
		break;
	}

	return bInvert ? !bResult : bResult;
}
