// Copyright (c) 2025 Aeyerji.
#include "Director/AeyerjiEncounterDirector.h"

#include "../../AeyerjiGameState.h"
#include "Director/AeyerjiSpawnerGroup.h"
#include "Director/AeyerjiSpawnRegion.h"
#include "Director/AeyerjiWorldSpawnProfile.h"
#include "Director/AeyerjiLevelDirector.h"
#include "Enemy/AeyerjiEnemyManagementBPFL.h"
#include "Enemy/EnemyParentNative.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/AeyerjiEnemyArchetypeComponent.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "AIController.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "BrainComponent.h"
#include "DrawDebugHelpers.h"
#include "Curves/CurveFloat.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "Engine/World.h"
#include "EngineUtils.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "GameFramework/Character.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "GameFramework/CharacterMovementComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Components/CapsuleComponent.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Net/UnrealNetwork.h"
#include "ProfilingDebugging/CountersTrace.h"
#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "Stats/Stats.h"
#include "Systems/AeyerjiDifficultyTuning.h"
#include "Systems/AeyerjiRiftRules.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogEncounterDirector, Log, All);

DECLARE_STATS_GROUP(TEXT("Aeyerji Encounter"), STATGROUP_AeyerjiEncounter, STATCAT_Advanced);
DECLARE_CYCLE_STAT(TEXT("Rift Population Prewarm"), STAT_AeyerjiRiftPopulationPrewarm, STATGROUP_AeyerjiEncounter);
DECLARE_CYCLE_STAT(TEXT("Rift Region Staging"), STAT_AeyerjiRiftRegionStaging, STATGROUP_AeyerjiEncounter);
DECLARE_CYCLE_STAT(TEXT("Rift Reinforcement Reveal"), STAT_AeyerjiRiftReinforcementReveal, STATGROUP_AeyerjiEncounter);
TRACE_DECLARE_INT_COUNTER(AeyerjiRiftAwakeEnemies, TEXT("Aeyerji/Rift/AwakeEnemies"));
TRACE_DECLARE_INT_COUNTER(AeyerjiRiftSleepingEnemies, TEXT("Aeyerji/Rift/SleepingEnemies"));
TRACE_DECLARE_INT_COUNTER(AeyerjiRiftRevealingEnemies, TEXT("Aeyerji/Rift/RevealingEnemies"));
TRACE_DECLARE_INT_COUNTER(AeyerjiRiftStagedPopulation, TEXT("Aeyerji/Rift/StagedPopulation"));
TRACE_DECLARE_INT_COUNTER(AeyerjiRiftPooledEnemies, TEXT("Aeyerji/Rift/PooledEnemies"));
TRACE_DECLARE_INT_COUNTER(AeyerjiRiftFreshConstructions, TEXT("Aeyerji/Rift/FreshConstructions"));
TRACE_DECLARE_INT_COUNTER(AeyerjiRiftEmergencySpawns, TEXT("Aeyerji/Rift/EmergencySpawns"));

namespace
{
	constexpr int32 MaxEncounterSpawnGroups = 256;
	constexpr int32 MaxEnemyClassesPerGroup = 256;
	constexpr int32 MaxDynamicEnemiesPerGroup = 10000;
	constexpr int32 MaxEncounterPopulation = 100000;
	constexpr int32 MaxRiftRegions = 4096;
	constexpr int32 MaxFixedSpawnRegions = 4096;
	constexpr int32 MaxPoolWarmSets = 4096;
	constexpr int32 MaxFixedClusters = 10000;
	constexpr int32 MaxSpawnAttempts = 256;
	constexpr int32 MaxSpawnsPerDirectorTick = 1000;
	constexpr int32 MaxRecentPathSamples = 1024;
	constexpr int32 MaxProgressPoints = 1000000000;
	constexpr int32 MaxRiftSpawnFailures = 100;
	constexpr float MaxWorldDistance = 1000000.f;
	constexpr float MaxDirectorSeconds = 86400.f;
	constexpr float MaxDirectorWorkMilliseconds = 1000.f;
	constexpr float MaxRuntimeMultiplier = 1000000.f;

	float ResolveFiniteFloat(const float Value, const float Fallback, const float Minimum, const float Maximum)
	{
		return FMath::IsFinite(Value) ? FMath::Clamp(Value, Minimum, Maximum) : Fallback;
	}

	bool IsFiniteBox(const FBox& Bounds)
	{
		return Bounds.IsValid && !Bounds.Min.ContainsNaN() && !Bounds.Max.ContainsNaN();
	}

	static TAutoConsoleVariable<float>& GetFixedPopulationBudgetScaleCVar()
	{
		// Intentionally leaked to avoid shutdown-order crashes when the console manager is destroyed.
		static TAutoConsoleVariable<float>* CVar = new TAutoConsoleVariable<float>(
			TEXT("aeyerji.FixedPopulation.BudgetScale"),
			1.0f,
			TEXT("Scales fixed world population target (0..1)."),
			ECVF_Default);
		return *CVar;
	}

	static TAutoConsoleVariable<int32>& GetFixedPopulationBudgetCapCVar()
	{
		// Intentionally leaked to avoid shutdown-order crashes when the console manager is destroyed.
		static TAutoConsoleVariable<int32>* CVar = new TAutoConsoleVariable<int32>(
			TEXT("aeyerji.FixedPopulation.BudgetCap"),
			0,
			TEXT("Hard cap on fixed world population target (0 disables)."),
			ECVF_Default);
		return *CVar;
	}

	FName ResolveBossObjectiveId(const AAeyerjiLevelDirector* LevelDirector)
	{
		if (!IsValid(LevelDirector))
		{
			return NAME_None;
		}

		if (*LevelDirector->BossPawnClass)
		{
			return LevelDirector->BossPawnClass->GetFName();
		}

		if (IsValid(LevelDirector->BossSpawner))
		{
			return LevelDirector->BossSpawner->GetFName();
		}

		if (IsValid(LevelDirector->BossSpawnMarker))
		{
			return LevelDirector->BossSpawnMarker->GetFName();
		}

		return NAME_None;
	}

	FName BuildObjectiveTextKey(const FAeyerjiObjectiveState& ObjectiveState)
	{
		if (ObjectiveState.bObjectiveComplete)
		{
			if (ObjectiveState.ObjectiveKind == EAeyerjiObjectiveKind::BossCleared)
			{
				return TEXT("Objective.BossCleared.Complete");
			}

			if (ObjectiveState.ObjectiveKind == EAeyerjiObjectiveKind::KillCount)
			{
				return TEXT("Objective.KillCount.Complete");
			}
		}

		switch (ObjectiveState.ObjectiveKind)
		{
		case EAeyerjiObjectiveKind::KillCount:
			return TEXT("Objective.KillCount.Active");
		case EAeyerjiObjectiveKind::KillNamedBoss:
			return ObjectiveState.bBossSpawned
				? TEXT("Objective.KillNamedBoss.Active")
				: TEXT("Objective.KillNamedBoss.Pending");
		case EAeyerjiObjectiveKind::KillCountThenBoss:
			return ObjectiveState.bPrimaryObjectiveComplete
				? TEXT("Objective.KillCountThenBoss.BossPhase")
				: TEXT("Objective.KillCountThenBoss.KillPhase");
		case EAeyerjiObjectiveKind::BossCleared:
			return ObjectiveState.bObjectiveComplete
				? TEXT("Objective.BossCleared.Complete")
				: TEXT("Objective.BossCleared.Active");
		case EAeyerjiObjectiveKind::None:
		default:
			return TEXT("Objective.None");
		}
	}
}

int32 UEnemySpawnGroupDefinition::ResolveSpawnCount() const
{
	int32 SafeMinCount = FMath::Clamp(MinCount, 1, MaxDynamicEnemiesPerGroup);
	int32 SafeMaxCount = FMath::Clamp(MaxCount, 1, MaxDynamicEnemiesPerGroup);
	if (SafeMaxCount < SafeMinCount)
	{
		Swap(SafeMinCount, SafeMaxCount);
	}
	return SafeMinCount == SafeMaxCount
		? SafeMinCount
		: FMath::RandRange(SafeMinCount, SafeMaxCount);
}

TSubclassOf<AEnemyParentNative> UEnemySpawnGroupDefinition::ResolveEnemyClass() const
{
	if (EnemyTypes.IsEmpty())
	{
		return nullptr;
	}

	TArray<TSubclassOf<AEnemyParentNative>> ValidEnemyTypes;
	for (TSubclassOf<AEnemyParentNative> EnemyClass : EnemyTypes)
	{
		if (ValidEnemyTypes.Num() >= MaxEnemyClassesPerGroup)
		{
			break;
		}
		if (*EnemyClass)
		{
			ValidEnemyTypes.Add(EnemyClass);
		}
	}

	if (ValidEnemyTypes.IsEmpty())
	{
		return nullptr;
	}

	const int32 Index = FMath::RandHelper(ValidEnemyTypes.Num());
	return ValidEnemyTypes[Index];
}

TSubclassOf<AEnemyParentNative> UEnemySpawnGroupDefinition::ResolveEliteEnemyClass() const
{
	if (EliteEnemyTypes.IsEmpty())
	{
		return nullptr;
	}

	TArray<TSubclassOf<AEnemyParentNative>> ValidEliteTypes;
	for (TSubclassOf<AEnemyParentNative> EnemyClass : EliteEnemyTypes)
	{
		if (ValidEliteTypes.Num() >= MaxEnemyClassesPerGroup)
		{
			break;
		}
		if (*EnemyClass)
		{
			ValidEliteTypes.Add(EnemyClass);
		}
	}

	if (ValidEliteTypes.IsEmpty())
	{
		return nullptr;
	}

	const int32 Index = FMath::RandHelper(ValidEliteTypes.Num());
	return ValidEliteTypes[Index];
}

AAeyerjiEncounterDirector::AAeyerjiEncounterDirector()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	PrimaryActorTick.TickInterval = TickIntervalSeconds;
}

void AAeyerjiEncounterDirector::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AAeyerjiEncounterDirector, TotalToKill);
	DOREPLIFETIME(AAeyerjiEncounterDirector, KilledCount);
	DOREPLIFETIME(AAeyerjiEncounterDirector, EnemiesDefeated);
	DOREPLIFETIME(AAeyerjiEncounterDirector, WeightedProgressPoints);
	DOREPLIFETIME(AAeyerjiEncounterDirector, WeightedProgressTarget);
	DOREPLIFETIME(AAeyerjiEncounterDirector, bBossSpawned);
}

void AAeyerjiEncounterDirector::BeginPlay()
{
	Super::BeginPlay();

	// Encounter spawning must run on the authority side only; otherwise each client will
	// spawn its own non-replicated enemies, causing invalid NetGUID references.
	if (GetNetMode() == NM_Client)
	{
		SetActorTickEnabled(false);
		return;
	}

	if (bApplyDirectorDefinitionOnBeginPlay)
	{
		ApplyDirectorDefinition();
	}
	SanitizeRuntimeSettings();

	SetActorTickInterval(TickIntervalSeconds);

	RefreshPlayerReference();
	RecentPlayerSamples.Reset();
	LastPathSampleTimestamp = -RecentPathSampleInterval;
	UpdateRecentPlayerPath();

	const double Now = GetWorld()->GetTimeSeconds();
	LastEncounterTimestamp = Now;
	LastKillTimestamp = Now;

	if (CachedPlayerPawn.IsValid())
	{
		LastEncounterLocation = CachedPlayerPawn->GetActorLocation();
	}
	else
	{
		LastEncounterLocation = GetActorLocation();
	}
}

void AAeyerjiEncounterDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	for (const TWeakObjectPtr<AActor>& EnemyPtr : LiveEnemies)
	{
		if (AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(EnemyPtr.Get()))
		{
			Enemy->OnEnemyDied.RemoveDynamic(this, &AAeyerjiEncounterDirector::HandleTrackedEnemyDied);
			Enemy->OnEnemyDied.RemoveDynamic(this, &AAeyerjiEncounterDirector::HandleProgressEnemyDied);
			Enemy->OnDestroyed.RemoveDynamic(this, &AAeyerjiEncounterDirector::HandleTrackedEnemyDestroyed);
			Enemy->OnDestroyed.RemoveDynamic(this, &AAeyerjiEncounterDirector::HandleProgressEnemyDestroyed);
		}
	}
	for (const TWeakObjectPtr<AActor>& EnemyPtr : ProgressOnlyEnemies)
	{
		if (AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(EnemyPtr.Get()))
		{
			Enemy->OnEnemyDied.RemoveDynamic(this, &AAeyerjiEncounterDirector::HandleProgressEnemyDied);
			Enemy->OnDestroyed.RemoveDynamic(this, &AAeyerjiEncounterDirector::HandleProgressEnemyDestroyed);
		}
	}

	LiveEnemies.Reset();
	ProgressOnlyEnemies.Reset();
	RegisteredProgressEnemyPoints.Reset();
	PendingSpawnRequests.Reset();
	FixedSpawnQueue.Reset();
	RiftSpawnQueue.Reset();
	RiftPrewarmQueue.Reset();
	Super::EndPlay(EndPlayReason);
}

void AAeyerjiEncounterDirector::ApplyDirectorDefinition()
{
	if (!DirectorDefinition || (!HasAuthority() && !IsTemplate()))
	{
		return;
	}

	if (DirectorDefinition->SpawnGroups.Num() > 0)
	{
		SpawnGroups.Reset();
		for (UEnemySpawnGroupDefinition* Group : DirectorDefinition->SpawnGroups)
		{
			if (SpawnGroups.Num() >= MaxEncounterSpawnGroups)
			{
				break;
			}
			if (IsValid(Group))
			{
				SpawnGroups.Add(Group);
			}
		}
	}

	TickIntervalSeconds = ResolveFiniteFloat(DirectorDefinition->TickIntervalSeconds, 0.2f, 0.f, MaxDirectorSeconds);
	MinDistanceBetweenEncounters = ResolveFiniteFloat(DirectorDefinition->MinDistanceBetweenEncounters, 1200.f, 0.f, MaxWorldDistance);
	KillVelocitySpawnFloor = ResolveFiniteFloat(DirectorDefinition->KillVelocitySpawnFloor, 0.25f, 0.f, MaxRuntimeMultiplier);
	KillVelocitySpawnCeil = ResolveFiniteFloat(DirectorDefinition->KillVelocitySpawnCeil, 1.5f, 0.01f, MaxRuntimeMultiplier);
	MinDistanceAtSlow = ResolveFiniteFloat(DirectorDefinition->MinDistanceAtSlow, 1800.f, 0.f, MaxWorldDistance);
	MinDistanceAtFast = ResolveFiniteFloat(DirectorDefinition->MinDistanceAtFast, 900.f, 0.f, MaxWorldDistance);
	MinDowntimeAtSlow = ResolveFiniteFloat(DirectorDefinition->MinDowntimeAtSlow, 2.f, 0.f, MaxDirectorSeconds);
	MinDowntimeAtFast = ResolveFiniteFloat(DirectorDefinition->MinDowntimeAtFast, 0.5f, 0.f, MaxDirectorSeconds);
	KillVelocityWindowSeconds = ResolveFiniteFloat(DirectorDefinition->KillVelocityWindowSeconds, 6.f, 0.1f, MaxDirectorSeconds);
	MaxGroupsPerTrigger = FMath::Clamp(DirectorDefinition->MaxGroupsPerTrigger, 1, MaxEncounterSpawnGroups);
	PostCombatDelaySeconds = ResolveFiniteFloat(DirectorDefinition->PostCombatDelaySeconds, 1.f, 0.f, MaxDirectorSeconds);
	MaxSpawnsPerTick = FMath::Clamp(DirectorDefinition->MaxSpawnsPerTick, 1, MaxSpawnsPerDirectorTick);
	MinSpawnDistanceFromPlayer = ResolveFiniteFloat(DirectorDefinition->MinSpawnDistanceFromPlayer, 0.f, 0.f, MaxWorldDistance);
	bAvoidRecentPlayerPath = DirectorDefinition->bAvoidRecentPlayerPath;
	RecentPathAvoidRadius = ResolveFiniteFloat(DirectorDefinition->RecentPathAvoidRadius, 600.f, 0.f, MaxWorldDistance);
	RecentPathSeconds = ResolveFiniteFloat(DirectorDefinition->RecentPathSeconds, 8.f, 0.1f, MaxDirectorSeconds);
	RecentPathSampleInterval = ResolveFiniteFloat(DirectorDefinition->RecentPathSampleInterval, 0.5f, 0.1f, MaxDirectorSeconds);
	RecentPathMaxSamples = FMath::Clamp(DirectorDefinition->RecentPathMaxSamples, 1, MaxRecentPathSamples);
	bAvoidPlayerForwardSpawnCone = DirectorDefinition->bAvoidPlayerForwardSpawnCone;
	ForwardSpawnConeDegrees = ResolveFiniteFloat(DirectorDefinition->ForwardSpawnConeDegrees, 120.f, 0.f, 180.f);
	bUseLineOfSightForForwardCone = DirectorDefinition->bUseLineOfSightForForwardCone;
	SpawnLocationSearchAttempts = FMath::Clamp(DirectorDefinition->SpawnLocationSearchAttempts, 1, MaxSpawnAttempts);
	GroundTraceUpOffset = ResolveFiniteFloat(DirectorDefinition->GroundTraceUpOffset, 120.f, 0.f, MaxWorldDistance);
	GroundTraceDownDistance = ResolveFiniteFloat(DirectorDefinition->GroundTraceDownDistance, 2000.f, 10.f, MaxWorldDistance);
	SpawnGroundOffset = ResolveFiniteFloat(DirectorDefinition->SpawnGroundOffset, 5.f, 0.f, MaxWorldDistance);
	RiftMinimumSpawnDistanceFromPlayers = ResolveFiniteFloat(DirectorDefinition->RiftMinimumSpawnDistanceFromPlayers, 1200.f, 0.f, MaxWorldDistance);
	RiftPressureEvaluationInterval = ResolveFiniteFloat(DirectorDefinition->RiftPressureEvaluationInterval, 2.f, 0.1f, MaxDirectorSeconds);
	RiftMinimumActiveEnemyPressure = FMath::Clamp(DirectorDefinition->RiftMinimumActiveEnemyPressure, 0, MaxEncounterPopulation);
	bRiftPreferHiddenSpawnLocations = DirectorDefinition->bRiftPreferHiddenSpawnLocations;
	RiftRegionStagingDistance = ResolveFiniteFloat(DirectorDefinition->RiftRegionStagingDistance, 6500.f, 0.f, MaxWorldDistance);
	RiftAmbientEnemyFraction = ResolveFiniteFloat(DirectorDefinition->RiftAmbientEnemyFraction, 0.33f, 0.f, 1.f);
	RiftRevealBatchSize = FMath::Clamp(DirectorDefinition->RiftRevealBatchSize, 1, MaxSpawnsPerDirectorTick);
	RiftRevealBatchInterval = ResolveFiniteFloat(DirectorDefinition->RiftRevealBatchInterval, 0.15f, 0.f, MaxDirectorSeconds);
	RiftRevealDurationSeconds = ResolveFiniteFloat(DirectorDefinition->RiftRevealDurationSeconds, 1.f, 0.f, MaxDirectorSeconds);
	RiftGroundRevealWeight = ResolveFiniteFloat(DirectorDefinition->RiftGroundRevealWeight, 0.75f, 0.f, MaxRuntimeMultiplier);
	RiftSkyRevealWeight = ResolveFiniteFloat(DirectorDefinition->RiftSkyRevealWeight, 0.25f, 0.f, MaxRuntimeMultiplier);
	RiftPrewarmActorsPerTick = FMath::Clamp(DirectorDefinition->RiftPrewarmActorsPerTick, 1, MaxSpawnsPerDirectorTick);
	RiftPrewarmWorkMillisecondsPerTick = ResolveFiniteFloat(DirectorDefinition->RiftPrewarmWorkMillisecondsPerTick, 4.f, 0.f, MaxDirectorWorkMilliseconds);
	RiftPrewarmReplicationSettleSeconds = ResolveFiniteFloat(DirectorDefinition->RiftPrewarmReplicationSettleSeconds, 1.f, 0.f, MaxDirectorSeconds);
	RiftEnemyWakeDistance = ResolveFiniteFloat(DirectorDefinition->RiftEnemyWakeDistance, 8000.f, 0.f, MaxWorldDistance);
	RiftEnemySleepDistance = ResolveFiniteFloat(DirectorDefinition->RiftEnemySleepDistance, 10000.f, RiftEnemyWakeDistance, MaxWorldDistance);
	RiftMaximumAwakeEnemies = FMath::Clamp(DirectorDefinition->RiftMaximumAwakeEnemies, 1, MaxEncounterPopulation);
	RiftPressureRadius = ResolveFiniteFloat(DirectorDefinition->RiftPressureRadius, 8000.f, 0.f, MaxWorldDistance);
	bEnableEnemyLODThrottling = DirectorDefinition->bEnableEnemyLODThrottling;
	EnemyLODUpdateInterval = ResolveFiniteFloat(DirectorDefinition->EnemyLODUpdateInterval, 0.5f, 0.05f, MaxDirectorSeconds);
	EnemyLODNearDistance = ResolveFiniteFloat(DirectorDefinition->EnemyLODNearDistance, 4000.f, 0.f, MaxWorldDistance);
	EnemyLODMidDistance = ResolveFiniteFloat(DirectorDefinition->EnemyLODMidDistance, 8000.f, EnemyLODNearDistance, MaxWorldDistance);
	EnemyLODFarDistance = ResolveFiniteFloat(DirectorDefinition->EnemyLODFarDistance, 12000.f, EnemyLODMidDistance, MaxWorldDistance);
	EnemyLODMidTickInterval = ResolveFiniteFloat(DirectorDefinition->EnemyLODMidTickInterval, 0.1f, 0.f, MaxDirectorSeconds);
	EnemyLODFarTickInterval = ResolveFiniteFloat(DirectorDefinition->EnemyLODFarTickInterval, 0.25f, 0.f, MaxDirectorSeconds);
	bEnableFixedClusterSleeping = DirectorDefinition->bEnableFixedClusterSleeping;
	FixedClusterSleepDistance = ResolveFiniteFloat(DirectorDefinition->FixedClusterSleepDistance, 14000.f, 0.f, MaxWorldDistance);
	FixedClusterWakeDistance = ResolveFiniteFloat(DirectorDefinition->FixedClusterWakeDistance, 11000.f, 0.f, FixedClusterSleepDistance);
	bDrawDebug = DirectorDefinition->bDrawDebug;
	DebugLogIntervalSeconds = ResolveFiniteFloat(DirectorDefinition->DebugLogIntervalSeconds, 1.f, 0.1f, MaxDirectorSeconds);

	UE_LOG(LogEncounterDirector, Display, TEXT("EncounterDirector %s applied DirectorDefinition=%s SpawnGroups=%d."),
		*GetNameSafe(this),
		*GetNameSafe(DirectorDefinition),
		SpawnGroups.Num());
}

void AAeyerjiEncounterDirector::SanitizeRuntimeSettings()
{
	if (SpawnGroups.Num() > MaxEncounterSpawnGroups)
	{
		SpawnGroups.SetNum(MaxEncounterSpawnGroups, EAllowShrinking::No);
	}
	TickIntervalSeconds = ResolveFiniteFloat(TickIntervalSeconds, 0.2f, 0.f, MaxDirectorSeconds);
	MinDistanceBetweenEncounters = ResolveFiniteFloat(MinDistanceBetweenEncounters, 1200.f, 0.f, MaxWorldDistance);
	KillVelocitySpawnFloor = ResolveFiniteFloat(KillVelocitySpawnFloor, 0.25f, 0.f, MaxRuntimeMultiplier);
	KillVelocitySpawnCeil = ResolveFiniteFloat(KillVelocitySpawnCeil, 1.5f, 0.01f, MaxRuntimeMultiplier);
	MinDistanceAtSlow = ResolveFiniteFloat(MinDistanceAtSlow, 1800.f, 0.f, MaxWorldDistance);
	MinDistanceAtFast = ResolveFiniteFloat(MinDistanceAtFast, 900.f, 0.f, MaxWorldDistance);
	MinDowntimeAtSlow = ResolveFiniteFloat(MinDowntimeAtSlow, 2.f, 0.f, MaxDirectorSeconds);
	MinDowntimeAtFast = ResolveFiniteFloat(MinDowntimeAtFast, 0.5f, 0.f, MaxDirectorSeconds);
	KillVelocityWindowSeconds = ResolveFiniteFloat(KillVelocityWindowSeconds, 6.f, 0.1f, MaxDirectorSeconds);
	MaxGroupsPerTrigger = FMath::Clamp(MaxGroupsPerTrigger, 1, MaxEncounterSpawnGroups);
	PostCombatDelaySeconds = ResolveFiniteFloat(PostCombatDelaySeconds, 1.f, 0.f, MaxDirectorSeconds);
	MaxSpawnsPerTick = FMath::Clamp(MaxSpawnsPerTick, 1, MaxSpawnsPerDirectorTick);
	MaxSpawnWorkMillisecondsPerTick = ResolveFiniteFloat(MaxSpawnWorkMillisecondsPerTick, 2.f, 0.f, MaxDirectorWorkMilliseconds);
	MinSpawnDistanceFromPlayer = ResolveFiniteFloat(MinSpawnDistanceFromPlayer, 0.f, 0.f, MaxWorldDistance);
	RecentPathAvoidRadius = ResolveFiniteFloat(RecentPathAvoidRadius, 600.f, 0.f, MaxWorldDistance);
	RecentPathSeconds = ResolveFiniteFloat(RecentPathSeconds, 8.f, 0.1f, MaxDirectorSeconds);
	RecentPathSampleInterval = ResolveFiniteFloat(RecentPathSampleInterval, 0.5f, 0.1f, MaxDirectorSeconds);
	RecentPathMaxSamples = FMath::Clamp(RecentPathMaxSamples, 1, MaxRecentPathSamples);
	ForwardSpawnConeDegrees = ResolveFiniteFloat(ForwardSpawnConeDegrees, 120.f, 0.f, 180.f);
	SpawnLocationSearchAttempts = FMath::Clamp(SpawnLocationSearchAttempts, 1, MaxSpawnAttempts);
	GroundTraceUpOffset = ResolveFiniteFloat(GroundTraceUpOffset, 120.f, 0.f, MaxWorldDistance);
	GroundTraceDownDistance = ResolveFiniteFloat(GroundTraceDownDistance, 2000.f, 10.f, MaxWorldDistance);
	SpawnGroundOffset = ResolveFiniteFloat(SpawnGroundOffset, 5.f, 0.f, MaxWorldDistance);
	RiftMinimumSpawnDistanceFromPlayers = ResolveFiniteFloat(RiftMinimumSpawnDistanceFromPlayers, 1200.f, 0.f, MaxWorldDistance);
	RiftPressureEvaluationInterval = ResolveFiniteFloat(RiftPressureEvaluationInterval, 2.f, 0.1f, MaxDirectorSeconds);
	RiftMinimumActiveEnemyPressure = FMath::Clamp(RiftMinimumActiveEnemyPressure, 0, MaxEncounterPopulation);
	RiftRegionStagingDistance = ResolveFiniteFloat(RiftRegionStagingDistance, 6500.f, 0.f, MaxWorldDistance);
	RiftAmbientEnemyFraction = ResolveFiniteFloat(RiftAmbientEnemyFraction, 0.33f, 0.f, 1.f);
	RiftRevealBatchSize = FMath::Clamp(RiftRevealBatchSize, 1, MaxSpawnsPerDirectorTick);
	RiftRevealBatchInterval = ResolveFiniteFloat(RiftRevealBatchInterval, 0.15f, 0.f, MaxDirectorSeconds);
	RiftRevealDurationSeconds = ResolveFiniteFloat(RiftRevealDurationSeconds, 1.f, 0.f, MaxDirectorSeconds);
	RiftGroundRevealWeight = ResolveFiniteFloat(RiftGroundRevealWeight, 0.75f, 0.f, MaxRuntimeMultiplier);
	RiftSkyRevealWeight = ResolveFiniteFloat(RiftSkyRevealWeight, 0.25f, 0.f, MaxRuntimeMultiplier);
	RiftPrewarmActorsPerTick = FMath::Clamp(RiftPrewarmActorsPerTick, 1, MaxSpawnsPerDirectorTick);
	RiftPrewarmWorkMillisecondsPerTick = ResolveFiniteFloat(RiftPrewarmWorkMillisecondsPerTick, 4.f, 0.f, MaxDirectorWorkMilliseconds);
	RiftPrewarmReplicationSettleSeconds = ResolveFiniteFloat(RiftPrewarmReplicationSettleSeconds, 1.f, 0.f, MaxDirectorSeconds);
	RiftEnemyWakeDistance = ResolveFiniteFloat(RiftEnemyWakeDistance, 8000.f, 0.f, MaxWorldDistance);
	RiftEnemySleepDistance = ResolveFiniteFloat(RiftEnemySleepDistance, 10000.f, RiftEnemyWakeDistance, MaxWorldDistance);
	RiftMaximumAwakeEnemies = FMath::Clamp(RiftMaximumAwakeEnemies, 1, MaxEncounterPopulation);
	RiftPressureRadius = ResolveFiniteFloat(RiftPressureRadius, 8000.f, 0.f, MaxWorldDistance);
	EnemyLODUpdateInterval = ResolveFiniteFloat(EnemyLODUpdateInterval, 0.5f, 0.05f, MaxDirectorSeconds);
	EnemyLODNearDistance = ResolveFiniteFloat(EnemyLODNearDistance, 4000.f, 0.f, MaxWorldDistance);
	EnemyLODMidDistance = ResolveFiniteFloat(EnemyLODMidDistance, 8000.f, EnemyLODNearDistance, MaxWorldDistance);
	EnemyLODFarDistance = ResolveFiniteFloat(EnemyLODFarDistance, 12000.f, EnemyLODMidDistance, MaxWorldDistance);
	EnemyLODMidTickInterval = ResolveFiniteFloat(EnemyLODMidTickInterval, 0.1f, 0.f, MaxDirectorSeconds);
	EnemyLODFarTickInterval = ResolveFiniteFloat(EnemyLODFarTickInterval, 0.25f, 0.f, MaxDirectorSeconds);
	FixedClusterSleepDistance = ResolveFiniteFloat(FixedClusterSleepDistance, 14000.f, 0.f, MaxWorldDistance);
	FixedClusterWakeDistance = ResolveFiniteFloat(FixedClusterWakeDistance, 11000.f, 0.f, FixedClusterSleepDistance);
	DebugLogIntervalSeconds = ResolveFiniteFloat(DebugLogIntervalSeconds, 1.f, 0.1f, MaxDirectorSeconds);
}

