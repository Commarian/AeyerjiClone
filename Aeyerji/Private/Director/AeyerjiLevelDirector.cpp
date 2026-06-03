#include "Director/AeyerjiLevelDirector.h"

#include "Director/AeyerjiSpawnerGroup.h"
#include "Director/AeyerjiEncounterDirector.h"
#include "Director/AeyerjiWorldSpawnProfile.h"
#include "AbilitySystemGlobals.h"
#include "../../AeyerjiGameState.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Progression/AeyerjiLevelingComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Enemy/AeyerjiEnemyManagementBPFL.h"
#include "Enemy/EnemyParentNative.h"
#include "Systems/AeyerjiDifficultyTuning.h"
#include "Systems/AeyerjiWorldStateSubsystem.h"
#include "World/AeyerjiLinkedTeleporter.h"
#include "../AeyerjiGameInstance.h"

namespace
{
	constexpr float RunTimerInterval = 0.1f;
}

FLootContext UAeyerjiBossDefinition::MakeBossLootContext(
	AActor* PlayerActor,
	const int32 EnemyLevel,
	const int32 PlayerLevel,
	const int32 WorldTier,
	const float DifficultyScale) const
{
	FLootContext Context;
	Context.PlayerActor = PlayerActor;
	Context.EnemyLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(EnemyLevel);
	Context.PlayerLevel = PlayerLevel > 0 ? UAeyerjiDifficultySettings::ClampGameplayLevel(PlayerLevel) : 0;
	Context.WorldTier = WorldTier;
	Context.SourceTag = LootSourceTag;
	Context.PityGroup = BossPityGroup;
	Context.ForcedItemDefinition = BossForcedItemDefinition;
	Context.BaseLegendaryChance = FMath::Clamp(BossBaseLegendaryChance, 0.f, 1.f);
	Context.MinimumRarity = BossMinimumRarity;
	Context.PitySuccessRarity = BossPitySuccessRarity;
	Context.RarityWeights = BossRarityWeights;
	Context.DifficultyScale = DifficultyScale;
	Context.ItemLevelJitterMin = BossItemLevelJitterMin;
	Context.ItemLevelJitterMax = BossItemLevelJitterMax;
	Context.PitySoftStartOverride = BossPitySoftStartOverride;
	Context.PitySoftSlopeOverride = BossPitySoftSlopeOverride;
	Context.PityHardAttemptsOverride = BossPityHardAttemptsOverride;
	Context.PityMaxChanceOverride = BossPityMaxChanceOverride;

	if (Context.ItemLevelJitterMin > Context.ItemLevelJitterMax)
	{
		Swap(Context.ItemLevelJitterMin, Context.ItemLevelJitterMax);
	}

	return Context;
}

AAeyerjiLevelDirector::AAeyerjiLevelDirector()
{
	PrimaryActorTick.bCanEverTick = false;
	WorldTier = UAeyerjiDifficultySettings::GetNormalWorldTier();
	DifficultySlider = UAeyerjiDifficultySettings::WorldTierToDifficultySlider(WorldTier);
}

void AAeyerjiLevelDirector::BeginPlay()
{
	Super::BeginPlay();

	if (UAeyerjiGameInstance* AeyerjiGI = Cast<UAeyerjiGameInstance>(GetGameInstance()))
	{
		if (AeyerjiGI->HasDifficultySelection() || AeyerjiGI->HasWorldTierSelection())
		{
			WorldTier = AeyerjiGI->GetWorldTier();
			DifficultySlider = AeyerjiGI->GetDifficultySlider();
		}
	}

	if (bApplyZoneRunDefinitionOnBeginPlay)
	{
		ApplyZoneRunDefinition();
	}

	for (AAeyerjiSpawnerGroup* Spawner : SpawnerSequence)
	{
		BindSpawner(Spawner);
	}

	if (BossSpawner)
	{
		BindSpawner(BossSpawner);
	}

	if (SpawnMode == EAeyerjiLevelSpawnMode::FixedWorldPopulation)
	{
		if (WorldPopulationSpawner)
		{
			WorldPopulationSpawner->LevelDirector = this;
		}
	}

	if (UGameplayStatics::GetPlayerPawn(this, 0))
	{
		HandleGameplayZoneActivated();
	}
	else if (SpawnMode == EAeyerjiLevelSpawnMode::FixedWorldPopulation)
	{
		GetOrFindEncounterDirector();
	}
}

void AAeyerjiLevelDirector::BindSpawner(AAeyerjiSpawnerGroup* Spawner)
{
	if (!IsValid(Spawner))
	{
		return;
	}

	if (!Spawner->LevelDirector)
	{
		Spawner->LevelDirector = this;
	}

	Spawner->OnEncounterStarted.RemoveDynamic(this, &AAeyerjiLevelDirector::HandleSpawnerStarted);
	Spawner->OnEncounterStarted.AddDynamic(this, &AAeyerjiLevelDirector::HandleSpawnerStarted);
	Spawner->OnEncounterCleared.RemoveDynamic(this, &AAeyerjiLevelDirector::HandleSpawnerCleared);
	Spawner->OnEncounterCleared.AddDynamic(this, &AAeyerjiLevelDirector::HandleSpawnerCleared);
}

void AAeyerjiLevelDirector::BindEncounterDirector(AAeyerjiEncounterDirector* Director)
{
	if (!IsValid(Director))
	{
		return;
	}

	if (AAeyerjiEncounterDirector* PreviousDirector = CachedEncounterDirector.Get())
	{
		if (PreviousDirector != Director)
		{
			PreviousDirector->OnFixedClusterCleared.RemoveDynamic(this, &AAeyerjiLevelDirector::HandleFixedClusterCleared);
			PreviousDirector->OnFixedPopulationCleared.RemoveDynamic(this, &AAeyerjiLevelDirector::HandleFixedPopulationCleared);
		}
	}

	CachedEncounterDirector = Director;

	Director->OnFixedClusterCleared.RemoveDynamic(this, &AAeyerjiLevelDirector::HandleFixedClusterCleared);
	Director->OnFixedClusterCleared.AddDynamic(this, &AAeyerjiLevelDirector::HandleFixedClusterCleared);
	Director->OnFixedPopulationCleared.RemoveDynamic(this, &AAeyerjiLevelDirector::HandleFixedPopulationCleared);
	Director->OnFixedPopulationCleared.AddDynamic(this, &AAeyerjiLevelDirector::HandleFixedPopulationCleared);

	if (ZoneRunDefinition && ZoneRunDefinition->EncounterDirectorDefinition)
	{
		Director->DirectorDefinition = ZoneRunDefinition->EncounterDirectorDefinition;
		Director->ApplyDirectorDefinition();
	}
}

AAeyerjiEncounterDirector* AAeyerjiLevelDirector::GetEncounterDirector()
{
	return GetOrFindEncounterDirector();
}

