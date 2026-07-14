#include "Director/AeyerjiLevelDirector.h"

#include "Director/AeyerjiSpawnerGroup.h"
#include "Director/AeyerjiEncounterDirector.h"
#include "Director/AeyerjiWorldSpawnProfile.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AeyerjiGameplayTags.h"
#include "../../AeyerjiPlayerController.h"
#include "../../AeyerjiPlayerState.h"
#include "../../AeyerjiGameState.h"
#include "CharacterStatsLibrary.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "GAS/GE_DamagePhysical.h"
#include "Interaction/AeyerjiInteractable.h"
#include "Inventory/AeyerjiRewardPresentationActor.h"
#include "Progression/AeyerjiLevelingComponent.h"
#include "Engine/World.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Enemy/AeyerjiEnemyManagementBPFL.h"
#include "Enemy/EnemyParentNative.h"
#include "Logging/AeyerjiLog.h"
#include "Systems/AeyerjiDifficultyTuning.h"
#include "Systems/AeyerjiWorldStateSubsystem.h"
#include "World/AeyerjiLinkedTeleporter.h"
#include "World/AeyerjiSurvivalDefenseObjectiveActor.h"
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
	Context.PitySoftStartOverride = BossPitySoftStartOverride;
	Context.PitySoftSlopeOverride = BossPitySoftSlopeOverride;
	Context.PityHardAttemptsOverride = BossPityHardAttemptsOverride;
	Context.PityMaxChanceOverride = BossPityMaxChanceOverride;

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
	Spawner->OnTrackedEnemiesRemoved.RemoveDynamic(this, &AAeyerjiLevelDirector::HandleSpawnerTrackedEnemiesRemoved);
	Spawner->OnTrackedEnemiesRemoved.AddDynamic(this, &AAeyerjiLevelDirector::HandleSpawnerTrackedEnemiesRemoved);
	Spawner->OnBossDefeated.RemoveDynamic(this, &AAeyerjiLevelDirector::HandleSpawnerBossDefeated);
	Spawner->OnBossDefeated.AddDynamic(this, &AAeyerjiLevelDirector::HandleSpawnerBossDefeated);
	Spawner->OnWaveStarted.RemoveDynamic(this, &AAeyerjiLevelDirector::HandleSpawnerWaveStarted);
	Spawner->OnWaveStarted.AddDynamic(this, &AAeyerjiLevelDirector::HandleSpawnerWaveStarted);
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

	InvalidateSurvivalRuntimeCaches();
	ClearSurvivalDefenseObjective();

	SpawnMode = ZoneRunDefinition->SpawnMode;
	RunWinCondition = ZoneRunDefinition->RunWinCondition;
	ShardsNeeded = FMath::Max(1, ZoneRunDefinition->ShardsNeeded);
	bAutoStartFirstRoom = ZoneRunDefinition->bAutoStartFirstRoom;
	ObjectiveKillTargetOverride = FMath::Max(0, ZoneRunDefinition->ObjectiveKillTargetOverride);
	RunTimeLimitSeconds = FMath::Max(0.f, ZoneRunDefinition->RunTimeLimitSeconds);
	RiftActivityType = ZoneRunDefinition->RiftActivityType;
	StandardRiftEnemyBudget = FMath::Max(1, ZoneRunDefinition->StandardRiftEnemyBudget);
	StandardRiftProgressTargetPoints = FMath::Max(1, ZoneRunDefinition->StandardRiftProgressTargetPoints);
	StandardRiftActivationDistance = FMath::Max(0.f, ZoneRunDefinition->StandardRiftActivationDistance);
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
	BossArenaRespawnPlayerStartTag = NAME_None;
	ActiveRiftTierNumber = 0;
	ActiveRiftActivity = FAeyerjiRiftActivitySnapshot();
	bHasActiveRiftActivity = false;
	ActiveRiftEnemyBudget = 0;
	ActiveRiftRegionActivationDistance = 2500.f;
	ActiveRiftDensityMultiplier = 1.f;
	ActiveRiftEliteRateMultiplier = 1.f;
	ActiveRiftEncounterSizeMultiplier = 1.f;
	ActiveRiftProgressMultiplier = 1.f;
	ActiveRiftMonsterPower = FAeyerjiRiftMonsterPowerSnapshot();

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
		BindSpawner(SurvivalRoundSpawner);
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
		BindSpawner(BossSpawner);
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
		BossArenaRespawnPlayerStartTag = BossDefinition->BossArenaRespawnPlayerStartTag;
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

	if (SpawnMode == EAeyerjiLevelSpawnMode::SurvivalRounds)
	{
		RebuildAuthoredSurvivalRoundsCache();
		BeginSurvivalAssetPreload();
	}
}

const FAeyerjiRiftTierRow* AAeyerjiLevelDirector::FindRiftTierRow(const int32 RiftTier) const
{
	if (RiftTier < 1)
	{
		return nullptr;
	}

	const UDataTable* TierTable = UAeyerjiDifficultySettings::GetRiftTierTable();
	if (!TierTable)
	{
		return nullptr;
	}

	const FName RowName(*FString::Printf(TEXT("Tier_%d"), RiftTier));
	return TierTable->FindRow<FAeyerjiRiftTierRow>(RowName, TEXT("Greater Rift Tier lookup"), false);
}

bool AAeyerjiLevelDirector::ApplyRiftActivityForNextRun(
	const FAeyerjiRiftActivitySnapshot& Activity,
	const FAeyerjiRiftTierRow* TierRow)
{
	if (!HasAuthority() || bRunActive || Activity.ActivityLevel <= 0)
	{
		return false;
	}

	if (Activity.ActivityType == EAeyerjiRiftActivityType::Excursion
		&& (!TierRow || Activity.ExcursionTier <= 0))
	{
		return false;
	}

	ActiveRiftActivity = Activity;
	ActiveRiftActivity.ActivityLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(Activity.ActivityLevel);
	if (Activity.ActivityType == EAeyerjiRiftActivityType::Excursion && TierRow)
	{
		// GameState resolves this from the snapped launch party. Clamp again here so
		// any future authority caller cannot bypass the authored Excursion ceiling.
		ActiveRiftActivity.ActivityLevel = FMath::Min(ActiveRiftActivity.ActivityLevel,
			FMath::Max(TierRow->MaxActivityLevel, 1));
	}
	bHasActiveRiftActivity = true;
	ActiveRiftTierNumber = Activity.ActivityType == EAeyerjiRiftActivityType::Excursion
		? Activity.ExcursionTier
		: 0;
	ActiveRiftMonsterPower = FAeyerjiRiftMonsterPowerSnapshot();
	ActiveRiftMonsterPower.MonsterPowerIndex = ActiveRiftTierNumber;
	ActiveRiftDensityMultiplier = 1.f;
	ActiveRiftEliteRateMultiplier = 1.f;
	ActiveRiftEncounterSizeMultiplier = 1.f;
	ActiveRiftProgressMultiplier = 1.f;

	if (Activity.ActivityType == EAeyerjiRiftActivityType::Excursion && TierRow)
	{
		// The encounter director applies density and encounter-size after this base
		// budget is frozen, preserving one finite population ceiling for the run.
		ActiveRiftEnemyBudget = FMath::Max(TierRow->EnemyBudget, 1);
		ActiveRiftRegionActivationDistance = FMath::Max(TierRow->RegionActivationDistance, 0.f);
		ActiveRiftMonsterPower.HealthMultiplier = FMath::Max(TierRow->HealthMultiplier, 0.f);
		ActiveRiftMonsterPower.DamageMultiplier = FMath::Max(TierRow->DamageMultiplier, 0.f);
		ActiveRiftMonsterPower.DefenseMultiplier = FMath::Max(TierRow->DefenseMultiplier, 0.f);
		ActiveRiftMonsterPower.RewardQualityMultiplier = FMath::Max(TierRow->RewardQualityMultiplier, 0.f);
		ActiveRiftDensityMultiplier = FMath::Max(TierRow->DensityMultiplier, 0.1f);
		ActiveRiftEliteRateMultiplier = FMath::Max(TierRow->EliteRateMultiplier, 0.f);
		ActiveRiftEncounterSizeMultiplier = FMath::Max(TierRow->EncounterSizeMultiplier, 0.1f);
		ActiveRiftProgressMultiplier = FMath::Max(TierRow->ProgressMultiplier, 0.1f);
		RunTimeLimitSeconds = FMath::Max(1.f, TierRow->TimeLimitSeconds);
		ObjectiveKillTargetOverride = FMath::Max(1, TierRow->ProgressTargetPoints);
	}
	else
	{
		ActiveRiftEnemyBudget = FMath::Max(StandardRiftEnemyBudget, 1);
		ActiveRiftRegionActivationDistance = FMath::Max(StandardRiftActivationDistance, 0.f);
		RunTimeLimitSeconds = RunTimeLimitSeconds > 0.f ? RunTimeLimitSeconds : 900.f;
		ObjectiveKillTargetOverride = FMath::Max(StandardRiftProgressTargetPoints, 1);
	}
	// Greater Rifts are independent of the legacy World Tier budget. Keep legacy
	// readers at Normal while selective monster-power multipliers own Rift combat.
	WorldTier = UAeyerjiDifficultySettings::GetNormalWorldTier();
	DifficultySlider = UAeyerjiDifficultySettings::WorldTierToDifficultySlider(WorldTier);

	// A Rift snapshots its difficulty. Player level-ups must not rescale living
	// enemies or groups that activate later in the same run.
	bResyncEnemyLevelsOnRunStart = false;
	bResyncEnemyLevelsOnPlayerLevelUp = false;

	UE_LOG(LogTemp, Display,
		TEXT("[RiftRun][Activity] Director=%s Type=%s ActivityLevel=%d ExcursionTier=%d Health=%.3f Damage=%.3f Defense=%.3f RewardQuality=%.3f EnemyBudget=%d ActivationDistance=%.1f"),
		*GetNameSafe(this),
		*StaticEnum<EAeyerjiRiftActivityType>()->GetNameStringByValue(static_cast<int64>(ActiveRiftActivity.ActivityType)),
		ActiveRiftActivity.ActivityLevel, ActiveRiftActivity.ExcursionTier,
		ActiveRiftMonsterPower.HealthMultiplier, ActiveRiftMonsterPower.DamageMultiplier,
		ActiveRiftMonsterPower.DefenseMultiplier, ActiveRiftMonsterPower.RewardQualityMultiplier,
		ActiveRiftEnemyBudget, ActiveRiftRegionActivationDistance);
	return true;
}

float AAeyerjiLevelDirector::GetRiftAttributeMultiplier(const FGameplayAttribute& Attribute) const
{
	if (ActiveRiftTierNumber <= 0 || !Attribute.IsValid())
	{
		return 1.f;
	}

	if (Attribute == UAeyerjiAttributeSet::GetHPMaxAttribute()
		|| Attribute == UAeyerjiAttributeSet::GetHPRegenAttribute())
	{
		return ActiveRiftMonsterPower.HealthMultiplier;
	}
	if (Attribute == UAeyerjiAttributeSet::GetAttackDamageAttribute()
		|| Attribute == UAeyerjiAttributeSet::GetSpellPowerAttribute()
		|| Attribute == UAeyerjiAttributeSet::GetStaggerPowerAttribute())
	{
		return ActiveRiftMonsterPower.DamageMultiplier;
	}
	if (Attribute == UAeyerjiAttributeSet::GetArmorAttribute()
		|| Attribute == UAeyerjiAttributeSet::GetPoiseMaxAttribute()
		|| Attribute == UAeyerjiAttributeSet::GetStaggerResistanceAttribute())
	{
		return ActiveRiftMonsterPower.DefenseMultiplier;
	}

	// Movement, attack speed, ranges, vision, and resources are deliberately not
	// monster-power stats. Their authored feel remains identical across Rift tiers.
	return 1.f;
}

float AAeyerjiLevelDirector::GetActiveRiftRewardQualityMultiplier() const
{
	return ActiveRiftTierNumber > 0 ? ActiveRiftMonsterPower.RewardQualityMultiplier : 1.f;
}

bool AAeyerjiLevelDirector::ValidateRunStartReadiness(FString& OutReason)
{
	OutReason.Reset();
	if (!HasAuthority())
	{
		OutReason = TEXT("LevelDirector is not authoritative");
		return false;
	}

	if (!GetOrFindEncounterDirector())
	{
		OutReason = TEXT("EncounterDirector is missing");
		return false;
	}

	const bool bUsesBoss = RunWinCondition == EAeyerjiRunWinCondition::BossCleared
		|| RunWinCondition == EAeyerjiRunWinCondition::KillTargetThenBoss;
	if (!bUsesBoss)
	{
		return true;
	}

	const bool bHasNativeBossPath = bEnableNativeBossSpawn && BossPawnClass && (BossSpawnMarker || BossSpawner);
	const bool bHasAuthoredBossPath = IsValid(BossSpawner) || IsValid(BossTriggerActor);
	if (!bHasNativeBossPath && !bHasAuthoredBossPath)
	{
		OutReason = TEXT("Required boss spawner/trigger or native boss class/marker is missing");
		return false;
	}

	if (BossLinkedTeleporterClass)
	{
		// Match the endpoint-A resolution order used when the teleporter actually spawns.
		// Tagged markers are the reliable map-authored path for streamed gameplay zones.
		const bool bHasEndpointA = bUseBossTeleporterEndpointATransform
			|| IsValid(BossTeleporterEndpointA)
			|| (!BossTeleporterEndpointATag.IsNone() && IsValid(FindActorByTag(BossTeleporterEndpointATag)));
		FTransform EndpointBTransform;
		if (!bHasEndpointA || !GetBossTeleporterEndpointBTransform(EndpointBTransform))
		{
			OutReason = TEXT("Boss linked teleporter has unresolved endpoint A or B");
			return false;
		}
	}

	return true;
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
		TEXT("LevelDirector=%s ZoneDef=%s ZoneId=%s SpawnMode=%s Win=%s Active=%d Sequence=%d SurvivalMission=%s SurvivalRound=%d SurvivalCycle=%d SurvivalLevelBonus=%d SurvivalHPx=%.3f SurvivalDMGx=%.3f SurvivalPhaseBoss=%d SurvivalSpawner=%s BossDef=%s BossSpawner=%s BossGate=%s BossMarker=%s BossTrigger=%s WorldSpawner=%s"),
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
		SurvivalEnemyLevelBonus,
		SurvivalEnemyHealthMultiplier,
		SurvivalEnemyDamageMultiplier,
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

	if (ActiveRiftTierNumber <= 0)
	{
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
	}

	bRunActive = true;
	AccumulatedRunSeconds = 0.f;
	bRunTimerExpiredBroadcast = false;
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
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(SurvivalUpgradeOfferTimeoutHandle);
			World->GetTimerManager().ClearTimer(SurvivalDefenseObjectiveRegenHandle);
		}
		ClearSurvivalUpgradeOfferState();
		if (AAeyerjiSurvivalDefenseObjectiveActor* Obj = Cast<AAeyerjiSurvivalDefenseObjectiveActor>(SurvivalDefenseObjectiveActor.Get()))
		{
			Obj->ResetSurvivalUpgrades();
		}
		SurvivalDefenseObjectiveReflectFraction = 0.f;
		SurvivalDefenseObjectiveRegenPerSecond = 0.f;
		CurrentSurvivalRound = 0;
		CurrentSurvivalCycle = 0;
		CurrentSurvivalRoundEnemyTotal = 0;
		CurrentSurvivalRoundEnemiesKilled = 0;
		CurrentSurvivalWaveIndex = INDEX_NONE;
		CurrentSurvivalWaveCount = 0;
		CurrentSurvivalWaveEnemyTotal = 0;
		CurrentSurvivalWaveDisplayLabel = FText::GetEmpty();
		bCurrentSurvivalWaveContainsBoss = false;
		CurrentSurvivalWaveEnemiesKilled = 0;
		CurrentSurvivalRoundPhase = EAeyerjiSurvivalRoundPhase::Inactive;
		SurvivalEnemyLevelBonus = 0;
		SurvivalEnemyHealthMultiplier = 1.f;
		SurvivalEnemyDamageMultiplier = 1.f;
		bSurvivalBossRoundActive = false;
		bSurvivalBossDefeatHandled = false;
		bSurvivalDefenseObjectiveDestroyed = false;
		ResolveSurvivalDefenseObjective();
		BeginSurvivalAssetPreload();
		if (!AreSurvivalAssetsReady())
		{
			const FName MessageKey = IsSurvivalDefenseObjectiveEnabled() && !SurvivalMissionDefinition->DefenseObjective.ObjectiveActiveMessageKey.IsNone()
				? SurvivalMissionDefinition->DefenseObjective.ObjectiveActiveMessageKey
				: FName(TEXT("Preloading"));
			PublishSurvivalRoundState(EAeyerjiSurvivalRoundPhase::Preparing, MessageKey);
			return;
		}
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

