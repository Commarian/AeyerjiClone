// Copyright (c) 2025 Aeyerji.
#include "Director/AeyerjiEncounterDirector.h"

#include "../../AeyerjiGameState.h"
#include "Director/AeyerjiSpawnerGroup.h"
#include "Director/AeyerjiSpawnRegion.h"
#include "Director/AeyerjiWorldSpawnProfile.h"
#include "Director/AeyerjiLevelDirector.h"
#include "Enemy/AeyerjiEnemyManagementBPFL.h"
#include "Enemy/EnemyParentNative.h"
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
#include "Net/UnrealNetwork.h"
#include "Systems/AeyerjiDifficultyTuning.h"
#include "Systems/AeyerjiRiftRules.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogEncounterDirector, Log, All);

namespace
{
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

TSubclassOf<AEnemyParentNative> UEnemySpawnGroupDefinition::ResolveEnemyClass() const
{
	if (EnemyTypes.IsEmpty())
	{
		return nullptr;
	}

	TArray<TSubclassOf<AEnemyParentNative>> ValidEnemyTypes;
	for (TSubclassOf<AEnemyParentNative> EnemyClass : EnemyTypes)
	{
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

	SetActorTickInterval(FMath::Max(0.f, TickIntervalSeconds));

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

void AAeyerjiEncounterDirector::ApplyDirectorDefinition()
{
	if (!DirectorDefinition)
	{
		return;
	}

	if (DirectorDefinition->SpawnGroups.Num() > 0)
	{
		SpawnGroups = DirectorDefinition->SpawnGroups;
	}

	TickIntervalSeconds = FMath::Max(0.f, DirectorDefinition->TickIntervalSeconds);
	MinDistanceBetweenEncounters = FMath::Max(0.f, DirectorDefinition->MinDistanceBetweenEncounters);
	KillVelocitySpawnFloor = FMath::Max(0.f, DirectorDefinition->KillVelocitySpawnFloor);
	KillVelocitySpawnCeil = FMath::Max(0.01f, DirectorDefinition->KillVelocitySpawnCeil);
	MinDistanceAtSlow = FMath::Max(0.f, DirectorDefinition->MinDistanceAtSlow);
	MinDistanceAtFast = FMath::Max(0.f, DirectorDefinition->MinDistanceAtFast);
	MinDowntimeAtSlow = FMath::Max(0.f, DirectorDefinition->MinDowntimeAtSlow);
	MinDowntimeAtFast = FMath::Max(0.f, DirectorDefinition->MinDowntimeAtFast);
	KillVelocityWindowSeconds = FMath::Max(0.1f, DirectorDefinition->KillVelocityWindowSeconds);
	MaxGroupsPerTrigger = FMath::Max(1, DirectorDefinition->MaxGroupsPerTrigger);
	PostCombatDelaySeconds = FMath::Max(0.f, DirectorDefinition->PostCombatDelaySeconds);
	MaxSpawnsPerTick = FMath::Max(1, DirectorDefinition->MaxSpawnsPerTick);
	MinSpawnDistanceFromPlayer = FMath::Max(0.f, DirectorDefinition->MinSpawnDistanceFromPlayer);
	bAvoidRecentPlayerPath = DirectorDefinition->bAvoidRecentPlayerPath;
	RecentPathAvoidRadius = FMath::Max(0.f, DirectorDefinition->RecentPathAvoidRadius);
	RecentPathSeconds = FMath::Max(0.1f, DirectorDefinition->RecentPathSeconds);
	RecentPathSampleInterval = FMath::Max(0.1f, DirectorDefinition->RecentPathSampleInterval);
	RecentPathMaxSamples = FMath::Max(1, DirectorDefinition->RecentPathMaxSamples);
	bAvoidPlayerForwardSpawnCone = DirectorDefinition->bAvoidPlayerForwardSpawnCone;
	ForwardSpawnConeDegrees = FMath::Clamp(DirectorDefinition->ForwardSpawnConeDegrees, 0.f, 180.f);
	bUseLineOfSightForForwardCone = DirectorDefinition->bUseLineOfSightForForwardCone;
	SpawnLocationSearchAttempts = FMath::Max(1, DirectorDefinition->SpawnLocationSearchAttempts);
	GroundTraceUpOffset = FMath::Max(0.f, DirectorDefinition->GroundTraceUpOffset);
	GroundTraceDownDistance = FMath::Max(10.f, DirectorDefinition->GroundTraceDownDistance);
	SpawnGroundOffset = FMath::Max(0.f, DirectorDefinition->SpawnGroundOffset);
	RiftMinimumSpawnDistanceFromPlayers = FMath::Max(0.f, DirectorDefinition->RiftMinimumSpawnDistanceFromPlayers);
	RiftPressureEvaluationInterval = FMath::Max(0.1f, DirectorDefinition->RiftPressureEvaluationInterval);
	RiftMinimumActiveEnemyPressure = FMath::Max(0, DirectorDefinition->RiftMinimumActiveEnemyPressure);
	bRiftPreferHiddenSpawnLocations = DirectorDefinition->bRiftPreferHiddenSpawnLocations;
	bEnableEnemyLODThrottling = DirectorDefinition->bEnableEnemyLODThrottling;
	EnemyLODUpdateInterval = FMath::Max(0.05f, DirectorDefinition->EnemyLODUpdateInterval);
	EnemyLODNearDistance = FMath::Max(0.f, DirectorDefinition->EnemyLODNearDistance);
	EnemyLODMidDistance = FMath::Max(0.f, DirectorDefinition->EnemyLODMidDistance);
	EnemyLODFarDistance = FMath::Max(0.f, DirectorDefinition->EnemyLODFarDistance);
	EnemyLODMidTickInterval = FMath::Max(0.f, DirectorDefinition->EnemyLODMidTickInterval);
	EnemyLODFarTickInterval = FMath::Max(0.f, DirectorDefinition->EnemyLODFarTickInterval);
	bEnableFixedClusterSleeping = DirectorDefinition->bEnableFixedClusterSleeping;
	FixedClusterSleepDistance = FMath::Max(0.f, DirectorDefinition->FixedClusterSleepDistance);
	FixedClusterWakeDistance = FMath::Max(0.f, DirectorDefinition->FixedClusterWakeDistance);
	bDrawDebug = DirectorDefinition->bDrawDebug;
	DebugLogIntervalSeconds = FMath::Max(0.1f, DirectorDefinition->DebugLogIntervalSeconds);

	UE_LOG(LogEncounterDirector, Display, TEXT("EncounterDirector %s applied DirectorDefinition=%s SpawnGroups=%d."),
		*GetNameSafe(this),
		*GetNameSafe(DirectorDefinition),
		SpawnGroups.Num());
}

FString AAeyerjiEncounterDirector::GetEncounterDirectorDebugString() const
{
	return FString::Printf(
		TEXT("EncounterDirector=%s Definition=%s State=%s Groups=%d Active=%d Progress=%d/%d BossSpawned=%d FixedActive=%d FixedRemaining=%d"),
		*GetNameSafe(this),
		*GetNameSafe(DirectorDefinition),
		*StaticEnum<EEncounterDirectorState>()->GetNameStringByValue(static_cast<int64>(DirectorState)),
		SpawnGroups.Num(),
		ActiveEnemyCount,
		KilledCount,
		TotalToKill,
		bBossSpawned ? 1 : 0,
		bFixedPopulationActive ? 1 : 0,
		FixedPopulationRemaining);
}

void AAeyerjiEncounterDirector::RegisterExternalEnemy(AEnemyParentNative* Enemy, bool bEnterCombatState)
{
	if (GetNetMode() == NM_Client || !IsValid(Enemy))
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
	if (GetNetMode() == NM_Client || !IsValid(Enemy))
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
		RegisteredProgressEnemyPoints.Add(EnemyKey, FMath::Max(ProgressPoints, 1));
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
	Enemy->OnEnemyDied.RemoveDynamic(this, &AAeyerjiEncounterDirector::HandleProgressEnemyDied);
	Enemy->OnEnemyDied.AddDynamic(this, &AAeyerjiEncounterDirector::HandleProgressEnemyDied);
	Enemy->OnDestroyed.RemoveDynamic(this, &AAeyerjiEncounterDirector::HandleProgressEnemyDestroyed);
	Enemy->OnDestroyed.AddDynamic(this, &AAeyerjiEncounterDirector::HandleProgressEnemyDestroyed);
	UE_LOG(LogEncounterDirector, Verbose,
		TEXT("[RiftRun][Registration] RunSerial=%d Enemy=%s Points=%d Registered=%d"),
		WeightedProgressRunSerial, *GetNameSafe(Enemy), FMath::Max(ProgressPoints, 1),
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
	WeightedProgressRunSerial = RunSerial;
	bWeightedProgressFrozen = false;
	EnemiesDefeated = 0;
	WeightedProgressPoints = 0;
	WeightedProgressTarget = FMath::Max(ProgressTargetPoints, 1);
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
	if (!HasAuthority() || RunSerial <= 0 || ProgressTargetPoints <= 0 || EnemyBudget <= 0)
	{
		OutReason = TEXT("Invalid Rift region run serial, target, or enemy budget");
		return false;
	}

	ResetRiftRegionRun();
	RiftRegionRunSerial = RunSerial;
	RiftRegionActivationDistance = FMath::Max(ActivationDistance, 0.f);
	RiftEliteRateMultiplier = FMath::Max(EliteRateMultiplier, 0.f);
	RiftProgressMultiplier = FMath::Max(ProgressMultiplier, 0.1f);
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
		AAeyerjiSpawnRegion* Region = *It;
		if (!IsValid(Region) || !Region->IsRiftEncounterEligible())
		{
			continue;
		}

		FRiftRegionPlan Plan;
		Plan.Region = Region;
		Plan.Bounds = Region->GetRegionBounds();
		Plan.StableKey = Region->GetPathName();
		Plan.Weight = FMath::Max(Region->RegionWeight, 0.f);
		Plan.EncounterGroup = Region->RiftEncounterGroup;
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
	const int32 EffectiveEnemyBudget = FMath::Max(FMath::RoundToInt(EnemyBudget
		* FMath::Max(DensityMultiplier, 0.1f)
		* FMath::Max(EncounterSizeMultiplier, 0.1f)), 1);
	const TArray<int32> RegionBudgets = AeyerjiRiftRules::AllocateLargestRemainder(RegionWeights, EffectiveEnemyBudget);
	RiftReservedRegionRequests.SetNum(RiftRegionPlans.Num());

	const UEnemySpawnGroupDefinition* LastPlannedGroup = nullptr;
	int32 PlannedProgressPoints = 0;
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
			const float EliteChance = Region
				? FMath::Clamp((Group->RiftEliteChance + Region->EliteChanceBonus) * RiftEliteRateMultiplier, 0.f, 1.f)
				: 0.f;
			const bool bPlanElite = bCanUseElitePool && RiftSpawnStream.FRand() <= EliteChance;
			const TArray<TSubclassOf<AEnemyParentNative>>& ClassPool = bPlanElite
				? Group->EliteEnemyTypes
				: Group->EnemyTypes;
			TArray<TSubclassOf<AEnemyParentNative>> ValidClasses;
			for (const TSubclassOf<AEnemyParentNative> EnemyClass : ClassPool)
			{
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
			Request.ProgressPoints = FMath::Max(FMath::RoundToInt(
				(bPlanElite ? Group->RiftEliteProgressPoints : Group->RiftProgressPoints) * RiftProgressMultiplier), 1);
			PlannedProgressPoints += Request.ProgressPoints;
			Plan.ReservedProgress += Request.ProgressPoints;
		}

		UE_LOG(LogEncounterDirector, Display,
			TEXT("[RiftRun][EncounterPlan] RunSerial=%d Seed=%d EncounterGroup=%s Anchor=%s Weight=%.3f Budget=%d ReservedProgress=%d"),
			RunSerial, RunSeed, *Plan.StableKey, *GetNameSafe(Plan.EncounterGroup.Get()), Plan.Weight,
			Plan.Budget, Plan.ReservedProgress);
	}

	const float ReserveRatio = static_cast<float>(PlannedProgressPoints) / static_cast<float>(ProgressTargetPoints);
	if (ReserveRatio < 1.2f)
	{
		OutReason = FString::Printf(TEXT("Reserved progress %d is below 120%% of target %d"),
			PlannedProgressPoints, ProgressTargetPoints);
		ResetRiftRegionRun();
		return false;
	}
	if (ReserveRatio > 1.3f)
	{
		UE_LOG(LogEncounterDirector, Warning,
			TEXT("[RiftRun][RegionPlan] RunSerial=%d reserved progress %d is %.1f%% of target %d (above 130%%)"),
			RunSerial, PlannedProgressPoints, ReserveRatio * 100.f, ProgressTargetPoints);
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
			Spawned->PoolSettings.bPrewarmDuringWorldFlowLoading = false;
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

	BeginWeightedProgressRun(RunSerial, ProgressTargetPoints);
	bRiftRegionActivationEnabled = true;
	UE_LOG(LogEncounterDirector, Display,
		TEXT("[RiftRun][EncounterPlan] Ready RunSerial=%d Seed=%d Anchors=%d BaseBudget=%d EffectiveBudget=%d ReservedProgress=%d Target=%d ActivationDistance=%.1f Density=%.2f Elite=%.2f EncounterSize=%.2f Progress=%.2f"),
		RunSerial, RunSeed, RiftRegionPlans.Num(), EnemyBudget, EffectiveEnemyBudget, PlannedProgressPoints,
		ProgressTargetPoints, RiftRegionActivationDistance, DensityMultiplier, RiftEliteRateMultiplier,
		EncounterSizeMultiplier, RiftProgressMultiplier);
	return true;
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
	bRiftRegionActivationEnabled = false;
	RiftRegionPlans.Reset();
	RiftReservedRegionRequests.Reset();
	RiftSpawnQueue.Reset();
	RiftParticipantRegionLatch.Reset();
	RiftRegionRunSerial = 0;
	RiftRegionActivationDistance = 2500.f;
	RiftEliteRateMultiplier = 1.f;
	RiftProgressMultiplier = 1.f;
	NextRiftPressureEvaluationTime = 0.0;
	RiftLevelDirector.Reset();

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

	const float ActivationDistanceSquared = FMath::Square(FMath::Max(RiftRegionActivationDistance, 0.f));
	int32 BestRegionIndex = INDEX_NONE;
	APawn* BestParticipant = nullptr;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	for (int32 RegionIndex = 0; RegionIndex < RiftRegionPlans.Num(); ++RegionIndex)
	{
		const FRiftRegionPlan& Plan = RiftRegionPlans[RegionIndex];
		if (Plan.bConsumed || Plan.Budget <= 0 || !Plan.Region.IsValid() || !Plan.Bounds.IsValid)
		{
			continue;
		}

		TArray<APawn*> LivingParticipants;
		GetLivingRiftParticipants(LivingParticipants);
		for (APawn* Participant : LivingParticipants)
		{
			const TWeakObjectPtr<APawn> ParticipantKey(Participant);
			if (const TWeakObjectPtr<AAeyerjiSpawnRegion>* LatchedRegionPtr = RiftParticipantRegionLatch.Find(ParticipantKey))
			{
				const AAeyerjiSpawnRegion* LatchedRegion = LatchedRegionPtr->Get();
				if (IsValid(LatchedRegion)
					&& LatchedRegion->GetRegionBounds().ComputeSquaredDistanceToPoint(Participant->GetActorLocation())
						<= ActivationDistanceSquared)
				{
					continue;
				}
				RiftParticipantRegionLatch.Remove(ParticipantKey);
			}

			const float DistanceSquared = Plan.Bounds.ComputeSquaredDistanceToPoint(Participant->GetActorLocation());
			if (DistanceSquared > ActivationDistanceSquared || DistanceSquared >= BestDistanceSquared)
			{
				continue;
			}
			if (!IsRiftRegionReachableFromParticipant(Plan.Anchor, Participant))
			{
				continue;
			}

			BestRegionIndex = RegionIndex;
			BestDistanceSquared = DistanceSquared;
			BestParticipant = Participant;
		}
	}
	if (RiftRegionPlans.IsValidIndex(BestRegionIndex) && IsValid(BestParticipant))
	{
		if (TryActivateRiftEncounterGroup(BestRegionIndex, BestParticipant, TEXT("Proximity")))
		{
			return;
		}
	}

	const double WorldTime = World->GetTimeSeconds();
	if (WorldTime < NextRiftPressureEvaluationTime)
	{
		return;
	}
	NextRiftPressureEvaluationTime = WorldTime + FMath::Max(RiftPressureEvaluationInterval, 0.1f);
	const int32 ActivePressure = GetActiveRiftEnemyPressure();
	if (ActivePressure >= RiftMinimumActiveEnemyPressure)
	{
		return;
	}
	APawn* PressureParticipant = nullptr;
	const int32 PressurePlanIndex = FindRiftPressureActivationCandidate(PressureParticipant);
	if (RiftRegionPlans.IsValidIndex(PressurePlanIndex) && IsValid(PressureParticipant))
	{
		TryActivateRiftEncounterGroup(PressurePlanIndex, PressureParticipant, TEXT("Timer"));
	}
}

bool AAeyerjiEncounterDirector::TryActivateRiftEncounterGroup(
	const int32 PlanIndex,
	APawn* Participant,
	const TCHAR* ActivationReason)
{
	if (!RiftRegionPlans.IsValidIndex(PlanIndex) || !IsValid(Participant)
		|| !RiftReservedRegionRequests.IsValidIndex(PlanIndex))
	{
		return false;
	}
	FRiftRegionPlan& Plan = RiftRegionPlans[PlanIndex];
	TArray<FRiftSpawnRequest>& ReservedRequests = RiftReservedRegionRequests[PlanIndex];
	if (Plan.bConsumed || ReservedRequests.IsEmpty() || bWeightedProgressFrozen
		|| WeightedProgressPoints >= WeightedProgressTarget)
	{
		UE_LOG(LogEncounterDirector, Verbose,
			TEXT("[RiftRun][EncounterActivation] Rejected RunSerial=%d Anchor=%s Group=%s Reason=%s State=ConsumedOrComplete"),
			RiftRegionRunSerial, *Plan.StableKey, *GetNameSafe(Plan.EncounterGroup.Get()), ActivationReason);
		return false;
	}

	FVector SafeLocation;
	FString RejectReason;
	const float HalfHeight = GetEnemyHalfHeight(ReservedRequests[0].EnemyClass);
	if (!ResolveRiftSpawnLocation(Plan, HalfHeight, Participant, SafeLocation, RejectReason))
	{
		UE_LOG(LogEncounterDirector, Display,
			TEXT("[RiftRun][EncounterActivation] Rejected RunSerial=%d Anchor=%s Group=%s Reason=%s Rejected=%s"),
			RiftRegionRunSerial, *Plan.StableKey, *GetNameSafe(Plan.EncounterGroup.Get()), ActivationReason,
			*RejectReason);
		return false;
	}

	// Mark consumed before queueing. Both proximity and pressure evaluation execute on
	// the server, and this is the shared one-shot reservation guard for both paths.
	Plan.bConsumed = true;
	RiftSpawnQueue.Append(ReservedRequests);
	ReservedRequests.Reset();
	if (FCString::Strcmp(ActivationReason, TEXT("Proximity")) == 0)
	{
		TArray<APawn*> LivingParticipants;
		GetLivingRiftParticipants(LivingParticipants);
		const float LatchDistanceSquared = FMath::Square(FMath::Max(RiftRegionActivationDistance, 0.f));
		for (APawn* NearbyParticipant : LivingParticipants)
		{
			if (Plan.Bounds.ComputeSquaredDistanceToPoint(NearbyParticipant->GetActorLocation()) <= LatchDistanceSquared)
			{
				RiftParticipantRegionLatch.Add(NearbyParticipant, Plan.Region);
			}
		}
	}
	if (AAeyerjiSpawnerGroup* Spawner = RiftPopulationSpawner.Get())
	{
		if (Spawner->IsCleared())
		{
			Spawner->ResetEncounter();
			Spawner->ConfigureAsRiftPopulationExecutor(RiftLevelDirector.Get());
		}
	}

	const float ActivationDistance = FMath::Sqrt(Plan.Bounds.ComputeSquaredDistanceToPoint(Participant->GetActorLocation()));
	UE_LOG(LogEncounterDirector, Display,
		TEXT("[RiftRun][EncounterActivation] RunSerial=%d Anchor=%s Group=%s Reason=%s Participant=%s Distance=%.1f Budget=%d PendingSpawns=%d"),
		RiftRegionRunSerial, *Plan.StableKey, *GetNameSafe(Plan.EncounterGroup.Get()), ActivationReason,
		*GetNameSafe(Participant), ActivationDistance, Plan.Budget, RiftSpawnQueue.Num());
	return true;
}

int32 AAeyerjiEncounterDirector::FindRiftPressureActivationCandidate(APawn*& OutParticipant) const
{
	OutParticipant = nullptr;
	TArray<APawn*> LivingParticipants;
	GetLivingRiftParticipants(LivingParticipants);
	const float ActivationDistanceSquared = FMath::Square(FMath::Max(RiftRegionActivationDistance, 0.f));
	float BestDistanceSquared = TNumericLimits<float>::Max();
	int32 BestPlanIndex = INDEX_NONE;
	for (int32 PlanIndex = 0; PlanIndex < RiftRegionPlans.Num(); ++PlanIndex)
	{
		const FRiftRegionPlan& Plan = RiftRegionPlans[PlanIndex];
		if (Plan.bConsumed || Plan.Budget <= 0 || !Plan.Region.IsValid() || !Plan.Bounds.IsValid)
		{
			continue;
		}
		for (APawn* Participant : LivingParticipants)
		{
			const float DistanceSquared = Plan.Bounds.ComputeSquaredDistanceToPoint(Participant->GetActorLocation());
			if (DistanceSquared <= ActivationDistanceSquared && DistanceSquared < BestDistanceSquared
				&& IsRiftRegionReachableFromParticipant(Plan.Anchor, Participant))
			{
				BestPlanIndex = PlanIndex;
				BestDistanceSquared = DistanceSquared;
				OutParticipant = Participant;
			}
		}
	}
	return BestPlanIndex;
}

int32 AAeyerjiEncounterDirector::GetActiveRiftEnemyPressure() const
{
	int32 ActivePressure = 0;
	for (const TWeakObjectPtr<AActor>& Enemy : LiveEnemies)
	{
		ActivePressure += Enemy.IsValid() ? 1 : 0;
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

	const int32 SpawnBudget = FMath::Min(FMath::Max(MaxSpawnsPerTick, 1), RiftSpawnQueue.Num());
	for (int32 AttemptIndex = 0; AttemptIndex < SpawnBudget && !RiftSpawnQueue.IsEmpty(); ++AttemptIndex)
	{
		FRiftSpawnRequest Request = RiftSpawnQueue[0];
		RiftSpawnQueue.RemoveAt(0, 1, EAllowShrinking::No);
		if (!SpawnRiftRequest(Request))
		{
			Request.FailedAttempts++;
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
			RiftSpawnQueue.Add(MoveTemp(Request));
		}
	}
}

bool AAeyerjiEncounterDirector::ResolveRiftRegionAnchor(const FBox& Bounds, FVector& OutAnchor)
{
	if (!Bounds.IsValid)
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
	if (!IsValid(Participant))
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
	if (!World || !Plan.Bounds.IsValid)
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
	for (int32 Attempt = 0; Attempt < FMath::Max(SpawnLocationSearchAttempts, 1); ++Attempt)
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
	const float MinimumDistanceSquared = FMath::Square(FMath::Max(RiftMinimumSpawnDistanceFromPlayers, 0.f));
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

bool AAeyerjiEncounterDirector::SpawnRiftRequest(FRiftSpawnRequest& Request)
{
	if (!Request.EnemyClass || !RiftRegionPlans.IsValidIndex(Request.RegionPlanIndex))
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
	if (!ResolveRiftSpawnLocation(Plan, HalfHeight, Participant, SpawnLocation, RejectReason))
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
	EnemyTemplate.ProgressPoints = FMath::Max(Request.ProgressPoints, 1);
	EnemyTemplate.SpawnInterval = 0.f;
	EnemyTemplate.EnemyArchetypeTag = ResolveArchetypeTagFromClass(Request.EnemyClass);
	EnemyTemplate.bIsElite = Request.bIsElite;
	EnemyTemplate.bSkipEliteAutoScaling = Request.bIsElite;

	const FTransform SpawnTransform(SpawnRotation, SpawnLocation);
	APawn* SpawnedPawn = UAeyerjiEnemyManagementBPFL::SpawnAndRegisterEnemyFromSet(
		this,
		EnemyTemplate,
		SpawnTransform,
		Spawner,
		this,
		Participant,
		true,
		true,
		false,
		true,
		Participant,
		Participant->GetController(),
		true);
	if (!IsValid(SpawnedPawn))
	{
		return false;
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
#endif

bool AAeyerjiEncounterDirector::StartFixedWorldPopulation(UAeyerjiWorldSpawnProfile* Profile, AAeyerjiSpawnerGroup* SpawnManager, AAeyerjiLevelDirector* LevelDirector)
{
	if (GetNetMode() == NM_Client)
	{
		return false;
	}

	if (!Profile)
	{
		UE_LOG(LogEncounterDirector, Warning, TEXT("Fixed population start skipped: missing profile on %s"), *GetNameSafe(this));
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
			if (!IsValid(Group))
			{
				return;
			}

			for (TSubclassOf<AEnemyParentNative> EnemyClass : Group->EnemyTypes)
			{
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
				if (!*EliteClass)
				{
					continue;
				}

				FEnemySet WarmSet;
				WarmSet.EnemyClass = EliteClass;
				WarmSet.Count = 1;
				WarmSet.bIsElite = true;
				WarmSet.bSkipEliteAutoScaling = true;
				WarmSet.EnemyArchetypeTag = ResolveArchetypeTagFromClass(EliteClass);
				WarmSets.Add(WarmSet);
			}
		};

		if (!Profile->SpawnGroups.IsEmpty())
		{
			for (const FWeightedSpawnGroup& WeightedGroup : Profile->SpawnGroups)
			{
				AddGroupToWarmSets(WeightedGroup.Group);
			}
		}
		else
		{
			for (const UEnemySpawnGroupDefinition* Group : SpawnGroups)
			{
				AddGroupToWarmSets(Group);
			}
		}

		FixedPopulationSpawner->PrewarmPoolForEnemySets(WarmSets, 1);
	}
	UpdateTotalToKill(FixedPopulationTarget);

	if (FixedPopulationTarget <= 0 || FixedSpawnQueue.IsEmpty())
	{
		UE_LOG(LogEncounterDirector, Warning, TEXT("Fixed population start skipped: no spawn requests generated on %s"), *GetNameSafe(this));
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
	if (GetNetMode() == NM_Client)
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
	ProcessRiftRegionActivation();
	ProcessRiftSpawnQueue();
	UpdateEnemyLOD(DeltaSeconds);

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

	DistanceFromLastEncounter = FVector::Dist(CachedPlayerPawn->GetActorLocation(), LastEncounterLocation);

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
		PostCombatTimeRemaining -= DeltaSeconds;
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

	const int32 MaxSamples = FMath::Max(1, RecentPathMaxSamples);
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
	if (!CachedPlayerPawn.IsValid())
	{
		return;
	}

	if (EnemyLODUpdateInterval > 0.f)
	{
		EnemyLODTimeAccumulator += DeltaSeconds;
		if (EnemyLODTimeAccumulator < EnemyLODUpdateInterval)
		{
			return;
		}
		EnemyLODTimeAccumulator = 0.f;
	}

	const FVector PlayerLocation = CachedPlayerPawn->GetActorLocation();
	UpdateFixedClusterLOD(PlayerLocation);

	if (!bEnableEnemyLODThrottling)
	{
		return;
	}

	const float NearDistance = FMath::Max(0.f, EnemyLODNearDistance);
	const float MidDistance = FMath::Max(NearDistance, EnemyLODMidDistance);
	const float FarDistance = FMath::Max(MidDistance, EnemyLODFarDistance);
	const float MidDistSq = FMath::Square(MidDistance);
	const float FarDistSq = FMath::Square(FarDistance);

	for (const TWeakObjectPtr<AActor>& Tracked : LiveEnemies)
	{
		AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(Tracked.Get());
		if (!IsValid(Enemy))
		{
			continue;
		}

		FEnemyLODState& State = GetOrCreateEnemyLODState(Enemy);
		if (State.bSleeping)
		{
			continue;
		}

		const float DistSq = FVector::DistSquared2D(Enemy->GetActorLocation(), PlayerLocation);
		uint8 NewBucket = 0;
		if (DistSq > FarDistSq)
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

void AAeyerjiEncounterDirector::UpdateFixedClusterLOD(const FVector& PlayerLocation)
{
	if (!bEnableFixedClusterSleeping || !bFixedPopulationActive || FixedClusters.IsEmpty())
	{
		return;
	}

	const float SleepDistance = FMath::Max(FixedClusterSleepDistance, FixedClusterWakeDistance);
	const float WakeDistance = FMath::Max(0.f, FMath::Min(FixedClusterWakeDistance, SleepDistance));
	const float SleepDistSq = FMath::Square(SleepDistance);
	const float WakeDistSq = FMath::Square(WakeDistance);

	for (TPair<int32, FFixedSpawnCluster>& Pair : FixedClusters)
	{
		FFixedSpawnCluster& Cluster = Pair.Value;
		const float DistSq = FVector::DistSquared2D(PlayerLocation, Cluster.Center);

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
	if (!IsValid(Enemy))
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
		if (AIController)
		{
			AIController->StopMovement();
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
	}
	else
	{
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

	const float DesiredInterval = (NewBucket == 1) ? EnemyLODMidTickInterval : (NewBucket == 2 ? EnemyLODFarTickInterval : 0.f);
	auto ApplyTickSettings = [NewBucket, DesiredInterval](UActorComponent* Component, bool bEnabled, float BaseInterval)
	{
		if (!Component)
		{
			return;
		}

		Component->SetComponentTickEnabled(bEnabled);
		if (bEnabled)
		{
			const float Interval = (NewBucket == 0) ? BaseInterval : FMath::Max(BaseInterval, DesiredInterval);
			Component->SetComponentTickInterval(Interval);
		}
	};

	ApplyTickSettings(Enemy->GetCharacterMovement(), State.bMovementTickEnabled, State.BaseMovementTickInterval);
	ApplyTickSettings(Enemy->GetMesh(), State.bMeshTickEnabled, State.BaseMeshTickInterval);

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
			State.BaseMovementTickInterval = MoveComp->PrimaryComponentTick.TickInterval;
			State.bMovementTickEnabled = MoveComp->IsComponentTickEnabled();
		}
	}

	if (!State.bCachedMesh)
	{
		if (USkeletalMeshComponent* MeshComp = Enemy->GetMesh())
		{
			State.bCachedMesh = true;
			State.BaseMeshTickInterval = MeshComp->PrimaryComponentTick.TickInterval;
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
				State.BasePerceptionTickInterval = Perception->PrimaryComponentTick.TickInterval;
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
	const double WindowStart = Now - KillVelocityWindowSeconds;

	for (int32 Index = KillTimestampHistory.Num() - 1; Index >= 0; --Index)
	{
		if (KillTimestampHistory[Index] < WindowStart)
		{
			KillTimestampHistory.RemoveAtSwap(Index);
		}
	}

	if (KillVelocityWindowSeconds <= 0.f)
	{
		CurrentKillVelocity = KillTimestampHistory.Num();
	}
	else
	{
		CurrentKillVelocity = static_cast<float>(KillTimestampHistory.Num()) / KillVelocityWindowSeconds;
	}
}

void AAeyerjiEncounterDirector::ResetProgress(int32 NewTotal)
{
	if (!HasAuthority())
	{
		return;
	}

	TotalToKill = FMath::Max(0, NewTotal);
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

	const int32 ClampedTotal = FMath::Max(0, NewTotal);
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
			if (IsValid(Weighted.Group) && !Weighted.Group->EnemyTypes.IsEmpty() && Weighted.Weight > 0.f)
			{
				FFixedSpawnGroupEntry Entry;
				Entry.Group = Weighted.Group;
				Entry.Weight = Weighted.Weight;
				GroupEntries.Add(Entry);
			}
		}
	}
	else
	{
		for (const UEnemySpawnGroupDefinition* Group : SpawnGroups)
		{
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

	const int32 BaseMinEnemyCount = FMath::Max(0, Profile->MinimumEnemyCount);
	const int32 BaseTargetEnemyCount = FMath::Max(0, Profile->TargetEnemyCount);
	int32 BaseMaxEnemyCount = Profile->MaximumEnemyCount > 0 ? Profile->MaximumEnemyCount : BaseTargetEnemyCount;
	BaseMaxEnemyCount = FMath::Max(BaseMaxEnemyCount, BaseMinEnemyCount);

	int32 MaxDifficultyMinEnemyCount = Profile->MinimumEnemyCountAtMaxDifficulty > 0
		? Profile->MinimumEnemyCountAtMaxDifficulty
		: BaseMinEnemyCount;
	int32 MaxDifficultyMaxEnemyCount = Profile->MaximumEnemyCountAtMaxDifficulty > 0
		? Profile->MaximumEnemyCountAtMaxDifficulty
		: BaseMaxEnemyCount;
	MaxDifficultyMinEnemyCount = FMath::Max(0, MaxDifficultyMinEnemyCount);
	MaxDifficultyMaxEnemyCount = FMath::Max(MaxDifficultyMaxEnemyCount, MaxDifficultyMinEnemyCount);

	float BudgetAlpha = 0.f;
	if (Profile->bScaleBudgetByDifficulty && FixedPopulationLevelDirector.IsValid())
	{
		const float Difficulty = FixedPopulationLevelDirector->GetCurvedDifficulty();
		BudgetAlpha = FMath::Clamp(Profile->DifficultyBudgetFloor + (1.f - Profile->DifficultyBudgetFloor) * Difficulty, 0.f, 1.f);
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
	const float BudgetScale = FMath::Clamp(GetFixedPopulationBudgetScaleCVar().GetValueOnGameThread(), 0.f, 1.f);
	if (BudgetScale < 1.f)
	{
		ResolvedTarget = FMath::RoundToInt(ResolvedTarget * BudgetScale);
	}

	const int32 BudgetCap = GetFixedPopulationBudgetCapCVar().GetValueOnGameThread();
	if (BudgetCap > 0)
	{
		ResolvedTarget = FMath::Min(ResolvedTarget, BudgetCap);
	}

	if (ResolvedTarget <= 0)
	{
		return;
	}

	FixedSpawnQueue.Reserve(ResolvedTarget);

	FixedSpawnSeed = Profile->Seed != 0 ? Profile->Seed : FMath::Rand();
	FixedSpawnStream.Initialize(FixedSpawnSeed);

	const int32 MinClusterSize = FMath::Max(1, Profile->MinClusterSize);
	const int32 MaxClusterSize = FMath::Max(MinClusterSize, Profile->MaxClusterSize);
	const int32 MinClusterCount = FMath::Max(1, Profile->MinClusterCount);
	const int32 MaxClusterCount = FMath::Max(MinClusterCount, Profile->MaxClusterCount);
	const float AvgClusterSize = (Profile->MinClusterSize + Profile->MaxClusterSize) * 0.5f;
	int32 ClusterCount = AvgClusterSize > 0.f ? FMath::RoundToInt(ResolvedTarget / AvgClusterSize) : MinClusterCount;
	ClusterCount = FMath::Clamp(ClusterCount, MinClusterCount, MaxClusterCount);
	const int32 MaxPossibleClusters = FMath::Max(1, ResolvedTarget / MinClusterSize);
	ClusterCount = FMath::Min(ClusterCount, MaxPossibleClusters);

	TArray<FFixedSpawnRegionEntry> Regions;
	if (Profile->bUseSpawnRegions)
	{
		for (TActorIterator<AAeyerjiSpawnRegion> It(GetWorld()); It; ++It)
		{
			AAeyerjiSpawnRegion* Region = *It;
			if (!IsValid(Region))
			{
				continue;
			}

			const FBox Bounds = Region->GetRegionBounds();
			if (!Bounds.IsValid)
			{
				continue;
			}

			FFixedSpawnRegionEntry Entry;
			Entry.Region = Region;
			Entry.Bounds = Bounds;
			const FVector RegionSize = Bounds.GetSize();
			const float SizeScore = FMath::Max(0.f, RegionSize.X + RegionSize.Y);
			Entry.Weight = FMath::Max(0.f, Region->RegionWeight) * SizeScore;
			Entry.DensityScale = FMath::Max(0.f, Region->DensityScale);
			Entry.EliteChanceBonus = FMath::Max(0.f, Region->EliteChanceBonus);
			Entry.RadiusScale = FMath::Max(0.f, Region->ClusterRadiusScale);
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
		float TotalWeight = 0.f;
		for (const FFixedSpawnRegionEntry& Entry : Entries)
		{
			TotalWeight += FMath::Max(0.f, Entry.Weight);
		}

		if (TotalWeight <= KINDA_SMALL_NUMBER)
		{
			return INDEX_NONE;
		}

		float Roll = FixedSpawnStream.FRandRange(0.f, TotalWeight);
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			Roll -= FMath::Max(0.f, Entries[Index].Weight);
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
			DensityAlpha = FMath::Pow(DensityAlpha, FMath::Max(0.01f, Profile->DensityExponent));
		}

		DensityAlpha = FMath::Clamp(DensityAlpha, 0.f, 1.f);
		if (RegionEntry)
		{
			DensityAlpha = FMath::Clamp(DensityAlpha * RegionEntry->DensityScale, 0.f, 1.f);
		}

		int32 ClusterSize = FMath::RoundToInt(FMath::Lerp(static_cast<float>(MinAllowed), static_cast<float>(MaxAllowed), DensityAlpha));
		ClusterSize = FMath::Clamp(ClusterSize, MinAllowed, MaxAllowed);

		float Radius = FMath::Lerp(Profile->ClusterRadiusMin, Profile->ClusterRadiusMax, DensityAlpha);
		if (RegionEntry)
		{
			Radius *= RegionEntry->RadiusScale;
		}
		Radius = FMath::Max(0.f, Radius);

		FVector ClusterCenter = GetActorLocation();
		if (!ResolveFixedClusterCenter(RegionEntry, FixedClusterCenters, Profile->MinClusterSpacing, ClusterCenter))
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
	const float DensePercentile = FMath::Clamp(Profile->DenseClusterPercentile, 0.f, 1.f);
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
	if (!bFixedPopulationActive)
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

	const int32 SpawnBudget = Profile->MaxFixedSpawnsPerTickOverride > 0 ? Profile->MaxFixedSpawnsPerTickOverride : FMath::Max(1, MaxSpawnsPerTick);
	int32 SpawnedThisTick = 0;
	const auto HandleSpawnSkip = [this](int32 ClusterId)
	{
		HandleFixedPopulationClusterDecrement(ClusterId);
		FixedPopulationTarget = FMath::Max(0, FixedPopulationTarget - 1);
		FixedPopulationRemaining = FMath::Max(0, FixedPopulationTarget - FixedPopulationSpawned);
		UpdateTotalToKill(FixedPopulationTarget);
	};

	while (SpawnedThisTick < SpawnBudget && FixedSpawnQueue.Num() > 0)
	{
		const FFixedSpawnRequest Request = FixedSpawnQueue[0];
		FixedSpawnQueue.RemoveAtSwap(0);

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
			float EliteChance = Profile->BaseEliteChance + Request.EliteChanceBonus;
			EliteChance += FMath::Clamp(Request.DensityAlpha, 0.f, 1.f) * Profile->DensityEliteChanceScale;

			if (Request.bDenseCluster)
			{
				EliteChance += Profile->DenseEliteChanceBonus;
			}

			EliteChance = FMath::Clamp(EliteChance, 0.f, Profile->EliteChanceCap);
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
		if (bSpawnElite)
		{
			// Elite pool classes are authored as final variants, so skip automatic elite multipliers/affixes.
			EnemyTemplate.bSkipEliteAutoScaling = true;
		}

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
	float TotalWeight = 0.f;
	for (const FFixedSpawnGroupEntry& Entry : Groups)
	{
		if (Entry.Group.IsValid())
		{
			TotalWeight += FMath::Max(0.f, Entry.Weight);
		}
	}

	if (TotalWeight <= KINDA_SMALL_NUMBER)
	{
		return nullptr;
	}

	float Roll = FixedSpawnStream.FRandRange(0.f, TotalWeight);
	for (const FFixedSpawnGroupEntry& Entry : Groups)
	{
		if (!Entry.Group.IsValid())
		{
			continue;
		}

		Roll -= FMath::Max(0.f, Entry.Weight);
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
	const int32 Attempts = FMath::Max(1, Profile->ClusterCenterSearchAttempts);
	const float MinSpacingSq = MinSpacing > 0.f ? FMath::Square(MinSpacing) : 0.f;
	const FVector ProjectionExtent(Profile->NavProjectionExtent);
	const FVector FallbackOrigin = CachedPlayerPawn.IsValid() ? CachedPlayerPawn->GetActorLocation() : GetActorLocation();
	const float FallbackRadius = FMath::Max(0.f, Profile->FallbackSpawnRadius);
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

		if (RegionEntry)
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
	UWorld* World = GetWorld();
	if (!World)
	{
		return ClusterCenter + FVector(0.f, 0.f, HalfHeight);
	}

	const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	const int32 Attempts = FMath::Max(1, SpawnLocationSearchAttempts);
	const bool bUseRegionBounds = bHasRegion && RegionBounds.IsValid;
	float SafeRadius = FMath::Max(0.f, Radius);
	if (bUseRegionBounds)
	{
		const float MaxRadiusX = FMath::Min(ClusterCenter.X - RegionBounds.Min.X, RegionBounds.Max.X - ClusterCenter.X);
		const float MaxRadiusY = FMath::Min(ClusterCenter.Y - RegionBounds.Min.Y, RegionBounds.Max.Y - ClusterCenter.Y);
		const float MaxRadius = FMath::Max(0.f, FMath::Min(MaxRadiusX, MaxRadiusY));
		SafeRadius = FMath::Min(SafeRadius, MaxRadius);
	}
	const float MinDistance = FMath::Max(0.f, MinSpawnDistanceFromPlayer);
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
	if (!CachedPlayerPawn.IsValid())
	{
		return true;
	}

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
	if (!bAvoidRecentPlayerPath || RecentPlayerSamples.IsEmpty())
	{
		return false;
	}

	const float Radius = FMath::Max(0.f, RecentPathAvoidRadius);
	if (Radius <= 0.f)
	{
		return false;
	}

	const float RadiusSq = FMath::Square(Radius);
	for (const FRecentPlayerSample& Sample : RecentPlayerSamples)
	{
		if (FVector::DistSquared2D(Candidate, Sample.Location) <= RadiusSq)
		{
			return true;
		}
	}

	return false;
}

bool AAeyerjiEncounterDirector::IsSpawnLocationVisible(const FVector& Candidate) const
{
	if (!bAvoidPlayerForwardSpawnCone || !CachedPlayerPawn.IsValid())
	{
		return false;
	}

	if (ForwardSpawnConeDegrees <= 0.f)
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

	const float CosHalfAngle = FMath::Cos(FMath::DegreesToRadians(ForwardSpawnConeDegrees * 0.5f));
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
	if (GetNetMode() == NM_Client)
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
	const int32 BurstCount = FMath::Clamp(1 + FMath::RoundToInt(SpeedAlpha * (MaxGroupsPerTrigger - 1)), 1, MaxGroupsPerTrigger);
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

	for (const UEnemySpawnGroupDefinition* Group : SpawnGroups)
	{
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

	const int32 Attempts = SpawnGroups.Num() * 2;
	for (int32 Attempt = 0; Attempt < Attempts; ++Attempt)
	{
		const int32 Index = FMath::RandHelper(SpawnGroups.Num());
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
	if (GetNetMode() == NM_Client || !Group)
	{
		return 0;
	}

	const int32 SpawnCount = Group->ResolveSpawnCount();
	if (SpawnCount <= 0)
	{
		return 0;
	}

	PendingSpawnRequests.Reserve(PendingSpawnRequests.Num() + SpawnCount);
	for (int32 Index = 0; Index < SpawnCount; ++Index)
	{
		PendingSpawnRequests.Add(Group);
	}

	return SpawnCount;
}

void AAeyerjiEncounterDirector::ProcessSpawnQueue()
{
	if (GetNetMode() == NM_Client)
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

	const int32 SpawnBudget = FMath::Max(1, MaxSpawnsPerTick);
	int32 SpawnedThisTick = 0;

	while (SpawnedThisTick < SpawnBudget && PendingSpawnRequests.Num() > 0)
	{
		const TWeakObjectPtr<const UEnemySpawnGroupDefinition> GroupPtr = PendingSpawnRequests[0];
		PendingSpawnRequests.RemoveAtSwap(0);

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
	const FVector SpawnLocation = ResolveSpawnLocation(Group->SpawnRadius, HalfHeight);
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
		Group->SpawnRadius,
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
	if (Radius <= 0.f)
	{
		return PlayerLocation + FVector(0.f, 0.f, HalfHeight);
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return PlayerLocation + FVector(0.f, 0.f, HalfHeight);
	}

	const float MinDistance = FMath::Clamp(MinSpawnDistanceFromPlayer, 0.f, Radius);
	const int32 Attempts = FMath::Max(1, SpawnLocationSearchAttempts);
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
	if (!SpawnedActor || !CachedPlayerPawn.IsValid())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const FVector Start = SpawnedActor->GetActorLocation() + FVector(0.f, 0.f, GroundTraceUpOffset);
	const FVector End = Start - FVector(0.f, 0.f, GroundTraceDownDistance);

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(EncounterDirector_SnapTrace), false, CachedPlayerPawn.Get());
	Params.AddIgnoredActor(SpawnedActor);

	if (World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params))
	{
		FVector Adjusted = SpawnedActor->GetActorLocation();
		Adjusted.Z = Hit.ImpactPoint.Z + SpawnGroundOffset + HalfHeight;
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
			return Capsule->GetScaledCapsuleHalfHeight();
		}
	}

	return 0.f;
}

float AAeyerjiEncounterDirector::GetKillSpeedAlpha() const
{
	if (KillVelocitySpawnCeil <= KillVelocitySpawnFloor)
	{
		return 0.f;
	}

	const float Range = KillVelocitySpawnCeil - KillVelocitySpawnFloor;
	const float Clamped = FMath::Clamp(CurrentKillVelocity - KillVelocitySpawnFloor, 0.f, Range);
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
		PostCombatTimeRemaining = PostCombatDelaySeconds;
		break;
	}
}

void AAeyerjiEncounterDirector::RegisterSpawnedEnemy(AEnemyParentNative* Enemy)
{
	if (!IsValid(Enemy))
	{
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