void AAeyerjiLevelDirector::HandleGameplayZoneActivated()
{
	if (bApplyZoneRunDefinitionOnBeginPlay && !bRunActive)
	{
		ApplyZoneRunDefinition();
	}

	UE_LOG(LogTemp, Display, TEXT("LevelDirector::HandleGameplayZoneActivated Director=%s SpawnMode=%s"),
		*GetNameSafe(this),
		*StaticEnum<EAeyerjiLevelSpawnMode>()->GetNameStringByValue(static_cast<int64>(SpawnMode)));

	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		Checkpoint = PlayerPawn->GetActorTransform();
		UE_LOG(LogTemp, Display, TEXT("LevelDirector::HandleGameplayZoneActivated PlayerPawn=%s Checkpoint refreshed."),
			*GetNameSafe(PlayerPawn));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("LevelDirector::HandleGameplayZoneActivated no player pawn was available yet."));
	}

	for (AAeyerjiSpawnerGroup* Spawner : SpawnerSequence)
	{
		BindSpawner(Spawner);
	}

	if (BossSpawner)
	{
		BindSpawner(BossSpawner);
	}

	if (SpawnMode == EAeyerjiLevelSpawnMode::FixedWorldPopulation && WorldPopulationSpawner)
	{
		WorldPopulationSpawner->LevelDirector = this;
	}

	BindPlayerLevelingComponent();

	if (AAeyerjiEncounterDirector* Director = GetOrFindEncounterDirector())
	{
		UE_LOG(LogTemp, Display, TEXT("LevelDirector::HandleGameplayZoneActivated EncounterDirector=%s"),
			*GetNameSafe(Director));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("LevelDirector::HandleGameplayZoneActivated could not resolve EncounterDirector."));
	}
}

void AAeyerjiLevelDirector::ApplyZoneRunDefinition()
{
	if (!ZoneRunDefinition)
	{
		return;
	}

	SpawnMode = ZoneRunDefinition->SpawnMode;
	RunWinCondition = ZoneRunDefinition->RunWinCondition;
	ShardsNeeded = FMath::Max(1, ZoneRunDefinition->ShardsNeeded);
	bAutoStartFirstRoom = ZoneRunDefinition->bAutoStartFirstRoom;
	ObjectiveKillTargetOverride = FMath::Max(0, ZoneRunDefinition->ObjectiveKillTargetOverride);
	RunTimeLimitSeconds = FMath::Max(0.f, ZoneRunDefinition->RunTimeLimitSeconds);
	WorldSpawnProfile = ZoneRunDefinition->WorldSpawnProfile;
	EndRunPortalClass = ZoneRunDefinition->EndRunPortalClass;
	bOpenBossGateOnFixedPopulationCleared = ZoneRunDefinition->bOpenBossGateOnFixedPopulationCleared;
	SurvivalMissionDefinition = ZoneRunDefinition->SurvivalMissionDefinition;
	bResyncEnemyLevelsOnRunStart = ZoneRunDefinition->bResyncEnemyLevelsOnRunStart;
	bResyncEnemyLevelsOnPlayerLevelUp = ZoneRunDefinition->bResyncEnemyLevelsOnPlayerLevelUp;

	// Zone definitions are the single designer-owned source of truth. Clear any legacy
	// actor-serialized references before resolving this zone's tags and child definitions.
	SpawnerSequence.Reset();
	WorldPopulationSpawner = nullptr;
	SurvivalRoundSpawner = nullptr;
	EndRunPortalSpawnPoint = nullptr;
	BossSpawner = nullptr;
	BossGateActor = nullptr;
	BossSpawnMarker = nullptr;
	BossTriggerActor = nullptr;
	BossPawnClass = nullptr;
	bEnableNativeBossSpawn = false;
	BossLinkedTeleporterClass = nullptr;
	BossTeleporterEndpointA = nullptr;
	BossTeleporterEndpointB = nullptr;
	BossTeleporterEndpointATag = NAME_None;
	BossTeleporterEndpointBTag = NAME_None;
	bUseBossTeleporterEndpointATransform = false;
	BossTeleporterEndpointATransform = FTransform::Identity;
	bUseBossTeleporterEndpointBTransform = false;
	BossTeleporterEndpointBTransform = FTransform(FRotator::ZeroRotator, FVector(600.f, 0.f, 0.f), FVector::OneVector);

	if (!ZoneRunDefinition->EndRunPortalSpawnPointTag.IsNone())
	{
		EndRunPortalSpawnPoint = FindActorByTag(ZoneRunDefinition->EndRunPortalSpawnPointTag);
	}

	if (ZoneRunDefinition->SpawnerSequenceActorTags.Num() > 0)
	{
		for (const FName SpawnerTag : ZoneRunDefinition->SpawnerSequenceActorTags)
		{
			if (AAeyerjiSpawnerGroup* Spawner = FindSpawnerByTag(SpawnerTag))
			{
				SpawnerSequence.Add(Spawner);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("LevelDirector %s could not resolve sequence spawner tag %s from %s."),
					*GetNameSafe(this),
					*SpawnerTag.ToString(),
					*GetNameSafe(ZoneRunDefinition));
			}
		}
	}

	if (!ZoneRunDefinition->WorldPopulationSpawnerActorTag.IsNone())
	{
		WorldPopulationSpawner = FindSpawnerByTag(ZoneRunDefinition->WorldPopulationSpawnerActorTag);
	}

	if (SurvivalMissionDefinition && !SurvivalMissionDefinition->RoundSpawnerActorTag.IsNone())
	{
		SurvivalRoundSpawner = FindSpawnerByTag(SurvivalMissionDefinition->RoundSpawnerActorTag);
	}

	const UAeyerjiBossDefinition* BossDefinition = SurvivalMissionDefinition && SurvivalMissionDefinition->BossDefinitionOverride
		? SurvivalMissionDefinition->BossDefinitionOverride.Get()
		: ZoneRunDefinition->BossDefinition.Get();
	const FName BossSpawnerTag = !ZoneRunDefinition->BossSpawnerActorTag.IsNone()
		? ZoneRunDefinition->BossSpawnerActorTag
		: (BossDefinition ? BossDefinition->BossSpawnerActorTag : NAME_None);
	if (!BossSpawnerTag.IsNone())
	{
		BossSpawner = FindSpawnerByTag(BossSpawnerTag);
	}

	const FName BossGateTag = !ZoneRunDefinition->BossGateActorTag.IsNone()
		? ZoneRunDefinition->BossGateActorTag
		: (BossDefinition ? BossDefinition->BossGateActorTag : NAME_None);
	if (!BossGateTag.IsNone())
	{
		BossGateActor = FindActorByTag(BossGateTag);
	}

	const FName BossMarkerTag = !ZoneRunDefinition->BossSpawnMarkerActorTag.IsNone()
		? ZoneRunDefinition->BossSpawnMarkerActorTag
		: (BossDefinition ? BossDefinition->BossSpawnMarkerActorTag : NAME_None);
	if (!BossMarkerTag.IsNone())
	{
		BossSpawnMarker = FindActorByTag(BossMarkerTag);
	}

	const FName BossTriggerTag = !ZoneRunDefinition->BossTriggerActorTag.IsNone()
		? ZoneRunDefinition->BossTriggerActorTag
		: (BossDefinition ? BossDefinition->BossTriggerActorTag : NAME_None);
	if (!BossTriggerTag.IsNone())
	{
		BossTriggerActor = FindActorByTag(BossTriggerTag);
	}

	if (BossDefinition && BossDefinition->BossPawnClass)
	{
		BossPawnClass = BossDefinition->BossPawnClass;
	}

	if (BossDefinition)
	{
		bEnableNativeBossSpawn = BossDefinition->bEnableNativeBossSpawn;
		BossLinkedTeleporterClass = BossDefinition->BossLinkedTeleporterClass;
		BossTeleporterEndpointATag = BossDefinition->BossTeleporterEndpointATag;
		BossTeleporterEndpointBTag = BossDefinition->BossTeleporterEndpointBTag;
		bUseBossTeleporterEndpointATransform = BossDefinition->bUseBossTeleporterEndpointATransform;
		BossTeleporterEndpointATransform = BossDefinition->BossTeleporterEndpointATransform;
		bUseBossTeleporterEndpointBTransform = BossDefinition->bUseBossTeleporterEndpointBTransform;
		BossTeleporterEndpointBTransform = BossDefinition->BossTeleporterEndpointBTransform;
	}

	if (AAeyerjiEncounterDirector* Director = GetOrFindEncounterDirector())
	{
		Director->DirectorDefinition = ZoneRunDefinition->EncounterDirectorDefinition;
		Director->ApplyDirectorDefinition();
	}

	UE_LOG(LogTemp, Display, TEXT("LevelDirector %s applied ZoneRunDefinition=%s Zone=%s Sequence=%d SurvivalMission=%s SurvivalSpawner=%s BossSpawner=%s BossDefinition=%s."),
		*GetNameSafe(this),
		*GetNameSafe(ZoneRunDefinition),
		*ZoneRunDefinition->ZoneId.ToString(),
		SpawnerSequence.Num(),
		*GetNameSafe(SurvivalMissionDefinition.Get()),
		*GetNameSafe(SurvivalRoundSpawner.Get()),
		*GetNameSafe(BossSpawner.Get()),
		*GetNameSafe(BossDefinition));
}