bool AAeyerjiLevelDirector::PrepareRiftRegionEncounterPlan(
	const int32 RunSerial,
	const int32 RunSeed,
	const int32 ProgressTarget,
	FString& OutReason)
{
	OutReason.Reset();
	if (!HasAuthority() || SpawnMode != EAeyerjiLevelSpawnMode::ProximityEncounterRegions)
	{
		return SpawnMode != EAeyerjiLevelSpawnMode::ProximityEncounterRegions;
	}
	AAeyerjiEncounterDirector* EncounterDirector = GetOrFindEncounterDirector();
	if (!EncounterDirector)
	{
		OutReason = TEXT("EncounterDirector disappeared while applying Rift region plan");
		return false;
	}
	return EncounterDirector->BeginRiftRegionRun(
		RunSerial,
		RunSeed,
		ProgressTarget,
		ActiveRiftEnemyBudget,
		ActiveRiftRegionActivationDistance,
		ActiveRiftDensityMultiplier,
		ActiveRiftEliteRateMultiplier,
		ActiveRiftEncounterSizeMultiplier,
		ActiveRiftProgressMultiplier,
		WorldPopulationSpawner,
		this,
		OutReason);
}

void AAeyerjiLevelDirector::DisableUnopenedRiftEncounterRegions()
{
	if (!HasAuthority())
	{
		return;
	}
	if (AAeyerjiEncounterDirector* EncounterDirector = GetOrFindEncounterDirector())
	{
		EncounterDirector->StopRiftRegionActivation();
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
		World->GetTimerManager().ClearTimer(SurvivalUpgradeOfferTimeoutHandle);
		World->GetTimerManager().ClearTimer(SurvivalDefenseObjectiveRegenHandle);
	}

	OnRunStateChanged.Broadcast(false);
	CurrentSurvivalRound = 0;
	CurrentSurvivalCycle = 0;
	CurrentSurvivalRoundEnemyTotal = 0;
	CurrentSurvivalRoundEnemiesKilled = 0;
	CurrentSurvivalWaveIndex = INDEX_NONE;
	CurrentSurvivalWaveCount = 0;
	CurrentSurvivalWaveEnemyTotal = 0;
	CurrentSurvivalWaveDisplayLabel = FText::GetEmpty();
	bCurrentSurvivalWaveContainsBoss = false;
	CurrentSurvivalWaveEnemiesKilled = 0;
	CurrentSurvivalRoundPhase = EAeyerjiSurvivalRoundPhase::Inactive;
	SurvivalEnemyLevelBonus = 0;
	SurvivalEnemyHealthMultiplier = 1.f;
	SurvivalEnemyDamageMultiplier = 1.f;
	bSurvivalBossRoundActive = false;
	bSurvivalBossDefeatHandled = false;
	ClearSurvivalUpgradeOfferState();
	if (AAeyerjiSurvivalDefenseObjectiveActor* Obj = Cast<AAeyerjiSurvivalDefenseObjectiveActor>(SurvivalDefenseObjectiveActor.Get()))
	{
		Obj->ResetSurvivalUpgrades();
	}
	SurvivalDefenseObjectiveReflectFraction = 0.f;
	SurvivalDefenseObjectiveRegenPerSecond = 0.f;
	ClearSurvivalDefenseObjective();
	if (AAeyerjiGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AAeyerjiGameState>() : nullptr)
	{
		GameState->ClearSurvivalRoundStateFromServer();
		GameState->ClearSurvivalUpgradeOfferStateFromServer();
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
			const bool bRiftOvertimeMode = ActiveRiftTierNumber > 0
				|| SpawnMode == EAeyerjiLevelSpawnMode::ProximityEncounterRegions;
			if (!bRunTimerExpiredBroadcast)
			{
				bRunTimerExpiredBroadcast = true;
				OnRunTimerExpired.Broadcast();
			}
			if (!bRiftOvertimeMode)
			{
				AccumulatedRunSeconds = RunTimeLimitSeconds;
				EndRun();
			}
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

bool AAeyerjiLevelDirector::IsActiveSurvivalRun() const
{
	return bRunActive
		&& SpawnMode == EAeyerjiLevelSpawnMode::SurvivalRounds
		&& IsValid(SurvivalMissionDefinition);
}

float AAeyerjiLevelDirector::ResolvePlayerRespawnDelaySeconds(const float DefaultDelay) const
{
	if (!IsActiveSurvivalRun()
		|| !SurvivalMissionDefinition->bOverridePlayerRespawnDelay)
	{
		return DefaultDelay;
	}

	return FMath::Max(0.f, SurvivalMissionDefinition->PlayerRespawnDelaySeconds);
}

bool AAeyerjiLevelDirector::IsSurvivalDefenseObjectiveEnabled() const
{
	return IsActiveSurvivalRun()
		&& SurvivalMissionDefinition->DefenseObjective.bEnabled
		&& !SurvivalMissionDefinition->DefenseObjective.ObjectiveActorTag.IsNone();
}

void AAeyerjiLevelDirector::OpenSurvivalDefenseObjectiveRepairMenu(AAeyerjiPlayerController* Controller, AActor* ObjectiveActor) const
{
	if (!HasAuthority() || !Controller)
	{
		return;
	}

	if (!IsSurvivalDefenseObjectiveEnabled()
		|| ObjectiveActor != SurvivalDefenseObjectiveActor.Get()
		|| !IsSurvivalDefenseObjectiveAlive())
	{
		Controller->Client_ShowMissionMessageKey(FName(TEXT("RepairUnavailable")), 2.f);
		return;
	}

	TArray<FAeyerjiDefenseRepairOption> RepairOptions = SurvivalMissionDefinition->DefenseObjective.RepairOptions;
	if (RepairOptions.IsEmpty())
	{
		RepairOptions = {
			FAeyerjiDefenseRepairOption(FName(TEXT("Small")), FName(TEXT("DefenseRepairSmall")), 25, 0.f, 0.15f),
			FAeyerjiDefenseRepairOption(FName(TEXT("Medium")), FName(TEXT("DefenseRepairMedium")), 60, 0.f, 0.40f),
			FAeyerjiDefenseRepairOption(FName(TEXT("Full")), FName(TEXT("DefenseRepairFull")), 120, 0.f, 1.00f)
		};
	}

	const AAeyerjiPlayerState* AeyerjiPS = Controller->GetPlayerState<AAeyerjiPlayerState>();
	Controller->Client_ShowDefenseObjectiveRepairMenu(
		ObjectiveActor,
		RepairOptions,
		AeyerjiPS ? AeyerjiPS->GetGold() : 0,
		GetSurvivalDefenseObjectiveHealth(),
		GetSurvivalDefenseObjectiveMaxHealth());
}

bool AAeyerjiLevelDirector::TryRepairSurvivalDefenseObjective(AAeyerjiPlayerController* Controller, AActor* ObjectiveActor, const FName OptionId)
{
	if (!HasAuthority() || !Controller)
	{
		return false;
	}

	auto Reject = [Controller](const FName MessageKey)
	{
		Controller->Client_ShowMissionMessageKey(MessageKey, 2.f);
		return false;
	};

	if (!IsSurvivalDefenseObjectiveEnabled()
		|| ObjectiveActor != SurvivalDefenseObjectiveActor.Get()
		|| !IsSurvivalDefenseObjectiveAlive())
	{
		return Reject(FName(TEXT("RepairUnavailable")));
	}

	APawn* Pawn = Controller->GetPawn();
	if (!IsValid(Pawn))
	{
		return Reject(FName(TEXT("RepairUnavailable")));
	}

	FVector InteractionLocation = ObjectiveActor->GetActorLocation();
	float InteractionRadius = 400.f;
	if (ObjectiveActor->GetClass()->ImplementsInterface(UAeyerjiInteractable::StaticClass()))
	{
		InteractionLocation = IAeyerjiInteractable::Execute_GetInteractionLocation(ObjectiveActor);
		InteractionRadius = FMath::Max(IAeyerjiInteractable::Execute_GetInteractionRadius(ObjectiveActor), 30.f);
	}

	if (FVector::Dist2D(Pawn->GetActorLocation(), InteractionLocation) > InteractionRadius)
	{
		return Reject(FName(TEXT("RepairUnavailable")));
	}

	TArray<FAeyerjiDefenseRepairOption> RepairOptions = SurvivalMissionDefinition->DefenseObjective.RepairOptions;
	if (RepairOptions.IsEmpty())
	{
		RepairOptions = {
			FAeyerjiDefenseRepairOption(FName(TEXT("Small")), FName(TEXT("DefenseRepairSmall")), 25, 0.f, 0.15f),
			FAeyerjiDefenseRepairOption(FName(TEXT("Medium")), FName(TEXT("DefenseRepairMedium")), 60, 0.f, 0.40f),
			FAeyerjiDefenseRepairOption(FName(TEXT("Full")), FName(TEXT("DefenseRepairFull")), 120, 0.f, 1.00f)
		};
	}

	const FAeyerjiDefenseRepairOption* SelectedOption = RepairOptions.FindByPredicate([OptionId](const FAeyerjiDefenseRepairOption& Option)
	{
		return Option.OptionId == OptionId;
	});
	if (!SelectedOption)
	{
		return Reject(FName(TEXT("RepairUnavailable")));
	}

	const float MaxHealth = GetSurvivalDefenseObjectiveMaxHealth();
	const float CurrentHealth = GetSurvivalDefenseObjectiveHealth();
	if (MaxHealth <= UE_SMALL_NUMBER || CurrentHealth >= MaxHealth - KINDA_SMALL_NUMBER)
	{
		return Reject(FName(TEXT("TreeAlreadyFull")));
	}

	AAeyerjiPlayerState* AeyerjiPS = Controller->GetPlayerState<AAeyerjiPlayerState>();
	if (!AeyerjiPS)
	{
		return Reject(FName(TEXT("RepairUnavailable")));
	}

	if (!AeyerjiPS->CanSpendGold(SelectedOption->GoldCost))
	{
		return Reject(FName(TEXT("NotEnoughGold")));
	}

	UAbilitySystemComponent* ObjectiveASC = CachedSurvivalDefenseObjectiveASC.Get();
	if (!ObjectiveASC)
	{
		return Reject(FName(TEXT("RepairUnavailable")));
	}

	const float HealAmount = FMath::Max(0.f, SelectedOption->FlatHeal) + (MaxHealth * FMath::Max(0.f, SelectedOption->PercentHeal));
	if (HealAmount <= KINDA_SMALL_NUMBER)
	{
		return Reject(FName(TEXT("RepairUnavailable")));
	}

	if (!AeyerjiPS->TrySpendGold(SelectedOption->GoldCost, FName(TEXT("DefenseObjectiveRepair"))))
	{
		return Reject(FName(TEXT("NotEnoughGold")));
	}

	ObjectiveASC->SetNumericAttributeBase(
		UAeyerjiAttributeSet::GetHPAttribute(),
		FMath::Clamp(CurrentHealth + HealAmount, 0.f, MaxHealth));

	PublishSurvivalRoundState(CurrentSurvivalRoundPhase, FName(TEXT("DefenseObjectiveRepaired")));
	return true;
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
	if (bRunActive && bHasActiveRiftActivity)
	{
		return ActiveRiftActivity.ActivityLevel;
	}
	return UAeyerjiDifficultySettings::ClampGameplayLevel(GetCurrentPlayerLevel() + FMath::Max(0, SurvivalEnemyLevelBonus));
}

int32 AAeyerjiLevelDirector::GetEffectiveEnemyLevelForPlayerLevel(const int32 PlayerLevel) const
{
	if (bRunActive && bHasActiveRiftActivity)
	{
		return ActiveRiftActivity.ActivityLevel;
	}
	return UAeyerjiDifficultySettings::Get()->EvaluateEnemyLevel(PlayerLevel);
}

int32 AAeyerjiLevelDirector::GetEffectiveEnemyLevel() const
{
	return GetEffectiveEnemyLevelForPlayerLevel(GetCurrentPlayerLevel());
}

float AAeyerjiLevelDirector::GetGlobalStatBudgetMultiplier() const
{
	if (ActiveRiftTierNumber > 0)
	{
		return 1.f;
	}
	return UAeyerjiDifficultySettings::Get()->EvaluateStatBudget(WorldTier);
}

float AAeyerjiLevelDirector::GetDerivedDifficultyAlpha() const
{
	if (ActiveRiftTierNumber > 0)
	{
		// Legacy UI consumes 0..1. This value is presentation-only; combat uses the
		// explicit snapshot above and is never reconstructed from this alpha.
		return FMath::Clamp((ActiveRiftMonsterPower.RewardQualityMultiplier - 1.f) / 3.f, 0.f, 1.f);
	}
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
		if (SpawnMode == EAeyerjiLevelSpawnMode::SurvivalRounds)
		{
			ApplySurvivalDefenseObjectiveToSpawner(BossSpawner, /*bBossRound=*/true);
		}
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
			SpawnSurvivalRoundClearReward();
			BeginSurvivalRoundUpgradeOfferOrScheduleNextRound();
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

void AAeyerjiLevelDirector::SpawnSurvivalRoundClearReward()
{
	if (!HasAuthority() || SpawnMode != EAeyerjiLevelSpawnMode::SurvivalRounds || CurrentSurvivalRound <= 0)
	{
		return;
	}

	if (LastSurvivalRoundClearRewardSpawnedRound == CurrentSurvivalRound)
	{
		UE_LOG(LogAeyerji, Warning,
			TEXT("[LootReward][SurvivalRound] Skipped duplicate reward spawn for round %d on %s."),
			CurrentSurvivalRound,
			*GetNameSafe(this));
		return;
	}

	const FAeyerjiSurvivalRoundRewardDefinition* Reward = ResolveSurvivalRoundClearReward();
	if (!Reward || !Reward->bEnabled)
	{
		return;
	}

	if (!IsSurvivalRoundRewardEligible(*Reward))
	{
		return;
	}

	if (Reward->MultiDropConfig.TotalBaseDrops <= 0 && Reward->MultiDropConfig.Buckets.IsEmpty())
	{
		UE_LOG(LogAeyerji, Warning,
			TEXT("[LootReward][SurvivalRound] LevelDirector %s skipped survival round reward for round %d: no total drops or buckets configured."),
			*GetNameSafe(this),
			CurrentSurvivalRound);
		return;
	}

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	AActor* PlayerActor = PlayerPawn;
	if (!PlayerActor)
	{
		PlayerActor = UGameplayStatics::GetPlayerController(this, 0);
	}

	FLootContext RuntimeContext = BuildSurvivalRewardLootContext(*Reward, PlayerActor);
	if (!RuntimeContext.SourceTag.IsValid() && !RuntimeContext.ForcedItemDefinition.Get())
	{
		UE_LOG(LogAeyerji, Warning,
			TEXT("[LootReward][SurvivalRound] LevelDirector %s skipped survival round reward for round %d: SourceTag or ForcedItemDefinition is required."),
			*GetNameSafe(this),
			CurrentSurvivalRound);
		return;
	}

	ULootService* LootService = UCharacterStatsLibrary::GetLootService(this);
	if (!LootService)
	{
		UE_LOG(LogAeyerji, Warning,
			TEXT("[LootReward][SurvivalRound] LevelDirector %s skipped survival round reward for round %d: LootService missing."),
			*GetNameSafe(this),
			CurrentSurvivalRound);
		return;
	}

	UE_LOG(LogAeyerji, Display,
		TEXT("[LootReward][SurvivalRound] Resolved config Round=%d SourceTag=%s DropMode=%d TotalBaseDrops=%d TotalVariance=%d Buckets=%d PresentationClass=%s"),
		CurrentSurvivalRound,
		*RuntimeContext.SourceTag.ToString(),
		static_cast<int32>(Reward->DropMode),
		Reward->MultiDropConfig.TotalBaseDrops,
		Reward->MultiDropConfig.TotalVariance,
		Reward->MultiDropConfig.Buckets.Num(),
		*GetNameSafe(Reward->PresentationActorClass.Get()));
	for (int32 BucketIndex = 0; BucketIndex < Reward->MultiDropConfig.Buckets.Num(); ++BucketIndex)
	{
		const FLootMultiDropBucket& Bucket = Reward->MultiDropConfig.Buckets[BucketIndex];
		UE_LOG(LogAeyerji, Display,
			TEXT("[LootReward][SurvivalRound] Bucket Round=%d Index=%d Tag=%s BaseDrops=%d Variance=%d MinimumRarity=%d UniqueWithin=%d UniqueAcross=%d"),
			CurrentSurvivalRound,
			BucketIndex,
			*Bucket.Tag.ToString(),
			Bucket.BaseDrops,
			Bucket.Variance,
			static_cast<int32>(Bucket.MinimumRarity),
			Bucket.bUniqueWithinBucket ? 1 : 0,
			Bucket.bUniqueAcrossBuckets ? 1 : 0);
	}

	TArray<FLootDropResult> LootResults;
	if (!LootService->RollMultiDrop(RuntimeContext, Reward->MultiDropConfig, LootResults) || LootResults.IsEmpty())
	{
		UE_LOG(LogAeyerji, Warning,
			TEXT("[LootReward][SurvivalRound] LevelDirector %s skipped survival round reward for round %d: RollMultiDrop produced no results."),
			*GetNameSafe(this),
			CurrentSurvivalRound);
		return;
	}

	LastSurvivalRoundClearRewardSpawnedRound = CurrentSurvivalRound;

	const FVector SpawnLocation = GetSurvivalRewardSpawnLocation(*Reward, PlayerPawn);
	if (Reward->PresentationActorClass)
	{
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParameters.Owner = UGameplayStatics::GetPlayerController(this, 0);
		SpawnParameters.Instigator = PlayerPawn;

		AAeyerjiRewardPresentationActor* PresentationActor = GetWorld()
			? GetWorld()->SpawnActor<AAeyerjiRewardPresentationActor>(
				Reward->PresentationActorClass,
				FTransform(FRotator::ZeroRotator, SpawnLocation),
				SpawnParameters)
			: nullptr;

		if (PresentationActor)
		{
			PresentationActor->InitializeReward(
				LootResults,
				Reward->DropMode,
				RuntimeContext.SourceTag,
				PlayerActor,
				Reward->LootReleaseOffset,
				Reward->PresentationLifeSpanAfterRelease);

			UE_LOG(LogAeyerji, Display,
				TEXT("[LootReward][SurvivalRound] Presentation stored reward Round=%d SourceTag=%s RolledResults=%d SpawnedPickups=0 Presentation=%s Location=%s"),
				CurrentSurvivalRound,
				*RuntimeContext.SourceTag.ToString(),
				LootResults.Num(),
				*GetNameSafe(PresentationActor),
				*SpawnLocation.ToCompactString());
			return;
		}

		UE_LOG(LogAeyerji, Warning,
			TEXT("[LootReward][SurvivalRound] LevelDirector %s failed to spawn reward presentation actor %s; spawning pickups directly."),
			*GetNameSafe(this),
			*GetNameSafe(Reward->PresentationActorClass.Get()));
	}

	const FAeyerjiLootSpawnSummary SpawnSummary = UAeyerjiInventoryBPFL::SpawnLootResults(
		this,
		LootResults,
		SpawnLocation,
		FRotator::ZeroRotator,
		/*SeedOverride=*/0,
		Reward->DropMode,
		PlayerActor);

	UE_LOG(LogAeyerji, Display,
		TEXT("[LootReward][SurvivalRound] Direct spawn complete Round=%d SourceTag=%s EnemyLevel=%d PlayerLevel=%d RolledResults=%d SpawnedPickups=%d FailedSpawns=%d Buckets=%d Location=%s"),
		CurrentSurvivalRound,
		*RuntimeContext.SourceTag.ToString(),
		RuntimeContext.EnemyLevel,
		RuntimeContext.PlayerLevel,
		LootResults.Num(),
		SpawnSummary.SpawnedPickupCount,
		SpawnSummary.FailedSpawnCount,
		Reward->MultiDropConfig.Buckets.Num(),
		*SpawnLocation.ToCompactString());
}

const FAeyerjiSurvivalRoundRewardDefinition* AAeyerjiLevelDirector::ResolveSurvivalRoundClearReward() const
{
	if (!SurvivalMissionDefinition)
	{
		return nullptr;
	}

	TArray<FAeyerjiSurvivalRoundDefinition> AuthoredRounds;
	BuildAuthoredSurvivalRounds(AuthoredRounds);
	if (!AuthoredRounds.IsEmpty() && CurrentSurvivalRound > 0)
	{
		const int32 PatternIndex = (CurrentSurvivalRound - 1) % AuthoredRounds.Num();
		if (AuthoredRounds.IsValidIndex(PatternIndex) && AuthoredRounds[PatternIndex].bOverrideRoundClearReward)
		{
			return &AuthoredRounds[PatternIndex].RoundClearReward;
		}
	}

	return &SurvivalMissionDefinition->DefaultRoundClearReward;
}

bool AAeyerjiLevelDirector::IsSurvivalRoundRewardEligible(const FAeyerjiSurvivalRoundRewardDefinition& Reward) const
{
	const int32 RoundNumber = FMath::Max(1, CurrentSurvivalRound);
	const int32 FirstRound = FMath::Max(1, Reward.FirstEligibleRound);
	const int32 Cadence = FMath::Max(1, Reward.RewardEveryNRounds);

	if (RoundNumber < FirstRound)
	{
		return false;
	}

	return ((RoundNumber - FirstRound) % Cadence) == 0;
}

FLootContext AAeyerjiLevelDirector::BuildSurvivalRewardLootContext(
	const FAeyerjiSurvivalRoundRewardDefinition& Reward,
	AActor* PlayerActor) const
{
	FLootContext RuntimeContext = Reward.LootContext;
	if (!RuntimeContext.PlayerActor.IsValid() && PlayerActor)
	{
		RuntimeContext.PlayerActor = PlayerActor;
	}

	RuntimeContext.EnemyLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(
		RuntimeContext.EnemyLevel > 0
			? RuntimeContext.EnemyLevel
			: GetEffectiveEnemyLevelForPlayerLevel(GetEnemyScalingPlayerLevel()));

	if (RuntimeContext.PlayerLevel <= 0)
	{
		RuntimeContext.PlayerLevel = GetCurrentPlayerLevel();
	}
	else
	{
		RuntimeContext.PlayerLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(RuntimeContext.PlayerLevel);
	}

	if (RuntimeContext.WorldTier <= 0)
	{
		RuntimeContext.WorldTier = WorldTier;
	}

	if (Reward.SourceTag.IsValid())
	{
		RuntimeContext.SourceTag = Reward.SourceTag;
	}

	if (RuntimeContext.DifficultyScale <= 0.f)
	{
		RuntimeContext.DifficultyScale = GetDifficultyScale();
	}

	return RuntimeContext;
}

FVector AAeyerjiLevelDirector::GetSurvivalRewardSpawnLocation(
	const FAeyerjiSurvivalRoundRewardDefinition& Reward,
	const APawn* PlayerPawn) const
{
	if (!PlayerPawn)
	{
		return GetActorLocation() + FVector(0.f, 0.f, Reward.SpawnHeightOffset);
	}

	const FVector Forward = PlayerPawn->GetActorForwardVector().GetSafeNormal();
	const FVector Direction = Forward.IsNearlyZero() ? FVector::ForwardVector : Forward;
	return PlayerPawn->GetActorLocation()
		+ (Direction * FMath::Max(0.f, Reward.SpawnDistanceFromPlayer))
		+ FVector(0.f, 0.f, Reward.SpawnHeightOffset);
}

void AAeyerjiLevelDirector::HandleSpawnerTrackedEnemiesRemoved(AAeyerjiSpawnerGroup* Spawner, const int32 RemovedCount)
{
	if (!HasAuthority()
		|| SpawnMode != EAeyerjiLevelSpawnMode::SurvivalRounds
		|| !bRunActive
		|| !IsValid(Spawner)
		|| RemovedCount <= 0)
	{
		return;
	}

	if (Spawner != SurvivalRoundSpawner && Spawner != BossSpawner)
	{
		return;
	}

	CurrentSurvivalRoundEnemiesKilled = FMath::Clamp(
		CurrentSurvivalRoundEnemiesKilled + RemovedCount,
		0,
		FMath::Max(CurrentSurvivalRoundEnemyTotal, CurrentSurvivalRoundEnemiesKilled + RemovedCount));

	CurrentSurvivalWaveEnemiesKilled = FMath::Clamp(
		CurrentSurvivalWaveEnemiesKilled + RemovedCount,
		0,
		FMath::Max(CurrentSurvivalWaveEnemyTotal, CurrentSurvivalWaveEnemiesKilled + RemovedCount));

	PublishCurrentSurvivalRoundProgress();
}

void AAeyerjiLevelDirector::HandleSpawnerBossDefeated(AAeyerjiSpawnerGroup* Spawner, AActor* BossEnemy)
{
	if (!HasAuthority()
		|| SpawnMode != EAeyerjiLevelSpawnMode::SurvivalRounds
		|| !bRunActive
		|| !IsValid(Spawner))
	{
		return;
	}

	if (Spawner == BossSpawner)
	{
		HandleSurvivalBossDefeated();
		return;
	}

	if (Spawner != SurvivalRoundSpawner || bSurvivalBossDefeatHandled)
	{
		return;
	}

	static_cast<void>(BossEnemy);
	bSurvivalBossDefeatHandled = true;
	WritePersistentFactsForTrigger(EAeyerjiPersistentFactWriteTrigger::BossDefeated);
	PublishSurvivalRoundState(CurrentSurvivalRoundPhase, FName(TEXT("BossDefeated")));
}

void AAeyerjiLevelDirector::HandleSpawnerWaveStarted(AAeyerjiSpawnerGroup* Spawner, const int32 WaveIndex)
{
	if (!HasAuthority()
		|| SpawnMode != EAeyerjiLevelSpawnMode::SurvivalRounds
		|| !bRunActive
		|| !IsValid(Spawner)
		|| Spawner != SurvivalRoundSpawner)
	{
		return;
	}

	CurrentSurvivalWaveIndex = WaveIndex;
	CurrentSurvivalWaveCount = Spawner->GetWaveCount();
	CurrentSurvivalWaveEnemyTotal = Spawner->GetWaveEnemyTotal(WaveIndex);
	CurrentSurvivalWaveDisplayLabel = Spawner->GetWaveDisplayLabel(WaveIndex);
	bCurrentSurvivalWaveContainsBoss = Spawner->DoesWaveContainBoss(WaveIndex);
	CurrentSurvivalWaveEnemiesKilled = 0;

	PublishSurvivalRoundState(
		CurrentSurvivalRoundPhase == EAeyerjiSurvivalRoundPhase::Inactive ? EAeyerjiSurvivalRoundPhase::Spawning : CurrentSurvivalRoundPhase,
		bCurrentSurvivalWaveContainsBoss ? FName(TEXT("BossIncoming")) : NAME_None);
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

void AAeyerjiLevelDirector::ResolveSurvivalDefenseObjective()
{
	ClearSurvivalDefenseObjective();
	bSurvivalDefenseObjectiveDestroyed = false;
	ResetSurvivalDefenseObjectiveHealthWarnings();

	if (!HasAuthority() || !SurvivalMissionDefinition)
	{
		return;
	}

	const FAeyerjiSurvivalDefenseObjectiveDefinition& ObjectiveDefinition = SurvivalMissionDefinition->DefenseObjective;
	if (!ObjectiveDefinition.bEnabled)
	{
		return;
	}

	if (ObjectiveDefinition.ObjectiveActorTag.IsNone())
	{
		UE_LOG(LogTemp, Warning, TEXT("LevelDirector %s has survival defense objective enabled, but ObjectiveActorTag is None. Set Defense Objective > Objective Actor Tag on %s to the placed tree actor tag."),
			*GetNameSafe(this),
			*GetNameSafe(SurvivalMissionDefinition));
		return;
	}

	AActor* ObjectiveActor = FindActorByTag(ObjectiveDefinition.ObjectiveActorTag);
	if (!IsValid(ObjectiveActor))
	{
		UE_LOG(LogTemp, Warning, TEXT("LevelDirector %s could not resolve survival defense objective tag %s."),
			*GetNameSafe(this),
			*ObjectiveDefinition.ObjectiveActorTag.ToString());
		return;
	}

	UAbilitySystemComponent* ObjectiveASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(ObjectiveActor, /*LookForComponent=*/true);
	if (!ObjectiveASC)
	{
		UE_LOG(LogTemp, Warning, TEXT("LevelDirector %s ignored survival defense objective %s because it has no AbilitySystemComponent."),
			*GetNameSafe(this),
			*GetNameSafe(ObjectiveActor));
		return;
	}

	SurvivalDefenseObjectiveActor = ObjectiveActor;
	CachedSurvivalDefenseObjectiveASC = ObjectiveASC;
	SurvivalDefenseObjectiveHealthChangedHandle = ObjectiveASC->GetGameplayAttributeValueChangeDelegate(UAeyerjiAttributeSet::GetHPAttribute())
		.AddUObject(this, &AAeyerjiLevelDirector::HandleSurvivalDefenseObjectiveHealthChanged);
	SurvivalDefenseObjectiveMaxHealthChangedHandle = ObjectiveASC->GetGameplayAttributeValueChangeDelegate(UAeyerjiAttributeSet::GetHPMaxAttribute())
		.AddUObject(this, &AAeyerjiLevelDirector::HandleSurvivalDefenseObjectiveHealthChanged);
	if (UAeyerjiAttributeSet* ObjectiveAttributeSet = const_cast<UAeyerjiAttributeSet*>(ObjectiveASC->GetSet<UAeyerjiAttributeSet>()))
	{
		CachedSurvivalDefenseObjectiveAttributeSet = ObjectiveAttributeSet;
		ObjectiveAttributeSet->OnDamageTaken.RemoveDynamic(this, &AAeyerjiLevelDirector::HandleSurvivalDefenseObjectiveDamageTaken);
		ObjectiveAttributeSet->OnDamageTaken.AddDynamic(this, &AAeyerjiLevelDirector::HandleSurvivalDefenseObjectiveDamageTaken);
	}

	if (AAeyerjiSurvivalDefenseObjectiveActor* NativeObjective = Cast<AAeyerjiSurvivalDefenseObjectiveActor>(ObjectiveActor))
	{
		NativeObjective->OnObjectiveOutOfHealth.RemoveDynamic(this, &AAeyerjiLevelDirector::HandleSurvivalDefenseObjectiveOutOfHealth);
		NativeObjective->OnObjectiveOutOfHealth.AddDynamic(this, &AAeyerjiLevelDirector::HandleSurvivalDefenseObjectiveOutOfHealth);
		NativeObjective->ResetSurvivalUpgrades(); // ensure clean per-run
		bSurvivalDefenseObjectiveDestroyed = NativeObjective->IsObjectiveDestroyed();
	}
	else
	{
		bSurvivalDefenseObjectiveDestroyed = !IsSurvivalDefenseObjectiveAlive();
	}
}

void AAeyerjiLevelDirector::ClearSurvivalDefenseObjective()
{
	if (UAbilitySystemComponent* ObjectiveASC = CachedSurvivalDefenseObjectiveASC.Get())
	{
		if (SurvivalDefenseObjectiveHealthChangedHandle.IsValid())
		{
			ObjectiveASC->GetGameplayAttributeValueChangeDelegate(UAeyerjiAttributeSet::GetHPAttribute()).Remove(SurvivalDefenseObjectiveHealthChangedHandle);
		}
		if (SurvivalDefenseObjectiveMaxHealthChangedHandle.IsValid())
		{
			ObjectiveASC->GetGameplayAttributeValueChangeDelegate(UAeyerjiAttributeSet::GetHPMaxAttribute()).Remove(SurvivalDefenseObjectiveMaxHealthChangedHandle);
		}
	}

	if (AAeyerjiSurvivalDefenseObjectiveActor* NativeObjective = Cast<AAeyerjiSurvivalDefenseObjectiveActor>(SurvivalDefenseObjectiveActor.Get()))
	{
		NativeObjective->OnObjectiveOutOfHealth.RemoveDynamic(this, &AAeyerjiLevelDirector::HandleSurvivalDefenseObjectiveOutOfHealth);
		NativeObjective->ResetSurvivalUpgrades();
	}

	if (UAeyerjiAttributeSet* ObjectiveAttributeSet = CachedSurvivalDefenseObjectiveAttributeSet.Get())
	{
		ObjectiveAttributeSet->OnDamageTaken.RemoveDynamic(this, &AAeyerjiLevelDirector::HandleSurvivalDefenseObjectiveDamageTaken);
	}

	SurvivalDefenseObjectiveHealthChangedHandle.Reset();
	SurvivalDefenseObjectiveMaxHealthChangedHandle.Reset();
	CachedSurvivalDefenseObjectiveAttributeSet.Reset();
	CachedSurvivalDefenseObjectiveASC.Reset();
	SurvivalDefenseObjectiveActor = nullptr;
	bSurvivalDefenseObjectiveDestroyed = false;
	ResetSurvivalDefenseObjectiveHealthWarnings();

	if (IsValid(SurvivalRoundSpawner))
	{
		SurvivalRoundSpawner->ClearDefenseObjectiveTarget();
	}
	if (IsValid(BossSpawner))
	{
		BossSpawner->ClearDefenseObjectiveTarget();
	}
}

void AAeyerjiLevelDirector::ApplySurvivalDefenseObjectiveToSpawner(AAeyerjiSpawnerGroup* Spawner, const bool bBossRound) const
{
	if (!HasAuthority() || !IsValid(Spawner))
	{
		return;
	}

	if (!IsSurvivalDefenseObjectiveEnabled()
		|| !IsSurvivalDefenseObjectiveAlive()
		|| (bBossRound && !SurvivalMissionDefinition->DefenseObjective.TargetingSettings.bApplyToBossRounds))
	{
		Spawner->ClearDefenseObjectiveTarget();
		return;
	}

	Spawner->ConfigureDefenseObjectiveTarget(
		SurvivalDefenseObjectiveActor.Get(),
		SurvivalMissionDefinition->DefenseObjective.TargetingSettings);
}

float AAeyerjiLevelDirector::GetSurvivalDefenseObjectiveHealth() const
{
	const UAbilitySystemComponent* ObjectiveASC = CachedSurvivalDefenseObjectiveASC.Get();
	return ObjectiveASC ? ObjectiveASC->GetNumericAttribute(UAeyerjiAttributeSet::GetHPAttribute()) : 0.f;
}

float AAeyerjiLevelDirector::GetSurvivalDefenseObjectiveMaxHealth() const
{
	const UAbilitySystemComponent* ObjectiveASC = CachedSurvivalDefenseObjectiveASC.Get();
	return ObjectiveASC ? ObjectiveASC->GetNumericAttribute(UAeyerjiAttributeSet::GetHPMaxAttribute()) : 0.f;
}

bool AAeyerjiLevelDirector::IsSurvivalDefenseObjectiveAlive() const
{
	if (!IsValid(SurvivalDefenseObjectiveActor.Get()) || bSurvivalDefenseObjectiveDestroyed)
	{
		return false;
	}

	if (SurvivalDefenseObjectiveActor.Get()->Tags.Contains(AeyerjiTags::State_Dead.GetTag().GetTagName()))
	{
		return false;
	}

	if (const UAbilitySystemComponent* ObjectiveASC = CachedSurvivalDefenseObjectiveASC.Get())
	{
		if (ObjectiveASC->HasMatchingGameplayTag(AeyerjiTags::State_Dead))
		{
			return false;
		}

		return ObjectiveASC->GetNumericAttribute(UAeyerjiAttributeSet::GetHPAttribute()) > 0.f;
	}

	return true;
}

void AAeyerjiLevelDirector::ResetSurvivalDefenseObjectiveHealthWarnings()
{
	FiredSurvivalDefenseObjectiveHealthWarningIndices.Reset();
}

FName AAeyerjiLevelDirector::ResolveSurvivalDefenseObjectiveHealthWarningMessageKey(const int32 ThresholdIndex, const float Threshold01) const
{
	if (SurvivalMissionDefinition && SurvivalMissionDefinition->DefenseObjective.HealthWarningMessageKeys.IsValidIndex(ThresholdIndex))
	{
		return SurvivalMissionDefinition->DefenseObjective.HealthWarningMessageKeys[ThresholdIndex];
	}

	const int32 ThresholdPercent = FMath::Clamp(FMath::RoundToInt(Threshold01 * 100.f), 0, 100);
	return FName(*FString::Printf(TEXT("DefenseObjectiveHealth%d"), ThresholdPercent));
}

FName AAeyerjiLevelDirector::ConsumeSurvivalDefenseObjectiveHealthWarningMessage(const FOnAttributeChangeData& Data)
{
	if (!SurvivalMissionDefinition
		|| !SurvivalMissionDefinition->DefenseObjective.bEnableHealthWarningMessages
		|| Data.Attribute != UAeyerjiAttributeSet::GetHPAttribute()
		|| Data.NewValue >= Data.OldValue)
	{
		return NAME_None;
	}

	const float MaxHealth = GetSurvivalDefenseObjectiveMaxHealth();
	if (MaxHealth <= UE_SMALL_NUMBER)
	{
		return NAME_None;
	}

	const TArray<float>& Thresholds = SurvivalMissionDefinition->DefenseObjective.HealthWarningThresholds;
	if (Thresholds.IsEmpty())
	{
		return NAME_None;
	}

	const float Progress01 = FMath::Clamp(GetSurvivalDefenseObjectiveHealth() / MaxHealth, 0.f, 1.f);
	TArray<int32> CrossedThresholdIndices;
	int32 MessageThresholdIndex = INDEX_NONE;
	float MessageThreshold = 0.f;

	for (int32 ThresholdIndex = 0; ThresholdIndex < Thresholds.Num(); ++ThresholdIndex)
	{
		const float Threshold = FMath::Clamp(Thresholds[ThresholdIndex], 0.f, 1.f);
		if (Threshold <= 0.f || FiredSurvivalDefenseObjectiveHealthWarningIndices.Contains(ThresholdIndex))
		{
			continue;
		}

		if (Progress01 <= Threshold)
		{
			CrossedThresholdIndices.Add(ThresholdIndex);
			MessageThresholdIndex = ThresholdIndex;
			MessageThreshold = Threshold;
		}
	}

	if (MessageThresholdIndex == INDEX_NONE)
	{
		return NAME_None;
	}

	for (const int32 ThresholdIndex : CrossedThresholdIndices)
	{
		FiredSurvivalDefenseObjectiveHealthWarningIndices.Add(ThresholdIndex);
	}

	return ResolveSurvivalDefenseObjectiveHealthWarningMessageKey(MessageThresholdIndex, MessageThreshold);
}

void AAeyerjiLevelDirector::HandleSurvivalDefenseObjectiveOutOfHealth(AActor* ObjectiveActor, AActor* InstigatorActor, const float DamageTaken)
{
	static_cast<void>(InstigatorActor);
	static_cast<void>(DamageTaken);

	if (!HasAuthority() || ObjectiveActor != SurvivalDefenseObjectiveActor.Get() || bSurvivalDefenseObjectiveDestroyed)
	{
		return;
	}

	bSurvivalDefenseObjectiveDestroyed = true;
	if (IsValid(SurvivalRoundSpawner))
	{
		SurvivalRoundSpawner->ClearDefenseObjectiveTarget();
	}
	if (IsValid(BossSpawner))
	{
		BossSpawner->ClearDefenseObjectiveTarget();
	}

	const FName MessageKey = SurvivalMissionDefinition
		? SurvivalMissionDefinition->DefenseObjective.ObjectiveDestroyedMessageKey
		: FName(TEXT("DefenseObjectiveDestroyed"));
	PublishSurvivalRoundState(CurrentSurvivalRoundPhase, MessageKey);

	if (SurvivalMissionDefinition && SurvivalMissionDefinition->DefenseObjective.bFailRunWhenDestroyed)
	{
		if (AAeyerjiGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AAeyerjiGameState>() : nullptr)
		{
			GameState->Server_FailRunDefenseObjectiveDestroyed();
		}
	}
}

void AAeyerjiLevelDirector::HandleSurvivalDefenseObjectiveHealthChanged(const FOnAttributeChangeData& Data)
{
	static_cast<void>(Data);

	if (!HasAuthority())
	{
		return;
	}

	if (!bSurvivalDefenseObjectiveDestroyed
		&& IsValid(SurvivalDefenseObjectiveActor.Get())
		&& GetSurvivalDefenseObjectiveHealth() <= 0.f)
	{
		HandleSurvivalDefenseObjectiveOutOfHealth(SurvivalDefenseObjectiveActor.Get(), nullptr, 0.f);
		return;
	}

	const FName WarningMessageKey = ConsumeSurvivalDefenseObjectiveHealthWarningMessage(Data);
	if (!WarningMessageKey.IsNone())
	{
		PublishSurvivalRoundState(CurrentSurvivalRoundPhase, WarningMessageKey);
		return;
	}

	PublishCurrentSurvivalRoundProgress();
}

void AAeyerjiLevelDirector::StartSurvivalRound(const int32 RoundNumber)
{
	if (!HasAuthority() || !bRunActive || !SurvivalMissionDefinition)
	{
		return;
	}

	TArray<FAeyerjiSurvivalRoundDefinition> AuthoredRounds;
	BuildAuthoredSurvivalRounds(AuthoredRounds);
	if (AuthoredRounds.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("LevelDirector %s cannot start survival round: mission %s has no imported or fallback survival rounds."),
			*GetNameSafe(this),
			*GetNameSafe(SurvivalMissionDefinition.Get()));
		return;
	}

	CurrentSurvivalRound = FMath::Max(1, RoundNumber);
	if (CurrentSurvivalRound <= 1)
	{
		LastSurvivalRoundClearRewardSpawnedRound = INDEX_NONE;
	}

	CurrentSurvivalCycle = GetSurvivalCycleForRound(CurrentSurvivalRound);
	const int32 RoundStep = FMath::Max(0, CurrentSurvivalRound - 1);
	SurvivalEnemyLevelBonus =
		(CurrentSurvivalCycle * FMath::Max(0, SurvivalMissionDefinition->EnemyLevelBonusPerCycle))
		+ (RoundStep * FMath::Max(0, SurvivalMissionDefinition->EnemyLevelBonusPerRound));
	SurvivalEnemyHealthMultiplier = FMath::Pow(FMath::Max(0.f, SurvivalMissionDefinition->EnemyHealthMultiplierPerRound), RoundStep);
	SurvivalEnemyDamageMultiplier = FMath::Pow(FMath::Max(0.f, SurvivalMissionDefinition->EnemyDamageMultiplierPerRound), RoundStep);
	CurrentSurvivalRoundEnemyTotal = 0;
	CurrentSurvivalRoundEnemiesKilled = 0;
	CurrentSurvivalWaveIndex = INDEX_NONE;
	CurrentSurvivalWaveCount = 0;
	CurrentSurvivalWaveEnemyTotal = 0;
	CurrentSurvivalWaveDisplayLabel = FText::GetEmpty();
	bCurrentSurvivalWaveContainsBoss = false;
	CurrentSurvivalWaveEnemiesKilled = 0;
	CurrentSurvivalRoundPhase = EAeyerjiSurvivalRoundPhase::Inactive;
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

	const int32 PatternIndex = (CurrentSurvivalRound - 1) % AuthoredRounds.Num();
	const FAeyerjiSurvivalRoundDefinition& RoundDefinition = AuthoredRounds[PatternIndex];
	TArray<FWaveDefinition> RuntimeWaves;
	if (!BuildRuntimeSurvivalWaves(RoundDefinition, AuthoredRounds, PatternIndex, CurrentSurvivalCycle, RuntimeWaves, CurrentSurvivalRoundEnemyTotal))
	{
		UE_LOG(LogTemp, Warning, TEXT("LevelDirector %s cannot start survival round %d: no valid runtime waves."),
			*GetNameSafe(this),
			CurrentSurvivalRound);
		PublishSurvivalRoundState(EAeyerjiSurvivalRoundPhase::Inactive, NAME_None);
		return;
	}

	CurrentSurvivalWaveIndex = 0;
	CurrentSurvivalWaveCount = RuntimeWaves.Num();
	CurrentSurvivalWaveEnemyTotal = RuntimeWaves.IsValidIndex(0) ? CountRuntimeWaveEnemies(RuntimeWaves[0]) : 0;
	CurrentSurvivalWaveDisplayLabel = RuntimeWaves.IsValidIndex(0) ? RuntimeWaves[0].WaveLabel : FText::GetEmpty();
	bCurrentSurvivalWaveContainsBoss = RuntimeWaves.IsValidIndex(0)
		&& RuntimeWaves[0].EnemySets.ContainsByPredicate([](const FEnemySet& EnemySet)
		{
			return EnemySet.bIsBoss;
		});
	CurrentSurvivalWaveEnemiesKilled = 0;

	APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0);
	AController* PlayerController = PlayerPawn ? PlayerPawn->GetController() : nullptr;
	const FName StartMessageKey = (CurrentSurvivalCycle > 0 && PatternIndex == 0)
		? FName(TEXT("CycleStart"))
		: RoundDefinition.RoundStartMessageKey;
	PublishSurvivalRoundState(EAeyerjiSurvivalRoundPhase::Preparing, StartMessageKey);
	SurvivalRoundSpawner->LevelDirector = this;
	SurvivalRoundSpawner->AggroSettings.bReissueAggroWhileActive = SurvivalMissionDefinition->bReissueAggroWhileActive;
	SurvivalRoundSpawner->AggroSettings.ReissueAggroIntervalSeconds = FMath::Max(0.1f, SurvivalMissionDefinition->ReissueAggroIntervalSeconds);
	ApplySurvivalDefenseObjectiveToSpawner(SurvivalRoundSpawner, /*bBossRound=*/false);
	SurvivalRoundSpawner->ActivateEncounterWithRuntimeWaves(RuntimeWaves, PlayerPawn, PlayerController);
	PublishSurvivalRoundState(EAeyerjiSurvivalRoundPhase::Spawning, NAME_None);
}

void AAeyerjiLevelDirector::BuildAuthoredSurvivalRounds(TArray<FAeyerjiSurvivalRoundDefinition>& OutRounds) const
{
	if (!bAuthoredSurvivalRoundsCacheValid)
	{
		RebuildAuthoredSurvivalRoundsCache();
	}

	OutRounds = CachedAuthoredSurvivalRounds;
}

void AAeyerjiLevelDirector::RebuildAuthoredSurvivalRoundsCache() const
{
	CachedAuthoredSurvivalRounds.Reset();
	if (!SurvivalMissionDefinition)
	{
		bAuthoredSurvivalRoundsCacheValid = true;
		return;
	}

	const UDataTable* RoundTable = SurvivalMissionDefinition->RoundTable;
	if (SurvivalMissionDefinition->bPreferRoundTable && RoundTable)
	{
		if (RoundTable->GetRowStruct() != FAeyerjiSurvivalRoundTableRow::StaticStruct())
		{
			UE_LOG(LogTemp, Warning, TEXT("LevelDirector %s ignored survival RoundTable %s because it uses row struct %s instead of FAeyerjiSurvivalRoundTableRow."),
				*GetNameSafe(this),
				*GetNameSafe(RoundTable),
				*GetNameSafe(RoundTable->GetRowStruct()));
		}
		else
		{
			struct FImportedSurvivalRoundRow
			{
				FName RowName = NAME_None;
				const FAeyerjiSurvivalRoundTableRow* Row = nullptr;
			};

			TArray<FImportedSurvivalRoundRow> ImportedRows;
			ImportedRows.Reserve(RoundTable->GetRowMap().Num());
			for (const TPair<FName, uint8*>& Pair : RoundTable->GetRowMap())
			{
				if (const FAeyerjiSurvivalRoundTableRow* Row = reinterpret_cast<const FAeyerjiSurvivalRoundTableRow*>(Pair.Value))
				{
					ImportedRows.Add({ Pair.Key, Row });
				}
			}

			ImportedRows.Sort([](const FImportedSurvivalRoundRow& A, const FImportedSurvivalRoundRow& B)
			{
				if (A.Row->RoundNumber != B.Row->RoundNumber)
				{
					return A.Row->RoundNumber < B.Row->RoundNumber;
				}

				if (A.Row->WaveNumber != B.Row->WaveNumber)
				{
					return A.Row->WaveNumber < B.Row->WaveNumber;
				}

				return A.RowName.LexicalLess(B.RowName);
			});

			for (const FImportedSurvivalRoundRow& ImportedRow : ImportedRows)
			{
				const FAeyerjiSurvivalRoundTableRow& Row = *ImportedRow.Row;
				if (Row.RoundNumber <= 0 || Row.WaveNumber <= 0 || Row.Count <= 0 || Row.EnemyClass.IsNull())
				{
					continue;
				}

				const int32 RoundIndex = Row.RoundNumber - 1;
				const int32 WaveIndex = Row.WaveNumber - 1;
				if (!CachedAuthoredSurvivalRounds.IsValidIndex(RoundIndex))
				{
					CachedAuthoredSurvivalRounds.SetNum(RoundIndex + 1);
				}

				FAeyerjiSurvivalRoundDefinition& RoundDefinition = CachedAuthoredSurvivalRounds[RoundIndex];
				if (RoundDefinition.DisplayLabel.IsEmpty())
				{
					RoundDefinition.DisplayLabel = Row.RoundDisplayLabel;
				}
				RoundDefinition.RoundType = Row.RoundType;
				RoundDefinition.EnemyCountMultiplier = FMath::Max(0.f, Row.EnemyCountMultiplier);
				RoundDefinition.RoundStartMessageKey = Row.RoundStartMessageKey.IsNone() ? FName(TEXT("RoundStart")) : Row.RoundStartMessageKey;
				RoundDefinition.RoundClearMessageKey = Row.RoundClearMessageKey.IsNone() ? FName(TEXT("RoundClear")) : Row.RoundClearMessageKey;
				RoundDefinition.BossIncomingMessageKey = Row.BossIncomingMessageKey.IsNone() ? FName(TEXT("BossIncoming")) : Row.BossIncomingMessageKey;
				if (Row.bOverrideRoundClearReward || RoundDefinition.Waves.IsEmpty())
				{
					RoundDefinition.bOverrideRoundClearReward = Row.bOverrideRoundClearReward;
					RoundDefinition.RoundClearReward = Row.RoundClearReward;
				}

				if (!RoundDefinition.Waves.IsValidIndex(WaveIndex))
				{
					RoundDefinition.Waves.SetNum(WaveIndex + 1);
				}

				FWaveDefData& WaveDefinition = RoundDefinition.Waves[WaveIndex];
				if (WaveDefinition.WaveLabel.IsEmpty())
				{
					WaveDefinition.WaveLabel = Row.WaveLabel;
				}
				WaveDefinition.PostSpawnDelay = FMath::Max(0.f, Row.PostSpawnDelay);

				FEnemySetDef& EnemySet = WaveDefinition.EnemySets.AddDefaulted_GetRef();
				EnemySet.EnemyClass = Row.EnemyClass;
				EnemySet.Count = FMath::Max(0, Row.Count);
				EnemySet.SpawnInterval = FMath::Max(0.f, Row.SpawnInterval);
				EnemySet.bIsElite = Row.bIsElite || Row.bIsMiniBoss || Row.bIsBoss;
				EnemySet.bIsMiniBoss = Row.bIsMiniBoss;
				EnemySet.bIsBoss = Row.bIsBoss;
			}

			CachedAuthoredSurvivalRounds.RemoveAll([](const FAeyerjiSurvivalRoundDefinition& RoundDefinition)
			{
				return RoundDefinition.Waves.IsEmpty();
			});

			if (!CachedAuthoredSurvivalRounds.IsEmpty())
			{
				bAuthoredSurvivalRoundsCacheValid = true;
				return;
			}

			UE_LOG(LogTemp, Warning, TEXT("LevelDirector %s found no valid survival rows in RoundTable %s; falling back to BaseRounds."),
				*GetNameSafe(this),
				*GetNameSafe(RoundTable));
		}
	}

	CachedAuthoredSurvivalRounds = SurvivalMissionDefinition->BaseRounds;
	bAuthoredSurvivalRoundsCacheValid = true;
}

void AAeyerjiLevelDirector::InvalidateSurvivalRuntimeCaches()
{
	if (SurvivalPreloadHandle.IsValid())
	{
		SurvivalPreloadHandle->CancelHandle();
		SurvivalPreloadHandle.Reset();
	}

	PreloadedSurvivalEnemyClasses.Reset();
	CachedAuthoredSurvivalRounds.Reset();
	bAuthoredSurvivalRoundsCacheValid = false;
	bSurvivalAssetsReady = false;
	bSurvivalAssetPreloadInProgress = false;
}

void AAeyerjiLevelDirector::BeginSurvivalAssetPreload()
{
	if (!HasAuthority() || !SurvivalMissionDefinition || bSurvivalAssetsReady || bSurvivalAssetPreloadInProgress)
	{
		return;
	}

	TArray<FAeyerjiSurvivalRoundDefinition> AuthoredRounds;
	BuildAuthoredSurvivalRounds(AuthoredRounds);

	TSet<FSoftObjectPath> UniqueClassPaths;
	for (const FAeyerjiSurvivalRoundDefinition& RoundDefinition : AuthoredRounds)
	{
		for (const FWaveDefData& WaveData : RoundDefinition.Waves)
		{
			for (const FEnemySetDef& SetData : WaveData.EnemySets)
			{
				const FSoftObjectPath ClassPath = SetData.EnemyClass.ToSoftObjectPath();
				if (ClassPath.IsValid())
				{
					UniqueClassPaths.Add(ClassPath);
				}
			}
		}
	}

	TArray<FSoftObjectPath> ClassPaths;
	ClassPaths.Reserve(UniqueClassPaths.Num());
	for (const FSoftObjectPath& ClassPath : UniqueClassPaths)
	{
		ClassPaths.Add(ClassPath);
	}

	if (ClassPaths.IsEmpty())
	{
		bSurvivalAssetsReady = true;
		return;
	}

	bSurvivalAssetPreloadInProgress = true;
	SurvivalPreloadHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		ClassPaths,
		FStreamableDelegate::CreateUObject(this, &AAeyerjiLevelDirector::HandleSurvivalAssetPreloadComplete),
		FStreamableManager::AsyncLoadHighPriority);
}

void AAeyerjiLevelDirector::HandleSurvivalAssetPreloadComplete()
{
	PreloadedSurvivalEnemyClasses.Reset();

	TArray<FAeyerjiSurvivalRoundDefinition> AuthoredRounds;
	BuildAuthoredSurvivalRounds(AuthoredRounds);
	for (const FAeyerjiSurvivalRoundDefinition& RoundDefinition : AuthoredRounds)
	{
		for (const FWaveDefData& WaveData : RoundDefinition.Waves)
		{
			for (const FEnemySetDef& SetData : WaveData.EnemySets)
			{
				if (UClass* LoadedClass = SetData.EnemyClass.Get())
				{
					PreloadedSurvivalEnemyClasses.AddUnique(LoadedClass);
				}
			}
		}
	}

	bSurvivalAssetPreloadInProgress = false;
	bSurvivalAssetsReady = true;
	UE_LOG(LogTemp, Display, TEXT("LevelDirector %s preloaded %d survival enemy classes."),
		*GetNameSafe(this),
		PreloadedSurvivalEnemyClasses.Num());

	if (HasAuthority() && bRunActive && SpawnMode == EAeyerjiLevelSpawnMode::SurvivalRounds && CurrentSurvivalRound <= 0)
	{
		StartSurvivalRound(1);
	}
}

bool AAeyerjiLevelDirector::AreSurvivalAssetsReady() const
{
	return bSurvivalAssetsReady || !SurvivalMissionDefinition;
}

void AAeyerjiLevelDirector::StartNextSurvivalRound()
{
	if (!HasAuthority() || !bRunActive)
	{
		return;
	}

	ClearSurvivalUpgradeOfferState();
	StartSurvivalRound(FMath::Max(1, CurrentSurvivalRound + 1));
}

void AAeyerjiLevelDirector::ScheduleNextSurvivalRound()
{
	if (!HasAuthority() || !bRunActive)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		const float Delay = SurvivalMissionDefinition ? FMath::Max(0.f, SurvivalMissionDefinition->InterRoundDelaySeconds) : 0.f;
		World->GetTimerManager().ClearTimer(SurvivalRoundDelayHandle);
		World->GetTimerManager().SetTimer(SurvivalRoundDelayHandle, this, &AAeyerjiLevelDirector::StartNextSurvivalRound, Delay, false);
	}
}