FString AAeyerjiEncounterDirector::GetEncounterDirectorDebugString() const
{
	int32 PlannedRegions = 0;
	int32 StagedRegions = 0;
	int32 RevealingRegions = 0;
	int32 ActiveRegions = 0;
	int32 RetiredRegions = 0;
	for (const FRiftRegionPlan& Plan : RiftRegionPlans)
	{
		switch (Plan.State)
		{
		case FRiftRegionPlan::EState::Planned: ++PlannedRegions; break;
		case FRiftRegionPlan::EState::Staged: ++StagedRegions; break;
		case FRiftRegionPlan::EState::Revealing: ++RevealingRegions; break;
		case FRiftRegionPlan::EState::Active: ++ActiveRegions; break;
		case FRiftRegionPlan::EState::Retired: ++RetiredRegions; break;
		}
	}
	const AAeyerjiSpawnerGroup* RiftSpawner = RiftPopulationSpawner.Get();
	return FString::Printf(
		TEXT("EncounterDirector=%s Definition=%s State=%s Groups=%d Active=%d Progress=%d/%d BossSpawned=%d FixedActive=%d FixedRemaining=%d RiftSerial=%d Prewarm=%d Remaining=%d Frontier=%d Regions=%d/%d/%d/%d/%d Pool=%d Checkout=%d Emergency=%d"),
		*GetNameSafe(this),
		*GetNameSafe(DirectorDefinition),
		*StaticEnum<EEncounterDirectorState>()->GetNameStringByValue(static_cast<int64>(DirectorState)),
		SpawnGroups.Num(),
		ActiveEnemyCount,
		KilledCount,
		TotalToKill,
		bBossSpawned ? 1 : 0,
		bFixedPopulationActive ? 1 : 0,
		FixedPopulationRemaining,
		RiftRegionRunSerial,
		bRiftPopulationPrewarmComplete ? 1 : 0,
		RiftPrewarmQueue.Num(),
		HighestRiftProgressionIndex,
		PlannedRegions,
		StagedRegions,
		RevealingRegions,
		ActiveRegions,
		RetiredRegions,
		RiftSpawner ? RiftSpawner->GetInactivePooledEnemyCount() : 0,
		RiftSpawner ? RiftSpawner->GetPooledCheckoutCount() : 0,
		RiftSpawner ? RiftSpawner->GetEmergencyRuntimeSpawnCount() : 0);
}

void AAeyerjiEncounterDirector::RegisterExternalEnemy(AEnemyParentNative* Enemy, bool bEnterCombatState)
{
	if (!HasAuthority() || !IsValid(Enemy) || Enemy->GetWorld() != GetWorld())
	{
		return;
	}

	for (const TWeakObjectPtr<AActor>& Tracked : LiveEnemies)
	{
		if (Tracked.Get() == Enemy)
		{
			return;
		}
	}

	RegisterSpawnedEnemy(Enemy);

	if (UWorld* World = GetWorld())
	{
		LastEncounterTimestamp = World->GetTimeSeconds();
	}

	if (bEnterCombatState)
	{
		LastEncounterLocation = Enemy->GetActorLocation();
		EnterState(EEncounterDirectorState::InCombat);
	}
}

void AAeyerjiEncounterDirector::RegisterProgressEnemy(AEnemyParentNative* Enemy, const int32 ProgressPoints, const int32 RunSerial)
{
	if (!HasAuthority() || !IsValid(Enemy) || Enemy->GetWorld() != GetWorld())
	{
		return;
	}

	const TWeakObjectPtr<AActor> EnemyKey(Enemy);
	if (RegisteredProgressEnemyPoints.Contains(EnemyKey))
	{
		UE_LOG(LogEncounterDirector, Verbose,
			TEXT("[RiftRun][Registration] Duplicate ignored RunSerial=%d Enemy=%s"),
			WeightedProgressRunSerial, *GetNameSafe(Enemy));
		return;
	}
	if (WeightedProgressRunSerial > 0)
	{
		if (bWeightedProgressFrozen || (RunSerial > 0 && RunSerial != WeightedProgressRunSerial))
		{
			return;
		}
		RegisteredProgressEnemyPoints.Add(EnemyKey, FMath::Clamp(ProgressPoints, 1, MaxProgressPoints));
	}
	else
	{
		for (const TWeakObjectPtr<AActor>& Tracked : ProgressOnlyEnemies)
		{
			if (Tracked.Get() == Enemy)
			{
				return;
			}
		}
	}

	ProgressOnlyEnemies.Add(Enemy);
	if (WeightedProgressRunSerial > 0)
	{
		RegisterSpawnedEnemy(Enemy);
	}
	Enemy->OnEnemyDied.RemoveDynamic(this, &AAeyerjiEncounterDirector::HandleProgressEnemyDied);
	Enemy->OnEnemyDied.AddDynamic(this, &AAeyerjiEncounterDirector::HandleProgressEnemyDied);
	Enemy->OnDestroyed.RemoveDynamic(this, &AAeyerjiEncounterDirector::HandleProgressEnemyDestroyed);
	Enemy->OnDestroyed.AddDynamic(this, &AAeyerjiEncounterDirector::HandleProgressEnemyDestroyed);
	UE_LOG(LogEncounterDirector, Verbose,
		TEXT("[RiftRun][Registration] RunSerial=%d Enemy=%s Points=%d Registered=%d"),
		WeightedProgressRunSerial, *GetNameSafe(Enemy), FMath::Clamp(ProgressPoints, 1, MaxProgressPoints),
		RegisteredProgressEnemyPoints.Num());
}

void AAeyerjiEncounterDirector::BeginWeightedProgressRun(const int32 RunSerial, const int32 ProgressTargetPoints)
{
	if (!HasAuthority() || RunSerial <= 0 || ProgressTargetPoints <= 0)
	{
		return;
	}

	for (const TWeakObjectPtr<AActor>& EnemyPtr : ProgressOnlyEnemies)
	{
		if (AActor* Enemy = EnemyPtr.Get())
		{
			if (AEnemyParentNative* TypedEnemy = Cast<AEnemyParentNative>(Enemy))
			{
				TypedEnemy->OnEnemyDied.RemoveDynamic(this, &AAeyerjiEncounterDirector::HandleProgressEnemyDied);
			}
			Enemy->OnDestroyed.RemoveDynamic(this, &AAeyerjiEncounterDirector::HandleProgressEnemyDestroyed);
		}
	}
	ProgressOnlyEnemies.Reset();
	RegisteredProgressEnemyPoints.Reset();
	WeightedProgressRunSerial = FMath::Clamp(RunSerial, 1, MAX_int32);
	bWeightedProgressFrozen = false;
	EnemiesDefeated = 0;
	WeightedProgressPoints = 0;
	WeightedProgressTarget = FMath::Clamp(ProgressTargetPoints, 1, MaxProgressPoints);
	KilledCount = 0;
	TotalToKill = WeightedProgressTarget;
	bBossSpawned = false;
	HandleProgressChanged();
	UE_LOG(LogEncounterDirector, Display,
		TEXT("[RiftRun][Progress] Began RunSerial=%d Target=%d"), RunSerial, WeightedProgressTarget);
}

bool AAeyerjiEncounterDirector::BeginRiftRegionRun(
	const int32 RunSerial,
	const int32 RunSeed,
	const int32 ProgressTargetPoints,
	const int32 EnemyBudget,
	const float ActivationDistance,
	const float DensityMultiplier,
	const float EliteRateMultiplier,
	const float EncounterSizeMultiplier,
	const float ProgressMultiplier,
	AAeyerjiSpawnerGroup* SpawnManager,
	AAeyerjiLevelDirector* LevelDirector,
	FString& OutReason)
{
	OutReason.Reset();
	if (!HasAuthority()
		|| RunSerial <= 0
		|| ProgressTargetPoints <= 0
		|| EnemyBudget <= 0
		|| !FMath::IsFinite(ActivationDistance)
		|| !FMath::IsFinite(DensityMultiplier)
		|| !FMath::IsFinite(EliteRateMultiplier)
		|| !FMath::IsFinite(EncounterSizeMultiplier)
		|| !FMath::IsFinite(ProgressMultiplier))
	{
		OutReason = TEXT("Invalid Rift region run serial, target, or enemy budget");
		return false;
	}
	if ((IsValid(SpawnManager) && SpawnManager->GetWorld() != GetWorld())
		|| (IsValid(LevelDirector) && LevelDirector->GetWorld() != GetWorld()))
	{
		OutReason = TEXT("Rift population executor and level director must belong to this world");
		return false;
	}

	ResetRiftRegionRun();
	RiftRegionRunSerial = FMath::Clamp(RunSerial, 1, MAX_int32);
	RiftRegionActivationDistance = ResolveFiniteFloat(ActivationDistance, 2500.f, 0.f, MaxWorldDistance);
	RiftEliteRateMultiplier = ResolveFiniteFloat(EliteRateMultiplier, 1.f, 0.f, MaxRuntimeMultiplier);
	RiftProgressMultiplier = ResolveFiniteFloat(ProgressMultiplier, 1.f, 0.1f, MaxRuntimeMultiplier);
	NextRiftPressureEvaluationTime = 0.0;
	RiftSpawnStream.Initialize(RunSeed != 0 ? RunSeed : RunSerial);
	// Never repurpose the boss executor as the common population manager. A missing
	// or conflicting world-population reference is safely replaced at runtime.
	RiftPopulationSpawner = LevelDirector && SpawnManager == LevelDirector->BossSpawner
		? nullptr
		: SpawnManager;
	if (SpawnManager && !RiftPopulationSpawner.IsValid())
	{
		UE_LOG(LogEncounterDirector, Warning,
			TEXT("[RiftRun][RegionPlan] Ignoring boss spawner %s as the population executor; creating a private runtime executor"),
			*GetNameSafe(SpawnManager));
	}
	RiftLevelDirector = LevelDirector;

	TArray<const UEnemySpawnGroupDefinition*> ValidGroups;
	for (const UEnemySpawnGroupDefinition* Group : SpawnGroups)
	{
		if (ValidGroups.Num() >= MaxEncounterSpawnGroups)
		{
			break;
		}
		if (IsValid(Group) && !Group->EnemyTypes.IsEmpty())
		{
			ValidGroups.Add(Group);
		}
	}
	ValidGroups.Sort([](const UEnemySpawnGroupDefinition& Left, const UEnemySpawnGroupDefinition& Right)
	{
		return Left.GetPathName() < Right.GetPathName();
	});
	UWorld* World = GetWorld();
	if (!World)
	{
		OutReason = TEXT("Rift region planning has no world");
		ResetRiftRegionRun();
		return false;
	}

	for (TActorIterator<AAeyerjiSpawnRegion> It(World); It; ++It)
	{
		if (RiftRegionPlans.Num() >= MaxRiftRegions)
		{
			break;
		}
		AAeyerjiSpawnRegion* Region = *It;
		if (!IsValid(Region) || !Region->IsRiftEncounterEligible())
		{
			continue;
		}

		FRiftRegionPlan Plan;
		Plan.Region = Region;
		Plan.Bounds = Region->GetRegionBounds();
		if (!IsFiniteBox(Plan.Bounds))
		{
			continue;
		}
		Plan.StableKey = Region->GetPathName();
		Plan.Weight = ResolveFiniteFloat(Region->RegionWeight, 0.f, 0.f, MaxRuntimeMultiplier);
		Plan.EncounterGroup = Region->RiftEncounterGroup;
		Plan.ProgressionIndex = Region->RiftProgressionIndex;
		if (Plan.ProgressionIndex < 0 || Plan.ProgressionIndex >= MaxRiftRegions)
		{
			OutReason = FString::Printf(
				TEXT("Rift SpawnRegion %s is missing a non-negative RiftProgressionIndex"),
				*Plan.StableKey);
			ResetRiftRegionRun();
			return false;
		}
		if (Plan.EncounterGroup.IsValid() && Plan.EncounterGroup->EnemyTypes.IsEmpty())
		{
			OutReason = FString::Printf(TEXT("Encounter anchor %s references empty group %s"),
				*Plan.StableKey, *GetNameSafe(Plan.EncounterGroup.Get()));
			ResetRiftRegionRun();
			return false;
		}
		RiftRegionPlans.Add(MoveTemp(Plan));
	}
	RiftRegionPlans.Sort([](const FRiftRegionPlan& Left, const FRiftRegionPlan& Right)
	{
		if (Left.ProgressionIndex != Right.ProgressionIndex)
		{
			return Left.ProgressionIndex < Right.ProgressionIndex;
		}
		return Left.StableKey < Right.StableKey;
	});
	if (RiftRegionPlans.IsEmpty())
	{
		OutReason = TEXT("No eligible AAeyerjiSpawnRegion actors were discovered; untag regions or remove Rift.Excluded");
		ResetRiftRegionRun();
		return false;
	}
	if (ValidGroups.IsEmpty())
	{
		bool bEveryAnchorHasGroup = true;
		for (const FRiftRegionPlan& Plan : RiftRegionPlans)
		{
			bEveryAnchorHasGroup &= Plan.EncounterGroup.IsValid();
		}
		if (!bEveryAnchorHasGroup)
		{
			OutReason = TEXT("EncounterDirectorDefinition has no fallback SpawnGroups and an encounter anchor has no authored RiftEncounterGroup");
			ResetRiftRegionRun();
			return false;
		}
	}

	TArray<float> RegionWeights;
	RegionWeights.Reserve(RiftRegionPlans.Num());
	for (const FRiftRegionPlan& Plan : RiftRegionPlans)
	{
		RegionWeights.Add(Plan.Weight);
	}
	const int32 SafeEnemyBudget = FMath::Clamp(EnemyBudget, 1, MaxEncounterPopulation);
	const double SafeDensityMultiplier = FMath::Clamp(static_cast<double>(DensityMultiplier), 0.1, static_cast<double>(MaxRuntimeMultiplier));
	const double SafeEncounterSizeMultiplier = FMath::Clamp(static_cast<double>(EncounterSizeMultiplier), 0.1, static_cast<double>(MaxRuntimeMultiplier));
	const double EffectiveBudgetValue = FMath::Clamp(
		static_cast<double>(SafeEnemyBudget) * SafeDensityMultiplier * SafeEncounterSizeMultiplier,
		1.0,
		static_cast<double>(MaxEncounterPopulation));
	const int32 EffectiveEnemyBudget = static_cast<int32>(FMath::RoundToDouble(EffectiveBudgetValue));
	const int32 SafeProgressTarget = FMath::Clamp(ProgressTargetPoints, 1, MaxProgressPoints);
	const TArray<int32> RegionBudgets = AeyerjiRiftRules::AllocateLargestRemainder(RegionWeights, EffectiveEnemyBudget);
	RiftReservedRegionRequests.SetNum(RiftRegionPlans.Num());

	const UEnemySpawnGroupDefinition* LastPlannedGroup = nullptr;
	int64 PlannedProgressPoints = 0;
	for (int32 RegionIndex = 0; RegionIndex < RiftRegionPlans.Num(); ++RegionIndex)
	{
		FRiftRegionPlan& Plan = RiftRegionPlans[RegionIndex];
		Plan.Budget = RegionBudgets.IsValidIndex(RegionIndex) ? RegionBudgets[RegionIndex] : 0;
		if (Plan.Budget <= 0)
		{
			continue;
		}
		if (!ResolveRiftRegionAnchor(Plan.Bounds, Plan.Anchor))
		{
			OutReason = FString::Printf(TEXT("SpawnRegion %s has no navigable point inside its bounds"), *Plan.StableKey);
			ResetRiftRegionRun();
			return false;
		}

		TArray<FRiftSpawnRequest>& ReservedRequests = RiftReservedRegionRequests[RegionIndex];
		ReservedRequests.Reserve(Plan.Budget);
		for (int32 SpawnIndex = 0; SpawnIndex < Plan.Budget; ++SpawnIndex)
		{
			const UEnemySpawnGroupDefinition* Group = Plan.EncounterGroup.Get();
			for (int32 Attempt = 0; !Group && Attempt < ValidGroups.Num() * 2; ++Attempt)
			{
				const UEnemySpawnGroupDefinition* Candidate = ValidGroups[RiftSpawnStream.RandRange(0, ValidGroups.Num() - 1)];
				if (Candidate->bAllowBackToBackSelection || Candidate != LastPlannedGroup || ValidGroups.Num() == 1)
				{
					Group = Candidate;
					break;
				}
			}
			Group = Group ? Group : ValidGroups[0];
			LastPlannedGroup = Group;

			const AAeyerjiSpawnRegion* Region = Plan.Region.Get();
			const bool bCanUseElitePool = Region && Region->bAllowElites && !Group->EliteEnemyTypes.IsEmpty();
			const float GroupEliteChance = ResolveFiniteFloat(Group->RiftEliteChance, 0.f, 0.f, 1.f);
			const float RegionEliteBonus = Region
				? ResolveFiniteFloat(Region->EliteChanceBonus, 0.f, -1.f, 1.f)
				: 0.f;
			const float EliteChance = Region
				? static_cast<float>(FMath::Clamp(
					(static_cast<double>(GroupEliteChance) + RegionEliteBonus) * RiftEliteRateMultiplier,
					0.0,
					1.0))
				: 0.f;
			const bool bPlanElite = bCanUseElitePool && RiftSpawnStream.FRand() <= EliteChance;
			const TArray<TSubclassOf<AEnemyParentNative>>& ClassPool = bPlanElite
				? Group->EliteEnemyTypes
				: Group->EnemyTypes;
			TArray<TSubclassOf<AEnemyParentNative>> ValidClasses;
			for (const TSubclassOf<AEnemyParentNative> EnemyClass : ClassPool)
			{
				if (ValidClasses.Num() >= MaxEnemyClassesPerGroup)
				{
					break;
				}
				if (*EnemyClass)
				{
					ValidClasses.Add(EnemyClass);
				}
			}
			if (ValidClasses.IsEmpty())
			{
				OutReason = FString::Printf(TEXT("Rift spawn group %s resolved an empty %s class pool"),
					*GetNameSafe(Group), bPlanElite ? TEXT("elite") : TEXT("ordinary"));
				ResetRiftRegionRun();
				return false;
			}

			FRiftSpawnRequest& Request = ReservedRequests.AddDefaulted_GetRef();
			Request.RegionPlanIndex = RegionIndex;
			Request.EnemyClass = ValidClasses[RiftSpawnStream.RandRange(0, ValidClasses.Num() - 1)];
			Request.bIsElite = bPlanElite;
			const int32 BaseProgressPoints = FMath::Clamp(
				bPlanElite ? Group->RiftEliteProgressPoints : Group->RiftProgressPoints,
				1,
				MaxProgressPoints);
			const double ResolvedProgressPoints = FMath::Clamp(
				static_cast<double>(BaseProgressPoints) * RiftProgressMultiplier,
				1.0,
				static_cast<double>(MaxProgressPoints));
			Request.ProgressPoints = static_cast<int32>(FMath::RoundToDouble(ResolvedProgressPoints));
			PlannedProgressPoints = FMath::Min<int64>(
				static_cast<int64>(MaxProgressPoints),
				PlannedProgressPoints + Request.ProgressPoints);
			Plan.ReservedProgress = FMath::Min(MaxProgressPoints, Plan.ReservedProgress + Request.ProgressPoints);
		}

		UE_LOG(LogEncounterDirector, Display,
			TEXT("[RiftRun][EncounterPlan] RunSerial=%d Seed=%d Anchor=%s Index=%d EncounterGroup=%s Weight=%.3f Budget=%d ReservedProgress=%d"),
			RunSerial, RunSeed, *Plan.StableKey, Plan.ProgressionIndex, *GetNameSafe(Plan.EncounterGroup.Get()), Plan.Weight,
			Plan.Budget, Plan.ReservedProgress);
	}

	const double ReserveRatio = static_cast<double>(PlannedProgressPoints) / static_cast<double>(SafeProgressTarget);
	if (ReserveRatio < 1.2)
	{
		OutReason = FString::Printf(TEXT("Reserved progress %lld is below 120%% of target %d"),
			PlannedProgressPoints, SafeProgressTarget);
		ResetRiftRegionRun();
		return false;
	}
	if (ReserveRatio > 1.3)
	{
		UE_LOG(LogEncounterDirector, Warning,
			TEXT("[RiftRun][RegionPlan] RunSerial=%d reserved progress %lld is %.1f%% of target %d (above 130%%)"),
			RunSerial, PlannedProgressPoints, ReserveRatio * 100.0, SafeProgressTarget);
	}

	if (!RiftPopulationSpawner.IsValid())
	{
		const FTransform SpawnTransform = GetActorTransform();
		AAeyerjiSpawnerGroup* Spawned = World->SpawnActorDeferred<AAeyerjiSpawnerGroup>(
			AAeyerjiSpawnerGroup::StaticClass(),
			SpawnTransform,
			this,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Spawned)
		{
			Spawned->bDisableActivationVolume = true;
			Spawned->bDisableActivationEvent = true;
			Spawned->bSuppressDoorControl = true;
			Spawned->bAllowManualActivationWithoutWaves = true;
			Spawned->PoolSettings.bEnablePooling = true;
			Spawned->PoolSettings.bPrewarmDuringWorldFlowLoading = true;
			UGameplayStatics::FinishSpawningActor(Spawned, SpawnTransform);
			RiftPopulationSpawner = Spawned;
			bSpawnedRiftPopulationSpawner = true;
		}
	}
	AAeyerjiSpawnerGroup* ResolvedSpawner = RiftPopulationSpawner.Get();
	if (!ResolvedSpawner)
	{
		OutReason = TEXT("No common SpawnerGroup is available for Rift region execution");
		ResetRiftRegionRun();
		return false;
	}
	ResolvedSpawner->ResetEncounter();
	ResolvedSpawner->ConfigureAsRiftPopulationExecutor(LevelDirector);
	ResolvedSpawner->BeginExactPoolPrewarm(EffectiveEnemyBudget);

	RiftPrewarmQueue.Reset();
	RiftPrewarmQueue.Reserve(EffectiveEnemyBudget);
	for (const TArray<FRiftSpawnRequest>& ReservedRequests : RiftReservedRegionRequests)
	{
		for (const FRiftSpawnRequest& Request : ReservedRequests)
		{
			FEnemySet& EnemyTemplate = RiftPrewarmQueue.AddDefaulted_GetRef();
			EnemyTemplate.EnemyClass = Request.EnemyClass;
			EnemyTemplate.Count = 1;
			EnemyTemplate.ProgressPoints = FMath::Max(Request.ProgressPoints, 1);
			EnemyTemplate.SpawnInterval = 0.f;
			EnemyTemplate.EnemyArchetypeTag = ResolveArchetypeTagFromClass(Request.EnemyClass);
			EnemyTemplate.bIsElite = Request.bIsElite;
		}
	}

	BeginWeightedProgressRun(RunSerial, SafeProgressTarget);
	bRiftRegionActivationEnabled = false;
	bRiftPopulationPrewarmInProgress = !RiftPrewarmQueue.IsEmpty();
	bRiftPopulationPrewarmComplete = RiftPrewarmQueue.IsEmpty();
	RiftPrewarmFinalizeTime = 0.0;
	RiftPrewarmStartPlatformSeconds = bRiftPopulationPrewarmInProgress ? FPlatformTime::Seconds() : 0.0;
	RiftPrewarmGameThreadWorkSeconds = 0.0;
	RiftPrewarmConstructionPasses = 0;
	RiftPrewarmInitialPopulation = RiftPrewarmQueue.Num();
	if (bRiftPopulationPrewarmInProgress)
	{
		// Actor/UObject construction is game-thread only, so remove the director's normal 0.2 s
		// scheduling gap while the loading screen is already gating gameplay.
		SetActorTickInterval(0.f);
	}
	HighestRiftProgressionIndex = INDEX_NONE;
	if (bRiftPopulationPrewarmComplete)
	{
		ResolvedSpawner->FinalizeExactPoolPrewarm();
		OnRiftPopulationPrewarmComplete.Broadcast(this);
	}
	UE_LOG(LogEncounterDirector, Display,
		TEXT("[RiftRun][EncounterPlan] Prewarming RunSerial=%d Seed=%d Anchors=%d BaseBudget=%d EffectiveBudget=%d ReservedProgress=%lld Target=%d ActivationDistance=%.1f StagingDistance=%.1f Density=%.2f Elite=%.2f EncounterSize=%.2f Progress=%.2f"),
		RunSerial, RunSeed, RiftRegionPlans.Num(), SafeEnemyBudget, EffectiveEnemyBudget, PlannedProgressPoints,
		SafeProgressTarget, RiftRegionActivationDistance, RiftRegionStagingDistance,
		DensityMultiplier, RiftEliteRateMultiplier,
		EncounterSizeMultiplier, RiftProgressMultiplier);
	return true;
}