void AAeyerjiLevelDirector::WritePersistentFactsForTrigger(const EAeyerjiPersistentFactWriteTrigger Trigger)
{
	if (!HasAuthority() || !ZoneRunDefinition)
	{
		return;
	}

	switch (Trigger)
	{
	case EAeyerjiPersistentFactWriteTrigger::BossDefeated:
		if (const UAeyerjiBossDefinition* BossDefinition = ZoneRunDefinition->BossDefinition)
		{
			ApplyPersistentFactWrites(BossDefinition->BossDefeatedFacts);
			ApplyPersistentFactWrites(BossDefinition->UnlockFacts);
		}
		break;
	case EAeyerjiPersistentFactWriteTrigger::ZoneCompleted:
		ApplyPersistentFactWrites(ZoneRunDefinition->ZoneCompletedFacts);
		break;
	case EAeyerjiPersistentFactWriteTrigger::Unlock:
		ApplyPersistentFactWrites(ZoneRunDefinition->UnlockFacts);
		break;
	default:
		break;
	}
}

FString AAeyerjiLevelDirector::GetRunDefinitionDebugString() const
{
	const UAeyerjiBossDefinition* BossDefinition = SurvivalMissionDefinition && SurvivalMissionDefinition->BossDefinitionOverride
		? SurvivalMissionDefinition->BossDefinitionOverride.Get()
		: (ZoneRunDefinition ? ZoneRunDefinition->BossDefinition.Get() : nullptr);
	return FString::Printf(
		TEXT("LevelDirector=%s ZoneDef=%s ZoneId=%s SpawnMode=%s Win=%s Active=%d Sequence=%d SurvivalMission=%s SurvivalRound=%d SurvivalCycle=%d SurvivalPhaseBoss=%d SurvivalSpawner=%s BossDef=%s BossSpawner=%s BossGate=%s BossMarker=%s BossTrigger=%s WorldSpawner=%s"),
		*GetNameSafe(this),
		*GetNameSafe(ZoneRunDefinition),
		ZoneRunDefinition ? *ZoneRunDefinition->ZoneId.ToString() : TEXT("None"),
		*StaticEnum<EAeyerjiLevelSpawnMode>()->GetNameStringByValue(static_cast<int64>(SpawnMode)),
		*StaticEnum<EAeyerjiRunWinCondition>()->GetNameStringByValue(static_cast<int64>(RunWinCondition)),
		bRunActive ? 1 : 0,
		SpawnerSequence.Num(),
		*GetNameSafe(SurvivalMissionDefinition.Get()),
		CurrentSurvivalRound,
		CurrentSurvivalCycle,
		bSurvivalBossRoundActive ? 1 : 0,
		*GetNameSafe(SurvivalRoundSpawner.Get()),
		*GetNameSafe(BossDefinition),
		*GetNameSafe(BossSpawner.Get()),
		*GetNameSafe(BossGateActor.Get()),
		*GetNameSafe(BossSpawnMarker.Get()),
		*GetNameSafe(BossTriggerActor.Get()),
		*GetNameSafe(WorldPopulationSpawner.Get()));
}

AAeyerjiEncounterDirector* AAeyerjiLevelDirector::GetOrFindEncounterDirector()
{
	if (AAeyerjiEncounterDirector* Director = CachedEncounterDirector.Get())
	{
		return Director;
	}

	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AAeyerjiEncounterDirector> It(World); It; ++It)
		{
			BindEncounterDirector(*It);
			return *It;
		}
	}

	return nullptr;
}