void AAeyerjiLevelDirector::BeginSurvivalRoundUpgradeOfferOrScheduleNextRound()
{
	if (!HasAuthority() || !bRunActive || !SurvivalMissionDefinition)
	{
		ScheduleNextSurvivalRound();
		return;
	}

	if (!SurvivalMissionDefinition->bEnableRoundUpgradeChoices)
	{
		ScheduleNextSurvivalRound();
		return;
	}

	StartSurvivalUpgradeOffer();
}

void AAeyerjiLevelDirector::StartSurvivalUpgradeOffer()
{
	if (!HasAuthority() || !SurvivalMissionDefinition)
	{
		ScheduleNextSurvivalRound();
		return;
	}

	TArray<FAeyerjiSurvivalUpgradeOption> OfferOptions;
	if (!BuildSurvivalUpgradeOfferOptions(OfferOptions))
	{
		UE_LOG(LogAeyerji, Warning, TEXT("StartSurvivalUpgradeOffer: failed to build options, skipping offer (Round=%d)"), CurrentSurvivalRound);
		ScheduleNextSurvivalRound();
		return;
	}

	TArray<AAeyerjiPlayerState*> EligiblePlayers;
	CollectActiveSurvivalUpgradePlayers(EligiblePlayers);
	if (EligiblePlayers.IsEmpty())
	{
		UE_LOG(LogAeyerji, Warning, TEXT("StartSurvivalUpgradeOffer: no eligible players, skipping offer (Round=%d)"), CurrentSurvivalRound);
		ScheduleNextSurvivalRound();
		return;
	}

	ClearSurvivalUpgradeOfferState();

	SurvivalUpgradeEligiblePlayers.Reset();
	for (AAeyerjiPlayerState* PlayerState : EligiblePlayers)
	{
		SurvivalUpgradeEligiblePlayers.Add(PlayerState);
	}

	const float TimeoutSeconds = FMath::Max(0.f, SurvivalMissionDefinition->UpgradeChoiceTimeoutSeconds);
	ActiveSurvivalUpgradeOffer = FAeyerjiSurvivalUpgradeOfferState();
	ActiveSurvivalUpgradeOffer.bActive = true;
	ActiveSurvivalUpgradeOffer.RoundNumber = CurrentSurvivalRound;
	ActiveSurvivalUpgradeOffer.Revision = ++SurvivalUpgradeOfferRevision;
	ActiveSurvivalUpgradeOffer.Options = OfferOptions;
	ActiveSurvivalUpgradeOffer.TimeoutSeconds = TimeoutSeconds;
	ActiveSurvivalUpgradeOffer.RequiredSelectionCount = EligiblePlayers.Num();
	ActiveSurvivalUpgradeOffer.SelectedCount = 0;
	ActiveSurvivalUpgradeOffer.OfferEndServerTimeSeconds = GetWorld()
		? GetWorld()->GetTimeSeconds() + TimeoutSeconds
		: TimeoutSeconds;

	if (AAeyerjiGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AAeyerjiGameState>() : nullptr)
	{
		GameState->SetSurvivalUpgradeOfferStateFromServer(ActiveSurvivalUpgradeOffer);
	}

	UE_LOG(LogAeyerji, Display, TEXT("Survival upgrade offer started: Round=%d Options=%d Players=%d Timeout=%.1fs"),
		CurrentSurvivalRound, OfferOptions.Num(), EligiblePlayers.Num(), TimeoutSeconds);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SurvivalUpgradeOfferTimeoutHandle);
		if (TimeoutSeconds > 0.f)
		{
			World->GetTimerManager().SetTimer(SurvivalUpgradeOfferTimeoutHandle, this, &AAeyerjiLevelDirector::HandleSurvivalUpgradeOfferTimeout, TimeoutSeconds, false);
		}
		else
		{
			FinishSurvivalUpgradeOffer(/*bApplyMissingSelections=*/true);
		}
	}
}