void AAeyerjiEncounterDirector::ActivatePreparedRiftRun()
{
	if (!HasAuthority() || RiftRegionRunSerial <= 0 || !bRiftPopulationPrewarmComplete)
	{
		return;
	}

	bRiftRegionActivationEnabled = true;
	NextRiftPressureEvaluationTime = 0.0;
	UE_LOG(LogEncounterDirector, Display,
		TEXT("[RiftRun][EncounterPlan] Activated prewarmed population RunSerial=%d InactivePool=%d"),
		RiftRegionRunSerial,
		RiftPopulationSpawner.IsValid() ? RiftPopulationSpawner->GetInactivePooledEnemyCount() : 0);
}

void AAeyerjiEncounterDirector::StopRiftRegionActivation()
{
	if (!HasAuthority() || !bRiftRegionActivationEnabled)
	{
		return;
	}
	bRiftRegionActivationEnabled = false;
	UE_LOG(LogEncounterDirector, Display,
		TEXT("[RiftRun][RegionActivation] Disabled RunSerial=%d PendingAcceptedSpawns=%d"),
		RiftRegionRunSerial, RiftSpawnQueue.Num());
}

void AAeyerjiEncounterDirector::ResetRiftRegionRun()
{
	if (!HasAuthority())
	{
		return;
	}

	// A new run must not inherit prior Rift enemies in pressure/LOD or objective
	// ledgers. The pool executor owns their physical teardown/reuse below.
	if (WeightedProgressRunSerial > 0)
	{
		for (const TWeakObjectPtr<AActor>& EnemyPtr : ProgressOnlyEnemies)
		{
			if (AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(EnemyPtr.Get()))
			{
				Enemy->OnEnemyDied.RemoveDynamic(this, &AAeyerjiEncounterDirector::HandleProgressEnemyDied);
				Enemy->OnEnemyDied.RemoveDynamic(this, &AAeyerjiEncounterDirector::HandleTrackedEnemyDied);
				Enemy->OnDestroyed.RemoveDynamic(this, &AAeyerjiEncounterDirector::HandleProgressEnemyDestroyed);
				Enemy->OnDestroyed.RemoveDynamic(this, &AAeyerjiEncounterDirector::HandleTrackedEnemyDestroyed);
				LiveEnemies.RemoveSingleSwap(Enemy, EAllowShrinking::No);
				RemoveEnemyLODState(Enemy);
			}
		}
		ProgressOnlyEnemies.Reset();
		RegisteredProgressEnemyPoints.Reset();
		ActiveEnemyCount = LiveEnemies.Num();
		WeightedProgressRunSerial = 0;
		bWeightedProgressFrozen = false;
	}

	bRiftRegionActivationEnabled = false;
	RiftRegionPlans.Reset();
	RiftReservedRegionRequests.Reset();
	RiftSpawnQueue.Reset();
	RiftPrewarmQueue.Reset();
	RiftParticipantRegionLatch.Reset();
	RiftRegionRunSerial = 0;
	RiftRegionActivationDistance = 2500.f;
	RiftEliteRateMultiplier = 1.f;
	RiftProgressMultiplier = 1.f;
	NextRiftPressureEvaluationTime = 0.0;
	RiftPrewarmFinalizeTime = 0.0;
	HighestRiftProgressionIndex = INDEX_NONE;
	bRiftPopulationPrewarmInProgress = false;
	bRiftPopulationPrewarmComplete = false;
	SetActorTickInterval(TickIntervalSeconds);
	RiftPrewarmStartPlatformSeconds = 0.0;
	RiftPrewarmGameThreadWorkSeconds = 0.0;
	RiftPrewarmConstructionPasses = 0;
	RiftPrewarmInitialPopulation = 0;
	RiftLevelDirector.Reset();

	if (AAeyerjiSpawnerGroup* ExistingSpawner = RiftPopulationSpawner.Get())
	{
		ExistingSpawner->ReleaseEnemyPool(/*bDestroyInactiveEnemies=*/true);
	}

	if (bSpawnedRiftPopulationSpawner)
	{
		if (AAeyerjiSpawnerGroup* Spawned = RiftPopulationSpawner.Get())
		{
			Spawned->Destroy();
		}
	}
	RiftPopulationSpawner.Reset();
	bSpawnedRiftPopulationSpawner = false;
}

void AAeyerjiEncounterDirector::ProcessRiftPopulationPrewarm()
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Aeyerji_RiftPopulationPrewarm);
	SCOPE_CYCLE_COUNTER(STAT_AeyerjiRiftPopulationPrewarm);

	if (!HasAuthority() || !bRiftPopulationPrewarmInProgress || bRiftPopulationPrewarmComplete)
	{
		return;
	}

	AAeyerjiSpawnerGroup* Spawner = RiftPopulationSpawner.Get();
	UWorld* World = GetWorld();
	if (!Spawner || !World)
	{
		return;
	}

	if (RiftPrewarmQueue.IsEmpty())
	{
		if (RiftPrewarmFinalizeTime <= 0.0)
		{
			const float SettleSeconds = World->GetNetMode() == NM_Standalone
				? 0.f
				: ResolveFiniteFloat(RiftPrewarmReplicationSettleSeconds, 1.f, 0.f, MaxDirectorSeconds);
			RiftPrewarmFinalizeTime = World->GetTimeSeconds()
				+ SettleSeconds;
		}
		if (World->GetTimeSeconds() >= RiftPrewarmFinalizeTime)
		{
			FinalizeRiftPopulationPrewarm();
		}
		return;
	}

	const int32 ActorBudget = FMath::Clamp(RiftPrewarmActorsPerTick, 1, MaxSpawnsPerDirectorTick);
	const float WorkMilliseconds = ResolveFiniteFloat(
		RiftPrewarmWorkMillisecondsPerTick,
		4.f,
		0.f,
		MaxDirectorWorkMilliseconds);
	const double Deadline = FPlatformTime::Seconds()
		+ (WorkMilliseconds / 1000.0);
	const double PassStartSeconds = FPlatformTime::Seconds();
	++RiftPrewarmConstructionPasses;
	int32 Attempted = 0;
	while (Attempted < ActorBudget
		&& !RiftPrewarmQueue.IsEmpty()
		&& (Attempted == 0 || WorkMilliseconds <= 0.f || FPlatformTime::Seconds() < Deadline))
	{
		// Construction order does not affect the frozen encounter plan. Pop from the end to
		// avoid shifting the remaining array after every one of potentially hundreds of actors.
		const FEnemySet ExactEnemySet = RiftPrewarmQueue.Pop(EAllowShrinking::No);
		if (Spawner->PrewarmExactEnemy(ExactEnemySet))
		{
		}
		else
		{
			UE_LOG(LogEncounterDirector, Error,
				TEXT("[RiftRun][Prewarm] Skipped invalid exact entry RunSerial=%d Class=%s Remaining=%d"),
				RiftRegionRunSerial,
				*GetNameSafe(ExactEnemySet.EnemyClass),
				RiftPrewarmQueue.Num());
			// A bad class must not hold the loading gate forever. Runtime spawning can
			// still use the spawner's bounded emergency-construction path.
		}
		++Attempted;
	}
	RiftPrewarmGameThreadWorkSeconds += FPlatformTime::Seconds() - PassStartSeconds;
}

void AAeyerjiEncounterDirector::FinalizeRiftPopulationPrewarm()
{
	if (!HasAuthority() || bRiftPopulationPrewarmComplete)
	{
		return;
	}

	if (AAeyerjiSpawnerGroup* Spawner = RiftPopulationSpawner.Get())
	{
		Spawner->FinalizeExactPoolPrewarm();
	}
	bRiftPopulationPrewarmInProgress = false;
	bRiftPopulationPrewarmComplete = true;
	RiftPrewarmFinalizeTime = 0.0;
	SetActorTickInterval(TickIntervalSeconds);
	const double WallMilliseconds = RiftPrewarmStartPlatformSeconds > 0.0
		? (FPlatformTime::Seconds() - RiftPrewarmStartPlatformSeconds) * 1000.0
		: 0.0;
	const double WorkMilliseconds = RiftPrewarmGameThreadWorkSeconds * 1000.0;

	UE_LOG(LogEncounterDirector, Display,
		TEXT("[RiftRun][Prewarm] Complete RunSerial=%d Planned=%d Constructed=%d Inactive=%d Passes=%d WallMs=%.1f GameThreadWorkMs=%.1f"),
		RiftRegionRunSerial,
		RiftPrewarmInitialPopulation,
		RiftPopulationSpawner.IsValid() ? RiftPopulationSpawner->GetPrewarmConstructionCount() : 0,
		RiftPopulationSpawner.IsValid() ? RiftPopulationSpawner->GetInactivePooledEnemyCount() : 0,
		RiftPrewarmConstructionPasses,
		WallMilliseconds,
		WorkMilliseconds);
	OnRiftPopulationPrewarmComplete.Broadcast(this);
}

void AAeyerjiEncounterDirector::ProcessRiftRegionActivation()
{
	if (!HasAuthority() || !bRiftRegionActivationEnabled || RiftRegionRunSerial <= 0)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (bWeightedProgressFrozen || WeightedProgressPoints >= WeightedProgressTarget)
	{
		StopRiftRegionActivation();
		return;
	}

	TArray<APawn*> LivingParticipants;
	GetLivingRiftParticipants(LivingParticipants);
	if (LivingParticipants.IsEmpty())
	{
		return;
	}

	const float SafeStagingDistance = ResolveFiniteFloat(RiftRegionStagingDistance, 6500.f, 0.f, MaxWorldDistance);
	const float SafeActivationDistance = ResolveFiniteFloat(RiftRegionActivationDistance, 2500.f, 0.f, MaxWorldDistance);
	const float StagingDistance = FMath::Max(SafeStagingDistance, SafeActivationDistance);
	const float StagingDistanceSquared = FMath::Square(StagingDistance);
	int32 BestStageRegionIndex = INDEX_NONE;
	APawn* BestStageParticipant = nullptr;
	float BestStageDistanceSquared = TNumericLimits<float>::Max();
	int32 BestStageProgressionIndex = MAX_int32;
	for (int32 RegionIndex = 0; RegionIndex < RiftRegionPlans.Num(); ++RegionIndex)
	{
		const FRiftRegionPlan& Plan = RiftRegionPlans[RegionIndex];
		if (Plan.State != FRiftRegionPlan::EState::Planned
			|| Plan.Budget <= 0
			|| !AeyerjiRiftRules::CanStageProgressionIndex(
				Plan.ProgressionIndex,
				HighestRiftProgressionIndex)
			|| !Plan.Region.IsValid()
			|| !Plan.Bounds.IsValid)
		{
			continue;
		}

		for (APawn* Participant : LivingParticipants)
		{
			const float DistanceSquared = Plan.Bounds.ComputeSquaredDistanceToPoint(Participant->GetActorLocation());
			if (DistanceSquared > StagingDistanceSquared)
			{
				continue;
			}
			if (!IsRiftRegionReachableFromParticipant(Plan.Anchor, Participant))
			{
				continue;
			}

			const bool bEarlierProgression = Plan.ProgressionIndex < BestStageProgressionIndex;
			const bool bSameProgressionCloser = Plan.ProgressionIndex == BestStageProgressionIndex
				&& DistanceSquared < BestStageDistanceSquared;
			if (bEarlierProgression || bSameProgressionCloser)
			{
				BestStageRegionIndex = RegionIndex;
				BestStageProgressionIndex = Plan.ProgressionIndex;
				BestStageDistanceSquared = DistanceSquared;
				BestStageParticipant = Participant;
			}
		}
	}
	if (RiftRegionPlans.IsValidIndex(BestStageRegionIndex) && IsValid(BestStageParticipant))
	{
		TryStageRiftEncounterGroup(BestStageRegionIndex, BestStageParticipant, TEXT("Proximity"));
	}

	const float ActivationDistanceSquared = FMath::Square(SafeActivationDistance);
	int32 BestRevealRegionIndex = INDEX_NONE;
	APawn* BestRevealParticipant = nullptr;
	float BestRevealDistanceSquared = TNumericLimits<float>::Max();
	for (int32 RegionIndex = 0; RegionIndex < RiftRegionPlans.Num(); ++RegionIndex)
	{
		const FRiftRegionPlan& Plan = RiftRegionPlans[RegionIndex];
		if (Plan.State != FRiftRegionPlan::EState::Staged)
		{
			continue;
		}

		const bool bAmbientEngaged = IsRiftAmbientGroupEngaged(Plan);
		for (APawn* Participant : LivingParticipants)
		{
			const float DistanceSquared = Plan.Bounds.ComputeSquaredDistanceToPoint(Participant->GetActorLocation());
			if (!bAmbientEngaged && DistanceSquared > ActivationDistanceSquared)
			{
				continue;
			}
			if (DistanceSquared < BestRevealDistanceSquared)
			{
				BestRevealRegionIndex = RegionIndex;
				BestRevealDistanceSquared = DistanceSquared;
				BestRevealParticipant = Participant;
			}
		}
	}
	if (RiftRegionPlans.IsValidIndex(BestRevealRegionIndex) && IsValid(BestRevealParticipant))
	{
		if (TryBeginRiftEncounterReveal(
			BestRevealRegionIndex,
			BestRevealParticipant,
			IsRiftAmbientGroupEngaged(RiftRegionPlans[BestRevealRegionIndex]) ? TEXT("Engaged") : TEXT("Proximity")))
		{
			return;
		}
	}

	const double WorldTime = World->GetTimeSeconds();
	if (WorldTime < NextRiftPressureEvaluationTime)
	{
		return;
	}
	NextRiftPressureEvaluationTime = WorldTime
		+ ResolveFiniteFloat(RiftPressureEvaluationInterval, 2.f, 0.1f, MaxDirectorSeconds);
	const int32 ActivePressure = GetActiveRiftEnemyPressure();
	if (ActivePressure >= FMath::Clamp(RiftMinimumActiveEnemyPressure, 0, MaxEncounterPopulation))
	{
		return;
	}
	APawn* PressureParticipant = nullptr;
	const int32 PressurePlanIndex = FindRiftPressureActivationCandidate(PressureParticipant);
	if (RiftRegionPlans.IsValidIndex(PressurePlanIndex) && IsValid(PressureParticipant))
	{
		TryStageRiftEncounterGroup(PressurePlanIndex, PressureParticipant, TEXT("Pressure"));
	}
}

bool AAeyerjiEncounterDirector::TryStageRiftEncounterGroup(
	const int32 PlanIndex,
	APawn* Participant,
	const TCHAR* ActivationReason)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Aeyerji_RiftRegionStaging);
	SCOPE_CYCLE_COUNTER(STAT_AeyerjiRiftRegionStaging);

	if (!HasAuthority()
		|| !RiftRegionPlans.IsValidIndex(PlanIndex)
		|| !IsValid(Participant)
		|| Participant->GetWorld() != GetWorld()
		|| !RiftReservedRegionRequests.IsValidIndex(PlanIndex))
	{
		return false;
	}
	FRiftRegionPlan& Plan = RiftRegionPlans[PlanIndex];
	TArray<FRiftSpawnRequest>& ReservedRequests = RiftReservedRegionRequests[PlanIndex];
	if (Plan.State != FRiftRegionPlan::EState::Planned
		|| ReservedRequests.IsEmpty()
		|| !AeyerjiRiftRules::CanStageProgressionIndex(
			Plan.ProgressionIndex,
			HighestRiftProgressionIndex)
		|| bWeightedProgressFrozen
		|| WeightedProgressPoints >= WeightedProgressTarget)
	{
		UE_LOG(LogEncounterDirector, Verbose,
			TEXT("[RiftRun][EncounterStage] Rejected RunSerial=%d Anchor=%s Group=%s Reason=%s State=%d Frontier=%d"),
			RiftRegionRunSerial,
			*Plan.StableKey,
			*GetNameSafe(Plan.EncounterGroup.Get()),
			ActivationReason,
			static_cast<int32>(Plan.State),
			HighestRiftProgressionIndex);
		return false;
	}

	RetireAndRedistributeSkippedRiftRegions(Plan.ProgressionIndex);
	HighestRiftProgressionIndex = FMath::Max(HighestRiftProgressionIndex, Plan.ProgressionIndex);
	Plan.State = FRiftRegionPlan::EState::Staged;

	const float AmbientFraction = ResolveFiniteFloat(RiftAmbientEnemyFraction, 0.33f, 0.f, 1.f);
	const int32 AmbientTarget = FMath::Clamp(
		FMath::RoundToInt(static_cast<float>(ReservedRequests.Num()) * AmbientFraction),
		ReservedRequests.IsEmpty() ? 0 : 1,
		ReservedRequests.Num());
	int32 AmbientSpawned = 0;
	for (int32 AmbientIndex = 0; AmbientIndex < AmbientTarget && !ReservedRequests.IsEmpty();)
	{
		FRiftSpawnRequest Request = ReservedRequests[0];
		Request.RevealStyle = EAeyerjiEnemyRevealStyle::Immediate;
		AEnemyParentNative* SpawnedEnemy = nullptr;
		if (!SpawnRiftRequest(Request, &SpawnedEnemy))
		{
			break;
		}
		ReservedRequests.RemoveAt(0, 1, EAllowShrinking::No);
		if (SpawnedEnemy)
		{
			Plan.AmbientEnemies.Add(SpawnedEnemy);
		}
		++AmbientIndex;
		++AmbientSpawned;
	}

	if (AmbientSpawned <= 0)
	{
		Plan.State = FRiftRegionPlan::EState::Planned;
		return false;
	}
	if (ReservedRequests.IsEmpty())
	{
		Plan.State = FRiftRegionPlan::EState::Active;
	}

	const float ActivationDistance = FMath::Sqrt(Plan.Bounds.ComputeSquaredDistanceToPoint(Participant->GetActorLocation()));
	UE_LOG(LogEncounterDirector, Display,
		TEXT("[RiftRun][EncounterStage] RunSerial=%d Anchor=%s Index=%d Group=%s Reason=%s Participant=%s Distance=%.1f Ambient=%d Reinforcements=%d Frontier=%d"),
		RiftRegionRunSerial, *Plan.StableKey, Plan.ProgressionIndex,
		*GetNameSafe(Plan.EncounterGroup.Get()), ActivationReason,
		*GetNameSafe(Participant), ActivationDistance, AmbientSpawned, ReservedRequests.Num(),
		HighestRiftProgressionIndex);
	return true;
}

bool AAeyerjiEncounterDirector::TryBeginRiftEncounterReveal(
	const int32 PlanIndex,
	APawn* Participant,
	const TCHAR* ActivationReason)
{
	TRACE_CPUPROFILER_EVENT_SCOPE(Aeyerji_RiftReinforcementReveal);
	SCOPE_CYCLE_COUNTER(STAT_AeyerjiRiftReinforcementReveal);

	if (!HasAuthority()
		|| !RiftRegionPlans.IsValidIndex(PlanIndex)
		|| !RiftReservedRegionRequests.IsValidIndex(PlanIndex)
		|| !IsValid(Participant)
		|| Participant->GetWorld() != GetWorld())
	{
		return false;
	}

	FRiftRegionPlan& Plan = RiftRegionPlans[PlanIndex];
	TArray<FRiftSpawnRequest>& ReservedRequests = RiftReservedRegionRequests[PlanIndex];
	if (Plan.State != FRiftRegionPlan::EState::Staged || ReservedRequests.IsEmpty())
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const int32 BatchSize = FMath::Clamp(RiftRevealBatchSize, 1, MaxSpawnsPerDirectorTick);
	const float BatchInterval = ResolveFiniteFloat(RiftRevealBatchInterval, 0.15f, 0.f, MaxDirectorSeconds);
	const float RevealDuration = ResolveFiniteFloat(RiftRevealDurationSeconds, 1.f, 0.f, MaxDirectorSeconds);
	const double Now = World->GetTimeSeconds();
	for (int32 RequestIndex = 0; RequestIndex < ReservedRequests.Num(); ++RequestIndex)
	{
		FRiftSpawnRequest Request = ReservedRequests[RequestIndex];
		Request.RevealStyle = ResolveRiftRevealStyle();
		Request.EarliestSpawnTime = Now
			+ static_cast<double>(RequestIndex / BatchSize) * BatchInterval;
		RiftSpawnQueue.Add(MoveTemp(Request));
	}
	Plan.PendingRevealCount = ReservedRequests.Num();
	Plan.RevealCompleteTime = Now
		+ static_cast<double>((FMath::Max(ReservedRequests.Num(), 1) - 1) / BatchSize)
			* BatchInterval
		+ RevealDuration;
	ReservedRequests.Reset();
	Plan.State = FRiftRegionPlan::EState::Revealing;

	UE_LOG(LogEncounterDirector, Display,
		TEXT("[RiftRun][EncounterReveal] RunSerial=%d Anchor=%s Index=%d Reason=%s Participant=%s Reinforcements=%d"),
		RiftRegionRunSerial,
		*Plan.StableKey,
		Plan.ProgressionIndex,
		ActivationReason,
		*GetNameSafe(Participant),
		Plan.PendingRevealCount);
	return true;
}

void AAeyerjiEncounterDirector::RetireAndRedistributeSkippedRiftRegions(const int32 NewFrontierIndex)
{
	TArray<FRiftSpawnRequest> RetiredRequests;
	for (int32 PlanIndex = 0; PlanIndex < RiftRegionPlans.Num(); ++PlanIndex)
	{
		FRiftRegionPlan& Plan = RiftRegionPlans[PlanIndex];
		if (Plan.State != FRiftRegionPlan::EState::Planned || Plan.ProgressionIndex >= NewFrontierIndex)
		{
			continue;
		}

		if (RiftReservedRegionRequests.IsValidIndex(PlanIndex))
		{
			RetiredRequests.Append(RiftReservedRegionRequests[PlanIndex]);
			RiftReservedRegionRequests[PlanIndex].Reset();
		}
		Plan.State = FRiftRegionPlan::EState::Retired;
		Plan.Budget = 0;
		Plan.ReservedProgress = 0;
	}

	if (RetiredRequests.IsEmpty())
	{
		return;
	}

	TArray<int32> ForwardPlans;
	for (int32 PlanIndex = 0; PlanIndex < RiftRegionPlans.Num(); ++PlanIndex)
	{
		const FRiftRegionPlan& Plan = RiftRegionPlans[PlanIndex];
		if (Plan.State == FRiftRegionPlan::EState::Planned
			&& Plan.ProgressionIndex >= NewFrontierIndex
			&& RiftReservedRegionRequests.IsValidIndex(PlanIndex))
		{
			ForwardPlans.Add(PlanIndex);
		}
	}

	if (ForwardPlans.IsEmpty())
	{
		UE_LOG(LogEncounterDirector, Error,
			TEXT("[RiftRun][Progression] Unable to redistribute %d skipped requests at frontier %d"),
			RetiredRequests.Num(),
			NewFrontierIndex);
		return;
	}

	const TArray<int32> DestinationCounts =
		AeyerjiRiftRules::AllocateTransferredPopulation(RetiredRequests.Num(), ForwardPlans.Num());
	int32 RequestIndex = 0;
	for (int32 DestinationOffset = 0; DestinationOffset < ForwardPlans.Num(); ++DestinationOffset)
	{
		const int32 DestinationIndex = ForwardPlans[DestinationOffset];
		const int32 DestinationCount = DestinationCounts.IsValidIndex(DestinationOffset)
			? DestinationCounts[DestinationOffset]
			: 0;
		for (int32 DestinationRequestIndex = 0;
			DestinationRequestIndex < DestinationCount && RetiredRequests.IsValidIndex(RequestIndex);
			++DestinationRequestIndex, ++RequestIndex)
		{
			FRiftSpawnRequest Request = RetiredRequests[RequestIndex];
			Request.RegionPlanIndex = DestinationIndex;
			RiftReservedRegionRequests[DestinationIndex].Add(Request);
			RiftRegionPlans[DestinationIndex].Budget = FMath::Min(
				MaxEncounterPopulation,
				RiftRegionPlans[DestinationIndex].Budget + 1);
			RiftRegionPlans[DestinationIndex].ReservedProgress = static_cast<int32>(FMath::Min<int64>(
				MaxProgressPoints,
				static_cast<int64>(RiftRegionPlans[DestinationIndex].ReservedProgress) + Request.ProgressPoints));
		}
	}

	UE_LOG(LogEncounterDirector, Display,
		TEXT("[RiftRun][Progression] Redistributed skipped requests=%d Frontier=%d Destinations=%d"),
		RetiredRequests.Num(),
		NewFrontierIndex,
		ForwardPlans.Num());
}

EAeyerjiEnemyRevealStyle AAeyerjiEncounterDirector::ResolveRiftRevealStyle()
{
	const float GroundWeight = ResolveFiniteFloat(RiftGroundRevealWeight, 0.75f, 0.f, MaxRuntimeMultiplier);
	const float SkyWeight = ResolveFiniteFloat(RiftSkyRevealWeight, 0.25f, 0.f, MaxRuntimeMultiplier);
	const float TotalWeight = GroundWeight + SkyWeight;
	if (TotalWeight <= UE_SMALL_NUMBER)
	{
		return EAeyerjiEnemyRevealStyle::GroundEmergence;
	}
	return RiftSpawnStream.FRandRange(0.f, TotalWeight) <= GroundWeight
		? EAeyerjiEnemyRevealStyle::GroundEmergence
		: EAeyerjiEnemyRevealStyle::SkyDrop;
}

bool AAeyerjiEncounterDirector::IsRiftAmbientGroupEngaged(const FRiftRegionPlan& Plan) const
{
	for (const TWeakObjectPtr<AEnemyParentNative>& EnemyPtr : Plan.AmbientEnemies)
	{
		const AEnemyParentNative* Enemy = EnemyPtr.Get();
		if (!IsValid(Enemy)
			|| Enemy->GetEncounterPhase() == EAeyerjiEnemyEncounterPhase::PooledInactive)
		{
			// A defeated/destroyed attractor is itself engagement and must not strand the
			// region in Staged after its visible pack has been cleared.
			return true;
		}
		const AEnemyAIController* EnemyAI = Enemy ? Cast<AEnemyAIController>(Enemy->GetController()) : nullptr;
		if (Enemy
			&& (Enemy->HasTakenDamageSincePooledActivation()
				|| (EnemyAI && IsValid(EnemyAI->GetTargetActor()))))
		{
			return true;
		}
	}
	return false;
}