void AAeyerjiLevelDirector::StartRun()
{
	if (bRunActive)
	{
		return;
	}

	if (UAeyerjiGameInstance* AeyerjiGI = Cast<UAeyerjiGameInstance>(GetGameInstance()))
	{
		UE_LOG(LogTemp, Display,
			TEXT("LevelDirector::StartRun Difficulty pre-sync HasDiff=%d HasTier=%d Diff=%.2f Tier=%d"),
			AeyerjiGI->HasDifficultySelection() ? 1 : 0,
			AeyerjiGI->HasWorldTierSelection() ? 1 : 0,
			AeyerjiGI->GetDifficultySlider(),
			AeyerjiGI->GetWorldTier());

		if (!AeyerjiGI->HasDifficultySelection() && !AeyerjiGI->HasWorldTierSelection())
		{
			UE_LOG(LogTemp, Warning, TEXT("LevelDirector::StartRun Difficulty missing in GI. Applying default WorldTier=%d."), UAeyerjiDifficultySettings::GetNormalWorldTier());
			AeyerjiGI->SetWorldTier(UAeyerjiDifficultySettings::GetNormalWorldTier());
		}

		WorldTier = AeyerjiGI->GetWorldTier();
		DifficultySlider = AeyerjiGI->GetDifficultySlider();
		UE_LOG(LogTemp, Display, TEXT("LevelDirector::StartRun Difficulty applied to director: WorldTier=%d Difficulty=%.2f"), WorldTier, DifficultySlider);
	}

	bRunActive = true;
	AccumulatedRunSeconds = 0.f;
	ResetPrimaryObjective();
	bNativeBossSpawnIssued = false;
	bBossEncounterTriggered = false;
	ClearBossLinkedTeleporter();

	BindPlayerLevelingComponent();

	// Reset shard count for a fresh run.
	const int32 OldShards = ShardCount;
	ShardCount = 0;
	if (ShardCount != OldShards)
	{
		OnShardsChanged.Broadcast(ShardCount);
	}

	// Restore boss gate to locked state at run start.
	if (BossGateActor)
	{
		BossGateActor->SetActorHiddenInGame(false);
		BossGateActor->SetActorEnableCollision(true);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(RunTimerHandle, this, &AAeyerjiLevelDirector::TickRunTimer, RunTimerInterval, true);
	}

	OnRunStateChanged.Broadcast(true);

	if (AAeyerjiEncounterDirector* Director = GetOrFindEncounterDirector())
	{
		Director->SetBossSpawned(false);
	}

	FixedPopulationClustersCleared = 0;

	if (bResyncEnemyLevelsOnRunStart)
	{
		RefreshEnemyLevelsToCurrentPlayer();
	}

	if (SpawnMode == EAeyerjiLevelSpawnMode::SurvivalRounds)
	{
		CurrentSurvivalRound = 0;
		CurrentSurvivalCycle = 0;
		CurrentSurvivalRoundEnemyTotal = 0;
		SurvivalEnemyLevelBonus = 0;
		bSurvivalBossRoundActive = false;
		bSurvivalBossDefeatHandled = false;
		StartSurvivalRound(1);
		return;
	}

	if (SpawnMode == EAeyerjiLevelSpawnMode::Sequence && bAutoStartFirstRoom)
	{
		APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
		AController* PlayerController = PlayerPawn ? PlayerPawn->GetController() : nullptr;

		for (int32 Index = 0; Index < SpawnerSequence.Num(); ++Index)
		{
			if (AAeyerjiSpawnerGroup* Spawner = SpawnerSequence[Index])
			{
				if (!Spawner->IsCleared())
				{
					CurrentIndex = Index;
					Spawner->ActivateEncounter(PlayerPawn, PlayerController);
					break;
				}
			}
		}
	}

	if (SpawnMode == EAeyerjiLevelSpawnMode::FixedWorldPopulation)
	{
		if (!CachedEncounterDirector.IsValid())
		{
			for (TActorIterator<AAeyerjiEncounterDirector> It(GetWorld()); It; ++It)
			{
				BindEncounterDirector(*It);
				break;
			}
		}

		if (AAeyerjiEncounterDirector* Director = CachedEncounterDirector.Get())
		{
			if (!Director->IsFixedWorldPopulationActive())
			{
				Director->StartFixedWorldPopulation(WorldSpawnProfile, WorldPopulationSpawner, this);
			}
		}
	}
}

int32 AAeyerjiLevelDirector::GetEffectiveObjectiveKillTargetRaw() const
{
	if (ObjectiveKillTargetOverride > 0)
	{
		return ObjectiveKillTargetOverride;
	}

	if (const AAeyerjiEncounterDirector* EncounterDirector = CachedEncounterDirector.Get())
	{
		return EncounterDirector->GetTotalToKillRaw();
	}

	return 0;
}

int32 AAeyerjiLevelDirector::GetEffectiveObjectiveKillTarget() const
{
	// Legacy objective widgets still divide by this value directly, so clamp the Blueprint-facing accessor.
	return FMath::Max(GetEffectiveObjectiveKillTargetRaw(), 1);
}

void AAeyerjiLevelDirector::MarkPrimaryObjectiveComplete()
{
	if (!HasAuthority())
	{
		return;
	}

	if (bPrimaryObjectiveComplete)
	{
		return;
	}

	bPrimaryObjectiveComplete = true;
	OnPrimaryObjectiveStateChanged.Broadcast(true);

	if (RunWinCondition == EAeyerjiRunWinCondition::KillTargetThenBoss)
	{
		OpenBossGate();
	}
}

void AAeyerjiLevelDirector::ResetPrimaryObjective()
{
	if (!HasAuthority())
	{
		return;
	}

	if (!bPrimaryObjectiveComplete)
	{
		return;
	}

	bPrimaryObjectiveComplete = false;
	bBossEncounterTriggered = false;
	OnPrimaryObjectiveStateChanged.Broadcast(false);
}

bool AAeyerjiLevelDirector::HasBossEncounterBeenTriggered() const
{
	if (bBossEncounterTriggered || bNativeBossSpawnIssued)
	{
		return true;
	}

	return IsValid(BossSpawner) && BossSpawner->IsCleared();
}

void AAeyerjiLevelDirector::RefreshEnemyLevelsToCurrentPlayer()
{
	if (!HasAuthority())
	{
		return;
	}

	const int32 EnemyLevel = GetEffectiveEnemyLevel();
	const float DifficultyAlpha = GetDerivedDifficultyAlpha();
	TSet<TWeakObjectPtr<AActor>> RescaledEnemies;

	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AAeyerjiSpawnerGroup> SpawnerIt(World); SpawnerIt; ++SpawnerIt)
		{
			if (AAeyerjiSpawnerGroup* Spawner = *SpawnerIt)
			{
				Spawner->RefreshTrackedEnemyScaling(RescaledEnemies);
			}
		}

		for (TActorIterator<AEnemyParentNative> It(World); It; ++It)
		{
			AEnemyParentNative* Enemy = *It;
			if (!IsValid(Enemy))
			{
				continue;
			}

			if (RescaledEnemies.Contains(TWeakObjectPtr<AActor>(Enemy)))
			{
				continue;
			}

			if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Enemy, /*LookForComponent*/ true))
			{
				ASC->SetNumericAttributeBase(UAeyerjiAttributeSet::GetLevelAttribute(), static_cast<float>(EnemyLevel));
			}

			Enemy->SetScalingSnapshot(EnemyLevel, DifficultyAlpha, Enemy->GetScalingSourceTag());
		}
	}
}

void AAeyerjiLevelDirector::HandlePlayerLevelUp(int32 OldLevel, int32 NewLevel)
{
	static_cast<void>(OldLevel);
	static_cast<void>(NewLevel);

	if (bResyncEnemyLevelsOnPlayerLevelUp)
	{
		RefreshEnemyLevelsToCurrentPlayer();
	}
}

void AAeyerjiLevelDirector::EndRun()
{
	if (!bRunActive)
	{
		return;
	}

	bRunActive = false;
	ClearBossLinkedTeleporter();

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RunTimerHandle);
		World->GetTimerManager().ClearTimer(SurvivalRoundDelayHandle);
	}

	OnRunStateChanged.Broadcast(false);
	CurrentSurvivalRound = 0;
	CurrentSurvivalCycle = 0;
	CurrentSurvivalRoundEnemyTotal = 0;
	SurvivalEnemyLevelBonus = 0;
	bSurvivalBossRoundActive = false;
	bSurvivalBossDefeatHandled = false;
	if (AAeyerjiGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AAeyerjiGameState>() : nullptr)
	{
		GameState->ClearSurvivalRoundStateFromServer();
	}

	if (SpawnMode == EAeyerjiLevelSpawnMode::FixedWorldPopulation)
	{
		if (AAeyerjiEncounterDirector* Director = CachedEncounterDirector.Get())
		{
			Director->StopFixedWorldPopulation();
		}
	}
}

void AAeyerjiLevelDirector::TickRunTimer()
{
	if (bRunActive)
	{
		AccumulatedRunSeconds += RunTimerInterval;

		if (RunTimeLimitSeconds > 0.f && AccumulatedRunSeconds >= RunTimeLimitSeconds)
		{
			AccumulatedRunSeconds = RunTimeLimitSeconds;
			EndRun();
			OnRunTimerExpired.Broadcast();
		}
	}
}