void AAeyerjiLevelDirector::FinishSurvivalUpgradeOffer(const bool bApplyMissingSelections)
{
	if (!HasAuthority())
	{
		return;
	}

	int32 AutoAppliedCount = 0;
	if (bApplyMissingSelections && ActiveSurvivalUpgradeOffer.bActive && ActiveSurvivalUpgradeOffer.Options.Num() > 0)
	{
		const FAeyerjiSurvivalUpgradeOption& DefaultOption = ActiveSurvivalUpgradeOffer.Options[0];
		for (const TWeakObjectPtr<AAeyerjiPlayerState>& WeakPlayerState : SurvivalUpgradeEligiblePlayers)
		{
			AAeyerjiPlayerState* PlayerState = WeakPlayerState.Get();
			if (!PlayerState || HasPlayerSelectedCurrentSurvivalUpgrade(PlayerState))
			{
				continue;
			}

			SurvivalUpgradeSelectedPlayers.Add(PlayerState);
			ApplySurvivalUpgradeOptionToPlayer(PlayerState, DefaultOption);
			++AutoAppliedCount;
		}
	}

	if (AutoAppliedCount > 0)
	{
		UE_LOG(LogAeyerji, Display, TEXT("Survival upgrade offer timeout: auto-applied default to %d player(s) (Round=%d)"),
			AutoAppliedCount, ActiveSurvivalUpgradeOffer.RoundNumber);
	}

	ClearSurvivalUpgradeOfferState();
	ScheduleNextSurvivalRound();
}