int32 AAeyerjiEncounterDirector::FindRiftPressureActivationCandidate(APawn*& OutParticipant) const
{
	OutParticipant = nullptr;
	TArray<APawn*> LivingParticipants;
	GetLivingRiftParticipants(LivingParticipants);
	const float StagingDistanceSquared = FMath::Square(FMath::Max(
		ResolveFiniteFloat(RiftRegionStagingDistance, 6500.f, 0.f, MaxWorldDistance),
		ResolveFiniteFloat(RiftRegionActivationDistance, 2500.f, 0.f, MaxWorldDistance)));
	float BestDistanceSquared = TNumericLimits<float>::Max();
	int32 BestProgressionIndex = MAX_int32;
	int32 BestPlanIndex = INDEX_NONE;
	for (int32 PlanIndex = 0; PlanIndex < RiftRegionPlans.Num(); ++PlanIndex)
	{
		const FRiftRegionPlan& Plan = RiftRegionPlans[PlanIndex];
		if (Plan.State != FRiftRegionPlan::EState::Planned
			|| !AeyerjiRiftRules::CanStageProgressionIndex(
				Plan.ProgressionIndex,
				HighestRiftProgressionIndex)
			|| Plan.Budget <= 0
			|| !Plan.Region.IsValid()
			|| !Plan.Bounds.IsValid)
		{
			continue;
		}
		for (APawn* Participant : LivingParticipants)
		{
			const float DistanceSquared = Plan.Bounds.ComputeSquaredDistanceToPoint(Participant->GetActorLocation());
			const bool bBetterProgression = Plan.ProgressionIndex < BestProgressionIndex;
			const bool bSameProgressionCloser = Plan.ProgressionIndex == BestProgressionIndex
				&& DistanceSquared < BestDistanceSquared;
			if (DistanceSquared <= StagingDistanceSquared
				&& (bBetterProgression || bSameProgressionCloser)
				&& IsRiftRegionReachableFromParticipant(Plan.Anchor, Participant))
			{
				BestPlanIndex = PlanIndex;
				BestProgressionIndex = Plan.ProgressionIndex;
				BestDistanceSquared = DistanceSquared;
				OutParticipant = Participant;
			}
		}
	}
	return BestPlanIndex;
}

int32 AAeyerjiEncounterDirector::GetActiveRiftEnemyPressure() const
{
	TArray<APawn*> LivingParticipants;
	GetLivingRiftParticipants(LivingParticipants);
	const float PressureRadiusSquared = FMath::Square(
		ResolveFiniteFloat(RiftPressureRadius, 8000.f, 0.f, MaxWorldDistance));
	int32 ActivePressure = 0;
	for (const TWeakObjectPtr<AActor>& EnemyPtr : LiveEnemies)
	{
		const AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(EnemyPtr.Get());
		if (!IsValid(Enemy) || !Enemy->IsEncounterCombatActive())
		{
			continue;
		}
		const FEnemyLODState* LODState =
			EnemyLODStates.Find(TWeakObjectPtr<AEnemyParentNative>(const_cast<AEnemyParentNative*>(Enemy)));
		if (LODState && LODState->bSleeping)
		{
			continue;
		}

		for (const APawn* Participant : LivingParticipants)
		{
			if (IsValid(Participant)
				&& FVector::DistSquared2D(Enemy->GetActorLocation(), Participant->GetActorLocation()) <= PressureRadiusSquared)
			{
				++ActivePressure;
				break;
			}
		}
		if (ActivePressure >= FMath::Clamp(RiftMaximumAwakeEnemies, 1, MaxEncounterPopulation))
		{
			break;
		}
	}
	return ActivePressure;
}

void AAeyerjiEncounterDirector::GetLivingRiftParticipants(TArray<APawn*>& OutParticipants) const
{
	OutParticipants.Reset();
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const FGameplayTag DeadTag = FGameplayTag::RequestGameplayTag(TEXT("State.Dead"), false);
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		AController* Controller = It->Get();
		APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		if (!IsValid(Pawn))
		{
			continue;
		}
		if (DeadTag.IsValid())
		{
			if (const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn, true))
			{
				if (ASC->HasMatchingGameplayTag(DeadTag))
				{
					continue;
				}
			}
		}
		OutParticipants.Add(Pawn);
	}
}

void AAeyerjiEncounterDirector::ProcessRiftSpawnQueue()
{
	if (!HasAuthority() || RiftRegionRunSerial <= 0 || RiftSpawnQueue.IsEmpty())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	const int32 SpawnBudget = FMath::Min(
		FMath::Clamp(MaxSpawnsPerTick, 1, MaxSpawnsPerDirectorTick),
		RiftSpawnQueue.Num());
	const float WorkMilliseconds = ResolveFiniteFloat(
		MaxSpawnWorkMillisecondsPerTick,
		2.f,
		0.f,
		MaxDirectorWorkMilliseconds);
	const double SpawnDeadline = FPlatformTime::Seconds() + (WorkMilliseconds / 1000.0);
	const double WorldTime = World->GetTimeSeconds();
	for (int32 AttemptIndex = 0;
		AttemptIndex < SpawnBudget
		&& !RiftSpawnQueue.IsEmpty()
		&& (AttemptIndex == 0 || WorkMilliseconds <= 0.f || FPlatformTime::Seconds() < SpawnDeadline);
		++AttemptIndex)
	{
		FRiftSpawnRequest Request = RiftSpawnQueue[0];
		if (!FMath::IsFinite(Request.EarliestSpawnTime))
		{
			Request.EarliestSpawnTime = WorldTime;
		}
		if (Request.EarliestSpawnTime > WorldTime)
		{
			break;
		}
		RiftSpawnQueue.RemoveAt(0, 1, EAllowShrinking::No);
		AEnemyParentNative* SpawnedEnemy = nullptr;
		if (!SpawnRiftRequest(Request, &SpawnedEnemy))
		{
			Request.FailedAttempts = FMath::Min(Request.FailedAttempts + 1, MaxRiftSpawnFailures);
			if (Request.FailedAttempts == 1 || Request.FailedAttempts % 20 == 0)
			{
				const FRiftRegionPlan* Plan = RiftRegionPlans.IsValidIndex(Request.RegionPlanIndex)
					? &RiftRegionPlans[Request.RegionPlanIndex]
					: nullptr;
				UE_LOG(LogEncounterDirector, Warning,
					TEXT("[RiftRun][EncounterSpawn] Deferred RunSerial=%d Anchor=%s Group=%s Class=%s Attempts=%d"),
					RiftRegionRunSerial, Plan ? *Plan->StableKey : TEXT("InvalidAnchor"),
					Plan ? *GetNameSafe(Plan->EncounterGroup.Get()) : TEXT("None"),
					*GetNameSafe(Request.EnemyClass), Request.FailedAttempts);
			}
			if (Request.FailedAttempts < MaxRiftSpawnFailures)
			{
				RiftSpawnQueue.Add(MoveTemp(Request));
			}
			else if (RiftRegionPlans.IsValidIndex(Request.RegionPlanIndex))
			{
				FRiftRegionPlan& Plan = RiftRegionPlans[Request.RegionPlanIndex];
				Plan.Budget = FMath::Max(0, Plan.Budget - 1);
				Plan.ReservedProgress = FMath::Max(0, Plan.ReservedProgress - FMath::Clamp(Request.ProgressPoints, 1, MaxProgressPoints));
				Plan.PendingRevealCount = FMath::Max(0, Plan.PendingRevealCount - 1);
				if (Plan.PendingRevealCount == 0 && Plan.State == FRiftRegionPlan::EState::Revealing)
				{
					Plan.RevealCompleteTime = WorldTime;
				}
				UE_LOG(LogEncounterDirector, Error,
					TEXT("[RiftRun][EncounterSpawn] Abandoned invalid request after %d attempts RunSerial=%d Anchor=%s Class=%s"),
					MaxRiftSpawnFailures,
					RiftRegionRunSerial,
					*Plan.StableKey,
					*GetNameSafe(Request.EnemyClass));
			}
		}
		else if (RiftRegionPlans.IsValidIndex(Request.RegionPlanIndex))
		{
			FRiftRegionPlan& Plan = RiftRegionPlans[Request.RegionPlanIndex];
			const double ActualRevealEndTime = WorldTime
				+ (Request.RevealStyle == EAeyerjiEnemyRevealStyle::Immediate
					? 0.0
					: static_cast<double>(ResolveFiniteFloat(RiftRevealDurationSeconds, 1.f, 0.f, MaxDirectorSeconds)));
			Plan.RevealCompleteTime = FMath::Max(Plan.RevealCompleteTime, ActualRevealEndTime);
			Plan.PendingRevealCount = FMath::Max(0, Plan.PendingRevealCount - 1);
			if (Plan.PendingRevealCount == 0 && Plan.State == FRiftRegionPlan::EState::Revealing)
			{
				UE_LOG(LogEncounterDirector, Display,
					TEXT("[RiftRun][EncounterReveal] Population placed RunSerial=%d Anchor=%s Index=%d UnlockAt=%.3f"),
					RiftRegionRunSerial,
					*Plan.StableKey,
					Plan.ProgressionIndex,
					Plan.RevealCompleteTime);
			}
		}
	}
}

void AAeyerjiEncounterDirector::UpdateRiftRevealStates()
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	for (FRiftRegionPlan& Plan : RiftRegionPlans)
	{
		if (Plan.State != FRiftRegionPlan::EState::Revealing
			|| Plan.PendingRevealCount > 0
			|| Now < Plan.RevealCompleteTime)
		{
			continue;
		}

		Plan.State = FRiftRegionPlan::EState::Active;
		UE_LOG(LogEncounterDirector, Display,
			TEXT("[RiftRun][EncounterReveal] Complete RunSerial=%d Anchor=%s Index=%d"),
			RiftRegionRunSerial,
			*Plan.StableKey,
			Plan.ProgressionIndex);
	}
}

bool AAeyerjiEncounterDirector::ResolveRiftRegionAnchor(const FBox& Bounds, FVector& OutAnchor)
{
	if (!IsFiniteBox(Bounds))
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys)
	{
		OutAnchor = Bounds.GetCenter();
		return true;
	}

	const FVector ProjectionExtent(
		FMath::Max(Bounds.GetExtent().X, 100.f),
		FMath::Max(Bounds.GetExtent().Y, 100.f),
		FMath::Max(Bounds.GetExtent().Z + 1000.f, 1000.f));
	for (int32 Attempt = 0; Attempt < 30; ++Attempt)
	{
		FVector Candidate = Attempt == 0 ? Bounds.GetCenter() : FVector(
			RiftSpawnStream.FRandRange(Bounds.Min.X, Bounds.Max.X),
			RiftSpawnStream.FRandRange(Bounds.Min.Y, Bounds.Max.Y),
			Bounds.GetCenter().Z);
		FNavLocation Projected;
		if (NavSys->ProjectPointToNavigation(Candidate, Projected, ProjectionExtent)
			&& Bounds.IsInsideXY(Projected.Location))
		{
			OutAnchor = Projected.Location;
			return true;
		}
	}
	return false;
}

bool AAeyerjiEncounterDirector::IsRiftRegionReachableFromParticipant(const FVector& RegionAnchor, const APawn* Participant) const
{
	if (!IsValid(Participant)
		|| Participant->GetWorld() != GetWorld()
		|| RegionAnchor.ContainsNaN()
		|| Participant->GetActorLocation().ContainsNaN())
	{
		return false;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}
	const UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(
		World, RegionAnchor, Participant->GetActorLocation());
	return Path && Path->IsValid() && !Path->IsPartial();
}

APawn* AAeyerjiEncounterDirector::ResolveNearestLiveParticipant(const FVector& FromLocation) const
{
	if (FromLocation.ContainsNaN())
	{
		return nullptr;
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}
	const FGameplayTag DeadTag = FGameplayTag::RequestGameplayTag(TEXT("State.Dead"), false);
	APawn* BestPawn = nullptr;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		AController* Controller = It->Get();
		APawn* Pawn = Controller ? Controller->GetPawn() : nullptr;
		if (!IsValid(Pawn))
		{
			continue;
		}
		if (DeadTag.IsValid())
		{
			if (const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn, true))
			{
				if (ASC->HasMatchingGameplayTag(DeadTag))
				{
					continue;
				}
			}
		}
		const float DistanceSquared = FVector::DistSquared(FromLocation, Pawn->GetActorLocation());
		if (!BestPawn || DistanceSquared < BestDistanceSquared)
		{
			BestPawn = Pawn;
			BestDistanceSquared = DistanceSquared;
		}
	}
	return BestPawn;
}

bool AAeyerjiEncounterDirector::ResolveRiftSpawnLocation(
	const FRiftRegionPlan& Plan,
	const float HalfHeight,
	const APawn* Participant,
	FVector& OutLocation,
	FString& OutRejectReason)
{
	OutRejectReason.Reset();
	UWorld* World = GetWorld();
	if (!World || !IsFiniteBox(Plan.Bounds) || !FMath::IsFinite(HalfHeight))
	{
		OutRejectReason = TEXT("Missing world or invalid encounter-anchor bounds");
		return false;
	}
	const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys)
	{
		OutRejectReason = TEXT("NavMesh is unavailable");
		return false;
	}
	TArray<APawn*> LivingParticipants;
	GetLivingRiftParticipants(LivingParticipants);
	if (LivingParticipants.IsEmpty())
	{
		OutRejectReason = TEXT("No living participants");
		return false;
	}
	const FVector ProjectionExtent(
		FMath::Max(Plan.Bounds.GetExtent().X, 100.f),
		FMath::Max(Plan.Bounds.GetExtent().Y, 100.f),
		FMath::Max(Plan.Bounds.GetExtent().Z + 1000.f, 1000.f));
	bool bFoundVisibleFallback = false;
	FVector VisibleFallback = FVector::ZeroVector;
	for (int32 Attempt = 0; Attempt < FMath::Clamp(SpawnLocationSearchAttempts, 1, MaxSpawnAttempts); ++Attempt)
	{
		FVector Candidate(
			RiftSpawnStream.FRandRange(Plan.Bounds.Min.X, Plan.Bounds.Max.X),
			RiftSpawnStream.FRandRange(Plan.Bounds.Min.Y, Plan.Bounds.Max.Y),
			Plan.Bounds.Max.Z);
		FNavLocation Projected;
		if (!NavSys->ProjectPointToNavigation(Candidate, Projected, ProjectionExtent))
		{
			continue;
		}
		Candidate = Projected.Location;
		if (!Plan.Bounds.IsInsideXY(Candidate)
			|| (Participant && !IsRiftRegionReachableFromParticipant(Candidate, Participant)))
		{
			continue;
		}

		const FVector TraceStart = Candidate + FVector(0.f, 0.f, GroundTraceUpOffset);
		const FVector TraceEnd = Candidate - FVector(0.f, 0.f, GroundTraceDownDistance);
		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(EncounterDirector_RiftRegionGround), false, Participant);
		if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params))
		{
			Candidate.Z = Hit.ImpactPoint.Z + SpawnGroundOffset;
		}
		Candidate += FVector(0.f, 0.f, HalfHeight);
		bool bVisibleToParticipant = false;
		FString SafetyRejectReason;
		if (!IsRiftSpawnLocationSafe(Candidate, LivingParticipants, bVisibleToParticipant, SafetyRejectReason))
		{
			OutRejectReason = SafetyRejectReason;
			continue;
		}
		if (bRiftPreferHiddenSpawnLocations && bVisibleToParticipant)
		{
			bFoundVisibleFallback = true;
			VisibleFallback = Candidate;
			continue;
		}
		OutLocation = Candidate;
		return true;
	}
	if (bFoundVisibleFallback)
	{
		OutLocation = VisibleFallback;
		return true;
	}
	if (OutRejectReason.IsEmpty())
	{
		OutRejectReason = TEXT("No navigable spawn location satisfies the player safety radius");
	}
	return false;
}

bool AAeyerjiEncounterDirector::IsRiftSpawnLocationSafe(
	const FVector& Candidate,
	const TArray<APawn*>& LivingParticipants,
	bool& bOutVisibleToParticipant,
	FString& OutRejectReason) const
{
	bOutVisibleToParticipant = false;
	OutRejectReason.Reset();
	if (Candidate.ContainsNaN())
	{
		OutRejectReason = TEXT("Spawn candidate is non-finite");
		return false;
	}
	const float MinimumDistanceSquared = FMath::Square(
		ResolveFiniteFloat(RiftMinimumSpawnDistanceFromPlayers, 1200.f, 0.f, MaxWorldDistance));
	UWorld* World = GetWorld();
	if (!World)
	{
		OutRejectReason = TEXT("World disappeared during spawn safety validation");
		return false;
	}
	for (APawn* Participant : LivingParticipants)
	{
		if (!IsValid(Participant))
		{
			continue;
		}
		if (FVector::DistSquared(Candidate, Participant->GetActorLocation()) < MinimumDistanceSquared)
		{
			OutRejectReason = FString::Printf(TEXT("Within %.0fcm safety radius of %s"),
				RiftMinimumSpawnDistanceFromPlayers, *GetNameSafe(Participant));
			return false;
		}

		FVector EyeLocation;
		FRotator EyeRotation;
		Participant->GetActorEyesViewPoint(EyeLocation, EyeRotation);
		FCollisionQueryParams VisibilityParams(SCENE_QUERY_STAT(EncounterDirector_RiftVisibility), false, Participant);
		const FVector VisibilityTarget = Candidate + FVector(0.f, 0.f, 75.f);
		if (!World->LineTraceTestByChannel(EyeLocation, VisibilityTarget, ECC_Visibility, VisibilityParams))
		{
			bOutVisibleToParticipant = true;
		}
	}
	return true;
}

bool AAeyerjiEncounterDirector::SpawnRiftRequest(
	FRiftSpawnRequest& Request,
	AEnemyParentNative** OutSpawnedEnemy)
{
	if (OutSpawnedEnemy)
	{
		*OutSpawnedEnemy = nullptr;
	}
	if (!HasAuthority()
		|| !Request.EnemyClass
		|| !RiftRegionPlans.IsValidIndex(Request.RegionPlanIndex))
	{
		return false;
	}
	AAeyerjiSpawnerGroup* Spawner = RiftPopulationSpawner.Get();
	if (!Spawner)
	{
		return false;
	}
	if (Spawner->IsCleared())
	{
		Spawner->ResetEncounter();
		Spawner->ConfigureAsRiftPopulationExecutor(RiftLevelDirector.Get());
	}

	const FRiftRegionPlan& Plan = RiftRegionPlans[Request.RegionPlanIndex];
	APawn* Participant = ResolveNearestLiveParticipant(Plan.Anchor);
	if (!Participant)
	{
		return false;
	}
	const float HalfHeight = GetEnemyHalfHeight(Request.EnemyClass);
	FVector SpawnLocation;
	FString RejectReason;
	bool bResolvedSpawnLocation = false;
	{
		TRACE_CPUPROFILER_EVENT_SCOPE(Aeyerji_RiftNavigationPlacement);
		bResolvedSpawnLocation = ResolveRiftSpawnLocation(
			Plan,
			HalfHeight,
			Participant,
			SpawnLocation,
			RejectReason);
	}
	if (!bResolvedSpawnLocation)
	{
		UE_LOG(LogEncounterDirector, Verbose,
			TEXT("[RiftRun][EncounterSpawn] Deferred RunSerial=%d Anchor=%s Group=%s Reason=%s"),
			RiftRegionRunSerial, *Plan.StableKey, *GetNameSafe(Plan.EncounterGroup.Get()), *RejectReason);
		return false;
	}
	const FRotator SpawnRotation = (Participant->GetActorLocation() - SpawnLocation).Rotation();

	FEnemySet EnemyTemplate;
	EnemyTemplate.EnemyClass = Request.EnemyClass;
	EnemyTemplate.Count = 1;
	EnemyTemplate.ProgressPoints = FMath::Clamp(Request.ProgressPoints, 1, MaxProgressPoints);
	EnemyTemplate.SpawnInterval = 0.f;
	EnemyTemplate.EnemyArchetypeTag = ResolveArchetypeTagFromClass(Request.EnemyClass);
	EnemyTemplate.bIsElite = Request.bIsElite;

	const FTransform SpawnTransform(SpawnRotation, SpawnLocation);
	APawn* SpawnedPawn = UAeyerjiEnemyManagementBPFL::SpawnAndRegisterEnemyFromSet(
		this,
		EnemyTemplate,
		SpawnTransform,
		Spawner,
		this,
		Participant,
		true,
		Request.RevealStyle == EAeyerjiEnemyRevealStyle::Immediate,
		false,
		true,
		Participant,
		Participant->GetController(),
		true);
	if (!IsValid(SpawnedPawn))
	{
		return false;
	}
	AEnemyParentNative* SpawnedEnemy = Cast<AEnemyParentNative>(SpawnedPawn);
	if (SpawnedEnemy)
	{
		FEnemyLODState& LODState = GetOrCreateEnemyLODState(SpawnedEnemy);
		LODState.bHasRiftHome = true;
		LODState.RiftRegionPlanIndex = Request.RegionPlanIndex;
		LODState.RiftHomeLocation = SpawnLocation;
		SpawnedEnemy->BeginEncounterReveal(
			Request.RevealStyle,
			Request.RevealStyle == EAeyerjiEnemyRevealStyle::Immediate
				? 0.f
				: ResolveFiniteFloat(RiftRevealDurationSeconds, 1.f, 0.f, MaxDirectorSeconds));
	}
	if (OutSpawnedEnemy)
	{
		*OutSpawnedEnemy = SpawnedEnemy;
	}

	UE_LOG(LogEncounterDirector, Verbose,
		TEXT("[RiftRun][EnemyLevel] RunSerial=%d Anchor=%s Group=%s Enemy=%s EnemyLevel=%d Points=%d Elite=%d Remaining=%d"),
		RiftRegionRunSerial, *Plan.StableKey, *GetNameSafe(Plan.EncounterGroup.Get()), *GetNameSafe(SpawnedPawn),
		RiftLevelDirector.IsValid() ? RiftLevelDirector->GetActiveRiftActivity().ActivityLevel : UAeyerjiDifficultySettings::GetRiftEnemyReferenceLevel(),
		EnemyTemplate.ProgressPoints, EnemyTemplate.bIsElite ? 1 : 0, RiftSpawnQueue.Num());
	return true;
}

void AAeyerjiEncounterDirector::FreezeWeightedProgress()
{
	if (!HasAuthority() || bWeightedProgressFrozen)
	{
		return;
	}
	bWeightedProgressFrozen = true;
	StopRiftRegionActivation();
	WeightedProgressPoints = FMath::Min(WeightedProgressPoints, WeightedProgressTarget);
	KilledCount = WeightedProgressPoints;
	if (const AAeyerjiSpawnerGroup* Spawner = RiftPopulationSpawner.Get())
	{
		const int32 EmergencySpawns = Spawner->GetEmergencyRuntimeSpawnCount();
		if (EmergencySpawns == 0)
		{
			UE_LOG(LogEncounterDirector, Display,
				TEXT("[RiftRun][PoolAcceptance] RunSerial=%d FreshSpawnsDuringGameplay=0 PrewarmConstructed=%d PooledCheckouts=%d"),
				RiftRegionRunSerial,
				Spawner->GetPrewarmConstructionCount(),
				Spawner->GetPooledCheckoutCount());
		}
		else
		{
			UE_LOG(LogEncounterDirector, Warning,
				TEXT("[RiftRun][PoolAcceptance] RunSerial=%d FreshSpawnsDuringGameplay=%d PrewarmConstructed=%d PooledCheckouts=%d"),
				RiftRegionRunSerial,
				EmergencySpawns,
				Spawner->GetPrewarmConstructionCount(),
				Spawner->GetPooledCheckoutCount());
		}
	}
	HandleProgressChanged();
}

#if WITH_DEV_AUTOMATION_TESTS
int32 AAeyerjiEncounterDirector::GetRegisteredProgressPointsForAutomation(const AActor* Enemy) const
{
	return Enemy
		? RegisteredProgressEnemyPoints.FindRef(TWeakObjectPtr<AActor>(const_cast<AActor*>(Enemy)))
		: 0;
}

void AAeyerjiEncounterDirector::NotifyProgressEnemyDiedForAutomation(AActor* Enemy)
{
	HandleProgressEnemyDied(Enemy);
}

void AAeyerjiEncounterDirector::NotifyProgressEnemyDestroyedForAutomation(AActor* Enemy)
{
	HandleProgressEnemyDestroyed(Enemy);
}

void AAeyerjiEncounterDirector::ConfigureRiftEnemyHomeForAutomation(
	AEnemyParentNative* Enemy,
	const int32 RegionPlanIndex,
	const FVector& HomeLocation)
{
	FEnemyLODState& State = GetOrCreateEnemyLODState(Enemy);
	State.bHasRiftHome = true;
	State.RiftRegionPlanIndex = RegionPlanIndex;
	State.RiftHomeLocation = HomeLocation;
}

void AAeyerjiEncounterDirector::SetEnemySleepingForAutomation(
	AEnemyParentNative* Enemy,
	const bool bSleeping)
{
	ApplyEnemySleepState(Enemy, bSleeping);
}

bool AAeyerjiEncounterDirector::IsEnemySleepingForAutomation(const AEnemyParentNative* Enemy) const
{
	const FEnemyLODState* State = Enemy
		? EnemyLODStates.Find(TWeakObjectPtr<AEnemyParentNative>(const_cast<AEnemyParentNative*>(Enemy)))
		: nullptr;
	return State && State->bSleeping;
}
#endif