void AAeyerjiLevelDirector::BindPlayerLevelingComponent()
{
	if (!HasAuthority())
	{
		return;
	}

	if (UAeyerjiLevelingComponent* PreviousLeveling = CachedPlayerLeveling.Get())
	{
		PreviousLeveling->OnLevelUp.RemoveDynamic(this, &AAeyerjiLevelDirector::HandlePlayerLevelUp);
	}

	CachedPlayerLeveling.Reset();

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!PlayerPawn)
	{
		UE_LOG(LogTemp, Warning, TEXT("LevelDirector::BindPlayerLevelingComponent failed - player pawn missing."));
		return;
	}

	UAeyerjiLevelingComponent* Leveling = PlayerPawn->FindComponentByClass<UAeyerjiLevelingComponent>();
	if (!Leveling)
	{
		UE_LOG(LogTemp, Warning, TEXT("LevelDirector::BindPlayerLevelingComponent failed - %s has no leveling component."),
			*GetNameSafe(PlayerPawn));
		return;
	}

	CachedPlayerLeveling = Leveling;
	Leveling->OnLevelUp.RemoveDynamic(this, &AAeyerjiLevelDirector::HandlePlayerLevelUp);
	Leveling->OnLevelUp.AddDynamic(this, &AAeyerjiLevelDirector::HandlePlayerLevelUp);
	UE_LOG(LogTemp, Display, TEXT("LevelDirector::BindPlayerLevelingComponent bound %s from %s."),
		*GetNameSafe(Leveling),
		*GetNameSafe(PlayerPawn));
}

float AAeyerjiLevelDirector::GetDifficultyScale() const
{
	return GetDerivedDifficultyAlpha();
}

float AAeyerjiLevelDirector::GetCurvedDifficulty() const
{
	return GetDerivedDifficultyAlpha();
}

float AAeyerjiLevelDirector::GetRemainingRunTimeSeconds() const
{
	if (RunTimeLimitSeconds <= 0.f)
	{
		return 0.f;
	}

	return FMath::Max(RunTimeLimitSeconds - AccumulatedRunSeconds, 0.f);
}

int32 AAeyerjiLevelDirector::GetCurrentPlayerLevel() const
{
	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PlayerPawn, /*LookForComponent*/ true))
		{
			const float Level = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetLevelAttribute());
			return UAeyerjiDifficultySettings::ClampGameplayLevel(FMath::RoundToInt(Level));
		}
	}

	return 1;
}

int32 AAeyerjiLevelDirector::GetEnemyScalingPlayerLevel() const
{
	return UAeyerjiDifficultySettings::ClampGameplayLevel(GetCurrentPlayerLevel() + FMath::Max(0, SurvivalEnemyLevelBonus));
}

int32 AAeyerjiLevelDirector::GetEffectiveEnemyLevelForPlayerLevel(const int32 PlayerLevel) const
{
	return UAeyerjiDifficultySettings::Get()->EvaluateEnemyLevel(PlayerLevel);
}

int32 AAeyerjiLevelDirector::GetEffectiveEnemyLevel() const
{
	return GetEffectiveEnemyLevelForPlayerLevel(GetCurrentPlayerLevel());
}

float AAeyerjiLevelDirector::GetGlobalStatBudgetMultiplier() const
{
	return UAeyerjiDifficultySettings::Get()->EvaluateStatBudget(WorldTier);
}

float AAeyerjiLevelDirector::GetDerivedDifficultyAlpha() const
{
	return UAeyerjiDifficultySettings::Get()->EvaluateDifficultyAlpha(WorldTier);
}

void AAeyerjiLevelDirector::AddShard(int32 Amount)
{
	if (Amount == 0)
	{
		return;
	}

	const int32 OldCount = ShardCount;
	ShardCount = FMath::Max(0, ShardCount + Amount);

	if (ShardCount != OldCount)
	{
		OnShardsChanged.Broadcast(ShardCount);

		if (ShardCount >= ShardsNeeded && !IsBossSpawnBlockedByPrimaryObjective())
		{
			OpenBossGate();
		}
	}
}

void AAeyerjiLevelDirector::OpenBossGate()
{
	if (!HasAuthority())
	{
		return;
	}

	if (IsBossSpawnBlockedByPrimaryObjective())
	{
		return;
	}

	if (HasBossEncounterBeenTriggered())
	{
		return;
	}

	if (BossGateActor)
	{
		BossGateActor->SetActorEnableCollision(false);
		BossGateActor->SetActorHiddenInGame(true);
	}

	SpawnBossLinkedTeleporter();

	if (bEnableNativeBossSpawn)
	{
		if (bBossEncounterTriggered || bNativeBossSpawnIssued)
		{
			return;
		}

		bBossEncounterTriggered = true;
		bNativeBossSpawnIssued = true;
		if (!SpawnBossEncounter(GetOrFindEncounterDirector()))
		{
			UE_LOG(LogTemp, Display,
				TEXT("LevelDirector::OpenBossGate SpawnBossEncounter returned no pawn (Director=%s BossSpawner=%s BossPawnClass=%s). Assuming Blueprint-owned spawn flow or trigger."),
				*GetNameSafe(this),
				*GetNameSafe(BossSpawner),
				*GetNameSafe(BossPawnClass));
		}

		return;
	}

	if (IsValid(BossSpawner) && !BossSpawner->IsCleared())
	{
		if (bBossEncounterTriggered)
		{
			return;
		}

		bBossEncounterTriggered = true;

		if (BossSpawner->IsActive())
		{
			return;
		}

		APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
		AController* PlayerController = PlayerPawn ? PlayerPawn->GetController() : nullptr;
		BossSpawner->ActivateEncounter(PlayerPawn, PlayerController);

		if (!BossSpawner->IsActive())
		{
			UE_LOG(LogTemp, Warning,
				TEXT("LevelDirector::OpenBossGate activate call did not mark the boss spawner active (Director=%s BossSpawner=%s)."),
				*GetNameSafe(this),
				*GetNameSafe(BossSpawner));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("LevelDirector::OpenBossGate could not activate boss spawner (Director=%s BossSpawner=%s Cleared=%d)."),
			*GetNameSafe(this),
			*GetNameSafe(BossSpawner),
			(IsValid(BossSpawner) && BossSpawner->IsCleared()) ? 1 : 0);
	}
}

void AAeyerjiLevelDirector::UpdateCheckpoint(const FTransform& NewCheckpoint)
{
	Checkpoint = NewCheckpoint;
}