void AAeyerjiLevelDirector::HandleSurvivalUpgradeOfferTimeout()
{
	UE_LOG(LogAeyerji, Display, TEXT("Survival upgrade offer timeout reached (Round=%d)"), CurrentSurvivalRound);
	FinishSurvivalUpgradeOffer(/*bApplyMissingSelections=*/true);
}

bool AAeyerjiLevelDirector::BuildSurvivalUpgradeOfferOptions(TArray<FAeyerjiSurvivalUpgradeOption>& OutOptions) const
{
	OutOptions.Reset();
	if (!SurvivalMissionDefinition)
	{
		return false;
	}

	TArray<FAeyerjiSurvivalUpgradeOption> Candidates;
	for (const FAeyerjiSurvivalUpgradeOption& Option : SurvivalMissionDefinition->RoundUpgradeOptions)
	{
		if (!Option.OptionId.IsNone() && Option.Weight > 0.f)
		{
			Candidates.Add(Option);
		}
	}

	if (Candidates.IsEmpty())
	{
		return false;
	}

	const int32 TargetCount = FMath::Clamp(SurvivalMissionDefinition->UpgradeChoicesPerOffer, 1, Candidates.Num());
	while (OutOptions.Num() < TargetCount && !Candidates.IsEmpty())
	{
		float TotalWeight = 0.f;
		for (const FAeyerjiSurvivalUpgradeOption& Candidate : Candidates)
		{
			TotalWeight += FMath::Max(0.f, Candidate.Weight);
		}

		if (TotalWeight <= UE_SMALL_NUMBER)
		{
			break;
		}

		float Roll = FMath::FRandRange(0.f, TotalWeight);
		int32 PickedIndex = 0;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			Roll -= FMath::Max(0.f, Candidates[Index].Weight);
			if (Roll <= 0.f)
			{
				PickedIndex = Index;
				break;
			}
		}

		OutOptions.Add(Candidates[PickedIndex]);
		Candidates.RemoveAt(PickedIndex);
	}

	return !OutOptions.IsEmpty();
}