bool AAeyerjiEncounterDirector::StartFixedWorldPopulation(UAeyerjiWorldSpawnProfile* Profile, AAeyerjiSpawnerGroup* SpawnManager, AAeyerjiLevelDirector* LevelDirector)
{
	if (!HasAuthority())
	{
		return false;
	}

	if (!Profile)
	{
		UE_LOG(LogEncounterDirector, Warning, TEXT("Fixed population start skipped: missing profile on %s"), *GetNameSafe(this));
		return false;
	}
	if ((IsValid(SpawnManager) && SpawnManager->GetWorld() != GetWorld())
		|| (IsValid(LevelDirector) && LevelDirector->GetWorld() != GetWorld()))
	{
		UE_LOG(LogEncounterDirector, Warning, TEXT("Fixed population start skipped: collaborators belong to another world on %s"), *GetNameSafe(this));
		return false;
	}

	StopFixedWorldPopulation();
	ResetProgress(0);
	RefreshPlayerReference();

	FixedSpawnProfile = Profile;
	FixedPopulationSpawner = SpawnManager;
	FixedPopulationLevelDirector = LevelDirector;
	bSpawnedPopulationSpawner = false;
	bFixedPopulationInitialSpawnComplete = false;

	if (!FixedPopulationSpawner.IsValid())
	{
		if (UWorld* World = GetWorld())
		{
			const FTransform SpawnTransform = GetActorTransform();
			AAeyerjiSpawnerGroup* Spawned = World->SpawnActorDeferred<AAeyerjiSpawnerGroup>(
				AAeyerjiSpawnerGroup::StaticClass(),
				SpawnTransform,
				this,
				nullptr,
				ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

			if (Spawned)
			{
				Spawned->bDisableActivationVolume = true;
				Spawned->bDisableActivationEvent = true;
				Spawned->bSuppressDoorControl = true;
				Spawned->bAllowManualActivationWithoutWaves = true;
				Spawned->PoolSettings.bEnablePooling = true;
				Spawned->PoolSettings.bPrewarmDuringWorldFlowLoading = true;
				UGameplayStatics::FinishSpawningActor(Spawned, SpawnTransform);
				FixedPopulationSpawner = Spawned;
				bSpawnedPopulationSpawner = true;
			}
		}
	}

	if (!FixedPopulationSpawner.IsValid())
	{
		UE_LOG(LogEncounterDirector, Warning, TEXT("Fixed population start on %s has no spawn manager; elites/scaling will be limited."), *GetNameSafe(this));
	}

	if (FixedPopulationSpawner.IsValid() && FixedPopulationLevelDirector.IsValid())
	{
		FixedPopulationSpawner->LevelDirector = FixedPopulationLevelDirector.Get();
	}

	if (FixedPopulationSpawner.IsValid())
	{
		FixedPopulationSpawner->ResetEncounter();
	}

	PendingSpawnRequests.Reset();
	BuildFixedPopulationPlan();
	if (FixedPopulationSpawner.IsValid()
		&& FixedPopulationSpawner->PoolSettings.bEnablePooling
		&& FixedPopulationSpawner->PoolSettings.bPrewarmDuringWorldFlowLoading)
	{
		TArray<FEnemySet> WarmSets;
		auto AddGroupToWarmSets = [this, &WarmSets](const UEnemySpawnGroupDefinition* Group)
		{
			if (!IsValid(Group) || WarmSets.Num() >= MaxPoolWarmSets)
			{
				return;
			}

			for (TSubclassOf<AEnemyParentNative> EnemyClass : Group->EnemyTypes)
			{
				if (WarmSets.Num() >= MaxPoolWarmSets)
				{
					break;
				}
				if (!*EnemyClass)
				{
					continue;
				}

				FEnemySet WarmSet;
				WarmSet.EnemyClass = EnemyClass;
				WarmSet.Count = 1;
				WarmSet.EnemyArchetypeTag = ResolveArchetypeTagFromClass(EnemyClass);
				WarmSets.Add(WarmSet);
			}

			for (TSubclassOf<AEnemyParentNative> EliteClass : Group->EliteEnemyTypes)
			{
				if (WarmSets.Num() >= MaxPoolWarmSets)
				{
					break;
				}
				if (!*EliteClass)
				{
					continue;
				}

				FEnemySet WarmSet;
				WarmSet.EnemyClass = EliteClass;
				WarmSet.Count = 1;
				WarmSet.bIsElite = true;
				WarmSet.EnemyArchetypeTag = ResolveArchetypeTagFromClass(EliteClass);
				WarmSets.Add(WarmSet);
			}
		};

		if (!Profile->SpawnGroups.IsEmpty())
		{
			const int32 GroupCount = FMath::Min(Profile->SpawnGroups.Num(), MaxEncounterSpawnGroups);
			for (int32 GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
			{
				AddGroupToWarmSets(Profile->SpawnGroups[GroupIndex].Group);
			}
		}
		else
		{
			const int32 GroupCount = FMath::Min(SpawnGroups.Num(), MaxEncounterSpawnGroups);
			for (int32 GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
			{
				AddGroupToWarmSets(SpawnGroups[GroupIndex]);
			}
		}

		FixedPopulationSpawner->PrewarmPoolForEnemySets(WarmSets, 1);
	}
	UpdateTotalToKill(FixedPopulationTarget);

	if (FixedPopulationTarget <= 0 || FixedSpawnQueue.IsEmpty())
	{
		UE_LOG(LogEncounterDirector, Warning, TEXT("Fixed population start skipped: no spawn requests generated on %s"), *GetNameSafe(this));
		StopFixedWorldPopulation();
		return false;
	}

	bFixedPopulationActive = true;
	bFixedPopulationComplete = false;
	EnterState(EEncounterDirectorState::InCombat);
	PushObjectiveStateToGameState();

	return true;
}

void AAeyerjiEncounterDirector::StopFixedWorldPopulation()
{
	if (!HasAuthority())
	{
		return;
	}

	for (TPair<int32, TArray<TWeakObjectPtr<AEnemyParentNative>>>& Pair : FixedClusterMembers)
	{
		for (const TWeakObjectPtr<AEnemyParentNative>& EnemyPtr : Pair.Value)
		{
			ApplyEnemySleepState(EnemyPtr.Get(), false);
		}
	}

	bFixedPopulationActive = false;
	bFixedPopulationInitialSpawnComplete = false;
	bFixedPopulationComplete = false;
	FixedSpawnQueue.Reset();
	FixedClusterCenters.Reset();
	FixedClusters.Reset();
	FixedEnemyClusterMap.Reset();
	FixedClusterMembers.Reset();
	FixedPopulationTarget = 0;
	FixedPopulationSpawned = 0;
	FixedPopulationRemaining = 0;
	FixedClustersRemaining = 0;
	FixedSpawnSeed = 0;
	FixedSpawnProfile.Reset();
	FixedPopulationLevelDirector.Reset();

	if (bSpawnedPopulationSpawner)
	{
		if (AAeyerjiSpawnerGroup* Spawned = FixedPopulationSpawner.Get())
		{
			Spawned->Destroy();
		}
	}

	FixedPopulationSpawner.Reset();
	bSpawnedPopulationSpawner = false;
	EnterState(EEncounterDirectorState::Idle);
	PushObjectiveStateToGameState();
}

float AAeyerjiEncounterDirector::GetProgress01() const
{
	if (TotalToKill <= 0)
	{
		return 0.f;
	}

	return FMath::Clamp(static_cast<float>(KilledCount) / static_cast<float>(TotalToKill), 0.f, 1.f);
}

int32 AAeyerjiEncounterDirector::GetTotalToKill() const
{
	// Legacy objective widgets still divide by this value directly, so clamp the Blueprint-facing accessor.
	return FMath::Max(TotalToKill, 1);
}

FAeyerjiObjectiveState AAeyerjiEncounterDirector::BuildObjectiveStateSnapshot(const AAeyerjiLevelDirector* LevelDirector, const AAeyerjiGameState* GameState) const
{
	FAeyerjiObjectiveState ObjectiveState;

	const AAeyerjiLevelDirector* ResolvedLevelDirector = IsValid(LevelDirector) ? LevelDirector : ResolveObjectiveLevelDirector();
	const AAeyerjiGameState* ResolvedGameState = IsValid(GameState)
		? GameState
		: (GetWorld() ? GetWorld()->GetGameState<AAeyerjiGameState>() : nullptr);

	if (!IsValid(ResolvedLevelDirector) || !IsValid(ResolvedGameState))
	{
		return ObjectiveState;
	}

	const EAeyerjiRunState CurrentRunState = ResolvedGameState->GetRunState();
	const EAeyerjiRunWinCondition WinCondition = ResolvedLevelDirector->GetRunWinCondition();
	const bool bResolvedVictory = CurrentRunState == EAeyerjiRunState::BossDefeated
		|| CurrentRunState == EAeyerjiRunState::ObjectiveComplete
		|| (CurrentRunState == EAeyerjiRunState::RunComplete
			&& ResolvedGameState->GetRunResults().Resolution == EAeyerjiRunResolution::Victory);

	ObjectiveState.bPrimaryObjectiveComplete = ResolvedLevelDirector->IsPrimaryObjectiveComplete();
	ObjectiveState.bObjectiveComplete = bResolvedVictory;
	ObjectiveState.bBossSpawned = bBossSpawned;
	ObjectiveState.BossId = ResolveBossObjectiveId(ResolvedLevelDirector);

	const int32 EffectiveKillTarget = ResolvedLevelDirector->GetEffectiveObjectiveKillTargetRaw();
	ObjectiveState.TotalToKill = EffectiveKillTarget > 0 ? EffectiveKillTarget : TotalToKill;
	ObjectiveState.KilledCount = ObjectiveState.TotalToKill > 0
		? FMath::Clamp(KilledCount, 0, ObjectiveState.TotalToKill)
		: FMath::Max(0, KilledCount);
	ObjectiveState.EnemiesDefeated = EnemiesDefeated;
	ObjectiveState.ProgressPoints = WeightedProgressRunSerial > 0 ? WeightedProgressPoints : ObjectiveState.KilledCount;
	ObjectiveState.ProgressPointTarget = WeightedProgressRunSerial > 0 ? WeightedProgressTarget : ObjectiveState.TotalToKill;

	switch (WinCondition)
	{
	case EAeyerjiRunWinCondition::KillTarget:
		ObjectiveState.ObjectiveKind = EAeyerjiObjectiveKind::KillCount;
		ObjectiveState.Progress01 = ObjectiveState.TotalToKill > 0
			? FMath::Clamp(static_cast<float>(ObjectiveState.KilledCount) / static_cast<float>(ObjectiveState.TotalToKill), 0.f, 1.f)
			: (ObjectiveState.bObjectiveComplete ? 1.f : 0.f);
		break;

	case EAeyerjiRunWinCondition::KillTargetThenBoss:
		if (ObjectiveState.bObjectiveComplete)
		{
			ObjectiveState.ObjectiveKind = EAeyerjiObjectiveKind::BossCleared;
			ObjectiveState.Progress01 = 1.f;
		}
		else if (ObjectiveState.bPrimaryObjectiveComplete)
		{
			ObjectiveState.ObjectiveKind = EAeyerjiObjectiveKind::KillNamedBoss;
			ObjectiveState.Progress01 = 0.f;
		}
		else
		{
			ObjectiveState.ObjectiveKind = EAeyerjiObjectiveKind::KillCountThenBoss;
			ObjectiveState.Progress01 = ObjectiveState.TotalToKill > 0
				? FMath::Clamp(static_cast<float>(ObjectiveState.KilledCount) / static_cast<float>(ObjectiveState.TotalToKill), 0.f, 1.f)
				: 0.f;
		}
		break;

	case EAeyerjiRunWinCondition::BossCleared:
	default:
		ObjectiveState.ObjectiveKind = ObjectiveState.bObjectiveComplete
			? EAeyerjiObjectiveKind::BossCleared
			: EAeyerjiObjectiveKind::KillNamedBoss;
		ObjectiveState.Progress01 = ObjectiveState.bObjectiveComplete ? 1.f : 0.f;
		break;
	}

	ObjectiveState.ObjectiveTextKey = BuildObjectiveTextKey(ObjectiveState);

	const bool bRunStateRenderable = CurrentRunState == EAeyerjiRunState::InRun
		|| CurrentRunState == EAeyerjiRunState::BossDefeated
		|| CurrentRunState == EAeyerjiRunState::ObjectiveComplete
		|| CurrentRunState == EAeyerjiRunState::RunComplete;
	const bool bKillDataReady = WinCondition == EAeyerjiRunWinCondition::BossCleared || ObjectiveState.TotalToKill > 0;
	bool bBossDataReady = true;

	if (ObjectiveState.ObjectiveKind == EAeyerjiObjectiveKind::KillNamedBoss
		|| ObjectiveState.ObjectiveKind == EAeyerjiObjectiveKind::BossCleared)
	{
		bBossDataReady = !ObjectiveState.BossId.IsNone();
	}

	ObjectiveState.bObjectiveReady = bRunStateRenderable
		&& ResolvedGameState->GetWorldFlowPhase() == EAeyerjiWorldFlowPhase::Gameplay
		&& bKillDataReady
		&& bBossDataReady;

	return ObjectiveState;
}

void AAeyerjiEncounterDirector::PushObjectiveStateToGameState()
{
	if (!HasAuthority())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	AAeyerjiGameState* GameState = World->GetGameState<AAeyerjiGameState>();
	if (!IsValid(GameState))
	{
		return;
	}

	AAeyerjiLevelDirector* LevelDirector = ResolveObjectiveLevelDirector();
	if (!IsValid(LevelDirector))
	{
		GameState->ClearObjectiveStateFromServer();
		return;
	}

	GameState->SetObjectiveStateFromServer(BuildObjectiveStateSnapshot(LevelDirector, GameState));
}

void AAeyerjiEncounterDirector::SetBossSpawned(bool bInBossSpawned)
{
	if (!HasAuthority())
	{
		return;
	}

	if (bBossSpawned == bInBossSpawned)
	{
		return;
	}

	bBossSpawned = bInBossSpawned;
	HandleProgressChanged();
}

AAeyerjiLevelDirector* AAeyerjiEncounterDirector::ResolveObjectiveLevelDirector() const
{
	if (AAeyerjiLevelDirector* LevelDirector = RiftLevelDirector.Get())
	{
		return LevelDirector;
	}
	if (AAeyerjiLevelDirector* LevelDirector = FixedPopulationLevelDirector.Get())
	{
		return LevelDirector;
	}

	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<AAeyerjiLevelDirector> It(World); It; ++It)
		{
			if (AAeyerjiLevelDirector* LevelDirector = *It)
			{
				return LevelDirector;
			}
		}
	}

	return nullptr;
}

void AAeyerjiEncounterDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (GetNetMode() == NM_Client)
	{
		return;
	}

	RefreshPlayerReference();
	UpdateRecentPlayerPath();
	CleanupInactiveEnemies();
	UpdateKillWindow();
	ProcessFixedSpawnQueue();
	ProcessRiftPopulationPrewarm();
	ProcessRiftRegionActivation();
	ProcessRiftSpawnQueue();
	UpdateRiftRevealStates();
	UpdateEnemyLOD(DeltaSeconds);
	UpdateRiftTraceCounters();

	if (bFixedPopulationActive)
	{
		return;
	}
	if (WeightedProgressRunSerial > 0)
	{
		// Greater Rift region activation and spawning are handled above. The normal
		// kill-velocity injector must not create additional unregistered packs.
		return;
	}

	ProcessSpawnQueue();

	if (!CachedPlayerPawn.IsValid())
	{
		return;
	}

	DistanceFromLastEncounter = (!CachedPlayerPawn->GetActorLocation().ContainsNaN() && !LastEncounterLocation.ContainsNaN())
		? FMath::Min(FVector::Dist(CachedPlayerPawn->GetActorLocation(), LastEncounterLocation), MaxWorldDistance)
		: 0.f;

	switch (DirectorState)
	{
	case EEncounterDirectorState::Idle:
		if (ShouldTriggerEncounter())
		{
			TriggerEncounter();
		}
		break;

	case EEncounterDirectorState::InCombat:
		if (ActiveEnemyCount <= 0 && PendingSpawnRequests.IsEmpty())
		{
			EnterState(EEncounterDirectorState::PostCombat);
		}
		break;

	case EEncounterDirectorState::PostCombat:
		PostCombatTimeRemaining -= FMath::IsFinite(DeltaSeconds) ? FMath::Max(0.f, DeltaSeconds) : 0.f;
		if (PostCombatTimeRemaining <= 0.f)
		{
			EnterState(EEncounterDirectorState::Idle);
		}
		break;
	}
}

void AAeyerjiEncounterDirector::RefreshPlayerReference()
{
	if (!CachedPlayerPawn.IsValid())
	{
		if (APawn* Pawn = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			CachedPlayerPawn = Pawn;
		}
	}

	if (!CachedPlayerController.IsValid())
	{
		if (APawn* Pawn = CachedPlayerPawn.Get())
		{
			CachedPlayerController = Pawn->GetController();
		}
	}
}

void AAeyerjiEncounterDirector::UpdateRecentPlayerPath()
{
	if (!bAvoidRecentPlayerPath || !CachedPlayerPawn.IsValid())
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	if ((Now - LastPathSampleTimestamp) < RecentPathSampleInterval)
	{
		return;
	}

	LastPathSampleTimestamp = Now;

	FRecentPlayerSample Sample;
	Sample.Location = CachedPlayerPawn->GetActorLocation();
	if (Sample.Location.ContainsNaN())
	{
		return;
	}
	Sample.Timestamp = Now;
	RecentPlayerSamples.Add(Sample);

	const double Cutoff = Now - RecentPathSeconds;
	for (int32 Index = RecentPlayerSamples.Num() - 1; Index >= 0; --Index)
	{
		if (RecentPlayerSamples[Index].Timestamp < Cutoff)
		{
			RecentPlayerSamples.RemoveAt(Index);
		}
	}

	const int32 MaxSamples = FMath::Clamp(RecentPathMaxSamples, 1, MaxRecentPathSamples);
	if (RecentPlayerSamples.Num() > MaxSamples)
	{
		const int32 TrimCount = RecentPlayerSamples.Num() - MaxSamples;
		RecentPlayerSamples.RemoveAt(0, TrimCount, EAllowShrinking::No);
	}
}

void AAeyerjiEncounterDirector::CleanupInactiveEnemies()
{
	for (int32 Index = LiveEnemies.Num() - 1; Index >= 0; --Index)
	{
		if (!LiveEnemies[Index].IsValid())
		{
			LiveEnemies.RemoveAtSwap(Index);
		}
	}

	ActiveEnemyCount = LiveEnemies.Num();
}

void AAeyerjiEncounterDirector::UpdateEnemyLOD(float DeltaSeconds)
{
	TArray<APawn*> LivingParticipants;
	GetLivingRiftParticipants(LivingParticipants);
	if (LivingParticipants.IsEmpty())
	{
		return;
	}

	const float SafeDeltaSeconds = FMath::IsFinite(DeltaSeconds) ? FMath::Max(0.f, DeltaSeconds) : 0.f;
	const float SafeLODUpdateInterval = ResolveFiniteFloat(EnemyLODUpdateInterval, 0.5f, 0.05f, MaxDirectorSeconds);
	if (SafeLODUpdateInterval > 0.f)
	{
		EnemyLODTimeAccumulator = FMath::Min(
			EnemyLODTimeAccumulator + SafeDeltaSeconds,
			MaxDirectorSeconds);
		if (EnemyLODTimeAccumulator < SafeLODUpdateInterval)
		{
			return;
		}
		EnemyLODTimeAccumulator = 0.f;
	}

	UpdateFixedClusterLOD(LivingParticipants);

	if (!bEnableEnemyLODThrottling)
	{
		return;
	}

	const float NearDistance = ResolveFiniteFloat(EnemyLODNearDistance, 4000.f, 0.f, MaxWorldDistance);
	const float MidDistance = ResolveFiniteFloat(EnemyLODMidDistance, 8000.f, NearDistance, MaxWorldDistance);
	const float FarDistance = ResolveFiniteFloat(EnemyLODFarDistance, 12000.f, MidDistance, MaxWorldDistance);
	const float MidDistSq = FMath::Square(MidDistance);
	const float FarDistSq = FMath::Square(FarDistance);

	struct FEnemyDistanceEntry
	{
		TWeakObjectPtr<AEnemyParentNative> Enemy;
		float DistanceSquared = TNumericLimits<float>::Max();
	};
	TArray<FEnemyDistanceEntry> DistanceEntries;
	DistanceEntries.Reserve(LiveEnemies.Num());
	for (const TWeakObjectPtr<AActor>& Tracked : LiveEnemies)
	{
		AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(Tracked.Get());
		if (!IsValid(Enemy))
		{
			continue;
		}
		const FVector EnemyLocation = Enemy->GetActorLocation();
		if (EnemyLocation.ContainsNaN())
		{
			continue;
		}

		float DistSq = TNumericLimits<float>::Max();
		for (const APawn* Participant : LivingParticipants)
		{
			if (IsValid(Participant) && !Participant->GetActorLocation().ContainsNaN())
			{
				DistSq = FMath::Min(DistSq, FVector::DistSquared2D(EnemyLocation, Participant->GetActorLocation()));
			}
		}
		FEnemyDistanceEntry& Entry = DistanceEntries.AddDefaulted_GetRef();
		Entry.Enemy = Enemy;
		Entry.DistanceSquared = DistSq;
	}
	DistanceEntries.Sort([](const FEnemyDistanceEntry& Left, const FEnemyDistanceEntry& Right)
	{
		return Left.DistanceSquared < Right.DistanceSquared;
	});

	const bool bRiftPopulationActive = WeightedProgressRunSerial > 0;
	const float SafeRiftWakeDistance = ResolveFiniteFloat(RiftEnemyWakeDistance, 8000.f, 0.f, MaxWorldDistance);
	const float SafeRiftSleepDistance = ResolveFiniteFloat(RiftEnemySleepDistance, 10000.f, SafeRiftWakeDistance, MaxWorldDistance);
	const float RiftWakeDistanceSquared = FMath::Square(SafeRiftWakeDistance);
	const float RiftSleepDistanceSquared = FMath::Square(SafeRiftSleepDistance);
	const int32 MaxAwakeRiftEnemies = FMath::Clamp(RiftMaximumAwakeEnemies, 1, MaxEncounterPopulation);
	int32 RiftAwakeCandidateIndex = 0;
	for (int32 EnemyIndex = 0; EnemyIndex < DistanceEntries.Num(); ++EnemyIndex)
	{
		AEnemyParentNative* Enemy = DistanceEntries[EnemyIndex].Enemy.Get();
		if (!IsValid(Enemy))
		{
			continue;
		}

		FEnemyLODState& State = GetOrCreateEnemyLODState(Enemy);
		const float DistSq = DistanceEntries[EnemyIndex].DistanceSquared;
		if (bRiftPopulationActive
			&& State.bHasRiftHome
			&& Enemy->GetEncounterPhase() != EAeyerjiEnemyEncounterPhase::Revealing)
		{
			const bool bBeyondAwakeBudget = RiftAwakeCandidateIndex >= MaxAwakeRiftEnemies;
			++RiftAwakeCandidateIndex;
			const bool bBeyondSleepDistance = DistSq >= RiftSleepDistanceSquared;
			if (!State.bSleeping && (bBeyondAwakeBudget || bBeyondSleepDistance))
			{
				if (AEnemyAIController* EnemyAI = Cast<AEnemyAIController>(Enemy->GetController()))
				{
					EnemyAI->SetTargetActor(nullptr);
					EnemyAI->ClearLastKnownTarget();
				}
				ApplyEnemySleepState(Enemy, true);
				continue;
			}
			if (State.bSleeping)
			{
				if (bBeyondAwakeBudget || DistSq > RiftWakeDistanceSquared)
				{
					continue;
				}
				ApplyEnemySleepState(Enemy, false);
			}
		}
		else if (State.bSleeping)
		{
			ApplyEnemySleepState(Enemy, false);
		}

		uint8 NewBucket = 0;
		const AEnemyAIController* EnemyAI = Cast<AEnemyAIController>(Enemy->GetController());
		if (EnemyAI && IsValid(EnemyAI->GetTargetActor()))
		{
			NewBucket = 0;
		}
		else if (DistSq > FarDistSq)
		{
			NewBucket = 2;
		}
		else if (DistSq > MidDistSq)
		{
			NewBucket = 1;
		}

		if (State.LODBucket != NewBucket)
		{
			ApplyEnemyLODBucket(Enemy, State, NewBucket);
		}
	}
}

void AAeyerjiEncounterDirector::UpdateFixedClusterLOD(const TArray<APawn*>& LivingParticipants)
{
	if (!bEnableFixedClusterSleeping || !bFixedPopulationActive || FixedClusters.IsEmpty())
	{
		return;
	}

	const float SleepDistance = ResolveFiniteFloat(FixedClusterSleepDistance, 14000.f, 0.f, MaxWorldDistance);
	const float WakeDistance = ResolveFiniteFloat(FixedClusterWakeDistance, 11000.f, 0.f, SleepDistance);
	const float SleepDistSq = FMath::Square(SleepDistance);
	const float WakeDistSq = FMath::Square(WakeDistance);

	for (TPair<int32, FFixedSpawnCluster>& Pair : FixedClusters)
	{
		FFixedSpawnCluster& Cluster = Pair.Value;
		float DistSq = TNumericLimits<float>::Max();
		if (Cluster.Center.ContainsNaN())
		{
			continue;
		}
		for (const APawn* Participant : LivingParticipants)
		{
			if (IsValid(Participant) && !Participant->GetActorLocation().ContainsNaN())
			{
				DistSq = FMath::Min(DistSq, FVector::DistSquared2D(Participant->GetActorLocation(), Cluster.Center));
			}
		}

		if (!Cluster.bSleeping && DistSq >= SleepDistSq)
		{
			Cluster.bSleeping = true;
			ApplyFixedClusterSleepState(Pair.Key, true);
		}
		else if (Cluster.bSleeping && DistSq <= WakeDistSq)
		{
			Cluster.bSleeping = false;
			ApplyFixedClusterSleepState(Pair.Key, false);
		}
	}
}

void AAeyerjiEncounterDirector::ApplyFixedClusterSleepState(int32 ClusterId, bool bSleep)
{
	TArray<TWeakObjectPtr<AEnemyParentNative>>* Members = FixedClusterMembers.Find(ClusterId);
	if (!Members)
	{
		return;
	}

	for (int32 Index = Members->Num() - 1; Index >= 0; --Index)
	{
		AEnemyParentNative* Enemy = (*Members)[Index].Get();
		if (!IsValid(Enemy))
		{
			Members->RemoveAtSwap(Index);
			continue;
		}

		ApplyEnemySleepState(Enemy, bSleep);
	}

	if (Members->IsEmpty())
	{
		FixedClusterMembers.Remove(ClusterId);
	}
}

void AAeyerjiEncounterDirector::ApplyEnemySleepState(AEnemyParentNative* Enemy, bool bSleep)
{
	if (!HasAuthority() || !IsValid(Enemy) || Enemy->GetWorld() != GetWorld())
	{
		return;
	}

	FEnemyLODState& State = GetOrCreateEnemyLODState(Enemy);
	if (State.bSleeping == bSleep)
	{
		return;
	}

	State.bSleeping = bSleep;

	UCharacterMovementComponent* MoveComp = Enemy->GetCharacterMovement();
	USkeletalMeshComponent* MeshComp = Enemy->GetMesh();
	AAIController* AIController = Cast<AAIController>(Enemy->GetController());
	UBrainComponent* Brain = AIController ? AIController->BrainComponent : nullptr;
	UAIPerceptionComponent* Perception = AIController ? AIController->GetPerceptionComponent() : nullptr;

	if (bSleep)
	{
		Enemy->ForceNetUpdate();
		if (AIController)
		{
			AIController->StopMovement();
			if (AEnemyAIController* EnemyAI = Cast<AEnemyAIController>(AIController))
			{
				EnemyAI->SetPathFollowingGameplayEnabled(false, TEXT("EncounterDirectorLODSleep"));
			}
		}

		if (Brain && !State.bPausedByLOD && !Brain->IsPaused())
		{
			Brain->PauseLogic(TEXT("EncounterDirectorLOD"));
			State.bPausedByLOD = true;
		}

		if (MoveComp)
		{
			MoveComp->SetComponentTickEnabled(false);
		}

		if (MeshComp)
		{
			MeshComp->SetComponentTickEnabled(false);
		}

		if (Perception)
		{
			Perception->SetComponentTickEnabled(false);
		}
		Enemy->SetNetDormancy(DORM_DormantAll);
	}
	else
	{
		Enemy->SetNetDormancy(DORM_Awake);
		Enemy->FlushNetDormancy();
		if (AEnemyAIController* EnemyAI = Cast<AEnemyAIController>(AIController))
		{
			// Reacquire a Detour slot before the StateTree resumes and can issue a move.
			EnemyAI->SetPathFollowingGameplayEnabled(true, TEXT("EncounterDirectorLODWake"));
		}
		if (MoveComp)
		{
			MoveComp->SetComponentTickEnabled(State.bMovementTickEnabled);
			if (State.bMovementTickEnabled)
			{
				MoveComp->SetComponentTickInterval(State.BaseMovementTickInterval);
			}
		}

		if (MeshComp)
		{
			MeshComp->SetComponentTickEnabled(State.bMeshTickEnabled);
			if (State.bMeshTickEnabled)
			{
				MeshComp->SetComponentTickInterval(State.BaseMeshTickInterval);
			}
		}

		if (Perception)
		{
			Perception->SetComponentTickEnabled(State.bPerceptionTickEnabled);
			if (State.bPerceptionTickEnabled)
			{
				Perception->SetComponentTickInterval(State.BasePerceptionTickInterval);
			}
		}

		if (Brain && State.bPausedByLOD)
		{
			Brain->ResumeLogic(TEXT("EncounterDirectorLOD"));
			State.bPausedByLOD = false;
		}

		State.LODBucket = 255;
	}
}