void AAeyerjiLevelDirector::RespawnAtCheckpoint()
{
	if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
	{
		PlayerPawn->SetActorTransform(Checkpoint, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

void AAeyerjiLevelDirector::HandleSpawnerStarted(AAeyerjiSpawnerGroup* Spawner)
{
	// Hook for audio / UI events if needed later.
}

void AAeyerjiLevelDirector::HandleSpawnerCleared(AAeyerjiSpawnerGroup* Spawner)
{
	if (!IsValid(Spawner))
	{
		return;
	}

	if (SpawnMode == EAeyerjiLevelSpawnMode::SurvivalRounds)
	{
		if (Spawner == SurvivalRoundSpawner)
		{
			PublishSurvivalRoundState(EAeyerjiSurvivalRoundPhase::RoundComplete, FName(TEXT("RoundClear")));
			if (UWorld* World = GetWorld())
			{
				const float Delay = SurvivalMissionDefinition ? FMath::Max(0.f, SurvivalMissionDefinition->InterRoundDelaySeconds) : 0.f;
				World->GetTimerManager().SetTimer(SurvivalRoundDelayHandle, this, &AAeyerjiLevelDirector::StartNextSurvivalRound, Delay, false);
			}
		}
		else if (Spawner == BossSpawner)
		{
			HandleSurvivalBossDefeated();
		}
		return;
	}

	if (SpawnMode == EAeyerjiLevelSpawnMode::FixedWorldPopulation)
	{
		if (Spawner == BossSpawner)
		{
			// Boss encounter cleared; fixed population mode does not auto-advance spawners.
		}
		return;
	}

	UpdateCheckpoint(Spawner->GetActorTransform());

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	AController* PlayerController = PlayerPawn ? PlayerPawn->GetController() : nullptr;

	const int32 ClearedIndex = SpawnerSequence.IndexOfByKey(Spawner);
	if (ClearedIndex != INDEX_NONE)
	{
		CurrentIndex = ClearedIndex + 1;

		if (ShardCount < ShardsNeeded)
		{
			AddShard(1);
		}

		for (int32 Index = CurrentIndex; Index < SpawnerSequence.Num(); ++Index)
		{
			if (AAeyerjiSpawnerGroup* Next = SpawnerSequence[Index])
			{
				if (!Next->IsCleared())
				{
					CurrentIndex = Index;
					Next->ActivateEncounter(PlayerPawn, PlayerController);
					return;
				}
			}
		}

		if (ShardCount >= ShardsNeeded
			&& IsValid(BossSpawner)
			&& !BossSpawner->IsCleared()
			&& !IsBossSpawnBlockedByPrimaryObjective())
		{
			OpenBossGate();
		}
	}
	else if (Spawner == BossSpawner)
	{
		// Boss encounter cleared — nothing else to auto trigger for now.
	}
}

void AAeyerjiLevelDirector::HandleFixedClusterCleared(int32 ClusterId, float DensityAlpha, bool bDenseCluster)
{
	if (!bRunActive)
	{
		return;
	}

	static_cast<void>(ClusterId);
	static_cast<void>(DensityAlpha);
	static_cast<void>(bDenseCluster);

	FixedPopulationClustersCleared++;

	const int32 ClustersPerShard = WorldSpawnProfile ? FMath::Max(1, WorldSpawnProfile->ClustersPerShard) : 1;
	if (FixedPopulationClustersCleared % ClustersPerShard == 0 && ShardCount < ShardsNeeded)
	{
		AddShard(1);
	}
}

void AAeyerjiLevelDirector::HandleFixedPopulationCleared()
{
	if (!bRunActive)
	{
		return;
	}

	if (bOpenBossGateOnFixedPopulationCleared)
	{
		OpenBossGate();
	}
}

void AAeyerjiLevelDirector::StartSurvivalRound(const int32 RoundNumber)
{
	if (!HasAuthority() || !bRunActive || !SurvivalMissionDefinition)
	{
		return;
	}

	if (SurvivalMissionDefinition->BaseRounds.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("LevelDirector %s cannot start survival round: mission %s has no BaseRounds."),
			*GetNameSafe(this),
			*GetNameSafe(SurvivalMissionDefinition.Get()));
		return;
	}

	CurrentSurvivalRound = FMath::Max(1, RoundNumber);
	CurrentSurvivalCycle = GetSurvivalCycleForRound(CurrentSurvivalRound);
	SurvivalEnemyLevelBonus = CurrentSurvivalCycle * FMath::Max(0, SurvivalMissionDefinition->EnemyLevelBonusPerCycle);
	CurrentSurvivalRoundEnemyTotal = 0;
	bSurvivalBossRoundActive = false;
	bSurvivalBossDefeatHandled = false;

	if (IsSurvivalBossRound(CurrentSurvivalRound))
	{
		StartSurvivalBossRound(CurrentSurvivalRound);
		return;
	}

	if (!IsValid(SurvivalRoundSpawner))
	{
		UE_LOG(LogTemp, Warning, TEXT("LevelDirector %s cannot start survival round %d: missing SurvivalRoundSpawner."),
			*GetNameSafe(this),
			CurrentSurvivalRound);
		PublishSurvivalRoundState(EAeyerjiSurvivalRoundPhase::Inactive, NAME_None);
		return;
	}

	const int32 PatternIndex = (CurrentSurvivalRound - 1) % SurvivalMissionDefinition->BaseRounds.Num();
	const FAeyerjiSurvivalRoundDefinition& RoundDefinition = SurvivalMissionDefinition->BaseRounds[PatternIndex];
	TArray<FWaveDefinition> RuntimeWaves;
	if (!BuildRuntimeSurvivalWaves(RoundDefinition, CurrentSurvivalCycle, RuntimeWaves, CurrentSurvivalRoundEnemyTotal))
	{
		UE_LOG(LogTemp, Warning, TEXT("LevelDirector %s cannot start survival round %d: no valid runtime waves."),
			*GetNameSafe(this),
			CurrentSurvivalRound);
		PublishSurvivalRoundState(EAeyerjiSurvivalRoundPhase::Inactive, NAME_None);
		return;
	}

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	AController* PlayerController = PlayerPawn ? PlayerPawn->GetController() : nullptr;
	const FName StartMessageKey = (CurrentSurvivalCycle > 0 && PatternIndex == 0)
		? FName(TEXT("CycleStart"))
		: RoundDefinition.RoundStartMessageKey;
	PublishSurvivalRoundState(EAeyerjiSurvivalRoundPhase::Preparing, StartMessageKey);
	SurvivalRoundSpawner->LevelDirector = this;
	SurvivalRoundSpawner->ActivateEncounterWithRuntimeWaves(RuntimeWaves, PlayerPawn, PlayerController);
	PublishSurvivalRoundState(EAeyerjiSurvivalRoundPhase::Spawning, NAME_None);
}

void AAeyerjiLevelDirector::StartNextSurvivalRound()
{
	if (!HasAuthority() || !bRunActive)
	{
		return;
	}

	StartSurvivalRound(FMath::Max(1, CurrentSurvivalRound + 1));
}

void AAeyerjiLevelDirector::StartSurvivalBossRound(const int32 RoundNumber)
{
	CurrentSurvivalRound = FMath::Max(1, RoundNumber);
	CurrentSurvivalCycle = GetSurvivalCycleForRound(CurrentSurvivalRound);
	SurvivalEnemyLevelBonus = CurrentSurvivalCycle * FMath::Max(0, SurvivalMissionDefinition ? SurvivalMissionDefinition->EnemyLevelBonusPerCycle : 0);
	bSurvivalBossRoundActive = true;
	bSurvivalBossDefeatHandled = false;
	bBossEncounterTriggered = false;
	bNativeBossSpawnIssued = false;
	CurrentSurvivalRoundEnemyTotal = 1;
	if (IsValid(BossSpawner))
	{
		BossSpawner->ResetEncounter();
	}
	PublishSurvivalRoundState(EAeyerjiSurvivalRoundPhase::Boss, FName(TEXT("BossIncoming")));
	OpenBossGate();
}

void AAeyerjiLevelDirector::PublishSurvivalRoundState(const EAeyerjiSurvivalRoundPhase Phase, const FName MessageKey)
{
	if (!HasAuthority())
	{
		return;
	}

	if (AAeyerjiGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AAeyerjiGameState>() : nullptr)
	{
		FAeyerjiSurvivalRoundState State;
		State.RoundNumber = CurrentSurvivalRound;
		State.CycleNumber = CurrentSurvivalCycle;
		State.Phase = Phase;
		State.EnemiesRequired = CurrentSurvivalRoundEnemyTotal;
		State.EnemiesKilled = 0;
		if (Phase == EAeyerjiSurvivalRoundPhase::RoundComplete)
		{
			State.EnemiesKilled = CurrentSurvivalRoundEnemyTotal;
		}
		State.bBossRound = bSurvivalBossRoundActive || IsSurvivalBossRound(CurrentSurvivalRound);
		State.MessageKey = MessageKey;
		State.bActive = Phase != EAeyerjiSurvivalRoundPhase::Inactive;
		GameState->SetSurvivalRoundStateFromServer(State);
	}
}

bool AAeyerjiLevelDirector::BuildRuntimeSurvivalWaves(const FAeyerjiSurvivalRoundDefinition& RoundDefinition, const int32 CycleNumber, TArray<FWaveDefinition>& OutWaves, int32& OutEnemyCount) const
{
	OutWaves.Reset();
	OutEnemyCount = 0;

	const float CycleMultiplier = SurvivalMissionDefinition
		? FMath::Pow(FMath::Max(0.f, SurvivalMissionDefinition->EnemyCountScalePerCycle), FMath::Max(0, CycleNumber))
		: 1.f;
	const float CountMultiplier = FMath::Max(0.f, RoundDefinition.EnemyCountMultiplier) * CycleMultiplier;

	for (const FWaveDefData& WaveData : RoundDefinition.Waves)
	{
		FWaveDefinition RuntimeWave;
		RuntimeWave.WaveLabel = WaveData.WaveLabel;
		RuntimeWave.PostSpawnDelay = WaveData.PostSpawnDelay;

		for (const FEnemySetDef& SetData : WaveData.EnemySets)
		{
			TSubclassOf<APawn> EnemyClass = SetData.EnemyClass.LoadSynchronous();
			if (!EnemyClass)
			{
				continue;
			}

			FEnemySet RuntimeSet;
			RuntimeSet.EnemyClass = EnemyClass;
			RuntimeSet.Count = FMath::Max(0, FMath::RoundToInt(static_cast<float>(SetData.Count) * CountMultiplier));
			RuntimeSet.SpawnInterval = SetData.SpawnInterval;
			RuntimeSet.bIsElite = SetData.bIsElite;
			RuntimeSet.bIsMiniBoss = SetData.bIsMiniBoss;
			RuntimeSet.MiniBossGrantedAbilities = SetData.MiniBossGrantedAbilities;
			RuntimeSet.ForcedEliteAffixes = SetData.ForcedEliteAffixes;
			RuntimeSet.EliteAffixPoolOverride = SetData.EliteAffixPoolOverride;
			RuntimeSet.MinEliteAffixes = SetData.MinEliteAffixes;
			RuntimeSet.MaxEliteAffixes = SetData.MaxEliteAffixes;
			RuntimeSet.EliteHealthMultiplierOverride = SetData.EliteHealthMultiplierOverride;
			RuntimeSet.EliteDamageMultiplierOverride = SetData.EliteDamageMultiplierOverride;
			RuntimeSet.EliteRangeMultiplierOverride = SetData.EliteRangeMultiplierOverride;
			RuntimeSet.EliteScaleMultiplierOverride = SetData.EliteScaleMultiplierOverride;
			RuntimeSet.EliteXPMultiplierOverride = SetData.EliteXpMultiplierOverride;
			RuntimeSet.MiniBossXPMultiplierOverride = SetData.MiniBossXpMultiplierOverride;

			if (RuntimeSet.Count > 0)
			{
				OutEnemyCount += RuntimeSet.Count;
				RuntimeWave.EnemySets.Add(RuntimeSet);
			}
		}

		if (!RuntimeWave.EnemySets.IsEmpty())
		{
			OutWaves.Add(RuntimeWave);
		}
	}

	return !OutWaves.IsEmpty() && OutEnemyCount > 0;
}

int32 AAeyerjiLevelDirector::GetSurvivalCycleForRound(const int32 RoundNumber) const
{
	const int32 BossCadence = SurvivalMissionDefinition ? FMath::Max(1, SurvivalMissionDefinition->BossEveryNRounds) : 5;
	return FMath::Max(0, (FMath::Max(1, RoundNumber) - 1) / BossCadence);
}

bool AAeyerjiLevelDirector::IsSurvivalBossRound(const int32 RoundNumber) const
{
	const int32 BossCadence = SurvivalMissionDefinition ? FMath::Max(1, SurvivalMissionDefinition->BossEveryNRounds) : 5;
	return BossCadence > 0 && FMath::Max(1, RoundNumber) % BossCadence == 0;
}

bool AAeyerjiLevelDirector::HandleSurvivalBossDefeated()
{
	if (!HasAuthority()
		|| SpawnMode != EAeyerjiLevelSpawnMode::SurvivalRounds
		|| !bRunActive
		|| !SurvivalMissionDefinition)
	{
		return false;
	}

	if (bSurvivalBossDefeatHandled)
	{
		return true;
	}

	if (!bSurvivalBossRoundActive)
	{
		return false;
	}

	if (!SurvivalMissionDefinition->bLoopAfterBoss)
	{
		return false;
	}

	WritePersistentFactsForTrigger(EAeyerjiPersistentFactWriteTrigger::BossDefeated);
	bSurvivalBossDefeatHandled = true;
	bSurvivalBossRoundActive = false;
	PublishSurvivalRoundState(EAeyerjiSurvivalRoundPhase::RoundComplete, FName(TEXT("BossDefeated")));

	if (UWorld* World = GetWorld())
	{
		const float Delay = FMath::Max(0.f, SurvivalMissionDefinition->InterRoundDelaySeconds);
		World->GetTimerManager().SetTimer(SurvivalRoundDelayHandle, this, &AAeyerjiLevelDirector::StartNextSurvivalRound, Delay, false);
	}

	return true;
}

APawn* AAeyerjiLevelDirector::SpawnBossEncounter_Implementation(AAeyerjiEncounterDirector* EncounterDirector)
{
	// Blueprint is expected to drive boss spawning. Set bEnableNativeBossSpawn=true if you want this native fallback to run.
	if (!bEnableNativeBossSpawn)
	{
		return nullptr;
	}

	if (!HasAuthority() || !IsValid(BossSpawner) || !*BossPawnClass)
	{
		return nullptr;
	}

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	AController* PlayerController = PlayerPawn ? PlayerPawn->GetController() : nullptr;

	const FTransform SpawnTransform = BossSpawnMarker ? BossSpawnMarker->GetActorTransform() : BossSpawner->GetActorTransform();

	FEnemySet BossEnemySet;
	BossEnemySet.EnemyClass = BossPawnClass;
	BossEnemySet.Count = 1;
	BossEnemySet.SpawnInterval = 0.0f;
	BossEnemySet.bIsBoss = true;

	APawn* SpawnedBoss = UAeyerjiEnemyManagementBPFL::SpawnAndRegisterEnemyFromSet(
		this,
		BossEnemySet,
		SpawnTransform,
		BossSpawner,
		/*Owner=*/this,
		PlayerPawn,
		/*bApplyEliteSettings=*/false,
		/*bApplyAggro=*/false,
		/*bAutoActivate=*/true,
		/*bAutoActivateOnlyIfNoWaves=*/true,
		PlayerPawn,
		PlayerController,
		/*bSkipRandomEliteResolution=*/true);
	if (!SpawnedBoss)
	{
		return nullptr;
	}

	if (IsValid(EncounterDirector))
	{
		EncounterDirector->SetBossSpawned(true);
	}
	else if (AAeyerjiEncounterDirector* Director = GetOrFindEncounterDirector())
	{
		Director->SetBossSpawned(true);
	}

	return SpawnedBoss;
}

void AAeyerjiLevelDirector::SetBossSpawnMarker(AActor* NewMarker)
{
	BossSpawnMarker = NewMarker;

	if (AAeyerjiEncounterDirector* EncounterDirector = GetOrFindEncounterDirector())
	{
		EncounterDirector->PushObjectiveStateToGameState();
	}
}

AAeyerjiLinkedTeleporter* AAeyerjiLevelDirector::SpawnBossLinkedTeleporter()
{
	if (!HasAuthority())
	{
		return ActiveBossLinkedTeleporter;
	}

	if (IsValid(ActiveBossLinkedTeleporter))
	{
		return ActiveBossLinkedTeleporter;
	}

	if (!*BossLinkedTeleporterClass)
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	AAeyerjiLinkedTeleporter* SpawnedTeleporter = World->SpawnActor<AAeyerjiLinkedTeleporter>(
		BossLinkedTeleporterClass,
		GetBossTeleporterEndpointATransform(),
		SpawnParams);
	if (!SpawnedTeleporter)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("LevelDirector::SpawnBossLinkedTeleporter failed to spawn teleporter (Director=%s Class=%s)."),
			*GetNameSafe(this),
			*GetNameSafe(BossLinkedTeleporterClass.Get()));
		return nullptr;
	}

	FTransform EndpointBTransform;
	if (GetBossTeleporterEndpointBTransform(EndpointBTransform))
	{
		SpawnedTeleporter->SetEndpointBWorldTransform(EndpointBTransform);
	}

	ActiveBossLinkedTeleporter = SpawnedTeleporter;
	return ActiveBossLinkedTeleporter;
}