void AAeyerjiLevelDirector::CollectActiveSurvivalUpgradePlayers(TArray<AAeyerjiPlayerState*>& OutPlayers) const
{
	OutPlayers.Reset();
	UWorld* World = GetWorld();
	AGameStateBase* GameState = World ? World->GetGameState() : nullptr;
	if (!GameState)
	{
		return;
	}

	for (APlayerState* PlayerState : GameState->PlayerArray)
	{
		AAeyerjiPlayerState* AeyerjiPS = Cast<AAeyerjiPlayerState>(PlayerState);
		if (AeyerjiPS && AeyerjiPS->GetPawn())
		{
			OutPlayers.Add(AeyerjiPS);
		}
	}
}

bool AAeyerjiLevelDirector::IsPlayerEligibleForCurrentSurvivalUpgrade(AAeyerjiPlayerState* PlayerState) const
{
	if (!PlayerState)
	{
		return false;
	}

	for (const TWeakObjectPtr<AAeyerjiPlayerState>& WeakPlayerState : SurvivalUpgradeEligiblePlayers)
	{
		if (WeakPlayerState.Get() == PlayerState)
		{
			return true;
		}
	}

	return false;
}

bool AAeyerjiLevelDirector::HasPlayerSelectedCurrentSurvivalUpgrade(AAeyerjiPlayerState* PlayerState) const
{
	if (!PlayerState)
	{
		return false;
	}

	for (const TWeakObjectPtr<AAeyerjiPlayerState>& WeakPlayerState : SurvivalUpgradeSelectedPlayers)
	{
		if (WeakPlayerState.Get() == PlayerState)
		{
			return true;
		}
	}

	return false;
}

const FAeyerjiSurvivalUpgradeOption* AAeyerjiLevelDirector::FindCurrentSurvivalUpgradeOption(const FName OptionId) const
{
	return ActiveSurvivalUpgradeOffer.Options.FindByPredicate([OptionId](const FAeyerjiSurvivalUpgradeOption& Option)
	{
		return Option.OptionId == OptionId;
	});
}

bool AAeyerjiLevelDirector::SubmitSurvivalUpgradeChoice(AAeyerjiPlayerState* PlayerState, const FName OptionId, const int32 OfferRevision)
{
	if (!HasAuthority())
	{
		UE_LOG(LogAeyerji, Warning, TEXT("SubmitSurvivalUpgradeChoice called on non-authority"));
		return false;
	}

	if (!ActiveSurvivalUpgradeOffer.bActive)
	{
		UE_LOG(LogAeyerji, Warning, TEXT("SubmitSurvivalUpgradeChoice rejected: no active offer (Player=%s Option=%s Rev=%d)"),
			*GetNameSafe(PlayerState), *OptionId.ToString(), OfferRevision);
		return false;
	}

	if (ActiveSurvivalUpgradeOffer.Revision != OfferRevision)
	{
		UE_LOG(LogAeyerji, Warning, TEXT("SubmitSurvivalUpgradeChoice rejected: revision mismatch (Player=%s Option=%s ExpectedRev=%d Got=%d)"),
			*GetNameSafe(PlayerState), *OptionId.ToString(), ActiveSurvivalUpgradeOffer.Revision, OfferRevision);
		return false;
	}

	if (!IsPlayerEligibleForCurrentSurvivalUpgrade(PlayerState))
	{
		UE_LOG(LogAeyerji, Warning, TEXT("SubmitSurvivalUpgradeChoice rejected: not eligible (Player=%s Option=%s)"),
			*GetNameSafe(PlayerState), *OptionId.ToString());
		return false;
	}

	if (HasPlayerSelectedCurrentSurvivalUpgrade(PlayerState))
	{
		UE_LOG(LogAeyerji, Warning, TEXT("SubmitSurvivalUpgradeChoice rejected: already selected (Player=%s Option=%s)"),
			*GetNameSafe(PlayerState), *OptionId.ToString());
		return false;
	}

	const FAeyerjiSurvivalUpgradeOption* Option = FindCurrentSurvivalUpgradeOption(OptionId);
	if (!Option)
	{
		UE_LOG(LogAeyerji, Warning, TEXT("SubmitSurvivalUpgradeChoice rejected: unknown option id (Player=%s Option=%s)"),
			*GetNameSafe(PlayerState), *OptionId.ToString());
		return false;
	}

	SurvivalUpgradeSelectedPlayers.Add(PlayerState);
	ApplySurvivalUpgradeOptionToPlayer(PlayerState, *Option);

	ActiveSurvivalUpgradeOffer.SelectedCount = SurvivalUpgradeSelectedPlayers.Num();

	UE_LOG(LogAeyerji, Display, TEXT("SubmitSurvivalUpgradeChoice accepted: Player=%s Option=%s Type=%d Magnitude=%.1f Scaled=%.2f Selected=%d/%d"),
		*GetNameSafe(PlayerState), *OptionId.ToString(), (int32)Option->UpgradeType, Option->BaseMagnitude,
		Option->BaseMagnitude / FMath::Max(1, ActiveSurvivalUpgradeOffer.RequiredSelectionCount),
		ActiveSurvivalUpgradeOffer.SelectedCount, ActiveSurvivalUpgradeOffer.RequiredSelectionCount);

	if (AAeyerjiGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AAeyerjiGameState>() : nullptr)
	{
		GameState->SetSurvivalUpgradeOfferStateFromServer(ActiveSurvivalUpgradeOffer);
	}

	// Edge case handling: if players disconnect during offer, early-finish when all *remaining* eligible have selected
	bool bAllRemainingSelected = true;
	for (const TWeakObjectPtr<AAeyerjiPlayerState>& Weak : SurvivalUpgradeEligiblePlayers)
	{
		AAeyerjiPlayerState* PS = Weak.Get();
		if (PS && !HasPlayerSelectedCurrentSurvivalUpgrade(PS))
		{
			bAllRemainingSelected = false;
			break;
		}
	}
	if (bAllRemainingSelected)
	{
		UE_LOG(LogAeyerji, Display, TEXT("All remaining players selected survival upgrade offer (Round=%d). Advancing early."),
			ActiveSurvivalUpgradeOffer.RoundNumber);
		FinishSurvivalUpgradeOffer(/*bApplyMissingSelections=*/false);
	}

	return true;
}