void AAeyerjiEncounterDirector::ApplyEnemyLODBucket(AEnemyParentNative* Enemy, FEnemyLODState& State, uint8 NewBucket)
{
	if (!IsValid(Enemy))
	{
		return;
	}

	const float DesiredInterval = NewBucket == 1
		? ResolveFiniteFloat(EnemyLODMidTickInterval, 0.1f, 0.f, MaxDirectorSeconds)
		: (NewBucket == 2
			? ResolveFiniteFloat(EnemyLODFarTickInterval, 0.25f, 0.f, MaxDirectorSeconds)
			: 0.f);
	auto ApplyTickSettings = [NewBucket, DesiredInterval](UActorComponent* Component, bool bEnabled, float BaseInterval)
	{
		if (!Component)
		{
			return;
		}

		Component->SetComponentTickEnabled(bEnabled);
		if (bEnabled)
		{
			const float SafeBaseInterval = ResolveFiniteFloat(BaseInterval, 0.f, 0.f, MaxDirectorSeconds);
			const float Interval = (NewBucket == 0) ? SafeBaseInterval : FMath::Max(SafeBaseInterval, DesiredInterval);
			Component->SetComponentTickInterval(Interval);
		}
	};

	ApplyTickSettings(Enemy->GetCharacterMovement(), State.bMovementTickEnabled, State.BaseMovementTickInterval);
	ApplyTickSettings(Enemy->GetMesh(), State.bMeshTickEnabled, State.BaseMeshTickInterval);
	if (NewBucket == 0)
	{
		Enemy->SetNetUpdateFrequency(30.f);
		Enemy->SetMinNetUpdateFrequency(10.f);
	}
	else if (NewBucket == 1)
	{
		Enemy->SetNetUpdateFrequency(15.f);
		Enemy->SetMinNetUpdateFrequency(5.f);
	}
	else
	{
		Enemy->SetNetUpdateFrequency(5.f);
		Enemy->SetMinNetUpdateFrequency(2.f);
	}

	if (AAIController* AIController = Cast<AAIController>(Enemy->GetController()))
	{
		if (UAIPerceptionComponent* Perception = AIController->GetPerceptionComponent())
		{
			ApplyTickSettings(Perception, State.bPerceptionTickEnabled, State.BasePerceptionTickInterval);
		}
	}

	State.LODBucket = NewBucket;
}

AAeyerjiEncounterDirector::FEnemyLODState& AAeyerjiEncounterDirector::GetOrCreateEnemyLODState(AEnemyParentNative* Enemy)
{
	FEnemyLODState& State = EnemyLODStates.FindOrAdd(Enemy);
	if (!IsValid(Enemy))
	{
		return State;
	}

	if (!State.bInitialized)
	{
		State.bInitialized = true;
		State.LODBucket = 255;
	}

	if (!State.bCachedMovement)
	{
		if (UCharacterMovementComponent* MoveComp = Enemy->GetCharacterMovement())
		{
			State.bCachedMovement = true;
			State.BaseMovementTickInterval = ResolveFiniteFloat(
				MoveComp->PrimaryComponentTick.TickInterval,
				0.f,
				0.f,
				MaxDirectorSeconds);
			State.bMovementTickEnabled = MoveComp->IsComponentTickEnabled();
		}
	}

	if (!State.bCachedMesh)
	{
		if (USkeletalMeshComponent* MeshComp = Enemy->GetMesh())
		{
			State.bCachedMesh = true;
			State.BaseMeshTickInterval = ResolveFiniteFloat(
				MeshComp->PrimaryComponentTick.TickInterval,
				0.f,
				0.f,
				MaxDirectorSeconds);
			State.bMeshTickEnabled = MeshComp->IsComponentTickEnabled();
		}
	}

	if (!State.bCachedPerception)
	{
		if (AAIController* AIController = Cast<AAIController>(Enemy->GetController()))
		{
			if (UAIPerceptionComponent* Perception = AIController->GetPerceptionComponent())
			{
				State.bCachedPerception = true;
				State.BasePerceptionTickInterval = ResolveFiniteFloat(
					Perception->PrimaryComponentTick.TickInterval,
					0.f,
					0.f,
					MaxDirectorSeconds);
				State.bPerceptionTickEnabled = Perception->IsComponentTickEnabled();
			}
		}
	}

	return State;
}

void AAeyerjiEncounterDirector::RemoveEnemyLODState(AActor* Enemy)
{
	AEnemyParentNative* TypedEnemy = Cast<AEnemyParentNative>(Enemy);
	if (!TypedEnemy)
	{
		return;
	}

	EnemyLODStates.Remove(TypedEnemy);
}

void AAeyerjiEncounterDirector::UpdateRiftTraceCounters() const
{
	int32 AwakeCount = 0;
	int32 SleepingCount = 0;
	int32 RevealingCount = 0;
	for (const TWeakObjectPtr<AActor>& EnemyPtr : LiveEnemies)
	{
		const AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(EnemyPtr.Get());
		if (!IsValid(Enemy))
		{
			continue;
		}

		if (Enemy->GetEncounterPhase() == EAeyerjiEnemyEncounterPhase::Revealing)
		{
			++RevealingCount;
			continue;
		}

		const FEnemyLODState* State = EnemyLODStates.Find(Enemy);
		if (State && State->bSleeping)
		{
			++SleepingCount;
		}
		else if (Enemy->IsEncounterCombatActive())
		{
			++AwakeCount;
		}
	}

	int32 StagedPopulation = 0;
	for (int32 PlanIndex = 0; PlanIndex < RiftRegionPlans.Num(); ++PlanIndex)
	{
		const FRiftRegionPlan& Plan = RiftRegionPlans[PlanIndex];
		if (Plan.State == FRiftRegionPlan::EState::Staged
			&& RiftReservedRegionRequests.IsValidIndex(PlanIndex))
		{
			StagedPopulation += RiftReservedRegionRequests[PlanIndex].Num();
		}
		else if (Plan.State == FRiftRegionPlan::EState::Revealing)
		{
			StagedPopulation += Plan.PendingRevealCount;
		}
	}

	const AAeyerjiSpawnerGroup* Spawner = RiftPopulationSpawner.Get();
	TRACE_COUNTER_SET(AeyerjiRiftAwakeEnemies, AwakeCount);
	TRACE_COUNTER_SET(AeyerjiRiftSleepingEnemies, SleepingCount);
	TRACE_COUNTER_SET(AeyerjiRiftRevealingEnemies, RevealingCount);
	TRACE_COUNTER_SET(AeyerjiRiftStagedPopulation, StagedPopulation);
	TRACE_COUNTER_SET(AeyerjiRiftPooledEnemies, Spawner ? Spawner->GetInactivePooledEnemyCount() : 0);
	TRACE_COUNTER_SET(AeyerjiRiftFreshConstructions, Spawner ? Spawner->GetFreshEnemyConstructionCount() : 0);
	TRACE_COUNTER_SET(AeyerjiRiftEmergencySpawns, Spawner ? Spawner->GetEmergencyRuntimeSpawnCount() : 0);
}

void AAeyerjiEncounterDirector::RemoveFixedClusterMember(int32 ClusterId, AActor* Enemy)
{
	TArray<TWeakObjectPtr<AEnemyParentNative>>* Members = FixedClusterMembers.Find(ClusterId);
	if (!Members)
	{
		return;
	}

	for (int32 Index = Members->Num() - 1; Index >= 0; --Index)
	{
		if (!Members->operator[](Index).IsValid() || Members->operator[](Index).Get() == Enemy)
		{
			Members->RemoveAtSwap(Index);
		}
	}

	if (Members->IsEmpty())
	{
		FixedClusterMembers.Remove(ClusterId);
	}
}

bool AAeyerjiEncounterDirector::RemoveProgressEnemy(AActor* Enemy)
{
	if (!Enemy)
	{
		return false;
	}

	bool bRemoved = false;
	for (int32 Index = ProgressOnlyEnemies.Num() - 1; Index >= 0; --Index)
	{
		if (!ProgressOnlyEnemies[Index].IsValid())
		{
			ProgressOnlyEnemies.RemoveAtSwap(Index);
			continue;
		}

		if (ProgressOnlyEnemies[Index].Get() == Enemy)
		{
			ProgressOnlyEnemies.RemoveAtSwap(Index);
			bRemoved = true;
		}
	}

	if (bRemoved)
	{
		if (AEnemyParentNative* TypedEnemy = Cast<AEnemyParentNative>(Enemy))
		{
			TypedEnemy->OnEnemyDied.RemoveDynamic(this, &AAeyerjiEncounterDirector::HandleProgressEnemyDied);
		}
		Enemy->OnDestroyed.RemoveDynamic(this, &AAeyerjiEncounterDirector::HandleProgressEnemyDestroyed);
	}
	RegisteredProgressEnemyPoints.Remove(TWeakObjectPtr<AActor>(Enemy));

	return bRemoved;
}

void AAeyerjiEncounterDirector::UpdateKillWindow()
{
	if (KillTimestampHistory.IsEmpty())
	{
		CurrentKillVelocity = 0.f;
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	const float SafeWindowSeconds = ResolveFiniteFloat(
		KillVelocityWindowSeconds,
		6.f,
		0.1f,
		MaxDirectorSeconds);
	const double WindowStart = Now - SafeWindowSeconds;

	for (int32 Index = KillTimestampHistory.Num() - 1; Index >= 0; --Index)
	{
		if (KillTimestampHistory[Index] < WindowStart)
		{
			KillTimestampHistory.RemoveAtSwap(Index);
		}
	}

	CurrentKillVelocity = FMath::Min(
		static_cast<float>(KillTimestampHistory.Num()) / SafeWindowSeconds,
		MaxRuntimeMultiplier);
}

void AAeyerjiEncounterDirector::ResetProgress(int32 NewTotal)
{
	if (!HasAuthority())
	{
		return;
	}

	TotalToKill = FMath::Clamp(NewTotal, 0, MaxProgressPoints);
	KilledCount = 0;
	EnemiesDefeated = 0;
	WeightedProgressPoints = 0;
	WeightedProgressTarget = 0;
	WeightedProgressRunSerial = 0;
	bWeightedProgressFrozen = false;
	RegisteredProgressEnemyPoints.Reset();
	bBossSpawned = false;
	HandleProgressChanged();
}

void AAeyerjiEncounterDirector::UpdateTotalToKill(int32 NewTotal)
{
	if (!HasAuthority())
	{
		return;
	}

	const int32 ClampedTotal = FMath::Clamp(NewTotal, 0, MaxProgressPoints);
	if (TotalToKill == ClampedTotal)
	{
		return;
	}

	TotalToKill = ClampedTotal;
	KilledCount = FMath::Clamp(KilledCount, 0, TotalToKill);
	HandleProgressChanged();
}

void AAeyerjiEncounterDirector::IncrementKillCount()
{
	if (!HasAuthority())
	{
		return;
	}

	if (TotalToKill <= 0 || KilledCount >= TotalToKill)
	{
		return;
	}

	KilledCount++;
	HandleProgressChanged();
}

void AAeyerjiEncounterDirector::HandleProgressChanged()
{
	if (LastBroadcastKilled == KilledCount && LastBroadcastTotal == TotalToKill && bLastBroadcastBossSpawned == bBossSpawned)
	{
		return;
	}

	LastBroadcastKilled = KilledCount;
	LastBroadcastTotal = TotalToKill;
	bLastBroadcastBossSpawned = bBossSpawned;

	OnProgressChanged.Broadcast(GetProgress01(), KilledCount, TotalToKill);
	PushObjectiveStateToGameState();
}

void AAeyerjiEncounterDirector::HandleProgressEnemyDied(AActor* DeadEnemy)
{
	if (!DeadEnemy)
	{
		return;
	}

	const int32 AwardedPoints = RegisteredProgressEnemyPoints.FindRef(TWeakObjectPtr<AActor>(DeadEnemy));
	if (RemoveProgressEnemy(DeadEnemy))
	{
		if (WeightedProgressRunSerial > 0)
		{
			if (!bWeightedProgressFrozen && WeightedProgressPoints < WeightedProgressTarget)
			{
				EnemiesDefeated++;
				WeightedProgressPoints = AeyerjiRiftRules::ApplyAcceptedProgressAward(
					WeightedProgressPoints, WeightedProgressTarget, AwardedPoints);
				KilledCount = WeightedProgressPoints;
				TotalToKill = WeightedProgressTarget;
				bWeightedProgressFrozen = WeightedProgressPoints >= WeightedProgressTarget;
				if (bWeightedProgressFrozen)
				{
					StopRiftRegionActivation();
				}
				RecordKillTimestamp();
				HandleProgressChanged();
			}
		}
		else
		{
			IncrementKillCount();
		}
	}
}

void AAeyerjiEncounterDirector::HandleProgressEnemyDestroyed(AActor* DestroyedActor)
{
	if (!DestroyedActor)
	{
		return;
	}

	if (RemoveProgressEnemy(DestroyedActor))
	{
		UE_LOG(LogEncounterDirector, Warning,
			TEXT("[RiftRun][Progress] Registered enemy destroyed without death; no progress awarded RunSerial=%d Enemy=%s"),
			WeightedProgressRunSerial, *GetNameSafe(DestroyedActor));
	}
}

void AAeyerjiEncounterDirector::OnRep_ProgressData()
{
	HandleProgressChanged();
}

void AAeyerjiEncounterDirector::OnRep_BossSpawned()
{
	HandleProgressChanged();
}

void AAeyerjiEncounterDirector::BuildFixedPopulationPlan()
{
	FixedSpawnQueue.Reset();
	FixedClusterCenters.Reset();
	FixedClusters.Reset();
	FixedEnemyClusterMap.Reset();
	FixedPopulationTarget = 0;
	FixedPopulationSpawned = 0;
	FixedPopulationRemaining = 0;
	FixedClustersRemaining = 0;

	const UAeyerjiWorldSpawnProfile* Profile = FixedSpawnProfile.Get();
	if (!Profile)
	{
		return;
	}

	TArray<FFixedSpawnGroupEntry> GroupEntries;
	if (!Profile->SpawnGroups.IsEmpty())
	{
		for (const FWeightedSpawnGroup& Weighted : Profile->SpawnGroups)
		{
			if (GroupEntries.Num() >= MaxEncounterSpawnGroups)
			{
				break;
			}
			if (IsValid(Weighted.Group)
				&& !Weighted.Group->EnemyTypes.IsEmpty()
				&& FMath::IsFinite(Weighted.Weight)
				&& Weighted.Weight > 0.f)
			{
				FFixedSpawnGroupEntry Entry;
				Entry.Group = Weighted.Group;
				Entry.Weight = FMath::Min(Weighted.Weight, MaxRuntimeMultiplier);
				GroupEntries.Add(Entry);
			}
		}
	}
	else
	{
		for (const UEnemySpawnGroupDefinition* Group : SpawnGroups)
		{
			if (GroupEntries.Num() >= MaxEncounterSpawnGroups)
			{
				break;
			}
			if (IsValid(Group) && !Group->EnemyTypes.IsEmpty())
			{
				FFixedSpawnGroupEntry Entry;
				Entry.Group = Group;
				Entry.Weight = 1.0f;
				GroupEntries.Add(Entry);
			}
		}
	}

	if (GroupEntries.IsEmpty())
	{
		UE_LOG(LogEncounterDirector, Warning, TEXT("Fixed population build skipped: no spawn groups on %s"), *GetNameSafe(this));
		return;
	}

	const int32 BaseMinEnemyCount = FMath::Clamp(Profile->MinimumEnemyCount, 0, MaxEncounterPopulation);
	const int32 BaseTargetEnemyCount = FMath::Clamp(Profile->TargetEnemyCount, 0, MaxEncounterPopulation);
	int32 BaseMaxEnemyCount = Profile->MaximumEnemyCount > 0
		? FMath::Min(Profile->MaximumEnemyCount, MaxEncounterPopulation)
		: BaseTargetEnemyCount;
	BaseMaxEnemyCount = FMath::Max(BaseMaxEnemyCount, BaseMinEnemyCount);

	int32 MaxDifficultyMinEnemyCount = Profile->MinimumEnemyCountAtMaxDifficulty > 0
		? Profile->MinimumEnemyCountAtMaxDifficulty
		: BaseMinEnemyCount;
	int32 MaxDifficultyMaxEnemyCount = Profile->MaximumEnemyCountAtMaxDifficulty > 0
		? Profile->MaximumEnemyCountAtMaxDifficulty
		: BaseMaxEnemyCount;
	MaxDifficultyMinEnemyCount = FMath::Clamp(MaxDifficultyMinEnemyCount, 0, MaxEncounterPopulation);
	MaxDifficultyMaxEnemyCount = FMath::Clamp(
		MaxDifficultyMaxEnemyCount,
		MaxDifficultyMinEnemyCount,
		MaxEncounterPopulation);

	float BudgetAlpha = 0.f;
	if (Profile->bScaleBudgetByDifficulty && FixedPopulationLevelDirector.IsValid())
	{
		const float Difficulty = ResolveFiniteFloat(
			FixedPopulationLevelDirector->GetCurvedDifficulty(),
			0.f,
			0.f,
			1.f);
		const float DifficultyFloor = ResolveFiniteFloat(Profile->DifficultyBudgetFloor, 0.f, 0.f, 1.f);
		BudgetAlpha = FMath::Clamp(DifficultyFloor + (1.f - DifficultyFloor) * Difficulty, 0.f, 1.f);
	}
	const int32 MinEnemyCount = FMath::RoundToInt(FMath::Lerp(static_cast<float>(BaseMinEnemyCount), static_cast<float>(MaxDifficultyMinEnemyCount), BudgetAlpha));
	const int32 MaxEnemyCount = FMath::RoundToInt(FMath::Lerp(static_cast<float>(BaseMaxEnemyCount), static_cast<float>(MaxDifficultyMaxEnemyCount), BudgetAlpha));
	const int32 ClampedMaxEnemyCount = FMath::Max(MaxEnemyCount, MinEnemyCount);

	float TargetAlpha = 0.f;
	if (BaseMaxEnemyCount > BaseMinEnemyCount)
	{
		// Preserve the target's relative position within the base min/max range.
		TargetAlpha = static_cast<float>(BaseTargetEnemyCount - BaseMinEnemyCount) / static_cast<float>(BaseMaxEnemyCount - BaseMinEnemyCount);
		TargetAlpha = FMath::Clamp(TargetAlpha, 0.f, 1.f);
	}

	int32 ResolvedTarget = FMath::RoundToInt(FMath::Lerp(static_cast<float>(MinEnemyCount), static_cast<float>(ClampedMaxEnemyCount), TargetAlpha));
	ResolvedTarget = FMath::Clamp(ResolvedTarget, MinEnemyCount, ClampedMaxEnemyCount);

	// Allow runtime scaling/caps to keep fixed population counts playable.
	const float BudgetScale = ResolveFiniteFloat(
		GetFixedPopulationBudgetScaleCVar().GetValueOnGameThread(),
		1.f,
		0.f,
		1.f);
	if (BudgetScale < 1.f)
	{
		ResolvedTarget = FMath::RoundToInt(ResolvedTarget * BudgetScale);
	}

	const int32 BudgetCap = GetFixedPopulationBudgetCapCVar().GetValueOnGameThread();
	if (BudgetCap > 0)
	{
		ResolvedTarget = FMath::Min(ResolvedTarget, FMath::Min(BudgetCap, MaxEncounterPopulation));
	}
	ResolvedTarget = FMath::Clamp(ResolvedTarget, 0, MaxEncounterPopulation);

	if (ResolvedTarget <= 0)
	{
		return;
	}

	FixedSpawnQueue.Reserve(ResolvedTarget);

	FixedSpawnSeed = Profile->Seed != 0 ? Profile->Seed : FMath::Rand();
	FixedSpawnStream.Initialize(FixedSpawnSeed);

	const int32 MinClusterSize = FMath::Clamp(Profile->MinClusterSize, 1, ResolvedTarget);
	const int32 MaxClusterSize = FMath::Clamp(Profile->MaxClusterSize, MinClusterSize, ResolvedTarget);
	const int32 MinClusterCount = FMath::Clamp(Profile->MinClusterCount, 1, MaxFixedClusters);
	const int32 MaxClusterCount = FMath::Clamp(Profile->MaxClusterCount, MinClusterCount, MaxFixedClusters);
	const float AvgClusterSize = (static_cast<float>(MinClusterSize) + static_cast<float>(MaxClusterSize)) * 0.5f;
	int32 ClusterCount = AvgClusterSize > 0.f ? FMath::RoundToInt(ResolvedTarget / AvgClusterSize) : MinClusterCount;
	ClusterCount = FMath::Clamp(ClusterCount, MinClusterCount, MaxClusterCount);
	const int32 MaxPossibleClusters = FMath::Max(1, ResolvedTarget / MinClusterSize);
	ClusterCount = FMath::Min(ClusterCount, MaxPossibleClusters);

	TArray<FFixedSpawnRegionEntry> Regions;
	if (Profile->bUseSpawnRegions)
	{
		for (TActorIterator<AAeyerjiSpawnRegion> It(GetWorld()); It; ++It)
		{
			if (Regions.Num() >= MaxFixedSpawnRegions)
			{
				break;
			}
			AAeyerjiSpawnRegion* Region = *It;
			if (!IsValid(Region))
			{
				continue;
			}

			const FBox Bounds = Region->GetRegionBounds();
			if (!IsFiniteBox(Bounds))
			{
				continue;
			}

			FFixedSpawnRegionEntry Entry;
			Entry.Region = Region;
			Entry.Bounds = Bounds;
			const FVector RegionSize = Bounds.GetSize();
			const double SizeScore = FMath::Clamp(
				static_cast<double>(RegionSize.X) + static_cast<double>(RegionSize.Y),
				0.0,
				static_cast<double>(MaxWorldDistance) * 2.0);
			const double RegionWeight = ResolveFiniteFloat(Region->RegionWeight, 0.f, 0.f, MaxRuntimeMultiplier);
			Entry.Weight = static_cast<float>(FMath::Clamp(
				RegionWeight * SizeScore,
				0.0,
				static_cast<double>(MaxRuntimeMultiplier)));
			Entry.DensityScale = ResolveFiniteFloat(Region->DensityScale, 1.f, 0.f, MaxRuntimeMultiplier);
			Entry.EliteChanceBonus = ResolveFiniteFloat(Region->EliteChanceBonus, 0.f, -1.f, 1.f);
			Entry.RadiusScale = ResolveFiniteFloat(Region->ClusterRadiusScale, 1.f, 0.f, MaxRuntimeMultiplier);
			Entry.bAllowElites = Region->bAllowElites;

			if (Entry.Weight > 0.f)
			{
				Regions.Add(Entry);
			}
		}

		UE_LOG(LogEncounterDirector, Log, TEXT("SpawnDiag: %s found %d spawn regions (fallbackRadius=%.1f seed=%d)"),
			*GetNameSafe(this),
			Regions.Num(),
			Profile->FallbackSpawnRadius,
			FixedSpawnSeed);

		int32 RegionLogCount = 0;
		for (const FFixedSpawnRegionEntry& Entry : Regions)
		{
			if (RegionLogCount++ >= 5)
			{
				break;
			}

			const FVector RegionSize = Entry.Bounds.GetSize();
			UE_LOG(LogEncounterDirector, Log, TEXT("SpawnDiag: Region %s weight=%.2f sizeXY=(%.1f, %.1f) bounds=%s"),
				*GetNameSafe(Entry.Region.Get()),
				Entry.Weight,
				RegionSize.X,
				RegionSize.Y,
				*Entry.Bounds.ToString());
		}
	}

	if (Profile->bUseSpawnRegions && Regions.IsEmpty())
	{
		UE_LOG(LogEncounterDirector, Warning, TEXT("SpawnDiag: Fixed population build skipped: bUseSpawnRegions is true but no valid spawn regions were found on %s"), *GetNameSafe(this));
		return;
	}

	auto ChooseRegionIndex = [this](const TArray<FFixedSpawnRegionEntry>& Entries) -> int32
	{
		double TotalWeight = 0.0;
		for (const FFixedSpawnRegionEntry& Entry : Entries)
		{
			if (FMath::IsFinite(Entry.Weight))
			{
				TotalWeight += FMath::Max(0.f, Entry.Weight);
			}
		}

		if (!FMath::IsFinite(TotalWeight) || TotalWeight <= KINDA_SMALL_NUMBER)
		{
			return INDEX_NONE;
		}

		double Roll = static_cast<double>(FixedSpawnStream.FRand()) * TotalWeight;
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			Roll -= FMath::IsFinite(Entries[Index].Weight) ? FMath::Max(0.f, Entries[Index].Weight) : 0.f;
			if (Roll <= 0.f)
			{
				return Index;
			}
		}

		return Entries.Num() > 0 ? 0 : INDEX_NONE;
	};

	TArray<int32> RegionAssignments;
	if (!Regions.IsEmpty() && ClusterCount >= Regions.Num())
	{
		RegionAssignments.Reserve(ClusterCount);
		for (int32 Index = 0; Index < Regions.Num(); ++Index)
		{
			RegionAssignments.Add(Index);
		}

		for (int32 Index = Regions.Num(); Index < ClusterCount; ++Index)
		{
			const int32 RegionIndex = ChooseRegionIndex(Regions);
			RegionAssignments.Add(RegionIndex);
		}

		for (int32 Index = RegionAssignments.Num() - 1; Index > 0; --Index)
		{
			const int32 SwapIndex = FixedSpawnStream.RandRange(0, Index);
			RegionAssignments.Swap(Index, SwapIndex);
		}
	}

	TArray<FFixedSpawnCluster> ClusterList;
	ClusterList.Reserve(ClusterCount);

	TArray<float> DensitySamples;
	DensitySamples.Reserve(ClusterCount);

	int32 RemainingEnemies = ResolvedTarget;
	for (int32 ClusterIndex = 0; ClusterIndex < ClusterCount; ++ClusterIndex)
	{
		const int32 ClustersLeft = ClusterCount - ClusterIndex;
		int32 MinAllowed = RemainingEnemies - ((ClustersLeft - 1) * MaxClusterSize);
		int32 MaxAllowed = RemainingEnemies - ((ClustersLeft - 1) * MinClusterSize);
		MinAllowed = FMath::Clamp(MinAllowed, MinClusterSize, MaxClusterSize);
		MaxAllowed = FMath::Clamp(MaxAllowed, MinClusterSize, MaxClusterSize);
		if (MaxAllowed < MinAllowed)
		{
			MaxAllowed = MinAllowed;
		}

		int32 RegionIndex = INDEX_NONE;
		if (RegionAssignments.IsValidIndex(ClusterIndex))
		{
			RegionIndex = RegionAssignments[ClusterIndex];
		}
		else if (!Regions.IsEmpty())
		{
			RegionIndex = ChooseRegionIndex(Regions);
		}

		const FFixedSpawnRegionEntry* RegionEntry = Regions.IsValidIndex(RegionIndex) ? &Regions[RegionIndex] : nullptr;

		float DensityAlpha = FixedSpawnStream.FRand();
		if (Profile->DensityCurve)
		{
			DensityAlpha = Profile->DensityCurve->GetFloatValue(DensityAlpha);
		}
		else
		{
			const float DensityExponent = ResolveFiniteFloat(Profile->DensityExponent, 1.5f, 0.01f, 100.f);
			DensityAlpha = FMath::Pow(DensityAlpha, DensityExponent);
		}

		DensityAlpha = ResolveFiniteFloat(DensityAlpha, 0.f, 0.f, 1.f);
		if (RegionEntry)
		{
			DensityAlpha = FMath::Clamp(DensityAlpha * RegionEntry->DensityScale, 0.f, 1.f);
		}

		int32 ClusterSize = FMath::RoundToInt(FMath::Lerp(static_cast<float>(MinAllowed), static_cast<float>(MaxAllowed), DensityAlpha));
		ClusterSize = FMath::Clamp(ClusterSize, MinAllowed, MaxAllowed);

		const float RadiusMin = ResolveFiniteFloat(Profile->ClusterRadiusMin, 500.f, 0.f, MaxWorldDistance);
		const float RadiusMax = ResolveFiniteFloat(Profile->ClusterRadiusMax, 1400.f, RadiusMin, MaxWorldDistance);
		float Radius = FMath::Lerp(RadiusMin, RadiusMax, DensityAlpha);
		if (RegionEntry)
		{
			Radius *= RegionEntry->RadiusScale;
		}
		Radius = ResolveFiniteFloat(Radius, RadiusMin, 0.f, MaxWorldDistance);

		FVector ClusterCenter = GetActorLocation();
		const float MinClusterSpacing = ResolveFiniteFloat(Profile->MinClusterSpacing, 1500.f, 0.f, MaxWorldDistance);
		if (!ResolveFixedClusterCenter(RegionEntry, FixedClusterCenters, MinClusterSpacing, ClusterCenter))
		{
			if (RegionEntry)
			{
				ClusterCenter = RegionEntry->Bounds.GetCenter();
			}
			else if (CachedPlayerPawn.IsValid())
			{
				ClusterCenter = CachedPlayerPawn->GetActorLocation();
			}
		}
		if (ClusterCenter.ContainsNaN())
		{
			continue;
		}

		if (RegionEntry && RegionEntry->Bounds.IsValid)
		{
			const float MaxRadiusX = FMath::Min(ClusterCenter.X - RegionEntry->Bounds.Min.X, RegionEntry->Bounds.Max.X - ClusterCenter.X);
			const float MaxRadiusY = FMath::Min(ClusterCenter.Y - RegionEntry->Bounds.Min.Y, RegionEntry->Bounds.Max.Y - ClusterCenter.Y);
			const float MaxRadius = FMath::Max(0.f, FMath::Min(MaxRadiusX, MaxRadiusY));
			if (MaxRadius < Radius && ClusterIndex < 5)
			{
				UE_LOG(LogEncounterDirector, Log, TEXT("SpawnDiag: Cluster %d radius clamped %.1f -> %.1f to stay inside region bounds"),
					ClusterIndex,
					Radius,
					MaxRadius);
			}
			Radius = FMath::Min(Radius, MaxRadius);
		}

		if (ClusterIndex < 5)
		{
			UE_LOG(LogEncounterDirector, Log, TEXT("SpawnDiag: Cluster %d region=%s density=%.2f radius=%.1f center=%s"),
				ClusterIndex,
				*GetNameSafe(RegionEntry ? RegionEntry->Region.Get() : nullptr),
				DensityAlpha,
				Radius,
				*ClusterCenter.ToCompactString());
		}

		FixedClusterCenters.Add(ClusterCenter);

		FFixedSpawnCluster Cluster;
		Cluster.ClusterId = ClusterIndex;
		Cluster.Center = ClusterCenter;
		Cluster.Radius = Radius;
		Cluster.RegionBounds = RegionEntry ? RegionEntry->Bounds : FBox(EForceInit::ForceInit);
		Cluster.bHasRegion = RegionEntry != nullptr;
		Cluster.DensityAlpha = DensityAlpha;
		Cluster.EliteChanceBonus = RegionEntry ? RegionEntry->EliteChanceBonus : 0.f;
		Cluster.bAllowElites = RegionEntry ? RegionEntry->bAllowElites : true;
		Cluster.TotalEnemies = ClusterSize;
		Cluster.RemainingEnemies = ClusterSize;

		ClusterList.Add(Cluster);
		DensitySamples.Add(DensityAlpha);
		RemainingEnemies -= ClusterSize;
	}

	if (RemainingEnemies > 0 && !ClusterList.IsEmpty())
	{
		ClusterList.Last().TotalEnemies += RemainingEnemies;
		ClusterList.Last().RemainingEnemies += RemainingEnemies;
		RemainingEnemies = 0;
	}

	float DenseThreshold = 1.0f;
	const float DensePercentile = ResolveFiniteFloat(Profile->DenseClusterPercentile, 0.2f, 0.f, 1.f);
	if (DensePercentile > 0.f && DensitySamples.Num() > 0)
	{
		DensitySamples.Sort();
		const int32 Index = FMath::Clamp(FMath::RoundToInt((1.f - DensePercentile) * static_cast<float>(DensitySamples.Num() - 1)), 0, DensitySamples.Num() - 1);
		DenseThreshold = DensitySamples[Index];
	}

	for (FFixedSpawnCluster& Cluster : ClusterList)
	{
		Cluster.bDenseCluster = DensePercentile > 0.f && Cluster.DensityAlpha >= DenseThreshold;
		FixedClusters.Add(Cluster.ClusterId, Cluster);
	}

	FixedClustersRemaining = FixedClusters.Num();

	for (const FFixedSpawnCluster& Cluster : ClusterList)
	{
		for (int32 SpawnIndex = 0; SpawnIndex < Cluster.TotalEnemies; ++SpawnIndex)
		{
			const UEnemySpawnGroupDefinition* Group = ChooseFixedSpawnGroup(GroupEntries);
			if (!Group)
			{
				continue;
			}

			FFixedSpawnRequest Request;
			Request.Group = Group;
			Request.ClusterId = Cluster.ClusterId;
			Request.ClusterCenter = Cluster.Center;
			Request.ClusterRadius = Cluster.Radius;
			Request.DensityAlpha = Cluster.DensityAlpha;
			Request.EliteChanceBonus = Cluster.EliteChanceBonus;
			Request.bDenseCluster = Cluster.bDenseCluster;
			Request.bAllowElites = Cluster.bAllowElites;
			FixedSpawnQueue.Add(Request);
		}
	}

	for (int32 Index = FixedSpawnQueue.Num() - 1; Index > 0; --Index)
	{
		const int32 SwapIndex = FixedSpawnStream.RandRange(0, Index);
		FixedSpawnQueue.Swap(Index, SwapIndex);
	}

	FixedPopulationTarget = FixedSpawnQueue.Num();
	FixedPopulationRemaining = FixedPopulationTarget;

	UE_LOG(LogEncounterDirector, Log, TEXT("Fixed population plan: %d clusters, %d enemies (seed=%d)."),
		FixedClusters.Num(),
		FixedPopulationTarget,
		FixedSpawnSeed);
}