void AAeyerjiLevelDirector::ClearBossLinkedTeleporter()
{
	if (!HasAuthority())
	{
		return;
	}

	if (IsValid(ActiveBossLinkedTeleporter))
	{
		ActiveBossLinkedTeleporter->Destroy();
	}

	ActiveBossLinkedTeleporter = nullptr;
}

bool AAeyerjiLevelDirector::IsBossSpawnBlockedByPrimaryObjective() const
{
	return RunWinCondition == EAeyerjiRunWinCondition::KillTargetThenBoss && !bPrimaryObjectiveComplete;
}

AActor* AAeyerjiLevelDirector::FindActorByTag(const FName ActorTag) const
{
	if (ActorTag.IsNone())
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (IsValid(Actor) && Actor->ActorHasTag(ActorTag))
		{
			return Actor;
		}
	}

	return nullptr;
}

AAeyerjiSpawnerGroup* AAeyerjiLevelDirector::FindSpawnerByTag(const FName ActorTag) const
{
	AActor* Actor = FindActorByTag(ActorTag);
	if (!Actor)
	{
		return nullptr;
	}

	AAeyerjiSpawnerGroup* Spawner = Cast<AAeyerjiSpawnerGroup>(Actor);
	if (!Spawner)
	{
		UE_LOG(LogTemp, Warning, TEXT("LevelDirector %s resolved tag %s to %s, but it is not an AAeyerjiSpawnerGroup."),
			*GetNameSafe(this),
			*ActorTag.ToString(),
			*GetNameSafe(Actor));
	}

	return Spawner;
}