void AAeyerjiLevelDirector::ApplySurvivalUpgradeOptionToPlayer(AAeyerjiPlayerState* PlayerState, const FAeyerjiSurvivalUpgradeOption& Option)
{
	const float Divisor = static_cast<float>(FMath::Max(1, ActiveSurvivalUpgradeOffer.RequiredSelectionCount));
	const float ScaledMagnitude = Option.BaseMagnitude / Divisor;
	if (ScaledMagnitude <= 0.f)
	{
		return;
	}

	AAeyerjiSurvivalDefenseObjectiveActor* Objective = Cast<AAeyerjiSurvivalDefenseObjectiveActor>(SurvivalDefenseObjectiveActor.Get());

	switch (Option.UpgradeType)
	{
	case EAeyerjiSurvivalUpgradeType::TreeMaxHP:
		if (Objective)
		{
			Objective->ApplyTreeMaxHealthUpgrade(ScaledMagnitude);
			PublishCurrentSurvivalRoundProgress();
		}
		else
		{
			ApplySurvivalTreeMaxHealthUpgrade(ScaledMagnitude); // fallback for non-native objective
		}
		break;
	case EAeyerjiSurvivalUpgradeType::TreeReflectDamage:
		if (Objective)
		{
			Objective->AddTreeReflectFraction(ScaledMagnitude);
		}
		else
		{
			SurvivalDefenseObjectiveReflectFraction += ScaledMagnitude;
		}
		break;
	case EAeyerjiSurvivalUpgradeType::TreeRegen:
	{
		if (Objective)
		{
			Objective->AddTreeRegenPerSecond(ScaledMagnitude);
			if (UWorld* World = GetWorld())
			{
				World->GetTimerManager().ClearTimer(SurvivalDefenseObjectiveRegenHandle);
				World->GetTimerManager().SetTimer(SurvivalDefenseObjectiveRegenHandle, this, &AAeyerjiLevelDirector::TickSurvivalDefenseObjectiveRegen, 1.f, true);
			}
		}
		else
		{
			SurvivalDefenseObjectiveRegenPerSecond += ScaledMagnitude;
			if (UWorld* World = GetWorld())
			{
				World->GetTimerManager().ClearTimer(SurvivalDefenseObjectiveRegenHandle);
				World->GetTimerManager().SetTimer(SurvivalDefenseObjectiveRegenHandle, this, &AAeyerjiLevelDirector::TickSurvivalDefenseObjectiveRegen, 1.f, true);
			}
		}
		break;
	}
	case EAeyerjiSurvivalUpgradeType::PlayerXP:
		if (APawn* Pawn = PlayerState ? PlayerState->GetPawn() : nullptr)
		{
			if (UAeyerjiLevelingComponent* Leveling = Pawn->FindComponentByClass<UAeyerjiLevelingComponent>())
			{
				Leveling->AddXP(ScaledMagnitude);
				UE_LOG(LogAeyerji, Display, TEXT("Applied survival PlayerXP upgrade: Player=%s +%.1f XP"),
					*GetNameSafe(PlayerState), ScaledMagnitude);
			}
			else
			{
				UE_LOG(LogAeyerji, Warning, TEXT("Survival upgrade PlayerXP: no LevelingComponent on pawn for %s"),
					*GetNameSafe(PlayerState));
			}
		}
		break;
	default:
		break;
	}
}

void AAeyerjiLevelDirector::ApplySurvivalTreeMaxHealthUpgrade(const float DeltaHP)
{
	// Legacy path for non-native defense objectives. Prefer AAeyerjiSurvivalDefenseObjectiveActor::ApplyTreeMaxHealthUpgrade.
	if (DeltaHP <= 0.f)
	{
		return;
	}

	UAbilitySystemComponent* ObjectiveASC = CachedSurvivalDefenseObjectiveASC.Get();
	if (!ObjectiveASC || !IsValid(SurvivalDefenseObjectiveActor.Get()))
	{
		return;
	}

	const float CurrentMax = ObjectiveASC->GetNumericAttribute(UAeyerjiAttributeSet::GetHPMaxAttribute());
	const float CurrentHP = ObjectiveASC->GetNumericAttribute(UAeyerjiAttributeSet::GetHPAttribute());
	const float NewMax = FMath::Max(1.f, CurrentMax + DeltaHP);
	ObjectiveASC->SetNumericAttributeBase(UAeyerjiAttributeSet::GetHPMaxAttribute(), NewMax);
	// Heal the delta on top of current (clamped to new max)
	ObjectiveASC->SetNumericAttributeBase(UAeyerjiAttributeSet::GetHPAttribute(), FMath::Clamp(CurrentHP + DeltaHP, 0.f, NewMax));
	PublishCurrentSurvivalRoundProgress();
}

void AAeyerjiLevelDirector::TickSurvivalDefenseObjectiveRegen()
{
	if (!HasAuthority())
	{
		return;
	}

	AAeyerjiSurvivalDefenseObjectiveActor* Objective = Cast<AAeyerjiSurvivalDefenseObjectiveActor>(SurvivalDefenseObjectiveActor.Get());
	float RegenRate = Objective ? Objective->UpgradeRegenPerSecond : SurvivalDefenseObjectiveRegenPerSecond;

	if (RegenRate <= 0.f)
	{
		return;
	}

	UAbilitySystemComponent* ObjectiveASC = CachedSurvivalDefenseObjectiveASC.Get();
	if (!ObjectiveASC || !IsSurvivalDefenseObjectiveAlive())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(SurvivalDefenseObjectiveRegenHandle);
		}
		return;
	}

	if (Objective)
	{
		Objective->ApplyRegenTick();
	}
	else
	{
		const float CurrentHP = ObjectiveASC->GetNumericAttribute(UAeyerjiAttributeSet::GetHPAttribute());
		const float MaxHP = ObjectiveASC->GetNumericAttribute(UAeyerjiAttributeSet::GetHPMaxAttribute());
		if (CurrentHP >= MaxHP - KINDA_SMALL_NUMBER)
		{
			return;
		}

		ObjectiveASC->SetNumericAttributeBase(
			UAeyerjiAttributeSet::GetHPAttribute(),
			FMath::Clamp(CurrentHP + RegenRate, 0.f, MaxHP));
	}

	PublishCurrentSurvivalRoundProgress();
}

void AAeyerjiLevelDirector::HandleSurvivalDefenseObjectiveDamageTaken(
	AActor* VictimActor,
	AActor* InstigatorActor,
	const float DamageTaken,
	const FGameplayTag DamageType)
{
	static_cast<void>(DamageType);

	if (!HasAuthority()
		|| VictimActor != SurvivalDefenseObjectiveActor.Get()
		|| DamageTaken <= 0.f)
	{
		return;
	}

	float CurrentReflect = 0.f;
	if (AAeyerjiSurvivalDefenseObjectiveActor* Obj = Cast<AAeyerjiSurvivalDefenseObjectiveActor>(SurvivalDefenseObjectiveActor.Get()))
	{
		CurrentReflect = Obj->UpgradeReflectFraction;
	}
	else
	{
		CurrentReflect = SurvivalDefenseObjectiveReflectFraction;
	}

	if (CurrentReflect <= 0.f)
	{
		return;
	}

	ApplySurvivalDefenseObjectiveReflect(InstigatorActor, DamageTaken, CurrentReflect);
}

void AAeyerjiLevelDirector::ApplySurvivalDefenseObjectiveReflect(AActor* Attacker, const float DamageTaken, float ReflectFraction) const
{
	if (!IsValid(Attacker) || Attacker == SurvivalDefenseObjectiveActor.Get())
	{
		return;
	}

	UAbilitySystemComponent* ObjectiveASC = CachedSurvivalDefenseObjectiveASC.Get();
	UAbilitySystemComponent* AttackerASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Attacker, /*LookForComponent=*/true);
	if (!ObjectiveASC || !AttackerASC)
	{
		return;
	}

	const float ReflectedDamage = DamageTaken * FMath::Max(0.f, ReflectFraction);
	if (ReflectedDamage <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	FGameplayEffectContextHandle ContextHandle = ObjectiveASC->MakeEffectContext();
	ContextHandle.AddInstigator(SurvivalDefenseObjectiveActor.Get(), SurvivalDefenseObjectiveActor.Get());
	ContextHandle.AddSourceObject(SurvivalDefenseObjectiveActor.Get());

	FGameplayEffectSpecHandle SpecHandle = ObjectiveASC->MakeOutgoingSpec(UGE_DamagePhysical::StaticClass(), 1.f, ContextHandle);
	if (!SpecHandle.IsValid() || !SpecHandle.Data.IsValid())
	{
		return;
	}

	SpecHandle.Data->AddDynamicAssetTag(AeyerjiTags::DamageType_Physical);
	SpecHandle.Data->SetSetByCallerMagnitude(AeyerjiTags::SBC_Damage_Instant, ReflectedDamage);
	ObjectiveASC->ApplyGameplayEffectSpecToTarget(*SpecHandle.Data.Get(), AttackerASC);
}

void AAeyerjiLevelDirector::ClearSurvivalUpgradeOfferState()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SurvivalUpgradeOfferTimeoutHandle);
	}

	ActiveSurvivalUpgradeOffer = FAeyerjiSurvivalUpgradeOfferState();
	SurvivalUpgradeEligiblePlayers.Reset();
	SurvivalUpgradeSelectedPlayers.Reset();

	if (HasAuthority())
	{
		if (AAeyerjiGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AAeyerjiGameState>() : nullptr)
		{
			GameState->ClearSurvivalUpgradeOfferStateFromServer();
		}
	}
}