void AAeyerjiEncounterDirector::ProcessFixedSpawnQueue()
{
	if (!HasAuthority() || !bFixedPopulationActive)
	{
		return;
	}

	const UAeyerjiWorldSpawnProfile* Profile = FixedSpawnProfile.Get();
	if (!Profile || FixedSpawnQueue.IsEmpty())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const int32 RawSpawnBudget = Profile->MaxFixedSpawnsPerTickOverride > 0
		? Profile->MaxFixedSpawnsPerTickOverride
		: MaxSpawnsPerTick;
	const int32 SpawnBudget = FMath::Clamp(RawSpawnBudget, 1, MaxSpawnsPerDirectorTick);
	int32 SpawnedThisTick = 0;
	int32 AttemptsThisTick = 0;
	const float WorkMilliseconds = ResolveFiniteFloat(
		MaxSpawnWorkMillisecondsPerTick,
		2.f,
		0.f,
		MaxDirectorWorkMilliseconds);
	const double SpawnDeadline = FPlatformTime::Seconds() + (WorkMilliseconds / 1000.0);
	const auto HandleSpawnSkip = [this](int32 ClusterId)
	{
		HandleFixedPopulationClusterDecrement(ClusterId);
		FixedPopulationTarget = FMath::Max(0, FixedPopulationTarget - 1);
		FixedPopulationRemaining = FMath::Max(0, FixedPopulationTarget - FixedPopulationSpawned);
		UpdateTotalToKill(FixedPopulationTarget);
	};

	while (AttemptsThisTick < SpawnBudget
		&& FixedSpawnQueue.Num() > 0
		&& (AttemptsThisTick == 0 || WorkMilliseconds <= 0.f || FPlatformTime::Seconds() < SpawnDeadline))
	{
		const FFixedSpawnRequest Request = FixedSpawnQueue[0];
		FixedSpawnQueue.RemoveAtSwap(0);
		++AttemptsThisTick;

		const UEnemySpawnGroupDefinition* Group = Request.Group.Get();
		if (!Group)
		{
			HandleSpawnSkip(Request.ClusterId);
			continue;
		}

		TSubclassOf<AEnemyParentNative> EnemyClass = Group->ResolveEnemyClass();
		if (!*EnemyClass)
		{
			HandleSpawnSkip(Request.ClusterId);
			continue;
		}

		bool bSpawnElite = false;

		if (Request.bAllowElites)
		{
			float EliteChance = ResolveFiniteFloat(Profile->BaseEliteChance, 0.12f, 0.f, 1.f)
				+ ResolveFiniteFloat(Request.EliteChanceBonus, 0.f, -1.f, 1.f);
			EliteChance += ResolveFiniteFloat(Request.DensityAlpha, 0.f, 0.f, 1.f)
				* ResolveFiniteFloat(Profile->DensityEliteChanceScale, 0.2f, 0.f, 1.f);

			if (Request.bDenseCluster)
			{
				EliteChance += ResolveFiniteFloat(Profile->DenseEliteChanceBonus, 0.25f, 0.f, 1.f);
			}

			const float EliteChanceCap = ResolveFiniteFloat(Profile->EliteChanceCap, 0.6f, 0.f, 1.f);
			EliteChance = ResolveFiniteFloat(EliteChance, 0.f, 0.f, EliteChanceCap);
			bSpawnElite = FixedSpawnStream.FRand() <= EliteChance;
		}

		if (bSpawnElite)
		{
			TSubclassOf<AEnemyParentNative> EliteClass = Group->ResolveEliteEnemyClass();
			if (*EliteClass)
			{
				EnemyClass = EliteClass;
			}
			else
			{
				// No explicit elite class was configured for this group, so skip elite promotion.
				bSpawnElite = false;
			}
		}

		const float HalfHeight = GetEnemyHalfHeight(EnemyClass);
		const FFixedSpawnCluster* Cluster = FixedClusters.Find(Request.ClusterId);
		const FBox RegionBounds = (Cluster && Cluster->bHasRegion) ? Cluster->RegionBounds : FBox(EForceInit::ForceInit);
		const bool bHasRegion = Cluster && Cluster->bHasRegion && RegionBounds.IsValid;
		const FVector SpawnLocation = ResolveFixedSpawnLocation(Request.ClusterCenter, Request.ClusterRadius, HalfHeight, RegionBounds, bHasRegion);
		const FRotator SpawnRotation = CachedPlayerPawn.IsValid()
			? (CachedPlayerPawn->GetActorLocation() - SpawnLocation).Rotation()
			: FRotator::ZeroRotator;

		FEnemySet EnemyTemplate;
		EnemyTemplate.EnemyClass = EnemyClass;
		EnemyTemplate.Count = 1;
		EnemyTemplate.SpawnInterval = 0.0f;
		EnemyTemplate.EnemyArchetypeTag = ResolveArchetypeTagFromClass(EnemyClass);
		EnemyTemplate.bIsElite = bSpawnElite;

		UE_LOG(LogEncounterDirector, Log,
			TEXT("FixedSpawn request: Director=%s ClusterId=%d Group=%s Class=%s Elite=%d SkipEliteAutoScaling=%d Density=%.2f EliteChanceBonus=%.2f"),
			*GetNameSafe(this),
			Request.ClusterId,
			*GetNameSafe(Group),
			*GetNameSafe(EnemyTemplate.EnemyClass),
			EnemyTemplate.bIsElite ? 1 : 0,
			EnemyTemplate.bSkipEliteAutoScaling ? 1 : 0,
			Request.DensityAlpha,
			Request.EliteChanceBonus);

		const bool bApplyAggro = Profile->bApplyAggroOnSpawn;
		AAeyerjiSpawnerGroup* Spawner = FixedPopulationSpawner.Get();
		const FTransform SpawnTransform(SpawnRotation, SpawnLocation);

		APawn* SpawnedPawn = UAeyerjiEnemyManagementBPFL::SpawnAndRegisterEnemyFromSet(
			this,
			EnemyTemplate,
			SpawnTransform,
			Spawner,
			/*Owner=*/this,
			CachedPlayerPawn.Get(),
			/*bApplyEliteSettings=*/true,
			bApplyAggro,
			/*bAutoActivate=*/true,
			/*bAutoActivateOnlyIfNoWaves=*/true,
			CachedPlayerPawn.Get(),
			CachedPlayerController.Get(),
			/*bSkipRandomEliteResolution=*/true);
		AEnemyParentNative* SpawnedEnemy = Cast<AEnemyParentNative>(SpawnedPawn);
		if (!SpawnedEnemy)
		{
			HandleSpawnSkip(Request.ClusterId);
			continue;
		}

		SnapActorToGround(SpawnedEnemy, HalfHeight);
		RegisterFixedClusterEnemy(SpawnedEnemy, Request.ClusterId);

		if (!Spawner && FixedPopulationSpawned == 0)
		{
			UE_LOG(LogEncounterDirector, Warning, TEXT("Fixed population on %s has no spawn manager; elite scaling will be skipped."), *GetNameSafe(this));
		}

		FixedPopulationSpawned++;
		FixedPopulationRemaining = FMath::Max(0, FixedPopulationTarget - FixedPopulationSpawned);
		SpawnedThisTick++;
	}

	if (FixedSpawnQueue.IsEmpty())
	{
		NotifyFixedPopulationInitialSpawnComplete();
	}
}

const UEnemySpawnGroupDefinition* AAeyerjiEncounterDirector::ChooseFixedSpawnGroup(const TArray<FFixedSpawnGroupEntry>& Groups)
{
	double TotalWeight = 0.0;
	for (const FFixedSpawnGroupEntry& Entry : Groups)
	{
		if (Entry.Group.IsValid() && FMath::IsFinite(Entry.Weight))
		{
			TotalWeight += FMath::Max(0.f, Entry.Weight);
		}
	}

	if (!FMath::IsFinite(TotalWeight) || TotalWeight <= KINDA_SMALL_NUMBER)
	{
		return nullptr;
	}

	double Roll = static_cast<double>(FixedSpawnStream.FRand()) * TotalWeight;
	for (const FFixedSpawnGroupEntry& Entry : Groups)
	{
		if (!Entry.Group.IsValid())
		{
			continue;
		}

		Roll -= FMath::IsFinite(Entry.Weight) ? FMath::Max(0.f, Entry.Weight) : 0.f;
		if (Roll <= 0.f)
		{
			return Entry.Group.Get();
		}
	}

	return Groups.Num() > 0 ? Groups[0].Group.Get() : nullptr;
}

bool AAeyerjiEncounterDirector::ResolveFixedClusterCenter(const FFixedSpawnRegionEntry* RegionEntry, const TArray<FVector>& ExistingCenters, float MinSpacing, FVector& OutCenter)
{
	const UAeyerjiWorldSpawnProfile* Profile = FixedSpawnProfile.Get();
	if (!Profile)
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const int32 Attempts = FMath::Clamp(Profile->ClusterCenterSearchAttempts, 1, MaxSpawnAttempts);
	const float SafeMinSpacing = ResolveFiniteFloat(MinSpacing, 0.f, 0.f, MaxWorldDistance);
	const float MinSpacingSq = SafeMinSpacing > 0.f ? FMath::Square(SafeMinSpacing) : 0.f;
	const FVector ProjectionExtent(ResolveFiniteFloat(Profile->NavProjectionExtent, 1200.f, 0.f, MaxWorldDistance));
	const FVector FallbackOrigin = CachedPlayerPawn.IsValid() ? CachedPlayerPawn->GetActorLocation() : GetActorLocation();
	const float FallbackRadius = ResolveFiniteFloat(Profile->FallbackSpawnRadius, 6000.f, 0.f, MaxWorldDistance);
	const bool bHasRegion = RegionEntry != nullptr;

	static int32 FallbackLogCount = 0;
	if (!bHasRegion && FallbackLogCount < 10)
	{
		UE_LOG(LogEncounterDirector, Log, TEXT("SpawnDiag: No region assigned. Using fallback origin=%s radius=%.1f"),
			*FallbackOrigin.ToString(),
			FallbackRadius);
		++FallbackLogCount;
	}

	for (int32 Attempt = 0; Attempt < Attempts; ++Attempt)
	{
		FVector Candidate = FallbackOrigin;

		if (RegionEntry && IsFiniteBox(RegionEntry->Bounds))
		{
			const FBox& Bounds = RegionEntry->Bounds;
			Candidate.X = FixedSpawnStream.FRandRange(Bounds.Min.X, Bounds.Max.X);
			Candidate.Y = FixedSpawnStream.FRandRange(Bounds.Min.Y, Bounds.Max.Y);
			Candidate.Z = FixedSpawnStream.FRandRange(Bounds.Min.Z, Bounds.Max.Z);
		}
		else if (FallbackRadius > 0.f)
		{
			const float Angle = FixedSpawnStream.FRandRange(0.f, 2.f * PI);
			const float Distance = FMath::Sqrt(FixedSpawnStream.FRand()) * FallbackRadius;
			Candidate += FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 0.f);
		}

		if (NavSys)
		{
			FNavLocation Projected;
			if (NavSys->ProjectPointToNavigation(Candidate, Projected, ProjectionExtent))
			{
				Candidate = Projected.Location;
			}
			else if (!bHasRegion && FallbackRadius > 0.f)
			{
				FNavLocation Reachable;
				if (NavSys->GetRandomReachablePointInRadius(Candidate, FallbackRadius, Reachable))
				{
					Candidate = Reachable.Location;
				}
			}
		}

		if (Candidate.ContainsNaN())
		{
			continue;
		}

		if (bHasRegion && !RegionEntry->Bounds.IsInsideXY(Candidate))
		{
			continue;
		}

		if (MinSpacingSq > 0.f)
		{
			bool bTooClose = false;
			for (const FVector& Center : ExistingCenters)
			{
				if (FVector::DistSquared2D(Candidate, Center) < MinSpacingSq)
				{
					bTooClose = true;
					break;
				}
			}

			if (bTooClose)
			{
				continue;
			}
		}

		OutCenter = Candidate;
		return true;
	}

	return false;
}

FVector AAeyerjiEncounterDirector::ResolveFixedSpawnLocation(const FVector& ClusterCenter, float Radius, float HalfHeight, const FBox& RegionBounds, bool bHasRegion)
{
	if (ClusterCenter.ContainsNaN() || !FMath::IsFinite(HalfHeight))
	{
		return GetActorLocation();
	}
	UWorld* World = GetWorld();
	if (!World)
	{
		return ClusterCenter + FVector(0.f, 0.f, HalfHeight);
	}

	const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const int32 Attempts = FMath::Clamp(SpawnLocationSearchAttempts, 1, MaxSpawnAttempts);
	const bool bUseRegionBounds = bHasRegion && IsFiniteBox(RegionBounds);
	float SafeRadius = ResolveFiniteFloat(Radius, 0.f, 0.f, MaxWorldDistance);
	if (bUseRegionBounds)
	{
		const float MaxRadiusX = FMath::Min(ClusterCenter.X - RegionBounds.Min.X, RegionBounds.Max.X - ClusterCenter.X);
		const float MaxRadiusY = FMath::Min(ClusterCenter.Y - RegionBounds.Min.Y, RegionBounds.Max.Y - ClusterCenter.Y);
		const float MaxRadius = FMath::Max(0.f, FMath::Min(MaxRadiusX, MaxRadiusY));
		SafeRadius = FMath::Min(SafeRadius, MaxRadius);
	}
	const float MinDistance = ResolveFiniteFloat(MinSpawnDistanceFromPlayer, 0.f, 0.f, MaxWorldDistance);
	static int32 OutOfBoundsLogCount = 0;
	static int32 FallbackLogCount = 0;

	for (int32 Attempt = 0; Attempt < Attempts; ++Attempt)
	{
		FVector Candidate = ClusterCenter;
		bool bFoundNav = false;

		if (NavSys && SafeRadius > 0.f)
		{
			FNavLocation Result;
			if (NavSys->GetRandomReachablePointInRadius(ClusterCenter, SafeRadius, Result))
			{
				Candidate = Result.Location;
				bFoundNav = true;
			}
		}

		if (!bFoundNav && SafeRadius > 0.f)
		{
			const float Angle = FixedSpawnStream.FRandRange(0.f, 2.f * PI);
			const float Distance = FMath::Sqrt(FixedSpawnStream.FRand()) * SafeRadius;
			Candidate += FVector(FMath::Cos(Angle) * Distance, FMath::Sin(Angle) * Distance, 0.f);
		}

		Candidate.Z += HalfHeight;

		if (bUseRegionBounds && !RegionBounds.IsInsideXY(Candidate))
		{
			if (OutOfBoundsLogCount < 10)
			{
				UE_LOG(LogEncounterDirector, Log, TEXT("SpawnDiag: Reject fixed spawn outside region (center=%s candidate=%s boundsMin=%s boundsMax=%s)"),
					*ClusterCenter.ToCompactString(),
					*Candidate.ToCompactString(),
					*RegionBounds.Min.ToCompactString(),
					*RegionBounds.Max.ToCompactString());
				OutOfBoundsLogCount++;
			}
			continue;
		}

		const FVector TraceStart = Candidate + FVector(0.f, 0.f, GroundTraceUpOffset);
		const FVector TraceEnd = Candidate - FVector(0.f, 0.f, GroundTraceDownDistance);

		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(EncounterDirector_FixedGroundTrace), false, CachedPlayerPawn.Get());
		if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params))
		{
			Candidate.Z = Hit.ImpactPoint.Z + SpawnGroundOffset + HalfHeight;
		}

		if (IsSpawnCandidateAllowed(Candidate, MinDistance))
		{
			return Candidate;
		}
	}

	if (bUseRegionBounds && FallbackLogCount < 5)
	{
		UE_LOG(LogEncounterDirector, Log, TEXT("SpawnDiag: Fixed spawn fallback to cluster center (center=%s boundsMin=%s boundsMax=%s radius=%.1f)"),
			*ClusterCenter.ToCompactString(),
			*RegionBounds.Min.ToCompactString(),
			*RegionBounds.Max.ToCompactString(),
			SafeRadius);
		FallbackLogCount++;
	}

	return ClusterCenter + FVector(0.f, 0.f, HalfHeight);
}

void AAeyerjiEncounterDirector::RegisterFixedClusterEnemy(AEnemyParentNative* Enemy, int32 ClusterId)
{
	if (!IsValid(Enemy))
	{
		return;
	}

	RegisterSpawnedEnemy(Enemy);

	if (ClusterId != INDEX_NONE)
	{
		FixedEnemyClusterMap.Add(Enemy, ClusterId);
		FixedClusterMembers.FindOrAdd(ClusterId).Add(Enemy);

		if (FFixedSpawnCluster* Cluster = FixedClusters.Find(ClusterId))
		{
			if (Cluster->bSleeping)
			{
				ApplyEnemySleepState(Enemy, true);
			}
		}
	}
}

void AAeyerjiEncounterDirector::HandleFixedPopulationEnemyRemoved(AActor* Enemy)
{
	if (!Enemy)
	{
		return;
	}

	int32* ClusterId = FixedEnemyClusterMap.Find(Enemy);
	if (!ClusterId)
	{
		return;
	}

	const int32 ResolvedClusterId = *ClusterId;
	FixedEnemyClusterMap.Remove(Enemy);
	RemoveFixedClusterMember(ResolvedClusterId, Enemy);

	HandleFixedPopulationClusterDecrement(ResolvedClusterId);
}

void AAeyerjiEncounterDirector::HandleFixedPopulationClusterDecrement(int32 ClusterId)
{
	if (ClusterId == INDEX_NONE)
	{
		return;
	}

	FFixedSpawnCluster* Cluster = FixedClusters.Find(ClusterId);
	if (!Cluster)
	{
		return;
	}

	Cluster->RemainingEnemies = FMath::Max(0, Cluster->RemainingEnemies - 1);
	if (Cluster->RemainingEnemies > 0)
	{
		return;
	}

	const float DensityAlpha = Cluster->DensityAlpha;
	const bool bDenseCluster = Cluster->bDenseCluster;

	FixedClusters.Remove(ClusterId);
	FixedClustersRemaining = FMath::Max(0, FixedClustersRemaining - 1);
	FixedClusterMembers.Remove(ClusterId);

	OnFixedClusterCleared.Broadcast(ClusterId, DensityAlpha, bDenseCluster);

	if (FixedClustersRemaining <= 0 && !bFixedPopulationComplete)
	{
		bFixedPopulationComplete = true;
		OnFixedPopulationCleared.Broadcast();
	}
}

void AAeyerjiEncounterDirector::NotifyFixedPopulationInitialSpawnComplete()
{
	if (!bFixedPopulationActive || bFixedPopulationInitialSpawnComplete)
	{
		return;
	}

	bFixedPopulationInitialSpawnComplete = true;

	UE_LOG(LogEncounterDirector, Display,
		TEXT("Fixed population initial spawn complete on %s (Spawned=%d Remaining=%d Target=%d)"),
		*GetNameSafe(this),
		FixedPopulationSpawned,
		FixedPopulationRemaining,
		FixedPopulationTarget);

	OnFixedPopulationInitialSpawnComplete.Broadcast(this);
}