void AAeyerjiLevelDirector::ApplyPersistentFactWrites(const TArray<FAeyerjiPersistentFactWrite>& FactWrites)
{
	if (FactWrites.Num() == 0 || !HasAuthority())
	{
		return;
	}

	UAeyerjiWorldStateSubsystem* WorldStateSubsystem = UAeyerjiWorldStateSubsystem::Get(this);
	if (!WorldStateSubsystem)
	{
		UE_LOG(LogTemp, Warning, TEXT("LevelDirector %s could not write persistent facts because WorldStateSubsystem is missing."), *GetNameSafe(this));
		return;
	}

	FName ActiveZoneId = NAME_None;
	if (const UWorld* World = GetWorld())
	{
		if (const AAeyerjiGameState* GameState = World->GetGameState<AAeyerjiGameState>())
		{
			ActiveZoneId = GameState->GetActiveZoneId();
		}
	}

	for (const FAeyerjiPersistentFactWrite& FactWrite : FactWrites)
	{
		if (!FactWrite.StateTag.IsValid())
		{
			continue;
		}

		const FName InstanceId = FactWrite.bUseActiveZoneAsInstanceId && !ActiveZoneId.IsNone()
			? ActiveZoneId
			: FactWrite.InstanceId;
		const FAeyerjiWorldStateKey Key(FactWrite.StateTag, InstanceId, FactWrite.OwnerId);
		WorldStateSubsystem->SetValue(
			Key,
			FAeyerjiWorldStateValue::FromBool(FactWrite.bValue),
			EAeyerjiWorldStatePersistence::Persistent,
			FactWrite.Replication,
			FactWrite.Scope);

		UE_LOG(LogTemp, Display, TEXT("LevelDirector %s wrote persistent fact %s=%d Scope=%d."),
			*GetNameSafe(this),
			*Key.ToString(),
			FactWrite.bValue ? 1 : 0,
			static_cast<int32>(FactWrite.Scope));
	}
}

FTransform AAeyerjiLevelDirector::GetBossTeleporterEndpointATransform() const
{
	if (bUseBossTeleporterEndpointATransform)
	{
		return BossTeleporterEndpointATransform;
	}

	if (BossTeleporterEndpointA)
	{
		return BossTeleporterEndpointA->GetActorTransform();
	}

	if (AActor* TaggedEndpointA = FindActorByTag(BossTeleporterEndpointATag))
	{
		return TaggedEndpointA->GetActorTransform();
	}

	if (BossSpawnMarker)
	{
		return BossSpawnMarker->GetActorTransform();
	}

	if (BossSpawner)
	{
		return BossSpawner->GetActorTransform();
	}

	return GetActorTransform();
}

bool AAeyerjiLevelDirector::GetBossTeleporterEndpointBTransform(FTransform& OutTransform) const
{
	if (bUseBossTeleporterEndpointBTransform)
	{
		OutTransform = BossTeleporterEndpointBTransform;
		return true;
	}

	if (BossTeleporterEndpointB)
	{
		OutTransform = BossTeleporterEndpointB->GetActorTransform();
		return true;
	}

	if (AActor* TaggedEndpointB = FindActorByTag(BossTeleporterEndpointBTag))
	{
		OutTransform = TaggedEndpointB->GetActorTransform();
		return true;
	}

	return false;
}
