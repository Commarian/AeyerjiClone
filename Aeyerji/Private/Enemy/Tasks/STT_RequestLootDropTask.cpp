#include "Enemy/Tasks/STT_RequestLootDropTask.h"

#include "CharacterStatsLibrary.h"
#include "Inventory/AeyerjiLootPickup.h"
#include "Logging/AeyerjiLog.h"
#include "StateTreeExecutionContext.h"
#include "Engine/World.h"

namespace
{
	AActor* ResolveStateTreeOwnerActor(UObject* Owner)
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

EStateTreeRunStatus USTT_RequestLootDropTask::EnterState(FStateTreeExecutionContext& Context, const FStateTreeTransitionResult& Transition)
{
	(void)Transition;

	UObject* Owner = Context.GetOwner();
	UWorld* World = Owner ? Owner->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_Client)
	{
		AJ_LOG(Owner, TEXT("[LootDirector] RequestLootDrop failed: invalid or client world Owner=%s World=%s NetMode=%d"),
			*GetNameSafe(Owner),
			*GetNameSafe(World),
			World ? static_cast<int32>(World->GetNetMode()) : -1);
		return EStateTreeRunStatus::Failed;
	}

	ULootService* LootService = UCharacterStatsLibrary::GetLootService(Owner);
	if (!LootService)
	{
		AJ_LOG(Owner, TEXT("[LootDirector] RequestLootDrop failed: LootService missing Owner=%s"), *GetNameSafe(Owner));
		return EStateTreeRunStatus::Failed;
	}

	AActor* OwnerActor = ResolveStateTreeOwnerActor(Owner);
	AActor* Instigator = InstigatorActor.Get();
	if (!Instigator && bUseOwnerAsInstigator)
	{
		Instigator = OwnerActor;
	}

	FLootContext RuntimeContext = LootContext;
	if (!RuntimeContext.PlayerActor.IsValid() && bUseOwnerAsPlayerActor)
	{
		RuntimeContext.PlayerActor = OwnerActor;
	}

	const FVector BaseLocation = (bUseOwnerLocation && OwnerActor) ? OwnerActor->GetActorLocation() : WorldLocation;
	const FVector DropLocation = BaseLocation + LocationOffset;

	if (bUseMultiDropConfig)
	{
		UAeyerjiInventoryBPFL::SpawnMultiDropFromContext(
			Owner,
			RuntimeContext,
			MultiDropConfig,
			DropLocation,
			WorldRotation,
			DropMode,
			Instigator);

		AJ_LOG(Owner, TEXT("[LootDirector] RequestLootDrop multi-drop SourceTag=%s Instigator=%s Location=%s Buckets=%d"),
			*RuntimeContext.SourceTag.ToString(),
			*GetNameSafe(Instigator),
			*DropLocation.ToString(),
			MultiDropConfig.Buckets.Num());
		return EStateTreeRunStatus::Succeeded;
	}

	const FLootDropResult Result = LootService->RollLoot(RuntimeContext);
	AAeyerjiLootPickup* SpawnedPickup = UAeyerjiInventoryBPFL::SpawnLootFromResult(
		Owner,
		Result,
		DropLocation,
		WorldRotation,
		/*SeedOverride=*/0,
		DropMode,
		Instigator);

	AJ_LOG(Owner, TEXT("[LootDirector] RequestLootDrop single SourceTag=%s Rarity=%d DefinitionKey=%s Instigator=%s Location=%s Pickup=%s"),
		*RuntimeContext.SourceTag.ToString(),
		static_cast<int32>(Result.Rarity),
		*Result.ItemDefinitionKey.ToString(),
		*GetNameSafe(Instigator),
		*DropLocation.ToString(),
		*GetNameSafe(SpawnedPickup));

	return SpawnedPickup ? EStateTreeRunStatus::Succeeded : EStateTreeRunStatus::Failed;
}