FGameplayTag AAeyerjiEncounterDirector::ResolveArchetypeTagFromClass(TSubclassOf<AEnemyParentNative> EnemyClass) const
{
	if (!*EnemyClass)
	{
		return FGameplayTag();
	}

	const AEnemyParentNative* CDO = EnemyClass->GetDefaultObject<AEnemyParentNative>();
	if (!CDO)
	{
		return FGameplayTag();
	}

	const UAeyerjiEnemyArchetypeComponent* ArchetypeComp = CDO->FindComponentByClass<UAeyerjiEnemyArchetypeComponent>();
	if (!ArchetypeComp || !ArchetypeComp->HasArchetypeData())
	{
		return FGameplayTag();
	}

	return ArchetypeComp->GetArchetypeTag();
}

bool AAeyerjiEncounterDirector::IsSpawnCandidateAllowed(const FVector& Candidate, float MinDistance) const
{
	if (Candidate.ContainsNaN())
	{
		return false;
	}

	if (!CachedPlayerPawn.IsValid())
	{
		return true;
	}

	MinDistance = ResolveFiniteFloat(MinDistance, 0.f, 0.f, MaxWorldDistance);
	if (MinDistance > 0.f)
	{
		const float MinDistanceSq = FMath::Square(MinDistance);
		if (FVector::DistSquared2D(Candidate, CachedPlayerPawn->GetActorLocation()) < MinDistanceSq)
		{
			return false;
		}
	}

	if (IsNearRecentPlayerPath(Candidate))
	{
		return false;
	}

	if (IsSpawnLocationVisible(Candidate))
	{
		return false;
	}

	return true;
}

bool AAeyerjiEncounterDirector::IsNearRecentPlayerPath(const FVector& Candidate) const
{
	if (Candidate.ContainsNaN())
	{
		return true;
	}

	if (!bAvoidRecentPlayerPath || RecentPlayerSamples.IsEmpty())
	{
		return false;
	}

	const float Radius = ResolveFiniteFloat(RecentPathAvoidRadius, 600.f, 0.f, MaxWorldDistance);
	if (Radius <= 0.f)
	{
		return false;
	}

	const float RadiusSq = FMath::Square(Radius);
	for (const FRecentPlayerSample& Sample : RecentPlayerSamples)
	{
		if (!Sample.Location.ContainsNaN()
			&& FVector::DistSquared2D(Candidate, Sample.Location) <= RadiusSq)
		{
			return true;
		}
	}

	return false;
}

bool AAeyerjiEncounterDirector::IsSpawnLocationVisible(const FVector& Candidate) const
{
	if (Candidate.ContainsNaN())
	{
		return true;
	}

	if (!bAvoidPlayerForwardSpawnCone || !CachedPlayerPawn.IsValid())
	{
		return false;
	}

	const float SafeForwardConeDegrees = ResolveFiniteFloat(ForwardSpawnConeDegrees, 120.f, 0.f, 180.f);
	if (SafeForwardConeDegrees <= 0.f)
	{
		return false;
	}

	const FVector PlayerLocation = CachedPlayerPawn->GetActorLocation();
	const FVector Forward2D = CachedPlayerPawn->GetActorForwardVector().GetSafeNormal2D();
	FVector ToCandidate2D = Candidate - PlayerLocation;
	ToCandidate2D.Z = 0.f;

	if (ToCandidate2D.IsNearlyZero() || Forward2D.IsNearlyZero())
	{
		return true;
	}

	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(SafeForwardConeDegrees * 0.5f));
	const float Dot = FVector::DotProduct(Forward2D, ToCandidate2D.GetSafeNormal());

	if (Dot < CosHalfAngle)
	{
		return false;
	}

	if (!bUseLineOfSightForForwardCone)
	{
		return true;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return true;
	}

	FVector ViewLocation;
	FRotator ViewRotation;
	CachedPlayerPawn->GetActorEyesViewPoint(ViewLocation, ViewRotation);

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EncounterDirector_SpawnLOS), false, CachedPlayerPawn.Get());
	const bool bHit = World->LineTraceSingleByChannel(Hit, ViewLocation, Candidate, ECC_Visibility, Params);

	return !bHit;
}

bool AAeyerjiEncounterDirector::ShouldTriggerEncounter()
{
	if (bFixedPopulationActive)
	{
		return false;
	}

	if (DirectorState != EEncounterDirectorState::Idle)
	{
		return false;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const double Now = World->GetTimeSeconds();
	const bool bCanLog = bDrawDebug &&
		DebugLogIntervalSeconds > 0.f &&
		(Now - LastDebugLogTimestamp) >= DebugLogIntervalSeconds;
	bool bLogged = false;

	if (!CachedPlayerPawn.IsValid())
	{
		if (bCanLog && !bLogged)
		{
			bLogged = true;
			LastDebugLogTimestamp = Now;
			UE_LOG(LogEncounterDirector, Verbose, TEXT("Gate: no valid player pawn."));
		}
		return false;
	}

	if (SpawnGroups.IsEmpty())
	{
		if (bCanLog && !bLogged)
		{
			bLogged = true;
			LastDebugLogTimestamp = Now;
			UE_LOG(LogEncounterDirector, Verbose, TEXT("Gate: SpawnGroups is empty."));
		}
		return false;
	}

	if (LiveEnemies.Num() > 0)
	{
		if (bCanLog && !bLogged)
		{
			bLogged = true;
			LastDebugLogTimestamp = Now;
			UE_LOG(LogEncounterDirector, Verbose, TEXT("Gate: waiting on %d live enemies."), LiveEnemies.Num());
		}
		return false;
	}

	if (!PendingSpawnRequests.IsEmpty())
	{
		if (bCanLog && !bLogged)
		{
			bLogged = true;
			LastDebugLogTimestamp = Now;
			UE_LOG(LogEncounterDirector, Verbose, TEXT("Gate: pending spawn requests (%d remaining)."), PendingSpawnRequests.Num());
		}
		return false;
	}

	const double TimeSinceEncounter = Now - LastEncounterTimestamp;
	const double TimeSinceKill = (LastKillTimestamp > 0.0) ? (Now - LastKillTimestamp) : TimeSinceEncounter;

	// Only spawn when the player is killing quickly enough.
	if (CurrentKillVelocity < KillVelocitySpawnFloor)
	{
		if (bCanLog && !bLogged)
		{
			bLogged = true;
			LastDebugLogTimestamp = Now;
			UE_LOG(LogEncounterDirector, Verbose, TEXT("Gate: KillVelocity %.2f < Floor %.2f (Window=%.2fs)."),
				CurrentKillVelocity,
				KillVelocitySpawnFloor,
				KillVelocityWindowSeconds);
		}
		return false;
	}

	const float SpeedAlpha = GetKillSpeedAlpha();
	const float RequiredDistance = FMath::Lerp(MinDistanceAtSlow, MinDistanceAtFast, SpeedAlpha);
	const float RequiredDowntime = FMath::Lerp(MinDowntimeAtSlow, MinDowntimeAtFast, SpeedAlpha);

	const bool bDistanceRequirement = DistanceFromLastEncounter >= FMath::Max(RequiredDistance, MinDistanceBetweenEncounters);
	const bool bDowntimeWindow = TimeSinceKill >= RequiredDowntime;

	if (bCanLog && !bLogged)
	{
		bLogged = true;
		LastDebugLogTimestamp = Now;
		UE_LOG(LogEncounterDirector, Verbose, TEXT("Gate: Dist %.1f/%.1f (Min=%.1f) Downtime %.2f/%.2f KillVel %.2f (Floor=%.2f Ceil=%.2f)."),
			DistanceFromLastEncounter,
			RequiredDistance,
			MinDistanceBetweenEncounters,
			TimeSinceKill,
			RequiredDowntime,
			CurrentKillVelocity,
			KillVelocitySpawnFloor,
			KillVelocitySpawnCeil);
	}

	return bDistanceRequirement && bDowntimeWindow;
}

void AAeyerjiEncounterDirector::TriggerEncounter()
{
	if (!HasAuthority())
	{
		return;
	}

	const UEnemySpawnGroupDefinition* Group = ChooseSpawnGroup();
	if (!Group || !CachedPlayerPawn.IsValid())
	{
		if (bDrawDebug)
		{
			UE_LOG(LogEncounterDirector, Verbose, TEXT("Trigger failed: Group=%s PlayerPawnValid=%d"),
				*GetNameSafe(Group),
				CachedPlayerPawn.IsValid() ? 1 : 0);
		}
		return;
	}

	LastEncounterLocation = CachedPlayerPawn->GetActorLocation();
	LastEncounterTimestamp = GetWorld()->GetTimeSeconds();
	LastSpawnedGroup = Group;

	if (bDrawDebug)
	{
		DrawDebugSphere(GetWorld(), LastEncounterLocation, 64.f, 12, FColor::Orange, false, 2.f);
	}

	const float SpeedAlpha = GetKillSpeedAlpha();
	const int32 SafeMaxGroups = FMath::Clamp(MaxGroupsPerTrigger, 1, MaxEncounterSpawnGroups);
	const int32 BurstCount = FMath::Clamp(
		1 + FMath::RoundToInt(SpeedAlpha * (SafeMaxGroups - 1)),
		1,
		SafeMaxGroups);
	int32 Enqueued = 0;
	for (int32 Burst = 0; Burst < BurstCount; ++Burst)
	{
		const UEnemySpawnGroupDefinition* BurstGroup = (Burst == 0) ? Group : ChooseSpawnGroup();
		if (BurstGroup)
		{
			Enqueued += QueueSpawnsFromGroup(BurstGroup);
		}
	}

	if (Enqueued <= 0)
	{
		if (bDrawDebug)
		{
			UE_LOG(LogEncounterDirector, Verbose, TEXT("Trigger skipped: no spawns queued (BurstCount=%d)."), BurstCount);
		}
		return;
	}

	ProcessSpawnQueue();
	EnterState(EEncounterDirectorState::InCombat);
}

const UEnemySpawnGroupDefinition* AAeyerjiEncounterDirector::ChooseSpawnGroup() const
{
	const UEnemySpawnGroupDefinition* Fallback = nullptr;
	const int32 GroupCount = FMath::Min(SpawnGroups.Num(), MaxEncounterSpawnGroups);
	for (int32 GroupIndex = 0; GroupIndex < GroupCount; ++GroupIndex)
	{
		const UEnemySpawnGroupDefinition* Group = SpawnGroups[GroupIndex];
		if (IsValid(Group) && !Group->EnemyTypes.IsEmpty())
		{
			Fallback = Group;
			break;
		}
	}

	if (!Fallback)
	{
		return nullptr;
	}

	const int32 Attempts = GroupCount * 2;
	for (int32 Attempt = 0; Attempt < Attempts; ++Attempt)
	{
		const int32 Index = FMath::RandHelper(GroupCount);
		const UEnemySpawnGroupDefinition* Candidate = SpawnGroups[Index];

		if (!IsValid(Candidate) || Candidate->EnemyTypes.IsEmpty())
		{
			continue;
		}

		if (!Candidate->bAllowBackToBackSelection && Candidate == LastSpawnedGroup.Get())
		{
			continue;
		}

		return Candidate;
	}

	return Fallback;
}

void AAeyerjiEncounterDirector::SpawnFromGroup(const UEnemySpawnGroupDefinition* Group)
{
	QueueSpawnsFromGroup(Group);
}

int32 AAeyerjiEncounterDirector::QueueSpawnsFromGroup(const UEnemySpawnGroupDefinition* Group)
{
	if (!HasAuthority() || !IsValid(Group))
	{
		return 0;
	}

	const int32 RemainingCapacity = FMath::Max(0, MaxEncounterPopulation - PendingSpawnRequests.Num());
	const int32 SpawnCount = FMath::Min(Group->ResolveSpawnCount(), RemainingCapacity);
	if (SpawnCount <= 0)
	{
		return 0;
	}

	PendingSpawnRequests.Reserve(FMath::Min(MaxEncounterPopulation, PendingSpawnRequests.Num() + SpawnCount));
	for (int32 Index = 0; Index < SpawnCount; ++Index)
	{
		PendingSpawnRequests.Add(Group);
	}

	return SpawnCount;
}

void AAeyerjiEncounterDirector::ProcessSpawnQueue()
{
	if (!HasAuthority())
	{
		return;
	}

	if (PendingSpawnRequests.IsEmpty())
	{
		return;
	}

	if (!CachedPlayerPawn.IsValid())
	{
		return;
	}

	const int32 SpawnBudget = FMath::Clamp(MaxSpawnsPerTick, 1, MaxSpawnsPerDirectorTick);
	int32 SpawnedThisTick = 0;
	int32 AttemptsThisTick = 0;
	const float WorkMilliseconds = ResolveFiniteFloat(
		MaxSpawnWorkMillisecondsPerTick,
		2.f,
		0.f,
		MaxDirectorWorkMilliseconds);
	const double SpawnDeadline = FPlatformTime::Seconds() + (WorkMilliseconds / 1000.0);

	while (AttemptsThisTick < SpawnBudget
		&& PendingSpawnRequests.Num() > 0
		&& (AttemptsThisTick == 0 || WorkMilliseconds <= 0.f || FPlatformTime::Seconds() < SpawnDeadline))
	{
		const TWeakObjectPtr<const UEnemySpawnGroupDefinition> GroupPtr = PendingSpawnRequests[0];
		PendingSpawnRequests.RemoveAtSwap(0);
		++AttemptsThisTick;

		if (const UEnemySpawnGroupDefinition* Group = GroupPtr.Get())
		{
			if (SpawnSingleFromGroup(Group))
			{
				++SpawnedThisTick;
			}
		}
	}

	ActiveEnemyCount = LiveEnemies.Num();
}

bool AAeyerjiEncounterDirector::SpawnSingleFromGroup(const UEnemySpawnGroupDefinition* Group)
{
	if (!Group || !CachedPlayerPawn.IsValid())
	{
		return false;
	}

	TSubclassOf<AEnemyParentNative> EnemyClass = Group->ResolveEnemyClass();
	if (!*EnemyClass)
	{
		return false;
	}

	const float HalfHeight = GetEnemyHalfHeight(EnemyClass);
	const float SpawnRadius = ResolveFiniteFloat(Group->SpawnRadius, 1000.f, 0.f, MaxWorldDistance);
	const FVector SpawnLocation = ResolveSpawnLocation(SpawnRadius, HalfHeight);
	if (SpawnLocation.ContainsNaN())
	{
		return false;
	}
	const FRotator SpawnRotation = (CachedPlayerPawn->GetActorLocation() - SpawnLocation).Rotation();

	FEnemySet EnemyTemplate;
	EnemyTemplate.EnemyClass = EnemyClass;
	EnemyTemplate.Count = 1;
	EnemyTemplate.SpawnInterval = 0.0f;
	EnemyTemplate.EnemyArchetypeTag = ResolveArchetypeTagFromClass(EnemyClass);

	UE_LOG(LogEncounterDirector, Log,
		TEXT("DynamicSpawn request: Director=%s Group=%s Class=%s Radius=%.0f Location=%s"),
		*GetNameSafe(this),
		*GetNameSafe(Group),
		*GetNameSafe(EnemyTemplate.EnemyClass),
		SpawnRadius,
		*SpawnLocation.ToCompactString());

	const FTransform SpawnTransform(SpawnRotation, SpawnLocation);
	APawn* SpawnedPawn = UAeyerjiEnemyManagementBPFL::SpawnAndRegisterEnemyFromSet(
		this,
		EnemyTemplate,
		SpawnTransform,
		/*Spawner=*/nullptr,
		/*Owner=*/this,
		CachedPlayerPawn.Get(),
		/*bApplyEliteSettings=*/true,
		/*bApplyAggro=*/true,
		/*bAutoActivate=*/false,
		/*bAutoActivateOnlyIfNoWaves=*/true,
		CachedPlayerPawn.Get(),
		CachedPlayerController.Get(),
		/*bSkipRandomEliteResolution=*/false);
	AEnemyParentNative* SpawnedEnemy = Cast<AEnemyParentNative>(SpawnedPawn);
	if (!SpawnedEnemy)
	{
		return false;
	}

	SnapActorToGround(SpawnedEnemy, HalfHeight);
	RegisterSpawnedEnemy(SpawnedEnemy);
	return true;
}

FVector AAeyerjiEncounterDirector::ResolveSpawnLocation(float Radius, float HalfHeight) const
{
	if (!CachedPlayerPawn.IsValid())
	{
		return GetActorLocation();
	}

	const FVector PlayerLocation = CachedPlayerPawn->GetActorLocation();
	if (PlayerLocation.ContainsNaN() || !FMath::IsFinite(HalfHeight))
	{
		return GetActorLocation();
	}
	Radius = ResolveFiniteFloat(Radius, 1000.f, 0.f, MaxWorldDistance);
	if (Radius <= 0.f)
	{
		return PlayerLocation + FVector(0.f, 0.f, HalfHeight);
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return PlayerLocation + FVector(0.f, 0.f, HalfHeight);
	}

	const float MinDistance = FMath::Clamp(
		ResolveFiniteFloat(MinSpawnDistanceFromPlayer, 0.f, 0.f, MaxWorldDistance),
		0.f,
		Radius);
	const int32 Attempts = FMath::Clamp(SpawnLocationSearchAttempts, 1, MaxSpawnAttempts);
	const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);

	for (int32 Attempt = 0; Attempt < Attempts; ++Attempt)
	{
		FVector Candidate = PlayerLocation;
		bool bFoundNav = false;

		if (NavSys)
		{
			FNavLocation Result;
			if (NavSys->GetRandomReachablePointInRadius(PlayerLocation, Radius, Result))
			{
				Candidate = Result.Location;
				bFoundNav = true;
			}
		}

		if (!bFoundNav)
		{
			const float Distance = (MinDistance > 0.f)
				? FMath::Lerp(MinDistance, Radius, FMath::FRand())
				: FMath::FRandRange(0.f, Radius);
			const FVector2D Offset2D = FMath::RandPointInCircle(Distance);
			Candidate += FVector(Offset2D.X, Offset2D.Y, 0.f);
		}

		Candidate.Z += HalfHeight;

		const FVector TraceStart = Candidate + FVector(0.f, 0.f, GroundTraceUpOffset);
		const FVector TraceEnd = Candidate - FVector(0.f, 0.f, GroundTraceDownDistance);

		FHitResult Hit;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(EncounterDirector_GroundTrace), false, CachedPlayerPawn.Get());
		if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params))
		{
			Candidate.Z = Hit.ImpactPoint.Z + SpawnGroundOffset + HalfHeight;
		}

		if (IsSpawnCandidateAllowed(Candidate, MinDistance))
		{
			return Candidate;
		}
	}

	FVector Fallback = PlayerLocation + FVector(0.f, 0.f, HalfHeight);
	if (NavSys)
	{
		FNavLocation Result;
		if (NavSys->GetRandomReachablePointInRadius(PlayerLocation, Radius, Result))
		{
			Fallback = Result.Location + FVector(0.f, 0.f, HalfHeight);
		}
	}
	else
	{
		const FVector2D Offset2D = FMath::RandPointInCircle(Radius);
		Fallback = PlayerLocation + FVector(Offset2D.X, Offset2D.Y, HalfHeight);
	}

	const FVector TraceStart = Fallback + FVector(0.f, 0.f, GroundTraceUpOffset);
	const FVector TraceEnd = Fallback - FVector(0.f, 0.f, GroundTraceDownDistance);

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EncounterDirector_GroundTrace), false, CachedPlayerPawn.Get());
	if (World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params))
	{
		Fallback.Z = Hit.ImpactPoint.Z + SpawnGroundOffset + HalfHeight;
	}

	return Fallback;
}

void AAeyerjiEncounterDirector::SnapActorToGround(AActor* SpawnedActor, float HalfHeight) const
{
	if (!IsValid(SpawnedActor)
		|| SpawnedActor->GetWorld() != GetWorld()
		|| !CachedPlayerPawn.IsValid()
		|| !FMath::IsFinite(HalfHeight)
		|| SpawnedActor->GetActorLocation().ContainsNaN())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float TraceUp = ResolveFiniteFloat(GroundTraceUpOffset, 120.f, 0.f, MaxWorldDistance);
	const float TraceDown = ResolveFiniteFloat(GroundTraceDownDistance, 2000.f, 10.f, MaxWorldDistance);
	const float GroundOffset = ResolveFiniteFloat(SpawnGroundOffset, 5.f, 0.f, MaxWorldDistance);
	const FVector Start = SpawnedActor->GetActorLocation() + FVector(0.f, 0.f, TraceUp);
	const FVector End = Start - FVector(0.f, 0.f, TraceDown);

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EncounterDirector_SnapTrace), false, CachedPlayerPawn.Get());
	Params.AddIgnoredActor(SpawnedActor);

	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		FVector Adjusted = SpawnedActor->GetActorLocation();
		Adjusted.Z = Hit.ImpactPoint.Z + GroundOffset + HalfHeight;
		if (Adjusted.ContainsNaN())
		{
			return;
		}
		SpawnedActor->SetActorLocation(Adjusted, false, nullptr, ETeleportType::TeleportPhysics);
	}
}

float AAeyerjiEncounterDirector::GetEnemyHalfHeight(TSubclassOf<AEnemyParentNative> EnemyClass) const
{
	if (!*EnemyClass)
	{
		return 0.f;
	}

	if (const ACharacter* CDO = Cast<ACharacter>(EnemyClass->GetDefaultObject()))
	{
		if (const UCapsuleComponent* Capsule = CDO->GetCapsuleComponent())
		{
			return ResolveFiniteFloat(Capsule->GetScaledCapsuleHalfHeight(), 0.f, 0.f, MaxWorldDistance);
		}
	}

	return 0.f;
}

float AAeyerjiEncounterDirector::GetKillSpeedAlpha() const
{
	const float SafeFloor = ResolveFiniteFloat(KillVelocitySpawnFloor, 0.25f, 0.f, MaxRuntimeMultiplier);
	const float SafeCeil = ResolveFiniteFloat(KillVelocitySpawnCeil, 1.5f, 0.01f, MaxRuntimeMultiplier);
	const float SafeVelocity = ResolveFiniteFloat(CurrentKillVelocity, 0.f, 0.f, MaxRuntimeMultiplier);
	if (SafeCeil <= SafeFloor)
	{
		return 0.f;
	}

	const float Range = SafeCeil - SafeFloor;
	const float Clamped = FMath::Clamp(SafeVelocity - SafeFloor, 0.f, Range);
	return Clamped / Range;
}

void AAeyerjiEncounterDirector::EnterState(EEncounterDirectorState NewState)
{
	if (DirectorState == NewState)
	{
		return;
	}

	DirectorState = NewState;

	switch (DirectorState)
	{
	case EEncounterDirectorState::Idle:
		PostCombatTimeRemaining = 0.0;
		break;
	case EEncounterDirectorState::InCombat:
		PostCombatTimeRemaining = 0.0;
		break;
	case EEncounterDirectorState::PostCombat:
		PostCombatTimeRemaining = ResolveFiniteFloat(PostCombatDelaySeconds, 1.f, 0.f, MaxDirectorSeconds);
		break;
	}
}

void AAeyerjiEncounterDirector::RegisterSpawnedEnemy(AEnemyParentNative* Enemy)
{
	if (!HasAuthority()
		|| !IsValid(Enemy)
		|| Enemy->GetWorld() != GetWorld())
	{
		return;
	}
	for (const TWeakObjectPtr<AActor>& TrackedEnemy : LiveEnemies)
	{
		if (TrackedEnemy.Get() == Enemy)
		{
			return;
		}
	}
	if (LiveEnemies.Num() >= MaxEncounterPopulation)
	{
		UE_LOG(LogEncounterDirector, Error,
			TEXT("EncounterDirector %s reached its live-enemy tracking cap; enemy %s was not registered."),
			*GetNameSafe(this),
			*GetNameSafe(Enemy));
		return;
	}

	// Weighted Rift enemies participate in both live encounter/LOD management and
	// the immutable point ledger. Legacy modes retain the old mutually-exclusive
	// behavior to avoid changing their kill-count semantics.
	if (WeightedProgressRunSerial <= 0)
	{
		RemoveProgressEnemy(Enemy);
	}

	LiveEnemies.Add(Enemy);
	ActiveEnemyCount = LiveEnemies.Num();

	Enemy->OnEnemyDied.RemoveDynamic(this, &AAeyerjiEncounterDirector::HandleTrackedEnemyDied);
	Enemy->OnEnemyDied.AddDynamic(this, &AAeyerjiEncounterDirector::HandleTrackedEnemyDied);
	Enemy->OnDestroyed.RemoveDynamic(this, &AAeyerjiEncounterDirector::HandleTrackedEnemyDestroyed);
	Enemy->OnDestroyed.AddDynamic(this, &AAeyerjiEncounterDirector::HandleTrackedEnemyDestroyed);

	GetOrCreateEnemyLODState(Enemy);
}

void AAeyerjiEncounterDirector::HandleTrackedEnemyDied(AActor* DeadEnemy)
{
	if (!DeadEnemy)
	{
		return;
	}

	bool bWasTracked = false;
	for (int32 Index = LiveEnemies.Num() - 1; Index >= 0; --Index)
	{
		if (!LiveEnemies[Index].IsValid())
		{
			LiveEnemies.RemoveAtSwap(Index);
			continue;
		}

		if (LiveEnemies[Index].Get() == DeadEnemy)
		{
			LiveEnemies.RemoveAtSwap(Index);
			bWasTracked = true;
		}
	}

	HandleFixedPopulationEnemyRemoved(DeadEnemy);
	RemoveEnemyLODState(DeadEnemy);
	if (bWasTracked)
	{
		RecordKillTimestamp();
		if (WeightedProgressRunSerial <= 0)
		{
			IncrementKillCount();
		}
	}
	ActiveEnemyCount = LiveEnemies.Num();

	if (DirectorState == EEncounterDirectorState::InCombat && ActiveEnemyCount <= 0)
	{
		EnterState(EEncounterDirectorState::PostCombat);
	}
}

void AAeyerjiEncounterDirector::HandleTrackedEnemyDestroyed(AActor* DestroyedActor)
{
	if (!DestroyedActor)
	{
		return;
	}

	bool bWasTracked = false;
	for (int32 Index = LiveEnemies.Num() - 1; Index >= 0; --Index)
	{
		if (!LiveEnemies[Index].IsValid())
		{
			LiveEnemies.RemoveAtSwap(Index);
			continue;
		}

		if (LiveEnemies[Index].Get() == DestroyedActor)
		{
			LiveEnemies.RemoveAtSwap(Index);
			bWasTracked = true;
		}
	}

	HandleFixedPopulationEnemyRemoved(DestroyedActor);
	RemoveEnemyLODState(DestroyedActor);
	if (bWasTracked)
	{
		UE_LOG(LogEncounterDirector, Warning,
			TEXT("Encounter enemy destroyed without death; no objective progress awarded Enemy=%s"),
			*GetNameSafe(DestroyedActor));
	}
	ActiveEnemyCount = LiveEnemies.Num();

	if (DirectorState == EEncounterDirectorState::InCombat && ActiveEnemyCount <= 0)
	{
		EnterState(EEncounterDirectorState::PostCombat);
	}
}

void AAeyerjiEncounterDirector::RecordKillTimestamp()
{
	if (UWorld* World = GetWorld())
	{
		const double Now = World->GetTimeSeconds();
		LastKillTimestamp = Now;
		KillTimestampHistory.Add(Now);
	}
}