void AAeyerjiLevelDirector::StartSurvivalBossRound(const int32 RoundNumber)
{
	CurrentSurvivalRound = FMath::Max(1, RoundNumber);
	CurrentSurvivalCycle = GetSurvivalCycleForRound(CurrentSurvivalRound);
	const int32 RoundStep = FMath::Max(0, CurrentSurvivalRound - 1);
	SurvivalEnemyLevelBonus = SurvivalMissionDefinition
		? (CurrentSurvivalCycle * FMath::Max(0, SurvivalMissionDefinition->EnemyLevelBonusPerCycle))
			+ (RoundStep * FMath::Max(0, SurvivalMissionDefinition->EnemyLevelBonusPerRound))
		: 0;
	SurvivalEnemyHealthMultiplier = SurvivalMissionDefinition
		? FMath::Pow(FMath::Max(0.f, SurvivalMissionDefinition->EnemyHealthMultiplierPerRound), RoundStep)
		: 1.f;
	SurvivalEnemyDamageMultiplier = SurvivalMissionDefinition
		? FMath::Pow(FMath::Max(0.f, SurvivalMissionDefinition->EnemyDamageMultiplierPerRound), RoundStep)
		: 1.f;
	bSurvivalBossRoundActive = true;
	bSurvivalBossDefeatHandled = false;
	bBossEncounterTriggered = false;
	bNativeBossSpawnIssued = false;
	CurrentSurvivalRoundEnemyTotal = 1;
	CurrentSurvivalRoundEnemiesKilled = 0;
	CurrentSurvivalWaveIndex = 0;
	CurrentSurvivalWaveCount = 1;
	CurrentSurvivalWaveEnemyTotal = 1;
	CurrentSurvivalWaveDisplayLabel = FText::GetEmpty();
	bCurrentSurvivalWaveContainsBoss = true;
	CurrentSurvivalWaveEnemiesKilled = 0;
	if (IsValid(BossSpawner))
	{
		BossSpawner->ResetEncounter();
		ApplySurvivalDefenseObjectiveToSpawner(BossSpawner, /*bBossRound=*/true);
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

	CurrentSurvivalRoundPhase = Phase;
	if (Phase == EAeyerjiSurvivalRoundPhase::Inactive)
	{
		CurrentSurvivalRoundEnemiesKilled = 0;
		CurrentSurvivalWaveEnemiesKilled = 0;
	}
	else if (Phase == EAeyerjiSurvivalRoundPhase::RoundComplete)
	{
		CurrentSurvivalRoundEnemiesKilled = CurrentSurvivalRoundEnemyTotal;
		CurrentSurvivalWaveEnemiesKilled = CurrentSurvivalWaveEnemyTotal;
	}

	if (AAeyerjiGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AAeyerjiGameState>() : nullptr)
	{
		TArray<FAeyerjiSurvivalRoundDefinition> AuthoredRounds;
		BuildAuthoredSurvivalRounds(AuthoredRounds);
		const int32 BossCadence = SurvivalMissionDefinition ? FMath::Max(0, SurvivalMissionDefinition->BossEveryNRounds) : 0;
		const bool bEndlessMission = SurvivalMissionDefinition ? SurvivalMissionDefinition->bLoopAfterBoss : false;
		const bool bBossRound = bSurvivalBossRoundActive || bCurrentSurvivalWaveContainsBoss || IsSurvivalBossRound(CurrentSurvivalRound);
		const int32 PatternCount = BossCadence > 0
			? BossCadence
			: AuthoredRounds.Num();
		const int32 PatternNumber = PatternCount > 0 && CurrentSurvivalRound > 0
			? ((CurrentSurvivalRound - 1) % PatternCount) + 1
			: 0;
		const int32 AuthoredRoundIndex = !AuthoredRounds.IsEmpty() && CurrentSurvivalRound > 0
			? (CurrentSurvivalRound - 1) % AuthoredRounds.Num()
			: INDEX_NONE;

		FAeyerjiSurvivalRoundState State;
		State.RoundNumber = CurrentSurvivalRound;
		State.CycleNumber = CurrentSurvivalCycle;
		State.CycleDisplayNumber = CurrentSurvivalCycle + 1;
		State.RoundPatternNumber = PatternNumber;
		State.RoundPatternCount = PatternCount;
		State.BossEveryNRounds = BossCadence;
		State.MaxRoundNumber = bEndlessMission ? 0 : BossCadence;
		State.bEndless = bEndlessMission;
		State.Phase = Phase;
		State.RoundType = bBossRound ? EAeyerjiSurvivalRoundType::Boss : EAeyerjiSurvivalRoundType::Normal;
		if (AuthoredRounds.IsValidIndex(AuthoredRoundIndex))
		{
			const FAeyerjiSurvivalRoundDefinition& RoundDefinition = AuthoredRounds[AuthoredRoundIndex];
			State.RoundDisplayLabel = RoundDefinition.DisplayLabel;
			if (!bBossRound)
			{
				State.RoundType = RoundDefinition.RoundType;
			}
		}
		State.WaveNumber = CurrentSurvivalWaveIndex >= 0 ? CurrentSurvivalWaveIndex + 1 : 0;
		State.WaveCount = CurrentSurvivalWaveCount;
		State.WaveDisplayLabel = CurrentSurvivalWaveDisplayLabel;
		State.WaveEnemiesRequired = CurrentSurvivalWaveEnemyTotal;
		State.WaveEnemiesKilled = FMath::Clamp(CurrentSurvivalWaveEnemiesKilled, 0, FMath::Max(CurrentSurvivalWaveEnemyTotal, CurrentSurvivalWaveEnemiesKilled));
		State.EnemiesRequired = CurrentSurvivalRoundEnemyTotal;
		State.EnemiesKilled = FMath::Clamp(CurrentSurvivalRoundEnemiesKilled, 0, FMath::Max(CurrentSurvivalRoundEnemyTotal, CurrentSurvivalRoundEnemiesKilled));
		State.bBossRound = bBossRound;
		State.bDefenseObjectiveActive = IsSurvivalDefenseObjectiveEnabled() && IsValid(SurvivalDefenseObjectiveActor.Get());
		State.bDefenseObjectiveDestroyed = bSurvivalDefenseObjectiveDestroyed;
		State.DefenseObjectiveHealth = GetSurvivalDefenseObjectiveHealth();
		State.DefenseObjectiveHealthMax = GetSurvivalDefenseObjectiveMaxHealth();
		State.DefenseObjectiveProgress01 = State.DefenseObjectiveHealthMax > 0.f
			? FMath::Clamp(State.DefenseObjectiveHealth / State.DefenseObjectiveHealthMax, 0.f, 1.f)
			: 0.f;
		State.DefenseObjectiveActorTag = SurvivalMissionDefinition
			? SurvivalMissionDefinition->DefenseObjective.ObjectiveActorTag
			: NAME_None;
		State.MessageKey = MessageKey;
		State.bActive = bRunActive
			&& CurrentSurvivalRound > 0
			&& Phase != EAeyerjiSurvivalRoundPhase::Inactive;
		GameState->SetSurvivalRoundStateFromServer(State);
	}
}

void AAeyerjiLevelDirector::PublishCurrentSurvivalRoundProgress()
{
	if (!HasAuthority() || CurrentSurvivalRoundPhase == EAeyerjiSurvivalRoundPhase::Inactive)
	{
		return;
	}

	PublishSurvivalRoundState(CurrentSurvivalRoundPhase, NAME_None);
}

bool AAeyerjiLevelDirector::BuildRuntimeSurvivalWaves(const FAeyerjiSurvivalRoundDefinition& RoundDefinition, const TArray<FAeyerjiSurvivalRoundDefinition>& AuthoredRounds, const int32 AuthoredRoundIndex, const int32 CycleNumber, TArray<FWaveDefinition>& OutWaves, int32& OutEnemyCount) const
{
	OutWaves.Reset();
	OutEnemyCount = 0;

	const float CycleMultiplier = SurvivalMissionDefinition
		? FMath::Pow(FMath::Max(0.f, SurvivalMissionDefinition->EnemyCountScalePerCycle), FMath::Max(0, CycleNumber))
		: 1.f;
	const float CountMultiplier = FMath::Max(0.f, RoundDefinition.EnemyCountMultiplier) * CycleMultiplier;

	const int32 BaseRoundCount = AuthoredRounds.Num();
	const bool bUseBlendedRoster = SurvivalMissionDefinition
		&& SurvivalMissionDefinition->bBlendPreviousRoundEnemySets
		&& BaseRoundCount > 1
		&& AuthoredRoundIndex > 0;
	const bool bUseBlendedWaveRoster = SurvivalMissionDefinition
		&& SurvivalMissionDefinition->bBlendPreviousWaveEnemySets
		&& RoundDefinition.Waves.Num() > 1;
	const float CarryWeight = SurvivalMissionDefinition
		? FMath::Clamp(SurvivalMissionDefinition->PreviousRoundCarryWeight, 0.f, 1.f)
		: 0.f;
	const int32 MaxLookback = bUseBlendedRoster
		? FMath::Clamp(SurvivalMissionDefinition->RoundBlendLookback, 1, AuthoredRoundIndex)
		: 0;
	const int32 SourceCount = bUseBlendedRoster && CarryWeight > 0.f
		? MaxLookback + 1
		: 1;
	int32 MaxWaveCount = RoundDefinition.Waves.Num();
	for (int32 Offset = 1; Offset < SourceCount; ++Offset)
	{
		const int32 SourceRoundIndex = AuthoredRoundIndex - Offset;
		if (AuthoredRounds.IsValidIndex(SourceRoundIndex))
		{
			MaxWaveCount = FMath::Max(MaxWaveCount, AuthoredRounds[SourceRoundIndex].Waves.Num());
		}
	}

	auto ResolveBlendWeight = [CarryWeight, SourceCount](const int32 Offset) -> float
	{
		if (SourceCount <= 1)
		{
			return 1.f;
		}

		if (Offset == SourceCount - 1)
		{
			return FMath::Pow(CarryWeight, Offset);
		}

		return (1.f - CarryWeight) * FMath::Pow(CarryWeight, Offset);
	};

	auto ResolveVariableSourceBlendWeight = [CarryWeight](const int32 Offset, const int32 SourceCountForBlend) -> float
	{
		if (SourceCountForBlend <= 1)
		{
			return 1.f;
		}

		if (Offset == SourceCountForBlend - 1)
		{
			return FMath::Pow(CarryWeight, Offset);
		}

		return (1.f - CarryWeight) * FMath::Pow(CarryWeight, Offset);
	};

	struct FWeightedSurvivalEnemySet
	{
		const FEnemySetDef* SetData = nullptr;
		float RawCount = 0.f;
		float Remainder = 0.f;
		int32 SourceOffset = 0;
		int32 AllocatedCount = 0;
	};

	auto AppendEnemySet = [&](const FEnemySetDef& SetData, const int32 SpawnCount, FWaveDefinition& RuntimeWave)
	{
		TSubclassOf<APawn> EnemyClass = SetData.EnemyClass.Get();
		if (!EnemyClass || SpawnCount <= 0)
		{
			if (!EnemyClass && SpawnCount > 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("LevelDirector %s skipped unloaded survival enemy class %s. Preload should complete before round build."),
					*GetNameSafe(this),
					*SetData.EnemyClass.ToSoftObjectPath().ToString());
			}
			return;
		}

		FEnemySet RuntimeSet;
		RuntimeSet.EnemyClass = EnemyClass;
		RuntimeSet.Count = SpawnCount;
		RuntimeSet.SpawnInterval = SetData.SpawnInterval;
		RuntimeSet.bIsElite = SetData.bIsElite;
		RuntimeSet.bIsMiniBoss = SetData.bIsMiniBoss;
		RuntimeSet.bIsBoss = SetData.bIsBoss;
		RuntimeSet.MiniBossGrantedAbilities = SetData.MiniBossGrantedAbilities;
		RuntimeSet.BossGrantedAbilities = SetData.BossGrantedAbilities;
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
	};

	for (int32 WaveIndex = 0; WaveIndex < MaxWaveCount; ++WaveIndex)
	{
		FWaveDefinition RuntimeWave;
		if (RoundDefinition.Waves.IsValidIndex(WaveIndex))
		{
			RuntimeWave.WaveLabel = RoundDefinition.Waves[WaveIndex].WaveLabel;
			RuntimeWave.PostSpawnDelay = RoundDefinition.Waves[WaveIndex].PostSpawnDelay;
		}

		TArray<FWeightedSurvivalEnemySet> WeightedSets;
		float RawWaveTotal = 0.f;

		auto AddWeightedEnemySets = [&](const FWaveDefData& WaveData, const float WeightedCountMultiplier, const int32 SourceOffset)
		{
			for (const FEnemySetDef& SetData : WaveData.EnemySets)
			{
				if (SourceOffset > 0
					&& SurvivalMissionDefinition->bExcludeBossSetsFromRoundBlend
					&& SetData.bIsBoss)
				{
					continue;
				}

				const float RawCount = static_cast<float>(SetData.Count) * WeightedCountMultiplier;
				if (RawCount <= 0.f)
				{
					continue;
				}

				FWeightedSurvivalEnemySet& WeightedSet = WeightedSets.AddDefaulted_GetRef();
				WeightedSet.SetData = &SetData;
				WeightedSet.RawCount = RawCount;
				WeightedSet.Remainder = FMath::Frac(RawCount);
				WeightedSet.SourceOffset = SourceOffset;
				WeightedSet.AllocatedCount = FMath::FloorToInt(RawCount);
				RawWaveTotal += RawCount;
			}
		};

		if (bUseBlendedWaveRoster)
		{
			const int32 WaveLookback = CarryWeight > 0.f
				? FMath::Clamp(SurvivalMissionDefinition->RoundBlendLookback, 1, WaveIndex)
				: 0;
			const int32 WaveSourceCount = WaveLookback + 1;
			for (int32 Offset = 0; Offset < WaveSourceCount; ++Offset)
			{
				const int32 SourceWaveIndex = WaveIndex - Offset;
				if (!RoundDefinition.Waves.IsValidIndex(SourceWaveIndex))
				{
					continue;
				}

				const FWaveDefData& WaveData = RoundDefinition.Waves[SourceWaveIndex];
				if (RuntimeWave.WaveLabel.IsEmpty())
				{
					RuntimeWave.WaveLabel = WaveData.WaveLabel;
				}
				if (RuntimeWave.PostSpawnDelay <= 0.f)
				{
					RuntimeWave.PostSpawnDelay = WaveData.PostSpawnDelay;
				}

				AddWeightedEnemySets(WaveData, CountMultiplier * ResolveVariableSourceBlendWeight(Offset, WaveSourceCount), Offset);
			}
		}
		else
		{
			for (int32 Offset = 0; Offset < SourceCount; ++Offset)
			{
				const int32 SourceRoundIndex = AuthoredRoundIndex - Offset;
				if (!AuthoredRounds.IsValidIndex(SourceRoundIndex))
				{
					continue;
				}

				const FAeyerjiSurvivalRoundDefinition& SourceRound = AuthoredRounds[SourceRoundIndex];
				if (!SourceRound.Waves.IsValidIndex(WaveIndex))
				{
					continue;
				}

				const FWaveDefData& WaveData = SourceRound.Waves[WaveIndex];
				if (RuntimeWave.WaveLabel.IsEmpty())
				{
					RuntimeWave.WaveLabel = WaveData.WaveLabel;
				}
				if (RuntimeWave.PostSpawnDelay <= 0.f)
				{
					RuntimeWave.PostSpawnDelay = WaveData.PostSpawnDelay;
				}

				AddWeightedEnemySets(WaveData, CountMultiplier * ResolveBlendWeight(Offset), Offset);
			}
		}

		int32 DesiredWaveTotal = FMath::Max(0, FMath::RoundToInt(RawWaveTotal));
		int32 AllocatedWaveTotal = 0;
		for (FWeightedSurvivalEnemySet& WeightedSet : WeightedSets)
		{
			if (WeightedSet.SourceOffset == 0
				&& SurvivalMissionDefinition
				&& SurvivalMissionDefinition->bGuaranteeCurrentRoundBlendEntries
				&& WeightedSet.RawCount > 0.f
				&& WeightedSet.AllocatedCount <= 0)
			{
				WeightedSet.AllocatedCount = 1;
			}

			AllocatedWaveTotal += WeightedSet.AllocatedCount;
		}

		DesiredWaveTotal = FMath::Max(DesiredWaveTotal, AllocatedWaveTotal);
		WeightedSets.Sort([](const FWeightedSurvivalEnemySet& A, const FWeightedSurvivalEnemySet& B)
		{
			if (!FMath::IsNearlyEqual(A.Remainder, B.Remainder))
			{
				return A.Remainder > B.Remainder;
			}

			return A.SourceOffset < B.SourceOffset;
		});

		int32 RemainingToAllocate = FMath::Max(0, DesiredWaveTotal - AllocatedWaveTotal);
		for (FWeightedSurvivalEnemySet& WeightedSet : WeightedSets)
		{
			if (RemainingToAllocate <= 0)
			{
				break;
			}

			WeightedSet.AllocatedCount++;
			RemainingToAllocate--;
		}

		for (const FWeightedSurvivalEnemySet& WeightedSet : WeightedSets)
		{
			if (WeightedSet.SetData)
			{
				AppendEnemySet(*WeightedSet.SetData, WeightedSet.AllocatedCount, RuntimeWave);
			}
		}

		if (!RuntimeWave.EnemySets.IsEmpty())
		{
			if (bUseBlendedRoster || bUseBlendedWaveRoster)
			{
				const int32 EffectiveBlendLookback = bUseBlendedWaveRoster && CarryWeight > 0.f
					? FMath::Clamp(SurvivalMissionDefinition->RoundBlendLookback, 1, WaveIndex)
					: MaxLookback;
				UE_LOG(LogTemp, Display, TEXT("SurvivalBlend Director=%s Mode=%s Round=%d Pattern=%d Wave=%d RawTotal=%.2f RuntimeSets=%d RuntimeEnemies=%d Carry=%.2f Lookback=%d."),
					*GetNameSafe(this),
					bUseBlendedWaveRoster ? TEXT("Wave") : TEXT("Round"),
					CurrentSurvivalRound,
					AuthoredRoundIndex + 1,
					WaveIndex + 1,
					RawWaveTotal,
					RuntimeWave.EnemySets.Num(),
					CountRuntimeWaveEnemies(RuntimeWave),
					CarryWeight,
					EffectiveBlendLookback);
			}
			OutWaves.Add(RuntimeWave);
		}
	}

	return !OutWaves.IsEmpty() && OutEnemyCount > 0;
}

int32 AAeyerjiLevelDirector::CountRuntimeWaveEnemies(const FWaveDefinition& WaveDefinition) const
{
	int32 EnemyTotal = 0;
	for (const FEnemySet& EnemySet : WaveDefinition.EnemySets)
	{
		EnemyTotal += FMath::Max(0, EnemySet.Count);
	}

	return EnemyTotal;
}

int32 AAeyerjiLevelDirector::GetSurvivalCycleForRound(const int32 RoundNumber) const
{
	TArray<FAeyerjiSurvivalRoundDefinition> AuthoredRounds;
	BuildAuthoredSurvivalRounds(AuthoredRounds);
	const int32 AuthoredRoundCount = AuthoredRounds.Num();
	const int32 BossCadence = SurvivalMissionDefinition ? SurvivalMissionDefinition->BossEveryNRounds : 0;
	const int32 CycleLength = BossCadence > 0 ? BossCadence : FMath::Max(1, AuthoredRoundCount);
	return FMath::Max(0, (FMath::Max(1, RoundNumber) - 1) / CycleLength);
}

bool AAeyerjiLevelDirector::IsSurvivalBossRound(const int32 RoundNumber) const
{
	const int32 BossCadence = SurvivalMissionDefinition ? SurvivalMissionDefinition->BossEveryNRounds : 0;
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
	SpawnSurvivalRoundClearReward();
	BeginSurvivalRoundUpgradeOfferOrScheduleNextRound();

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
	SpawnedTeleporter->SetAllowedDirections(/*bAllowAToB=*/true, /*bAllowBToA=*/false);

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
