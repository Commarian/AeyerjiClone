#include "Director/AeyerjiSpawnerGroup.h"

#include "../../AeyerjiGameState.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Kismet/GameplayStatics.h"
#include "Systems/AeyerjiGameplayEventSubsystem.h"
#include "TimerManager.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "AIController.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "Director/AeyerjiEncounterDefinition.h"
#include "Director/AeyerjiEncounterDirector.h"
#include "Director/AeyerjiLevelDirector.h"
#include "Director/AeyerjiSpawnRegion.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "Enemy/AeyerjiEnemyManagementBPFL.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyParentNative.h"
#include "EngineUtils.h"
#include "Engine/Engine.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Attributes/AeyerjiRewardAttributeSet.h"
#include "GameplayEffect.h"
#include "GameplayAbilitySpec.h"
#include "Logging/AeyerjiLog.h"
#include "Navigation/AeyerjiNavSafetyLibrary.h"
#include "NavigationPath.h"
#include "NavigationSystem.h"
#include "Systems/AeyerjiDifficultyTuning.h"
#include "Algo/RandomShuffle.h"

namespace
{
	void RefreshSpawnedPawnStatusBar(APawn* SpawnedPawn)
	{
		if (!IsValid(SpawnedPawn))
		{
			return;
		}

		if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(SpawnedPawn, /*LookForComponent=*/true))
		{
			ASC->ForceReplication();
		}

		SpawnedPawn->ForceNetUpdate();

		if (AAeyerjiCharacter* Character = Cast<AAeyerjiCharacter>(SpawnedPawn))
		{
			// Status bars already listen to this delegate as the generic "attributes changed, re-pull now" signal.
			Character->OnAbilitySystemReady.Broadcast();
		}
	}
}

AAeyerjiSpawnerGroup::AAeyerjiSpawnerGroup()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	ActivationVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("ActivationVolume"));
	SetRootComponent(ActivationVolume);

	if (ActivationVolume)
	{
		ActivationVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		ActivationVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
		ActivationVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		ActivationVolume->SetBoxExtent(FVector(200.f));
	}
}

void AAeyerjiSpawnerGroup::BeginPlay()
{
	Super::BeginPlay();

	if (ActivationVolume)
	{
		if (bDisableActivationVolume)
		{
			ActivationVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		else
		{
			ActivationVolume->OnComponentBeginOverlap.AddDynamic(this, &AAeyerjiSpawnerGroup::HandleActivationOverlap);
		}
	}

	ResetEncounter();
	RebuildSpawnDiscoveryCache();
	BeginEnemyScalingTablePreload();

	if (!bDisableActivationEvent && ActivationEventTag.IsValid())
	{
		if (UAeyerjiGameplayEventSubsystem* EventSubsystem = UAeyerjiGameplayEventSubsystem::Get(this))
		{
			ActivationEventHandle = EventSubsystem->RegisterListener(
				ActivationEventTag,
				UAeyerjiGameplayEventSubsystem::FAeyerjiGameplayEventNativeSignature::FDelegate::CreateUObject(
					this, &AAeyerjiSpawnerGroup::HandleActivationEvent));
		}
	}
}

void AAeyerjiSpawnerGroup::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (EnemyScalingTableHandle.IsValid())
	{
		EnemyScalingTableHandle->CancelHandle();
		EnemyScalingTableHandle.Reset();
	}

	if (ActivationEventHandle.IsValid() && ActivationEventTag.IsValid())
	{
		if (UAeyerjiGameplayEventSubsystem* EventSubsystem = UAeyerjiGameplayEventSubsystem::Get(this))
		{
			EventSubsystem->UnregisterListener(ActivationEventTag, ActivationEventHandle);
		}
	}

	ClearAggroCache();
	ReleaseEnemyPool(/*bDestroyInactiveEnemies=*/true);

	Super::EndPlay(EndPlayReason);
}

void AAeyerjiSpawnerGroup::HandleActivationOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
                                                   UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
                                                   bool bFromSweep, const FHitResult& SweepResult)
{
	APawn* PawnInstigator = Cast<APawn>(OtherActor);
	AController* InstigatorController = PawnInstigator ? PawnInstigator->GetController() : nullptr;
	if (HasAuthority() && !bActive && PawnInstigator
		&& InstigatorController && InstigatorController->IsPlayerController())
	{
		ActivateEncounter(OtherActor, InstigatorController);
	}
}

void AAeyerjiSpawnerGroup::ConfigureAsRiftPopulationExecutor(AAeyerjiLevelDirector* InLevelDirector)
{
	if (!HasAuthority())
	{
		return;
	}

	LevelDirector = InLevelDirector;
	bDisableActivationVolume = true;
	bDisableActivationEvent = true;
	bSuppressDoorControl = true;
	bAllowManualActivationWithoutWaves = true;
	bPermanentRiftPursuit = true;
	AggroSettings.bEnableAggro = true;
	AggroSettings.bReissueAggroWhileActive = true;
	if (ActivationVolume)
	{
		ActivationVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
}

void AAeyerjiSpawnerGroup::ActivateEncounter(AActor* ActivationInstigator, AController* ActivationController)
{
	if (!HasAuthority())
	{
		return;
	}

	if (bActive || bCleared)
	{
		return;
	}

	EncounterWavesRuntime.Reset();
	const bool bUseEncounterAsset = EncounterWavesRuntime.IsEmpty() && EncounterDefinition && (bPreferEncounterAsset || Waves.Num() == 0);
	if (bUseEncounterAsset)
	{
		EncounterDefinition->BuildRuntimeWaves(EncounterWavesRuntime);
	}

	if (EncounterWavesRuntime.Num() == 0)
	{
		EncounterWavesRuntime = Waves;
	}

	const bool bHasRuntimeWaves = EncounterWavesRuntime.Num() > 0;
	if (!bHasRuntimeWaves && !bAllowManualActivationWithoutWaves)
	{
		return;
	}

	CacheActivationStimulus(ActivationInstigator, ActivationController);
	RebuildSpawnDiscoveryCache();
	ResetSpawnPointCycle();
	if (SpawnPointMode != EAeyerjiSpawnPointMode::Random)
	{
		RebuildSpawnPointOrder();
	}

	bActive = true;
#if WITH_DEV_AUTOMATION_TESTS
	++AutomationActivationCount;
#endif
	bCleared = false;
	bAwaitingManualSpawns = !bHasRuntimeWaves;
	CurrentWaveIndex = bHasRuntimeWaves ? 0 : INDEX_NONE;
	ResetTrackedEnemies();

	PendingSpawnCounts.Reset();
	SpawnTimerHandles.Reset();

	if (bHasRuntimeWaves)
	{
		// Build runtime spawn counts so editor-authored data stays untouched.
		PendingSpawnCounts.SetNum(EncounterWavesRuntime.Num());
		SpawnTimerHandles.SetNum(EncounterWavesRuntime.Num());

		for (int32 WaveIdx = 0; WaveIdx < EncounterWavesRuntime.Num(); ++WaveIdx)
		{
			const FWaveDefinition& WaveDef = EncounterWavesRuntime[WaveIdx];
			PendingSpawnCounts[WaveIdx].SetNum(WaveDef.EnemySets.Num());
			SpawnTimerHandles[WaveIdx].SetNum(WaveDef.EnemySets.Num());

			for (int32 SetIdx = 0; SetIdx < WaveDef.EnemySets.Num(); ++SetIdx)
			{
				const FEnemySet& EnemySet = WaveDef.EnemySets[SetIdx];
				PendingSpawnCounts[WaveIdx][SetIdx] = EnemySet.Count;
			}
		}
	}

	OnEncounterStarted.Broadcast(this);
	StartAggroReissueTimer();

	// Close doors before the first wave begins.
	SetDoorArrayEnabled(DoorsToClose, true);

	GetWorldTimerManager().ClearTimer(InitialSpawnDelayHandle);
	if (bHasRuntimeWaves && InitialSpawnDelay > 0.f)
	{
		GetWorldTimerManager().SetTimer(InitialSpawnDelayHandle, this, &AAeyerjiSpawnerGroup::KickoffFirstWave, InitialSpawnDelay, false);
	}
	else if (bHasRuntimeWaves)
	{
		KickoffFirstWave();
	}
}

void AAeyerjiSpawnerGroup::ActivateEncounterWithRuntimeWaves(const TArray<FWaveDefinition>& RuntimeWaves, AActor* ActivationInstigator, AController* ActivationController)
{
	if (!HasAuthority())
	{
		return;
	}

	if (bActive || RuntimeWaves.IsEmpty())
	{
		return;
	}

	GetWorldTimerManager().ClearAllTimersForObject(this);
	EncounterWavesRuntime = RuntimeWaves;
	CacheActivationStimulus(ActivationInstigator, ActivationController);
	RebuildSpawnDiscoveryCache();
	ResetSpawnPointCycle();
	if (SpawnPointMode != EAeyerjiSpawnPointMode::Random)
	{
		RebuildSpawnPointOrder();
	}

	bActive = true;
	bCleared = false;
	bAwaitingManualSpawns = false;
	CurrentWaveIndex = 0;
	ResetTrackedEnemies();

	PendingSpawnCounts.Reset();
	SpawnTimerHandles.Reset();
	PendingSpawnCounts.SetNum(EncounterWavesRuntime.Num());
	SpawnTimerHandles.SetNum(EncounterWavesRuntime.Num());

	int32 TotalPendingSpawns = 0;
	for (int32 WaveIdx = 0; WaveIdx < EncounterWavesRuntime.Num(); ++WaveIdx)
	{
		const FWaveDefinition& WaveDef = EncounterWavesRuntime[WaveIdx];
		PendingSpawnCounts[WaveIdx].SetNum(WaveDef.EnemySets.Num());
		SpawnTimerHandles[WaveIdx].SetNum(WaveDef.EnemySets.Num());

		for (int32 SetIdx = 0; SetIdx < WaveDef.EnemySets.Num(); ++SetIdx)
		{
			const FEnemySet& EnemySet = WaveDef.EnemySets[SetIdx];
			PendingSpawnCounts[WaveIdx][SetIdx] = EnemySet.Count;
			TotalPendingSpawns += PendingSpawnCounts[WaveIdx][SetIdx];
		}
	}

	AJ_LOG_VERY_VERBOSE(this, TEXT("SurvivalSpawner %s activated with %d waves, %d pending spawns, %d spawn points, aggro actor=%s controller=%s."),
		*GetNameSafe(this),
		EncounterWavesRuntime.Num(),
		TotalPendingSpawns,
		SpawnPoints.Num(),
		*GetNameSafe(ResolveAggroTargetActor()),
		*GetNameSafe(ResolveAggroController()));

	OnEncounterStarted.Broadcast(this);
	StartAggroReissueTimer();
	SetDoorArrayEnabled(DoorsToClose, true);

	if (InitialSpawnDelay > 0.f)
	{
		GetWorldTimerManager().SetTimer(InitialSpawnDelayHandle, this, &AAeyerjiSpawnerGroup::KickoffFirstWave, InitialSpawnDelay, false);
	}
	else
	{
		KickoffFirstWave();
	}
}

void AAeyerjiSpawnerGroup::ConfigureDefenseObjectiveTarget(AActor* ObjectiveActor, const FAeyerjiDefenseTargetingSettings& TargetingSettings)
{
	if (!HasAuthority())
	{
		return;
	}

	DefenseObjectiveTargetActor = ObjectiveActor;
	DefenseTargetingSettings = TargetingSettings;
	DefenseTargetingSettings.PlayerThreatAcquireRadius = FMath::Max(0.f, DefenseTargetingSettings.PlayerThreatAcquireRadius);
	DefenseTargetingSettings.PlayerThreatReleaseRadius = FMath::Max(
		DefenseTargetingSettings.PlayerThreatAcquireRadius,
		DefenseTargetingSettings.PlayerThreatReleaseRadius);
	DefenseTargetingSettings.PlayerThreatObjectiveAcquireRadius = DefenseTargetingSettings.PlayerThreatObjectiveAcquireRadius > 0.f
		? DefenseTargetingSettings.PlayerThreatObjectiveAcquireRadius
		: FAeyerjiDefenseTargetingSettings().PlayerThreatObjectiveAcquireRadius;
	DefenseTargetingSettings.PlayerThreatObjectiveReleaseRadius = DefenseTargetingSettings.PlayerThreatObjectiveReleaseRadius > 0.f
		? DefenseTargetingSettings.PlayerThreatObjectiveReleaseRadius
		: FAeyerjiDefenseTargetingSettings().PlayerThreatObjectiveReleaseRadius;
	DefenseTargetingSettings.PlayerThreatObjectiveReleaseRadius = FMath::Max(
		DefenseTargetingSettings.PlayerThreatObjectiveAcquireRadius,
		DefenseTargetingSettings.PlayerThreatObjectiveReleaseRadius);
	DefenseTargetingSettings.PlayerDistanceBias = FMath::Max(0.f, DefenseTargetingSettings.PlayerDistanceBias);

	for (const TWeakObjectPtr<AActor>& EnemyRef : TrackedLiveEnemies)
	{
		if (APawn* EnemyPawn = Cast<APawn>(EnemyRef.Get()))
		{
			if (AEnemyAIController* EnemyAI = Cast<AEnemyAIController>(EnemyPawn->GetController()))
			{
				EnemyAI->ConfigureDefenseObjectiveTargeting(DefenseObjectiveTargetActor.Get(), DefenseTargetingSettings);
			}
		}
	}
}

void AAeyerjiSpawnerGroup::ClearDefenseObjectiveTarget()
{
	if (!HasAuthority())
	{
		return;
	}

	AActor* PreviousObjective = DefenseObjectiveTargetActor.Get();
	DefenseObjectiveTargetActor.Reset();

	for (const TWeakObjectPtr<AActor>& EnemyRef : TrackedLiveEnemies)
	{
		if (APawn* EnemyPawn = Cast<APawn>(EnemyRef.Get()))
		{
			if (AEnemyAIController* EnemyAI = Cast<AEnemyAIController>(EnemyPawn->GetController()))
			{
				if (EnemyAI->GetTargetActor() == PreviousObjective)
				{
					EnemyAI->SetTargetActor(nullptr);
				}
				EnemyAI->SetDefenseObjectiveTargetActor(nullptr);
			}
		}
	}
}

int32 AAeyerjiSpawnerGroup::GetWaveEnemyTotal(const int32 WaveIndex) const
{
	if (!EncounterWavesRuntime.IsValidIndex(WaveIndex))
	{
		return 0;
	}

	int32 EnemyTotal = 0;
	for (const FEnemySet& EnemySet : EncounterWavesRuntime[WaveIndex].EnemySets)
	{
		EnemyTotal += FMath::Max(0, EnemySet.Count);
	}

	return EnemyTotal;
}

FText AAeyerjiSpawnerGroup::GetWaveDisplayLabel(const int32 WaveIndex) const
{
	return EncounterWavesRuntime.IsValidIndex(WaveIndex)
		? EncounterWavesRuntime[WaveIndex].WaveLabel
		: FText::GetEmpty();
}

bool AAeyerjiSpawnerGroup::DoesWaveContainBoss(const int32 WaveIndex) const
{
	if (!EncounterWavesRuntime.IsValidIndex(WaveIndex))
	{
		return false;
	}

	for (const FEnemySet& EnemySet : EncounterWavesRuntime[WaveIndex].EnemySets)
	{
		if (EnemySet.bIsBoss)
		{
			return true;
		}
	}

	return false;
}

void AAeyerjiSpawnerGroup::ResetEncounter()
{
	GetWorldTimerManager().ClearAllTimersForObject(this);
	StopAggroReissueTimer();

	bActive = false;
	bCleared = false;
	CurrentWaveIndex = INDEX_NONE;
	bAwaitingManualSpawns = false;
	ResetTrackedEnemies();

	PendingSpawnCounts.Reset();
	SpawnTimerHandles.Reset();
	EncounterWavesRuntime.Reset();
	ResetSpawnPointCycle();

	// Idle state: combat doors open, clear doors closed.
	SetDoorArrayEnabled(DoorsToClose, false);
	SetDoorArrayEnabled(DoorsToOpenOnClear, false);

	ClearAggroCache();
}

void AAeyerjiSpawnerGroup::RegisterExternalEnemy(APawn* SpawnedPawn, const FEnemySet& EnemyTemplate, bool bApplyEliteSettings, bool bApplyAggro, bool bAutoActivate, bool bAutoActivateOnlyIfNoWaves, AActor* ActivationInstigator, AController* ActivationController, bool bSkipRandomEliteResolution)
{
	if (!HasAuthority() || !IsValid(SpawnedPawn))
	{
		return;
	}

	if (bCleared)
	{
		return;
	}

	const bool bHasRuntimeWaves = !EncounterWavesRuntime.IsEmpty() || Waves.Num() > 0 || (EncounterDefinition && bPreferEncounterAsset);
	const bool bShouldAutoActivate = bAutoActivate && (!bAutoActivateOnlyIfNoWaves || !bHasRuntimeWaves);

	if (bShouldAutoActivate && !bActive)
	{
		ActivateEncounter(ActivationInstigator, ActivationController);
	}

	if (!bActive && !EncounterWavesRuntime.IsEmpty() && PendingSpawnCounts.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("RegisterExternalEnemy on %s without ActivateEncounter; enable bAutoActivate or call ActivateEncounter first to build wave state"), *GetNameSafe(this));
		return;
	}

	bool bStartedEncounter = false;
	if (!bActive)
	{
		CacheActivationStimulus(ActivationInstigator, ActivationController);
		bActive = true;
		bCleared = false;
		bAwaitingManualSpawns = false;
		CurrentWaveIndex = EncounterWavesRuntime.IsEmpty() ? INDEX_NONE : CurrentWaveIndex;

		SetDoorArrayEnabled(DoorsToClose, true);
		OnEncounterStarted.Broadcast(this);
		bStartedEncounter = true;
	}

	bAwaitingManualSpawns = false;

	SpawnedPawn->OnDestroyed.RemoveDynamic(this, &AAeyerjiSpawnerGroup::OnEnemyDestroyed);
	SpawnedPawn->OnDestroyed.AddDynamic(this, &AAeyerjiSpawnerGroup::OnEnemyDestroyed);
	if (AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(SpawnedPawn))
	{
		Enemy->OnEnemyDied.RemoveDynamic(this, &AAeyerjiSpawnerGroup::OnEnemyDied);
		Enemy->OnEnemyDied.AddDynamic(this, &AAeyerjiSpawnerGroup::OnEnemyDied);
	}

	const TWeakObjectPtr<AActor> EnemyKey(SpawnedPawn);
	if (!TrackedLiveEnemies.Contains(EnemyKey))
	{
		TrackedLiveEnemies.Add(EnemyKey);
	}
	LiveEnemies = TrackedLiveEnemies.Num();

	const bool bResolveElite = bApplyEliteSettings && !bSkipRandomEliteResolution;
	const FEnemySet ResolvedTemplate = bResolveElite ? ResolveEliteSpawnSet(EnemyTemplate) : EnemyTemplate;
	const bool bIsStaticBoss = ResolvedTemplate.bIsBoss;
	const bool bSkipEnemyScaling = bIsStaticBoss || (ResolvedTemplate.bIsElite && ResolvedTemplate.bSkipEliteAutoScaling);
	FTrackedEnemyScalingState& ScalingState = TrackedEnemyScalingStates.FindOrAdd(EnemyKey);
	ScalingState.ResolvedTemplate = ResolvedTemplate;
	ScalingState.EliteHealthMultiplier = 1.f;
	ScalingState.EliteDamageMultiplier = 1.f;
	ScalingState.EliteRangeMultiplier = 1.f;
	ScalingState.bHasEliteAutoScaling = bApplyEliteSettings && ResolvedTemplate.bIsElite && !bIsStaticBoss && !ResolvedTemplate.bSkipEliteAutoScaling;
	if (bIsStaticBoss)
	{
		TrackedBossEnemies.Add(EnemyKey);
	}

	if (PoolSettings.bEnablePooling)
	{
		CapturePooledEnemyBaseline(SpawnedPawn, ResolvedTemplate);
		if (AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(SpawnedPawn))
		{
			Enemy->SetOwningSpawnerPool(this, true);
		}
	}

	AJ_LOG(this, TEXT("RegisterExternalEnemy: Spawner=%s Pawn=%s InputClass=%s ResolvedClass=%s Elite=%d SkipEliteAutoScaling=%d SkipEnemyScaling=%d ApplyElite=%d ApplyAggro=%d AutoActivate=%d SkipRandomEliteResolution=%d ActivationInstigator=%s ActivationController=%s"),
		*GetNameSafe(this),
		*GetNameSafe(SpawnedPawn),
		*GetNameSafe(EnemyTemplate.EnemyClass),
		*GetNameSafe(ResolvedTemplate.EnemyClass),
		ResolvedTemplate.bIsElite ? 1 : 0,
		ResolvedTemplate.bSkipEliteAutoScaling ? 1 : 0,
		bSkipEnemyScaling ? 1 : 0,
		bApplyEliteSettings ? 1 : 0,
		bApplyAggro ? 1 : 0,
		bAutoActivate ? 1 : 0,
		bSkipRandomEliteResolution ? 1 : 0,
		*GetNameSafe(ActivationInstigator),
		*GetNameSafe(ActivationController));

	// Bosses are authored as static archetypes; avoid all runtime stat scaling for them.
	if (bIsStaticBoss)
	{
		if (AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(SpawnedPawn))
		{
			Enemy->ApplyArchetypeData();
		}

		if (!BossActorTag.IsNone())
		{
			SpawnedPawn->Tags.AddUnique(BossActorTag);
			TrackPooledActorTag(SpawnedPawn, BossActorTag);
		}

		if (BossGameplayTag.IsValid())
		{
			if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(SpawnedPawn))
			{
				ASC->AddLooseGameplayTag(BossGameplayTag);
				TrackPooledLooseTag(SpawnedPawn, BossGameplayTag);
			}
		}

		RefreshSpawnedPawnStatusBar(SpawnedPawn);
	}
	else if (!bSkipEnemyScaling)
	{
		ApplyEnemyScaling(SpawnedPawn, ResolvedTemplate);
	}

	if (bApplyAggro)
	{
		if (ActivationInstigator || ActivationController)
		{
			CacheActivationStimulus(ActivationInstigator, ActivationController);
		}

		ApplyAggroToSpawnedPawn(SpawnedPawn);
	}

	if (bApplyEliteSettings && !bIsStaticBoss)
	{
		UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(SpawnedPawn, /*LookForComponent*/ true);
		if (ScalingState.bHasEliteAutoScaling && ASC)
		{
			CaptureTrackedBaseValueIfNeeded(EnemyKey, ASC, UAeyerjiAttributeSet::GetHPMaxAttribute(), TEXT("AeyerjiAttributeSet.HPMax"));
			CaptureTrackedBaseValueIfNeeded(EnemyKey, ASC, UAeyerjiAttributeSet::GetAttackDamageAttribute(), TEXT("AeyerjiAttributeSet.AttackDamage"));
			CaptureTrackedBaseValueIfNeeded(EnemyKey, ASC, UAeyerjiAttributeSet::GetAttackRangeAttribute(), TEXT("AeyerjiAttributeSet.AttackRange"));
		}

		const float PreEliteHPMax = ASC ? ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetHPMaxAttribute()) : 0.f;
		const float PreEliteDamage = ASC ? ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetAttackDamageAttribute()) : 0.f;
		const float PreEliteRange = ASC ? ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetAttackRangeAttribute()) : 0.f;
		ApplyElitePackage(SpawnedPawn, ResolvedTemplate);

		if (ScalingState.bHasEliteAutoScaling && ASC)
		{
			const float PostEliteHPMax = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetHPMaxAttribute());
			const float PostEliteDamage = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetAttackDamageAttribute());
			const float PostEliteRange = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetAttackRangeAttribute());

			ScalingState.EliteHealthMultiplier = PreEliteHPMax > 0.f ? (PostEliteHPMax / PreEliteHPMax) : 1.f;
			ScalingState.EliteDamageMultiplier = PreEliteDamage > 0.f ? (PostEliteDamage / PreEliteDamage) : 1.f;
			ScalingState.EliteRangeMultiplier = PreEliteRange > 0.f ? (PostEliteRange / PreEliteRange) : 1.f;
		}
	}
	else if (EnemyTemplate.bIsMiniBoss && !MiniBossActorTag.IsNone())
	{
		SpawnedPawn->Tags.AddUnique(MiniBossActorTag);
		TrackPooledActorTag(SpawnedPawn, MiniBossActorTag);
	}

	RegisterProgressEnemy(SpawnedPawn, ResolvedTemplate.ProgressPoints);

	// If the encounter has no wave data and we just started it for this manual spawn, ensure completion can fire when the pawn dies.
	if (bStartedEncounter && EncounterWavesRuntime.IsEmpty())
	{
		CheckWaveCompletion();
	}
}

void AAeyerjiSpawnerGroup::StartWave(int32 WaveIndex)
{
	if (!HasAuthority())
	{
		return;
	}

	if (!bActive || !EncounterWavesRuntime.IsValidIndex(WaveIndex) || !PendingSpawnCounts.IsValidIndex(WaveIndex))
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(WaveDelayHandle);

	CurrentWaveIndex = WaveIndex;
	OnWaveStarted.Broadcast(this, CurrentWaveIndex);

	bool bScheduledAny = false;
	for (int32 SetIdx = 0; SetIdx < PendingSpawnCounts[WaveIndex].Num(); ++SetIdx)
	{
		if (PendingSpawnCounts[WaveIndex][SetIdx] > 0)
		{
			ScheduleNextSpawn(WaveIndex, SetIdx, 0.0f);
			bScheduledAny = true;
		}
	}

	if (!bScheduledAny)
	{
		// This wave is effectively empty; immediately attempt to progress.
		CheckWaveCompletion();
	}
}

void AAeyerjiSpawnerGroup::ScheduleNextSpawn(int32 WaveIndex, int32 SetIndex, float DelaySeconds)
{
	if (!HasAuthority())
	{
		return;
	}

	if (!PendingSpawnCounts.IsValidIndex(WaveIndex) ||
	    !PendingSpawnCounts[WaveIndex].IsValidIndex(SetIndex) ||
	    PendingSpawnCounts[WaveIndex][SetIndex] <= 0)
	{
		return;
	}

	FTimerDelegate Delegate;
	Delegate.BindUObject(this, &AAeyerjiSpawnerGroup::HandleSpawnTimer, WaveIndex, SetIndex);

	FTimerHandle& Handle = SpawnTimerHandles[WaveIndex][SetIndex];
	GetWorldTimerManager().ClearTimer(Handle);

	const float ClampedDelay = FMath::Max(DelaySeconds, KINDA_SMALL_NUMBER);
	GetWorldTimerManager().SetTimer(Handle, Delegate, ClampedDelay, false);
}

void AAeyerjiSpawnerGroup::HandleSpawnTimer(int32 WaveIndex, int32 SetIndex)
{
	if (!HasAuthority())
	{
		return;
	}

	if (!bActive || WaveIndex != CurrentWaveIndex)
	{
		return;
	}

	if (!PendingSpawnCounts.IsValidIndex(WaveIndex) ||
	    !PendingSpawnCounts[WaveIndex].IsValidIndex(SetIndex) ||
	    PendingSpawnCounts[WaveIndex][SetIndex] <= 0)
	{
		return;
	}

	if (!SpawnOneFromSet(WaveIndex, SetIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("Spawner %s failed to spawn wave=%d set=%d; retrying shortly."),
			*GetNameSafe(this),
			WaveIndex,
			SetIndex);
		ScheduleNextSpawn(WaveIndex, SetIndex, 0.5f);
		return;
	}

	int32& Remaining = PendingSpawnCounts[WaveIndex][SetIndex];
	Remaining = FMath::Max(0, Remaining - 1);

	if (Remaining > 0)
	{
		const float Interval = EncounterWavesRuntime.IsValidIndex(WaveIndex) &&
			EncounterWavesRuntime[WaveIndex].EnemySets.IsValidIndex(SetIndex)
				? FMath::Max(0.f, EncounterWavesRuntime[WaveIndex].EnemySets[SetIndex].SpawnInterval)
				: 0.f;
		ScheduleNextSpawn(WaveIndex, SetIndex, Interval);
	}
	else
	{
		CheckWaveCompletion();
	}
}

bool AAeyerjiSpawnerGroup::HaveAllSpawnsEmitted(int32 WaveIndex) const
{
	if (!PendingSpawnCounts.IsValidIndex(WaveIndex))
	{
		return true;
	}

	for (int32 Remaining : PendingSpawnCounts[WaveIndex])
	{
		if (Remaining > 0)
		{
			return false;
		}
	}

	return true;
}

void AAeyerjiSpawnerGroup::CheckWaveCompletion()
{
	if (!HasAuthority())
	{
		return;
	}

	if (!bActive)
	{
		return;
	}

	if (EncounterWavesRuntime.IsEmpty())
	{
		if (!bAwaitingManualSpawns && LiveEnemies <= 0)
		{
			FinishEncounter();
		}
		return;
	}

	if (CurrentWaveIndex == INDEX_NONE || !EncounterWavesRuntime.IsValidIndex(CurrentWaveIndex))
	{
		return;
	}

	if (!HaveAllSpawnsEmitted(CurrentWaveIndex) || LiveEnemies > 0)
	{
		return;
	}

	const int32 CompletedWave = CurrentWaveIndex;
	const float Delay = EncounterWavesRuntime.IsValidIndex(CompletedWave)
		                    ? FMath::Max(0.f, EncounterWavesRuntime[CompletedWave].PostSpawnDelay)
		                    : 0.f;

	CurrentWaveIndex++;

	if (CurrentWaveIndex >= EncounterWavesRuntime.Num())
	{
		FinishEncounter();
	}
	else
	{
		GetWorldTimerManager().SetTimer(
			WaveDelayHandle,
			FTimerDelegate::CreateUObject(this, &AAeyerjiSpawnerGroup::StartWave, CurrentWaveIndex),
			Delay,
			false);
	}
}

void AAeyerjiSpawnerGroup::FinishEncounter()
{
	if (!HasAuthority())
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(WaveDelayHandle);
	StopAggroReissueTimer();

	bActive = false;
	bCleared = true;
	bAwaitingManualSpawns = false;
	ResetTrackedEnemies();

	SetDoorArrayEnabled(DoorsToClose, false);
	SetDoorArrayEnabled(DoorsToOpenOnClear, true);

	OnEncounterCleared.Broadcast(this);
}

void AAeyerjiSpawnerGroup::KickoffFirstWave()
{
	if (!HasAuthority())
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(InitialSpawnDelayHandle);

	if (!bActive)
	{
		return;
	}

	if (CurrentWaveIndex == INDEX_NONE)
	{
		CurrentWaveIndex = 0;
	}

	StartWave(CurrentWaveIndex);
}

bool AAeyerjiSpawnerGroup::ChooseSpawnTransform(TSubclassOf<APawn> EnemyClass, FTransform& OutTransform)
{
	FVector ReferenceLocation = GetActorLocation();
	const bool bHasReferenceLocation = ResolveSpawnReferenceLocation(ReferenceLocation);

	const int32 SpawnIndex = GetNextSpawnPointIndex();
	if (SpawnPoints.IsValidIndex(SpawnIndex))
	{
		if (AActor* Point = SpawnPoints[SpawnIndex])
		{
			const FTransform PointTransform = Point->GetActorTransform();
			if (!bRequireSpawnReachableFromTarget || !bHasReferenceLocation || IsSpawnCandidateReachable(PointTransform.GetLocation(), ReferenceLocation))
			{
				OutTransform = PointTransform;
				return true;
			}
		}
	}

	const float SpawnHalfHeight = GetCachedSpawnHalfHeight(EnemyClass);

	if (UWorld* World = GetWorld())
	{
		const float MaxDistanceFromReference = FMath::Max(0.f, SpawnMaxDistanceFromTarget);
		const int32 MaxRegionSpawnAttempts = FMath::Max(1, SpawnRegionSearchAttempts);

		if (!CachedSpawnRegions.IsEmpty())
		{
			TArray<const FCachedAeyerjiSpawnRegion*> PreferredRegions;
			PreferredRegions.Reserve(CachedSpawnRegions.Num());
			for (const FCachedAeyerjiSpawnRegion& CachedRegion : CachedSpawnRegions)
			{
				if (!IsValid(CachedRegion.Region) || !CachedRegion.Bounds.IsValid || CachedRegion.Weight <= 0.f)
				{
					continue;
				}

				const float DistSq = CachedRegion.Bounds.ComputeSquaredDistanceToPoint(ReferenceLocation);
				if (!bHasReferenceLocation || MaxDistanceFromReference <= 0.f || DistSq <= FMath::Square(MaxDistanceFromReference))
				{
					PreferredRegions.Add(&CachedRegion);
				}
			}

			TArray<const FCachedAeyerjiSpawnRegion*> AllRegions;
			AllRegions.Reserve(CachedSpawnRegions.Num());
			for (const FCachedAeyerjiSpawnRegion& CachedRegion : CachedSpawnRegions)
			{
				if (IsValid(CachedRegion.Region) && CachedRegion.Bounds.IsValid && CachedRegion.Weight > 0.f)
				{
					AllRegions.Add(&CachedRegion);
				}
			}

			const TArray<const FCachedAeyerjiSpawnRegion*>& CandidateRegions = PreferredRegions.IsEmpty() ? AllRegions : PreferredRegions;
			const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			for (int32 Attempt = 0; Attempt < MaxRegionSpawnAttempts && !CandidateRegions.IsEmpty(); ++Attempt)
			{
				const FCachedAeyerjiSpawnRegion* CachedRegion = CandidateRegions[FMath::RandHelper(CandidateRegions.Num())];
				if (!CachedRegion)
				{
					continue;
				}

				const FBox& Bounds = CachedRegion->Bounds;
				const FVector Min = Bounds.Min;
				const FVector Max = Bounds.Max;
				const FVector SampleXY(
					FMath::FRandRange(Min.X, Max.X),
					FMath::FRandRange(Min.Y, Max.Y),
					Max.Z + SpawnGroundTraceUpOffset);

				FHitResult GroundHit;
				const FVector TraceEnd(SampleXY.X, SampleXY.Y, Min.Z - SpawnGroundTraceDownDistance);
				FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(AeyerjiSurvivalSpawnGround), true, this);
				if (!World->LineTraceSingleByChannel(GroundHit, SampleXY, TraceEnd, ECC_WorldStatic, QueryParams))
				{
					continue;
				}

				FVector GroundLocation = GroundHit.ImpactPoint;
				if (NavSys)
				{
					FNavLocation Projected;
					if (!NavSys->ProjectPointToNavigation(GroundLocation, Projected, SpawnNavProjectionExtent))
					{
						continue;
					}
					GroundLocation = Projected.Location;
				}

				if (bRequireSpawnReachableFromTarget && bHasReferenceLocation && !IsSpawnCandidateReachable(GroundLocation, ReferenceLocation))
				{
					continue;
				}

				const FVector SpawnLocation = GroundLocation + FVector(0.f, 0.f, SpawnHalfHeight + SpawnGroundClearance);
				OutTransform = FTransform(GetActorRotation(), SpawnLocation, FVector::OneVector);
				return true;
			}

			if (NavSys && bHasReferenceLocation && MaxDistanceFromReference > 0.f)
			{
				FNavLocation Reachable;
				if (NavSys->GetRandomReachablePointInRadius(ReferenceLocation, MaxDistanceFromReference, Reachable)
					&& (!bRequireSpawnReachableFromTarget || IsSpawnCandidateReachable(Reachable.Location, ReferenceLocation)))
				{
					OutTransform = FTransform(GetActorRotation(), Reachable.Location + FVector(0.f, 0.f, SpawnHalfHeight + SpawnGroundClearance), FVector::OneVector);
					return true;
				}
			}
		}
	}

	for (AActor* Point : SpawnPoints)
	{
		if (IsValid(Point))
		{
			const FTransform PointTransform = Point->GetActorTransform();
			if (!bRequireSpawnReachableFromTarget || !bHasReferenceLocation || IsSpawnCandidateReachable(PointTransform.GetLocation(), ReferenceLocation))
			{
				OutTransform = PointTransform;
				return true;
			}
		}
	}

	const FTransform OwnTransform = GetActorTransform();
	if (!bRequireSpawnReachableFromTarget || !bHasReferenceLocation || IsSpawnCandidateReachable(OwnTransform.GetLocation(), ReferenceLocation))
	{
		OutTransform = OwnTransform;
		return true;
	}

	UE_LOG(LogTemp, Warning, TEXT("Spawner %s could not find a reachable spawn point. Reference=%s MinDist=%.1f MaxDist=%.1f Regions/SpawnPoints may be on another nav island."),
		*GetNameSafe(this),
		*ReferenceLocation.ToCompactString(),
		SpawnMinDistanceFromTarget,
		SpawnMaxDistanceFromTarget);
	return false;
}

bool AAeyerjiSpawnerGroup::ResolveSpawnReferenceLocation(FVector& OutLocation) const
{
	if (const AActor* AggroActor = ResolveAggroTargetActor())
	{
		OutLocation = AggroActor->GetActorLocation();
		return true;
	}

	if (const APawn* AggroPawn = ResolveAggroTargetPawn())
	{
		OutLocation = AggroPawn->GetActorLocation();
		return true;
	}

	if (bFallbackReachabilityTargetToPlayerStart)
	{
		if (IsValid(CachedFallbackPlayerStart))
		{
			OutLocation = CachedFallbackPlayerStart->GetActorLocation();
			return true;
		}
	}

	return false;
}

bool AAeyerjiSpawnerGroup::IsSpawnCandidateReachable(const FVector& CandidateLocation, const FVector& ReferenceLocation) const
{
	const float DistSq = FVector::DistSquared2D(CandidateLocation, ReferenceLocation);
	const float MinDistance = FMath::Max(0.f, SpawnMinDistanceFromTarget);
	const float MaxDistance = FMath::Max(0.f, SpawnMaxDistanceFromTarget);
	if (MinDistance > 0.f && DistSq < FMath::Square(MinDistance))
	{
		return false;
	}

	if (MaxDistance > 0.f && DistSq > FMath::Square(MaxDistance))
	{
		return false;
	}

	if (!bRequireSpawnReachableFromTarget)
	{
		return true;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	UNavigationPath* Path = UNavigationSystemV1::FindPathToLocationSynchronously(World, CandidateLocation, ReferenceLocation);
	return Path && Path->IsValid() && !Path->IsPartial();
}

bool AAeyerjiSpawnerGroup::SpawnOneFromSet(int32 WaveIndex, int32 SetIndex)
{
	if (!HasAuthority())
	{
		return false;
	}

	if (!EncounterWavesRuntime.IsValidIndex(WaveIndex))
	{
		return false;
	}

	const FWaveDefinition& WaveDef = EncounterWavesRuntime[WaveIndex];
	if (!WaveDef.EnemySets.IsValidIndex(SetIndex))
	{
		return false;
	}

	const FEnemySet& EnemySet = WaveDef.EnemySets[SetIndex];
	if (!EnemySet.EnemyClass)
	{
		return false;
	}

	const FEnemySet ResolvedEnemySet = ResolveEliteSpawnSet(EnemySet);
	FTransform SpawnTransform;
	if (!ChooseSpawnTransform(ResolvedEnemySet.EnemyClass, SpawnTransform))
	{
		return false;
	}
	AActor* AggroActor = ResolveAggroTargetActor();
	AController* AggroController = ResolveAggroController();
	APawn* InstigatorPawn = ResolveAggroTargetPawn();

	AJ_LOG_VERY_VERBOSE(this, TEXT("SurvivalSpawner %s spawning wave=%d set=%d class=%s location=%s aggro=%s."),
		*GetNameSafe(this),
		WaveIndex,
		SetIndex,
		*GetNameSafe(ResolvedEnemySet.EnemyClass),
		*SpawnTransform.GetLocation().ToCompactString(),
		*GetNameSafe(AggroActor));

	APawn* SpawnedPawn = SpawnRegisteredEnemyFromSet(
		ResolvedEnemySet,
		SpawnTransform,
		/*Owner=*/this,
		InstigatorPawn,
		/*bApplyEliteSettings=*/true,
		/*bApplyAggro=*/true,
		/*bAutoActivate=*/false,
		/*bAutoActivateOnlyIfNoWaves=*/true,
		/*ActivationInstigator=*/AggroActor,
		/*ActivationController=*/AggroController,
		/*bSkipRandomEliteResolution=*/true);
	if (!SpawnedPawn)
	{
		return false;
	}

	if (bTagSpawnedEnemiesAsCullIgnored && !SpawnedEnemyCullIgnoreActorTag.IsNone())
	{
		SpawnedPawn->Tags.AddUnique(SpawnedEnemyCullIgnoreActorTag);
		TrackPooledActorTag(SpawnedPawn, SpawnedEnemyCullIgnoreActorTag);
	}
	return true;
}

FAeyerjiEnemyPoolKey AAeyerjiSpawnerGroup::MakePoolKey(const FEnemySet& EnemySet) const
{
	FAeyerjiEnemyPoolKey Key;
	Key.EnemyClass = EnemySet.EnemyClass;
	Key.EnemyArchetypeTag = EnemySet.EnemyArchetypeTag;
	Key.bIsBoss = EnemySet.bIsBoss;
	Key.bIsMiniBoss = EnemySet.bIsMiniBoss && !EnemySet.bIsBoss;
	Key.bIsElite = EnemySet.bIsElite || EnemySet.bIsMiniBoss || EnemySet.bIsBoss;
	return Key;
}

APawn* AAeyerjiSpawnerGroup::SpawnRawEnemyActor(const FEnemySet& EnemySet, const FTransform& SpawnTransform, AActor* SpawnOwner, APawn* InstigatorPawn, bool bValidateNav)
{
	if (!HasAuthority() || !EnemySet.EnemyClass)
	{
		return nullptr;
	}

	UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client)
	{
		return nullptr;
	}

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	Params.Owner = SpawnOwner ? SpawnOwner : this;
	Params.Instigator = InstigatorPawn;

	APawn* SpawnedPawn = World->SpawnActor<APawn>(EnemySet.EnemyClass, SpawnTransform, Params);
	if (!SpawnedPawn)
	{
		AJ_LOG(this, TEXT("SpawnRawEnemyActor failed: SpawnActor returned null for Class=%s Location=%s"),
			*GetNameSafe(EnemySet.EnemyClass),
			*SpawnTransform.GetLocation().ToCompactString());
		return nullptr;
	}

	if (!bValidateNav)
	{
		return SpawnedPawn;
	}

	FAeyerjiNavSafetyResolveParams NavParams;
	NavParams.ProjectionExtent = FVector(500.f, 500.f, 1000.f);
	NavParams.SearchRadius = 600.f;
	NavParams.GroundTraceHeight = 400.f;
	NavParams.GroundTraceDepth = 1000.f;

	FAeyerjiNavSafetyResult SpawnNavResult;
	if (!UAeyerjiNavSafetyLibrary::ResolveSafeNavLocationForPawn(this, SpawnTransform.GetLocation(), SpawnedPawn, NavParams, SpawnNavResult))
	{
		AJ_LOG(this, TEXT("SpawnRawEnemyActor rejected off-nav spawn: Pawn=%s Class=%s Location=%s Reason=%s"),
			*GetNameSafe(SpawnedPawn),
			*GetNameSafe(EnemySet.EnemyClass),
			*SpawnTransform.GetLocation().ToCompactString(),
			*SpawnNavResult.FailureReason.ToString());
		SpawnedPawn->Destroy();
		return nullptr;
	}

	if (!SpawnedPawn->GetActorLocation().Equals(SpawnNavResult.GroundedLocation, 1.f))
	{
		SpawnedPawn->SetActorLocation(
			SpawnNavResult.GroundedLocation,
			/*bSweep=*/false,
			nullptr,
			ETeleportType::TeleportPhysics);
	}

	return SpawnedPawn;
}

APawn* AAeyerjiSpawnerGroup::AcquireInactivePooledEnemy(const FAeyerjiEnemyPoolKey& PoolKey)
{
	TArray<TWeakObjectPtr<APawn>>* Bucket = InactiveEnemyPools.Find(PoolKey);
	if (!Bucket)
	{
		return nullptr;
	}

	for (int32 Index = Bucket->Num() - 1; Index >= 0; --Index)
	{
		APawn* Candidate = (*Bucket)[Index].Get();
		Bucket->RemoveAtSwap(Index);
		if (!IsValid(Candidate))
		{
			continue;
		}

		if (FPooledEnemyRuntimeState* State = PooledEnemyStates.Find(TWeakObjectPtr<APawn>(Candidate)))
		{
			if (State->State == EAeyerjiPooledEnemyState::Inactive)
			{
				State->State = EAeyerjiPooledEnemyState::Active;
				return Candidate;
			}
		}
	}

	return nullptr;
}

void AAeyerjiSpawnerGroup::CapturePooledEnemyBaseline(APawn* EnemyPawn, const FEnemySet& ResolvedEnemySet)
{
	if (!PoolSettings.bEnablePooling || !IsValid(EnemyPawn))
	{
		return;
	}

	FPooledEnemyRuntimeState& State = PooledEnemyStates.FindOrAdd(TWeakObjectPtr<APawn>(EnemyPawn));
	State.PoolKey = MakePoolKey(ResolvedEnemySet);
	State.State = EAeyerjiPooledEnemyState::Active;
	if (State.bBaselineCaptured)
	{
		return;
	}

	State.OriginalScale = EnemyPawn->GetActorScale3D();

	if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(EnemyPawn, /*LookForComponent*/ true))
	{
		for (TFieldIterator<FProperty> PropIt(UAeyerjiAttributeSet::StaticClass()); PropIt; ++PropIt)
		{
			const FStructProperty* StructProperty = CastField<FStructProperty>(*PropIt);
			if (!StructProperty || StructProperty->Struct != FGameplayAttributeData::StaticStruct())
			{
				continue;
			}

			const FGameplayAttribute Attribute(*PropIt);
			if (Attribute.IsValid())
			{
				State.BaselineAttributeValues.FindOrAdd(
					FName(*FString::Printf(TEXT("AeyerjiAttributeSet.%s"), *PropIt->GetName())),
					ASC->GetNumericAttribute(Attribute));
			}
		}

		if (ASC->GetSet<UAeyerjiRewardAttributeSet>())
		{
			State.BaselineAttributeValues.FindOrAdd(
				TEXT("AeyerjiRewardAttributeSet.XPRewardBase"),
				ASC->GetNumericAttribute(UAeyerjiRewardAttributeSet::GetXPRewardBaseAttribute()));
		}
	}

	State.bBaselineCaptured = true;
}

void AAeyerjiSpawnerGroup::RestorePooledEnemyForCheckout(APawn* EnemyPawn, const FEnemySet& ResolvedEnemySet, const FTransform& SpawnTransform)
{
	if (!IsValid(EnemyPawn))
	{
		return;
	}

	CapturePooledEnemyBaseline(EnemyPawn, ResolvedEnemySet);
	CleanupSpawnerAppliedRuntimeState(EnemyPawn);

	if (FPooledEnemyRuntimeState* State = PooledEnemyStates.Find(TWeakObjectPtr<APawn>(EnemyPawn)))
	{
		if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(EnemyPawn, /*LookForComponent*/ true))
		{
			for (const TPair<FName, float>& Pair : State->BaselineAttributeValues)
			{
				if (Pair.Key == TEXT("AeyerjiRewardAttributeSet.XPRewardBase"))
				{
					ASC->SetNumericAttributeBase(UAeyerjiRewardAttributeSet::GetXPRewardBaseAttribute(), Pair.Value);
					continue;
				}

				const FGameplayAttribute Attribute = ResolveAttribute(Pair.Key);
				if (Attribute.IsValid())
				{
					ASC->SetNumericAttributeBase(Attribute, Pair.Value);
				}
			}
		}

		State->PoolKey = MakePoolKey(ResolvedEnemySet);
		State->State = EAeyerjiPooledEnemyState::Active;
		EnemyPawn->SetActorScale3D(State->OriginalScale);
	}

	SetPooledEnemyActiveState(EnemyPawn, SpawnTransform);
	if (AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(EnemyPawn))
	{
		Enemy->SetOwningSpawnerPool(this, true);
		Enemy->PrepareForPooledActivation();
	}
	MulticastSetPooledEnemyActive(EnemyPawn, SpawnTransform);
}

void AAeyerjiSpawnerGroup::CleanupSpawnerAppliedRuntimeState(APawn* EnemyPawn)
{
	FPooledEnemyRuntimeState* State = PooledEnemyStates.Find(TWeakObjectPtr<APawn>(EnemyPawn));
	if (!State || !IsValid(EnemyPawn))
	{
		return;
	}

	if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(EnemyPawn, /*LookForComponent*/ true))
	{
		for (const FActiveGameplayEffectHandle& EffectHandle : State->AppliedEffectHandles)
		{
			if (EffectHandle.IsValid())
			{
				ASC->RemoveActiveGameplayEffect(EffectHandle);
			}
		}
		State->AppliedEffectHandles.Reset();

		for (const FGameplayAbilitySpecHandle& AbilityHandle : State->GrantedAbilityHandles)
		{
			if (AbilityHandle.IsValid())
			{
				ASC->ClearAbility(AbilityHandle);
			}
		}
		State->GrantedAbilityHandles.Reset();

		for (const FGameplayTag& Tag : State->AppliedLooseTags)
		{
			if (Tag.IsValid())
			{
				ASC->SetLooseGameplayTagCount(Tag, 0);
			}
		}
		State->AppliedLooseTags.Reset();
	}

	for (const FName ActorTag : State->AppliedActorTags)
	{
		EnemyPawn->Tags.Remove(ActorTag);
	}
	State->AppliedActorTags.Reset();

	for (TWeakObjectPtr<UNiagaraComponent>& NiagaraComponentPtr : State->SpawnedNiagaraComponents)
	{
		if (UNiagaraComponent* NiagaraComponent = NiagaraComponentPtr.Get())
		{
			NiagaraComponent->DestroyComponent();
		}
	}
	State->SpawnedNiagaraComponents.Reset();
	DestroySpawnerAppliedVFX(EnemyPawn);
}

void AAeyerjiSpawnerGroup::TrackPooledActorTag(APawn* EnemyPawn, FName ActorTag)
{
	if (ActorTag.IsNone())
	{
		return;
	}

	if (FPooledEnemyRuntimeState* State = PooledEnemyStates.Find(TWeakObjectPtr<APawn>(EnemyPawn)))
	{
		State->AppliedActorTags.Add(ActorTag);
	}
}

void AAeyerjiSpawnerGroup::TrackPooledLooseTag(APawn* EnemyPawn, FGameplayTag GameplayTag)
{
	if (!GameplayTag.IsValid())
	{
		return;
	}

	if (FPooledEnemyRuntimeState* State = PooledEnemyStates.Find(TWeakObjectPtr<APawn>(EnemyPawn)))
	{
		State->AppliedLooseTags.Add(GameplayTag);
	}
}

void AAeyerjiSpawnerGroup::TrackPooledAbility(APawn* EnemyPawn, FGameplayAbilitySpecHandle AbilityHandle)
{
	if (!AbilityHandle.IsValid())
	{
		return;
	}

	if (FPooledEnemyRuntimeState* State = PooledEnemyStates.Find(TWeakObjectPtr<APawn>(EnemyPawn)))
	{
		State->GrantedAbilityHandles.Add(AbilityHandle);
	}
}

void AAeyerjiSpawnerGroup::TrackPooledEffect(APawn* EnemyPawn, FActiveGameplayEffectHandle EffectHandle)
{
	if (!EffectHandle.IsValid())
	{
		return;
	}

	if (FPooledEnemyRuntimeState* State = PooledEnemyStates.Find(TWeakObjectPtr<APawn>(EnemyPawn)))
	{
		State->AppliedEffectHandles.Add(EffectHandle);
	}
}

void AAeyerjiSpawnerGroup::TrackPooledNiagara(APawn* EnemyPawn, UNiagaraComponent* NiagaraComponent)
{
	if (!NiagaraComponent)
	{
		return;
	}

	if (FPooledEnemyRuntimeState* State = PooledEnemyStates.Find(TWeakObjectPtr<APawn>(EnemyPawn)))
	{
		State->SpawnedNiagaraComponents.Add(NiagaraComponent);
	}
}

FVector AAeyerjiSpawnerGroup::ResolvePooledOriginalScale(APawn* EnemyPawn) const
{
	if (const FPooledEnemyRuntimeState* State = PooledEnemyStates.Find(TWeakObjectPtr<APawn>(EnemyPawn)))
	{
		if (State->bBaselineCaptured)
		{
			return State->OriginalScale;
		}
	}

	return IsValid(EnemyPawn) ? EnemyPawn->GetActorScale3D() : FVector::OneVector;
}

FVector AAeyerjiSpawnerGroup::GetPoolParkingLocation() const
{
	return GetActorTransform().TransformPosition(PoolSettings.PoolParkingOffset);
}

void AAeyerjiSpawnerGroup::DestroySpawnerAppliedVFX(APawn* EnemyPawn)
{
	if (!IsValid(EnemyPawn))
	{
		return;
	}

	TInlineComponentArray<UNiagaraComponent*> NiagaraComponents(EnemyPawn);
	for (UNiagaraComponent* NiagaraComponent : NiagaraComponents)
	{
		if (!NiagaraComponent)
		{
			continue;
		}

		const UNiagaraSystem* Asset = NiagaraComponent->GetAsset();
		bool bSpawnerOwned = Asset && Asset == EliteVFXSystem;
		if (!bSpawnerOwned)
		{
			for (const FEliteAffixDefinition& Affix : EliteAffixPool)
			{
				if (Asset && Asset == Affix.VFXSystem)
				{
					bSpawnerOwned = true;
					break;
				}
			}
		}

		if (bSpawnerOwned)
		{
			NiagaraComponent->DestroyComponent();
		}
	}
}

void AAeyerjiSpawnerGroup::SetPooledEnemyInactiveState(APawn* EnemyPawn, const FVector& ParkingLocation)
{
	if (!IsValid(EnemyPawn))
	{
		return;
	}

	DestroySpawnerAppliedVFX(EnemyPawn);
	EnemyPawn->SetActorHiddenInGame(true);
	EnemyPawn->SetActorEnableCollision(false);
	EnemyPawn->SetActorTickEnabled(false);
	EnemyPawn->SetActorLocation(ParkingLocation, false, nullptr, ETeleportType::TeleportPhysics);

	if (UCapsuleComponent* Capsule = Cast<UCapsuleComponent>(EnemyPawn->GetComponentByClass(UCapsuleComponent::StaticClass())))
	{
		Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Capsule->SetGenerateOverlapEvents(false);
	}

	if (USkeletalMeshComponent* Mesh = Cast<USkeletalMeshComponent>(EnemyPawn->GetComponentByClass(USkeletalMeshComponent::StaticClass())))
	{
		Mesh->SetVisibility(false, true);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetComponentTickEnabled(false);
	}

	if (ACharacter* Character = Cast<ACharacter>(EnemyPawn))
	{
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->StopMovementImmediately();
			Movement->DisableMovement();
			Movement->SetComponentTickEnabled(false);
		}
	}

	if (AAIController* AIController = Cast<AAIController>(EnemyPawn->GetController()))
	{
		AIController->StopMovement();
		AIController->ClearFocus(EAIFocusPriority::Gameplay);
		if (UBrainComponent* Brain = AIController->GetBrainComponent())
		{
			Brain->StopLogic(TEXT("PooledInactive"));
		}
		if (UAIPerceptionComponent* Perception = AIController->GetPerceptionComponent())
		{
			Perception->SetComponentTickEnabled(false);
		}
	}

	EnemyPawn->SetNetDormancy(DORM_DormantAll);
	EnemyPawn->ForceNetUpdate();
}

void AAeyerjiSpawnerGroup::SetPooledEnemyActiveState(APawn* EnemyPawn, const FTransform& SpawnTransform)
{
	if (!IsValid(EnemyPawn))
	{
		return;
	}

	EnemyPawn->FlushNetDormancy();
	EnemyPawn->SetNetDormancy(DORM_Awake);
	EnemyPawn->SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::TeleportPhysics);
	EnemyPawn->SetActorHiddenInGame(false);
	EnemyPawn->SetActorEnableCollision(true);
	EnemyPawn->SetActorTickEnabled(true);

	if (ACharacter* Character = Cast<ACharacter>(EnemyPawn))
	{
		if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
		{
			Capsule->SetCollisionProfileName(UCollisionProfile::Pawn_ProfileName);
			Capsule->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Capsule->SetGenerateOverlapEvents(true);
		}

		if (USkeletalMeshComponent* Mesh = Character->GetMesh())
		{
			Mesh->SetVisibility(true, true);
			Mesh->SetComponentTickEnabled(true);
		}

		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			Movement->SetComponentTickEnabled(true);
			Movement->SetMovementMode(MOVE_Walking);
			Movement->StopMovementImmediately();
		}
	}

	if (AEnemyAIController* EnemyAI = Cast<AEnemyAIController>(EnemyPawn->GetController()))
	{
		EnemyAI->ResetForPooledReuse(SpawnTransform.GetLocation());
	}

	EnemyPawn->ForceNetUpdate();
}

void AAeyerjiSpawnerGroup::MulticastSetPooledEnemyInactive_Implementation(APawn* EnemyPawn, FVector ParkingLocation)
{
	if (HasAuthority())
	{
		return;
	}

	SetPooledEnemyInactiveState(EnemyPawn, ParkingLocation);
}

void AAeyerjiSpawnerGroup::MulticastSetPooledEnemyActive_Implementation(APawn* EnemyPawn, FTransform SpawnTransform)
{
	if (HasAuthority())
	{
		return;
	}

	SetPooledEnemyActiveState(EnemyPawn, SpawnTransform);
}

APawn* AAeyerjiSpawnerGroup::SpawnRegisteredEnemyFromSet(const FEnemySet& EnemySet, const FTransform& SpawnTransform, AActor* SpawnOwner, APawn* InstigatorPawn, bool bApplyEliteSettings, bool bApplyAggro, bool bAutoActivate, bool bAutoActivateOnlyIfNoWaves, AActor* ActivationInstigator, AController* ActivationController, bool bSkipRandomEliteResolution)
{
	if (!HasAuthority() || !EnemySet.EnemyClass)
	{
		return nullptr;
	}

	FEnemySet ResolvedEnemySet = (bApplyEliteSettings && !bSkipRandomEliteResolution)
		? ResolveEliteSpawnSet(EnemySet)
		: EnemySet;
	if (!ResolvedEnemySet.EnemyClass)
	{
		return nullptr;
	}

	APawn* SpawnedPawn = nullptr;
	const bool bUsePooling = PoolSettings.bEnablePooling;
	const FAeyerjiEnemyPoolKey PoolKey = MakePoolKey(ResolvedEnemySet);
	if (bUsePooling)
	{
		SpawnedPawn = AcquireInactivePooledEnemy(PoolKey);
	}

	if (SpawnedPawn)
	{
		FAeyerjiNavSafetyResolveParams NavParams;
		NavParams.ProjectionExtent = FVector(500.f, 500.f, 1000.f);
		NavParams.SearchRadius = 600.f;
		NavParams.GroundTraceHeight = 400.f;
		NavParams.GroundTraceDepth = 1000.f;

		FAeyerjiNavSafetyResult SpawnNavResult;
		if (!UAeyerjiNavSafetyLibrary::ResolveSafeNavLocationForPawn(this, SpawnTransform.GetLocation(), SpawnedPawn, NavParams, SpawnNavResult))
		{
			AJ_LOG(this, TEXT("SpawnRegisteredEnemyFromSet rejected pooled off-nav spawn: Pawn=%s Class=%s Location=%s Reason=%s"),
				*GetNameSafe(SpawnedPawn),
				*GetNameSafe(ResolvedEnemySet.EnemyClass),
				*SpawnTransform.GetLocation().ToCompactString(),
				*SpawnNavResult.FailureReason.ToString());
			SetPooledEnemyInactiveState(SpawnedPawn, GetPoolParkingLocation());
			if (FPooledEnemyRuntimeState* State = PooledEnemyStates.Find(TWeakObjectPtr<APawn>(SpawnedPawn)))
			{
				State->State = EAeyerjiPooledEnemyState::Inactive;
			}
			InactiveEnemyPools.FindOrAdd(PoolKey).Add(SpawnedPawn);
			return nullptr;
		}

		FTransform SafeTransform = SpawnTransform;
		SafeTransform.SetLocation(SpawnNavResult.GroundedLocation);
		SpawnedPawn->SetOwner(SpawnOwner ? SpawnOwner : this);
		SpawnedPawn->SetInstigator(InstigatorPawn);
		RestorePooledEnemyForCheckout(SpawnedPawn, ResolvedEnemySet, SafeTransform);
	}
	else
	{
		SpawnedPawn = SpawnRawEnemyActor(ResolvedEnemySet, SpawnTransform, SpawnOwner ? SpawnOwner : this, InstigatorPawn, /*bValidateNav=*/true);
		if (!SpawnedPawn)
		{
			return nullptr;
		}

		if (bUsePooling)
		{
			CapturePooledEnemyBaseline(SpawnedPawn, ResolvedEnemySet);
			SetPooledEnemyActiveState(SpawnedPawn, SpawnedPawn->GetActorTransform());
			if (AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(SpawnedPawn))
			{
				Enemy->SetOwningSpawnerPool(this, true);
				Enemy->PrepareForPooledActivation();
			}
		}
	}

	RegisterExternalEnemy(
		SpawnedPawn,
		ResolvedEnemySet,
		bApplyEliteSettings,
		bApplyAggro,
		bAutoActivate,
		bAutoActivateOnlyIfNoWaves,
		ActivationInstigator,
		ActivationController,
		/*bSkipRandomEliteResolution=*/true);

	return SpawnedPawn;
}

bool AAeyerjiSpawnerGroup::ReturnEnemyToPool(APawn* EnemyPawn)
{
	if (!HasAuthority() || !PoolSettings.bEnablePooling || !IsValid(EnemyPawn))
	{
		return false;
	}

	FPooledEnemyRuntimeState* State = PooledEnemyStates.Find(TWeakObjectPtr<APawn>(EnemyPawn));
	if (!State || !State->bBaselineCaptured)
	{
		return false;
	}

	UnbindTrackedEnemy(EnemyPawn);
	TrackedLiveEnemies.Remove(TWeakObjectPtr<AActor>(EnemyPawn));
	TrackedBossEnemies.Remove(TWeakObjectPtr<AActor>(EnemyPawn));
	TrackedEnemyScalingStates.Remove(TWeakObjectPtr<AActor>(EnemyPawn));
	LiveEnemies = TrackedLiveEnemies.Num();

	const int32 MaxInactive = FMath::Max(0, PoolSettings.MaxInactivePerPoolKey);
	TArray<TWeakObjectPtr<APawn>>& Bucket = InactiveEnemyPools.FindOrAdd(State->PoolKey);
	for (int32 Index = Bucket.Num() - 1; Index >= 0; --Index)
	{
		if (!Bucket[Index].IsValid())
		{
			Bucket.RemoveAtSwap(Index);
		}
	}

	CleanupSpawnerAppliedRuntimeState(EnemyPawn);
	if (AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(EnemyPawn))
	{
		Enemy->PrepareForPooledDeactivation();
		Enemy->SetOwningSpawnerPool(this, true);
	}

	if (Bucket.Num() >= MaxInactive)
	{
		if (AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(EnemyPawn))
		{
			Enemy->SetOwningSpawnerPool(nullptr, false);
		}
		PooledEnemyStates.Remove(TWeakObjectPtr<APawn>(EnemyPawn));
		EnemyPawn->Destroy();
		return true;
	}

	State->State = EAeyerjiPooledEnemyState::Inactive;
	const FVector ParkingLocation = GetPoolParkingLocation();
	SetPooledEnemyInactiveState(EnemyPawn, ParkingLocation);
	MulticastSetPooledEnemyInactive(EnemyPawn, ParkingLocation);
	Bucket.AddUnique(TWeakObjectPtr<APawn>(EnemyPawn));
	return true;
}

void AAeyerjiSpawnerGroup::ReleaseEnemyPool(bool bDestroyInactiveEnemies)
{
	if (!HasAuthority())
	{
		return;
	}

	for (TPair<FAeyerjiEnemyPoolKey, TArray<TWeakObjectPtr<APawn>>>& Pair : InactiveEnemyPools)
	{
		for (TWeakObjectPtr<APawn>& PawnPtr : Pair.Value)
		{
			if (APawn* PooledPawn = PawnPtr.Get())
			{
				if (AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(PooledPawn))
				{
					Enemy->SetOwningSpawnerPool(nullptr, false);
				}

				if (bDestroyInactiveEnemies)
				{
					PooledPawn->Destroy();
				}
			}
		}
	}

	InactiveEnemyPools.Reset();
	if (bDestroyInactiveEnemies)
	{
		for (auto It = PooledEnemyStates.CreateIterator(); It; ++It)
		{
			if (APawn* Pawn = It.Key().Get())
			{
				if (AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(Pawn))
				{
					Enemy->SetOwningSpawnerPool(nullptr, false);
				}
			}
		}
		PooledEnemyStates.Reset();
	}
}

int32 AAeyerjiSpawnerGroup::GetInactivePooledEnemyCount() const
{
	int32 Count = 0;
	for (const TPair<FAeyerjiEnemyPoolKey, TArray<TWeakObjectPtr<APawn>>>& Pair : InactiveEnemyPools)
	{
		for (const TWeakObjectPtr<APawn>& PawnPtr : Pair.Value)
		{
			if (PawnPtr.IsValid())
			{
				++Count;
			}
		}
	}
	return Count;
}

void AAeyerjiSpawnerGroup::PrewarmPoolForEnemySets(const TArray<FEnemySet>& EnemySets, int32 DesiredCountPerSet)
{
	if (!HasAuthority() || !PoolSettings.bEnablePooling || DesiredCountPerSet <= 0)
	{
		return;
	}

	int32 RemainingBudget = FMath::Max(1, PoolSettings.PrewarmPerTick);
	const FVector ParkingLocation = GetPoolParkingLocation();
	for (const FEnemySet& EnemySet : EnemySets)
	{
		if (RemainingBudget <= 0)
		{
			break;
		}

		FEnemySet ResolvedEnemySet = ResolveEliteSpawnSet(EnemySet);
		if (!ResolvedEnemySet.EnemyClass)
		{
			continue;
		}

		const FAeyerjiEnemyPoolKey PoolKey = MakePoolKey(ResolvedEnemySet);
		TArray<TWeakObjectPtr<APawn>>& Bucket = InactiveEnemyPools.FindOrAdd(PoolKey);
		for (int32 Index = Bucket.Num() - 1; Index >= 0; --Index)
		{
			if (!Bucket[Index].IsValid())
			{
				Bucket.RemoveAtSwap(Index);
			}
		}

		const int32 DesiredTotal = FMath::Min(FMath::Max(0, PoolSettings.MaxInactivePerPoolKey), DesiredCountPerSet);
		while (Bucket.Num() < DesiredTotal && RemainingBudget > 0)
		{
			FTransform ParkingTransform(GetActorRotation(), ParkingLocation, FVector::OneVector);
			APawn* PooledPawn = SpawnRawEnemyActor(ResolvedEnemySet, ParkingTransform, this, nullptr, /*bValidateNav=*/false);
			if (!PooledPawn)
			{
				break;
			}

			CapturePooledEnemyBaseline(PooledPawn, ResolvedEnemySet);
			if (AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(PooledPawn))
			{
				Enemy->SetOwningSpawnerPool(this, true);
				Enemy->PrepareForPooledDeactivation();
			}

			if (FPooledEnemyRuntimeState* State = PooledEnemyStates.Find(TWeakObjectPtr<APawn>(PooledPawn)))
			{
				State->State = EAeyerjiPooledEnemyState::Inactive;
			}
			SetPooledEnemyInactiveState(PooledPawn, ParkingLocation);
			MulticastSetPooledEnemyInactive(PooledPawn, ParkingLocation);
			Bucket.Add(TWeakObjectPtr<APawn>(PooledPawn));
			--RemainingBudget;
		}
	}
}

void AAeyerjiSpawnerGroup::SetDoorArrayEnabled(const TArray<TObjectPtr<AActor>>& Targets, bool bEnabled)
{
	if (bSuppressDoorControl)
	{
		return;
	}

	for (AActor* Target : Targets)
	{
		if (!IsValid(Target))
		{
			continue;
		}

		Target->SetActorHiddenInGame(!bEnabled);
		Target->SetActorEnableCollision(bEnabled);
	}
}

void AAeyerjiSpawnerGroup::OnEnemyDestroyed(AActor* DestroyedEnemy)
{
	if (!HasAuthority())
	{
		return;
	}

	HandleTrackedEnemyRemoved(DestroyedEnemy);
}

void AAeyerjiSpawnerGroup::OnEnemyDied(AActor* DeadEnemy)
{
	if (!HasAuthority())
	{
		return;
	}

	HandleTrackedEnemyRemoved(DeadEnemy);
}

void AAeyerjiSpawnerGroup::HandleTrackedEnemyRemoved(AActor* EnemyActor)
{
	if (!HasAuthority())
	{
		return;
	}

	if (IsValid(EnemyActor))
	{
		UnbindTrackedEnemy(EnemyActor);
	}

	int32 RemovedCount = 0;
	for (auto It = TrackedLiveEnemies.CreateIterator(); It; ++It)
	{
		const TWeakObjectPtr<AActor>& TrackedEnemy = *It;
		if (!TrackedEnemy.IsValid() || TrackedEnemy.Get() == EnemyActor)
		{
			RemovedCount++;
			It.RemoveCurrent();
		}
	}

	if (RemovedCount <= 0)
	{
		return;
	}

	if (FPooledEnemyRuntimeState* PoolState = PooledEnemyStates.Find(TWeakObjectPtr<APawn>(Cast<APawn>(EnemyActor))))
	{
		PoolState->State = EAeyerjiPooledEnemyState::PendingReturn;
	}

	TrackedEnemyScalingStates.Remove(TWeakObjectPtr<AActor>(EnemyActor));
	const bool bRemovedBossEnemy = TrackedBossEnemies.Remove(TWeakObjectPtr<AActor>(EnemyActor)) > 0;
	LiveEnemies = TrackedLiveEnemies.Num();
	OnTrackedEnemiesRemoved.Broadcast(this, RemovedCount);
	if (bRemovedBossEnemy)
	{
		OnBossDefeated.Broadcast(this, EnemyActor);
	}
	CheckWaveCompletion();
}

void AAeyerjiSpawnerGroup::UnbindTrackedEnemy(AActor* EnemyActor)
{
	if (!IsValid(EnemyActor))
	{
		return;
	}

	EnemyActor->OnDestroyed.RemoveDynamic(this, &AAeyerjiSpawnerGroup::OnEnemyDestroyed);
	if (AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(EnemyActor))
	{
		Enemy->OnEnemyDied.RemoveDynamic(this, &AAeyerjiSpawnerGroup::OnEnemyDied);
	}
}

void AAeyerjiSpawnerGroup::ResetTrackedEnemies()
{
	for (const TWeakObjectPtr<AActor>& TrackedEnemy : TrackedLiveEnemies)
	{
		if (AActor* EnemyActor = TrackedEnemy.Get())
		{
			UnbindTrackedEnemy(EnemyActor);
		}
	}

	TrackedLiveEnemies.Reset();
	TrackedBossEnemies.Reset();
	TrackedEnemyScalingStates.Reset();
	LiveEnemies = 0;
}

float AAeyerjiSpawnerGroup::ResolvePreservedResourceValue(const float OldCurrent, const float OldMax, const float NewMax, const bool bPreserveRatio)
{
	if (!bPreserveRatio || OldMax <= 0.f)
	{
		return NewMax;
	}

	const float Ratio = FMath::Clamp(OldCurrent / OldMax, 0.f, 1.f);
	return NewMax * Ratio;
}

void AAeyerjiSpawnerGroup::CaptureTrackedBaseValueIfNeeded(const TWeakObjectPtr<AActor>& EnemyKey, UAbilitySystemComponent* ASC, const FGameplayAttribute& Attribute, const FName& AttributeName)
{
	if (!EnemyKey.IsValid() || !ASC || !Attribute.IsValid() || AttributeName.IsNone())
	{
		return;
	}

	if (FTrackedEnemyScalingState* ScalingState = TrackedEnemyScalingStates.Find(EnemyKey))
	{
		ScalingState->BaseAttributeValues.FindOrAdd(AttributeName, ASC->GetNumericAttribute(Attribute));
	}

	if (APawn* EnemyPawn = Cast<APawn>(EnemyKey.Get()))
	{
		if (FPooledEnemyRuntimeState* PoolState = PooledEnemyStates.Find(TWeakObjectPtr<APawn>(EnemyPawn)))
		{
			PoolState->BaselineAttributeValues.FindOrAdd(AttributeName, ASC->GetNumericAttribute(Attribute));
		}
	}
}

void AAeyerjiSpawnerGroup::RefreshTrackedEnemyScaling(TSet<TWeakObjectPtr<AActor>>& OutHandledEnemies)
{
	if (!HasAuthority())
	{
		return;
	}

	for (auto It = TrackedLiveEnemies.CreateIterator(); It; ++It)
	{
		const TWeakObjectPtr<AActor>& EnemyKey = *It;
		APawn* EnemyPawn = Cast<APawn>(EnemyKey.Get());
		if (!IsValid(EnemyPawn))
		{
			TrackedEnemyScalingStates.Remove(EnemyKey);
			It.RemoveCurrent();
			continue;
		}

		FTrackedEnemyScalingState* ScalingState = TrackedEnemyScalingStates.Find(EnemyKey);
		if (!ScalingState)
		{
			continue;
		}

		ApplyEnemyScaling(EnemyPawn, ScalingState->ResolvedTemplate);

		if (ScalingState->bHasEliteAutoScaling)
		{
			if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(EnemyPawn, /*LookForComponent*/ true))
			{
				CaptureTrackedBaseValueIfNeeded(EnemyKey, ASC, UAeyerjiAttributeSet::GetAttackRangeAttribute(), TEXT("AeyerjiAttributeSet.AttackRange"));

				if (const float* BaseRange = ScalingState->BaseAttributeValues.Find(TEXT("AeyerjiAttributeSet.AttackRange")))
				{
					ASC->SetNumericAttributeBase(UAeyerjiAttributeSet::GetAttackRangeAttribute(), *BaseRange);
				}
			}

			ApplyEliteStats(
				EnemyPawn,
				ScalingState->EliteHealthMultiplier,
				ScalingState->EliteDamageMultiplier,
				ScalingState->EliteRangeMultiplier,
				/*bPreserveHealthRatio=*/true);
		}

		RefreshSpawnedPawnStatusBar(EnemyPawn);
		OutHandledEnemies.Add(EnemyKey);
	}

	LiveEnemies = TrackedLiveEnemies.Num();
}

void AAeyerjiSpawnerGroup::RegisterProgressEnemy(APawn* SpawnedPawn, const int32 ProgressPoints)
{
	if (!HasAuthority())
	{
		return;
	}

	if (!IsValid(SpawnedPawn))
	{
		return;
	}

	AAeyerjiEncounterDirector* EncounterDirector = nullptr;
	if (LevelDirector)
	{
		EncounterDirector = LevelDirector->GetEncounterDirector();
	}

	if (!EncounterDirector)
	{
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<AAeyerjiEncounterDirector> It(World); It; ++It)
			{
				EncounterDirector = *It;
				break;
			}
		}
	}

	if (!EncounterDirector || (!EncounterDirector->IsFixedWorldPopulationActive() && !EncounterDirector->IsWeightedProgressActive()))
	{
		return;
	}

	if (AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(SpawnedPawn))
	{
		int32 RunSerial = 0;
		if (const AAeyerjiGameState* GameState = GetWorld() ? GetWorld()->GetGameState<AAeyerjiGameState>() : nullptr)
		{
			RunSerial = GameState->GetRiftRunState().RunSerial;
		}
		EncounterDirector->RegisterProgressEnemy(Enemy, ProgressPoints, RunSerial);
	}
}

void AAeyerjiSpawnerGroup::MulticastApplyElitePresentation_Implementation(APawn* SpawnedPawn, float ScaleMultiplier, const TArray<FGameplayTag>& AffixTags, FVector BaseScale)
{
	if (!IsValid(SpawnedPawn))
	{
		UE_LOG(LogTemp, Warning, TEXT("MulticastApplyElitePresentation skipped: invalid pawn"));
		return;
	}

	const ENetMode NetMode = GetNetMode();
	const auto* NetModeLabel = [NetMode]() -> const TCHAR*
	{
		switch (NetMode)
		{
		case NM_Client: return TEXT("Client");
		case NM_ListenServer: return TEXT("ListenServer");
		case NM_DedicatedServer: return TEXT("DedicatedServer");
		default: return TEXT("Standalone");
		}
	}();

	const float SafeScale = ScaleMultiplier > 0.f ? ScaleMultiplier : 1.0f;
	if (!FMath::IsNearlyEqual(SafeScale, 1.0f))
	{
		if (BaseScale.IsNearlyZero())
		{
			BaseScale = SpawnedPawn->GetActorScale3D();
		}
		const FVector NewScale = BaseScale * SafeScale;
		SpawnedPawn->SetActorScale3D(NewScale);
		UE_LOG(LogTemp, Log, TEXT("Applied elite scale %.2f to %s (NetMode=%s NewScale=%s)"),
			SafeScale,
			*GetNameSafe(SpawnedPawn),
			NetModeLabel,
			*NewScale.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("Elite scale unchanged for %s (ScaleMultiplier=%.2f NetMode=%s)"),
			*GetNameSafe(SpawnedPawn),
			SafeScale,
			NetModeLabel);
	}

	// Dedicated servers cannot render Niagara; only proceed there if replication is explicitly requested.
	if (NetMode == NM_DedicatedServer && !bReplicateEliteVFX)
	{
		UE_LOG(LogTemp, Log, TEXT("Skipping elite VFX on dedicated server for %s (bReplicateEliteVFX=%d)"),
			*GetNameSafe(SpawnedPawn),
			bReplicateEliteVFX ? 1 : 0);
		return;
	}

	// Honor the "Replicate" toggle: skip client-side cosmetic work if designer has disabled it.
	if (!bReplicateEliteVFX && NetMode == NM_Client)
	{
		UE_LOG(LogTemp, Warning, TEXT("Skipping elite VFX on client for %s because bReplicateEliteVFX is false"),
			*GetNameSafe(SpawnedPawn));
		return;
	}

	TArray<const FEliteAffixDefinition*> Affixes;
	for (const FGameplayTag& Tag : AffixTags)
	{
		if (const FEliteAffixDefinition* Def = FindAffixDefinition(Tag))
		{
			Affixes.Add(Def);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("Elite affix tag %s missing definition on %s; affix VFX may be missing"),
				*Tag.ToString(),
				*GetNameSafe(this));
		}
	}

	ApplyElitePresentation(SpawnedPawn, 1.0f, Affixes, /*bApplyScale=*/false);
}

void AAeyerjiSpawnerGroup::ApplyElitePresentation(APawn* SpawnedPawn, float ScaleMultiplier, const TArray<const FEliteAffixDefinition*>& Affixes, bool bApplyScale)
{
	if (!IsValid(SpawnedPawn))
	{
		return;
	}

	const float SafeScale = ScaleMultiplier > 0.f ? ScaleMultiplier : 1.0f;
	if (bApplyScale && !FMath::IsNearlyEqual(SafeScale, 1.0f))
	{
		const FVector NewScale = ResolvePooledOriginalScale(SpawnedPawn) * SafeScale;
		SpawnedPawn->SetActorScale3D(NewScale);
	}

	if (!EliteVFXSystem)
	{
		UE_LOG(LogTemp, Warning, TEXT("Elite VFX system not set; skipping FX for %s"), *GetNameSafe(SpawnedPawn));
		return;
	}

	// Skip cosmetic FX on dedicated servers unless replication is requested.
	if (SpawnedPawn->GetNetMode() == NM_DedicatedServer && !bReplicateEliteVFX)
	{
		return;
	}

	UWorld* World = SpawnedPawn->GetWorld();
	if (!World || !World->IsGameWorld())
	{
		UE_LOG(LogTemp, Warning, TEXT("Elite VFX skipped because world was invalid for %s"), *GetNameSafe(SpawnedPawn));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("Applying elite VFX for %s (NetMode=%d System=%s AttachSocket=%s Offset=%s ApplyScale=%d)"),
		*GetNameSafe(SpawnedPawn),
		static_cast<int32>(SpawnedPawn->GetNetMode()),
		*GetNameSafe(EliteVFXSystem),
		*EliteVFXSocket.ToString(),
		*EliteVFXOffset.ToString(),
		bApplyScale ? 1 : 0);

	USceneComponent* AttachParent = nullptr;
	if (ACharacter* CharacterOwner = Cast<ACharacter>(SpawnedPawn))
	{
		AttachParent = CharacterOwner->GetMesh();
	}

	if (!AttachParent)
	{
		AttachParent = SpawnedPawn->GetRootComponent();
	}

	// If we somehow have no valid, registered parent, bail out entirely to avoid crashing during component registration.
	if (!AttachParent || !AttachParent->IsRegistered() || !AttachParent->GetOwner())
	{
		UE_LOG(LogTemp, Warning, TEXT("Elite VFX skipped: invalid attach parent for %s (Parent=%s Registered=%d Owner=%s)"),
			*GetNameSafe(SpawnedPawn),
			*GetNameSafe(AttachParent),
			AttachParent ? (AttachParent->IsRegistered() ? 1 : 0) : 0,
			*GetNameSafe(AttachParent ? AttachParent->GetOwner() : nullptr));
		return;
	}

	UNiagaraComponent* NiagaraComp = nullptr;
	NiagaraComp = UNiagaraFunctionLibrary::SpawnSystemAttached(
		EliteVFXSystem,
		AttachParent,
		EliteVFXSocket,
		EliteVFXOffset,
		FRotator::ZeroRotator,
		EAttachLocation::KeepRelativeOffset,
		true);

	// As a last resort, spawn unattached in world space with the same offset.
	if (!NiagaraComp && World)
	{
		const FTransform OwnerTransform = SpawnedPawn->GetActorTransform();
		const FVector WorldOffset = OwnerTransform.TransformVector(EliteVFXOffset);
		NiagaraComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
			World,
			EliteVFXSystem,
			OwnerTransform.GetLocation() + WorldOffset,
			OwnerTransform.Rotator());
	}

	if (!NiagaraComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("Elite VFX failed to spawn for %s using system %s (AttachParent=%s WorldValid=%d NetMode=%d ApplyScale=%d)"),
			*GetNameSafe(SpawnedPawn),
			*GetNameSafe(EliteVFXSystem),
			*GetNameSafe(AttachParent),
			World ? 1 : 0,
			static_cast<int32>(SpawnedPawn->GetNetMode()),
			bApplyScale ? 1 : 0);
		return;
	}

	NiagaraComp->SetAutoDestroy(true);
	NiagaraComp->SetUsingAbsoluteScale(false);

	if (bReplicateEliteVFX && SpawnedPawn->GetLocalRole() == ROLE_Authority)
	{
		NiagaraComp->SetIsReplicated(true);
	}
	TrackPooledNiagara(SpawnedPawn, NiagaraComp);

	UE_LOG(LogTemp, Log, TEXT("Elite VFX spawned on %s (Attached=%d Socket=%s Offset=%s)"),
		*GetNameSafe(SpawnedPawn),
		NiagaraComp->GetAttachParent() ? 1 : 0,
		*EliteVFXSocket.ToString(),
		*EliteVFXOffset.ToString());

	for (const FEliteAffixDefinition* Affix : Affixes)
	{
		if (Affix && Affix->VFXSystem)
		{
			ApplyAffixVFX(SpawnedPawn, *Affix);
		}
	}
}

FEnemySet AAeyerjiSpawnerGroup::ResolveEliteSpawnSet(const FEnemySet& EnemySet) const
{
	FEnemySet ResolvedSet = EnemySet;

	// Mini bosses and bosses should always use their authored class path.
	if (ResolvedSet.bIsMiniBoss || ResolvedSet.bIsBoss)
	{
		return ResolvedSet;
	}

	TArray<TSubclassOf<APawn>> CandidateEliteClasses;
	const TArray<TSubclassOf<APawn>>& PreferredPool =
		ResolvedSet.EliteEnemyClassPoolOverride.Num() > 0 ? ResolvedSet.EliteEnemyClassPoolOverride : EliteEnemyClassPool;
	for (TSubclassOf<APawn> CandidateClass : PreferredPool)
	{
		if (*CandidateClass)
		{
			CandidateEliteClasses.Add(CandidateClass);
		}
	}

	auto ResolveEliteClass = [&CandidateEliteClasses]() -> TSubclassOf<APawn>
	{
		if (CandidateEliteClasses.Num() <= 0)
		{
			return nullptr;
		}

		const int32 Index = FMath::RandHelper(CandidateEliteClasses.Num());
		return CandidateEliteClasses[Index];
	};

	const bool bAlreadyElite = ResolvedSet.bIsElite;
	if (!bAlreadyElite)
	{
		if (!bAllowRandomElites)
		{
			return ResolvedSet;
		}

		const float Chance = FMath::Clamp(RandomEliteChance, 0.f, 1.f);
		if (Chance <= 0.f)
		{
			return ResolvedSet;
		}

		if (bRequireEliteClassPoolForRandomPromotion && CandidateEliteClasses.IsEmpty())
		{
			return ResolvedSet;
		}

		if (!(Chance >= 1.f || FMath::FRand() <= Chance))
		{
			return ResolvedSet;
		}

		ResolvedSet.bIsElite = true;
	}

	if (ResolvedSet.bIsElite)
	{
		TSubclassOf<APawn> EliteClass = ResolveEliteClass();
		if (*EliteClass)
		{
			ResolvedSet.EnemyClass = EliteClass;
		}
	}

	return ResolvedSet;
}

void AAeyerjiSpawnerGroup::ApplyEliteStats(APawn* SpawnedPawn, float HealthMultiplier, float DamageMultiplier, float RangeMultiplier, bool bPreserveHealthRatio)
{
	if (!HasAuthority() || !IsValid(SpawnedPawn))
	{
		return;
	}

	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(SpawnedPawn);
	if (!ASC)
	{
		return;
	}

	const FGameplayAttribute HPAttr = UAeyerjiAttributeSet::GetHPAttribute();
	const FGameplayAttribute HPMaxAttr = UAeyerjiAttributeSet::GetHPMaxAttribute();
	const FGameplayAttribute DamageAttr = UAeyerjiAttributeSet::GetAttackDamageAttribute();
	const FGameplayAttribute RangeAttr = UAeyerjiAttributeSet::GetAttackRangeAttribute();
	const float OldHP = HPAttr.IsValid() ? ASC->GetNumericAttribute(HPAttr) : 0.f;
	const float OldHPMax = HPMaxAttr.IsValid() ? ASC->GetNumericAttribute(HPMaxAttr) : 0.f;

	const float SafeHealth = FMath::Max(KINDA_SMALL_NUMBER, HealthMultiplier);
	const float SafeDamage = FMath::Max(KINDA_SMALL_NUMBER, DamageMultiplier);
	const float SafeRange = FMath::Max(KINDA_SMALL_NUMBER, RangeMultiplier);

	const auto MultiplyAttribute = [ASC](const FGameplayAttribute& Attr, float Multiplier)
	{
		if (!Attr.IsValid() || Multiplier <= 0.f)
		{
			return;
		}

		const float Current = ASC->GetNumericAttribute(Attr);
		ASC->SetNumericAttributeBase(Attr, Current * Multiplier);
	};

	// Static bumps for elites.
	MultiplyAttribute(DamageAttr, SafeDamage);
	MultiplyAttribute(RangeAttr, SafeRange);

	if (HPMaxAttr.IsValid())
	{
		const float OldMax = ASC->GetNumericAttribute(HPMaxAttr);
		const float NewMax = OldMax * SafeHealth;
		ASC->SetNumericAttributeBase(HPMaxAttr, NewMax);

		if (HPAttr.IsValid())
		{
			ASC->SetNumericAttributeBase(HPAttr, ResolvePreservedResourceValue(OldHP, OldHPMax, NewMax, bPreserveHealthRatio));
		}
	}
}

TArray<const FEliteAffixDefinition*> AAeyerjiSpawnerGroup::BuildEliteAffixLoadout(const FEnemySet& EnemySet) const
{
	TArray<const FEliteAffixDefinition*> Result;

	if (!EnemySet.bIsElite)
	{
		return Result;
	}

	TSet<FGameplayTag> UsedTags;
	for (const FGameplayTag& ForcedTag : EnemySet.ForcedEliteAffixes)
	{
		if (!ForcedTag.IsValid() || UsedTags.Contains(ForcedTag))
		{
			continue;
		}

		if (const FEliteAffixDefinition* Def = FindAffixDefinition(ForcedTag))
		{
			Result.Add(Def);
			UsedTags.Add(ForcedTag);
		}
	}

	TArray<const FEliteAffixDefinition*> Candidates;

	if (EnemySet.EliteAffixPoolOverride.Num() > 0)
	{
		for (const FGameplayTag& Tag : EnemySet.EliteAffixPoolOverride)
		{
			if (!Tag.IsValid() || UsedTags.Contains(Tag))
			{
				continue;
			}

			if (const FEliteAffixDefinition* Def = FindAffixDefinition(Tag))
			{
				Candidates.Add(Def);
			}
		}
	}
	else
	{
		for (const FEliteAffixDefinition& Def : EliteAffixPool)
		{
			if (Def.AffixTag.IsValid() && !UsedTags.Contains(Def.AffixTag))
			{
				Candidates.Add(&Def);
			}
		}
	}

	if (Candidates.Num() == 0)
	{
		return Result;
	}

	const int32 MinRolls = EnemySet.MinEliteAffixes > 0 ? EnemySet.MinEliteAffixes : DefaultEliteAffixMin;
	const int32 MaxRolls = EnemySet.MaxEliteAffixes > 0 ? EnemySet.MaxEliteAffixes : DefaultEliteAffixMax;

	const int32 ClampedMin = FMath::Max(0, MinRolls);
	const int32 ClampedMax = FMath::Max(ClampedMin, MaxRolls);

	TArray<const FEliteAffixDefinition*> Shuffled = Candidates;
	Algo::RandomShuffle(Shuffled);

	const int32 Rolls = FMath::Clamp(FMath::RandRange(ClampedMin, ClampedMax), 0, Shuffled.Num());
	for (int32 Index = 0; Index < Rolls; ++Index)
	{
		const FEliteAffixDefinition* Def = Shuffled[Index];
		if (!Def)
		{
			continue;
		}

		if (!UsedTags.Contains(Def->AffixTag))
		{
			Result.Add(Def);
			UsedTags.Add(Def->AffixTag);
		}
	}

	return Result;
}

const FEliteAffixDefinition* AAeyerjiSpawnerGroup::FindAffixDefinition(const FGameplayTag& Tag) const
{
	if (!Tag.IsValid())
	{
		return nullptr;
	}

	for (const FEliteAffixDefinition& Def : EliteAffixPool)
	{
		if (Def.AffixTag == Tag)
		{
			return &Def;
		}
	}

	return nullptr;
}

float AAeyerjiSpawnerGroup::ComputeEliteScale(const FEnemySet& EnemySet, const TArray<const FEliteAffixDefinition*>& Affixes) const
{
	float Scale = EnemySet.EliteScaleMultiplierOverride > 0.f ? EnemySet.EliteScaleMultiplierOverride : EliteScaleMultiplier;
	const bool bApplyMiniBossBonuses = EnemySet.bIsMiniBoss && !EnemySet.bIsBoss;

	if (bApplyMiniBossBonuses)
	{
		Scale *= MiniBossScaleMultiplier;
	}

	if (EnemySet.bIsBoss)
	{
		Scale *= BossScaleMultiplier;
	}

	for (const FEliteAffixDefinition* Affix : Affixes)
	{
		if (Affix)
		{
			Scale *= FMath::Max(KINDA_SMALL_NUMBER, Affix->ScaleMultiplier);
		}
	}

	return Scale;
}

void AAeyerjiSpawnerGroup::ApplyEliteGameplay(APawn* SpawnedPawn, const FEnemySet& EnemySet, const TArray<const FEliteAffixDefinition*>& Affixes, bool bApplyXPMultipliers)
{
	if (!IsValid(SpawnedPawn))
	{
		return;
	}

	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(SpawnedPawn);
	const bool bApplyMiniBossBonuses = EnemySet.bIsMiniBoss && !EnemySet.bIsBoss;

	if (ASC)
	{
		if (EliteGameplayTag.IsValid())
		{
			ASC->AddLooseGameplayTag(EliteGameplayTag);
			TrackPooledLooseTag(SpawnedPawn, EliteGameplayTag);
		}

		if (bApplyMiniBossBonuses && MiniBossGameplayTag.IsValid())
		{
			ASC->AddLooseGameplayTag(MiniBossGameplayTag);
			TrackPooledLooseTag(SpawnedPawn, MiniBossGameplayTag);
		}

		if (EnemySet.bIsBoss && BossGameplayTag.IsValid())
		{
			ASC->AddLooseGameplayTag(BossGameplayTag);
			TrackPooledLooseTag(SpawnedPawn, BossGameplayTag);
		}
	}

	for (const FEliteAffixDefinition* Affix : Affixes)
	{
		if (!Affix)
		{
			continue;
		}

		if (ASC && Affix->AffixTag.IsValid())
		{
			ASC->AddLooseGameplayTag(Affix->AffixTag);
			TrackPooledLooseTag(SpawnedPawn, Affix->AffixTag);
		}

		if (ASC && Affix->GameplayEffect)
		{
			const UGameplayEffect* EffectCDO = Affix->GameplayEffect->GetDefaultObject<UGameplayEffect>();
			if (EffectCDO)
			{
				const FActiveGameplayEffectHandle EffectHandle =
					ASC->ApplyGameplayEffectToSelf(EffectCDO, 1.f, ASC->MakeEffectContext());
				TrackPooledEffect(SpawnedPawn, EffectHandle);
			}
		}

		if (ASC)
		{
			for (TSubclassOf<UGameplayAbility> AbilityClass : Affix->GrantedAbilities)
			{
				if (*AbilityClass)
				{
					const FGameplayAbilitySpecHandle AbilityHandle = ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass));
					TrackPooledAbility(SpawnedPawn, AbilityHandle);
				}
			}
		}
	}

	// Mini-boss signature abilities (set override or default list).
	if (ASC && bApplyMiniBossBonuses)
	{
		const TArray<TSubclassOf<UGameplayAbility>>& MiniBossAbilities =
			(EnemySet.MiniBossGrantedAbilities.Num() > 0) ? EnemySet.MiniBossGrantedAbilities : DefaultMiniBossAbilities;

		for (TSubclassOf<UGameplayAbility> AbilityClass : MiniBossAbilities)
		{
			if (!*AbilityClass)
			{
				continue;
			}

			if (ASC->FindAbilitySpecFromClass(AbilityClass))
			{
				continue; // already granted (e.g., from base enemy)
			}

			const FGameplayAbilitySpecHandle AbilityHandle = ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass));
			TrackPooledAbility(SpawnedPawn, AbilityHandle);
			UE_LOG(LogTemp, Log, TEXT("Mini boss %s granted ability %s"),
				*GetNameSafe(SpawnedPawn),
				*AbilityClass->GetName());
		}
	}

	// Boss signature abilities.
	if (ASC && EnemySet.bIsBoss)
	{
		const TArray<TSubclassOf<UGameplayAbility>> BossAbilities =
			(EnemySet.BossGrantedAbilities.Num() > 0) ? EnemySet.BossGrantedAbilities : DefaultBossAbilities;

		for (TSubclassOf<UGameplayAbility> AbilityClass : BossAbilities)
		{
			if (!*AbilityClass)
			{
				continue;
			}

			if (ASC->FindAbilitySpecFromClass(AbilityClass))
			{
				continue;
			}

			const FGameplayAbilitySpecHandle AbilityHandle = ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass));
			TrackPooledAbility(SpawnedPawn, AbilityHandle);
			UE_LOG(LogTemp, Log, TEXT("Boss %s granted ability %s"),
				*GetNameSafe(SpawnedPawn),
				*AbilityClass->GetName());
		}
	}

	if (ASC && bApplyXPMultipliers)
	{
		const UAeyerjiRewardAttributeSet* RewardSet = ASC->GetSet<UAeyerjiRewardAttributeSet>();
		if (RewardSet)
		{
			const FGameplayAttribute XPAttr = UAeyerjiRewardAttributeSet::GetXPRewardBaseAttribute();
			const float CurrentXP = ASC->GetNumericAttribute(XPAttr);

			float XPMult = EnemySet.EliteXPMultiplierOverride > 0.f ? EnemySet.EliteXPMultiplierOverride : EliteXPMultiplier;
			if (bApplyMiniBossBonuses)
			{
				const float MiniMult = EnemySet.MiniBossXPMultiplierOverride > 0.f ? EnemySet.MiniBossXPMultiplierOverride : MiniBossXPMultiplier;
				XPMult *= MiniMult;
			}

			if (EnemySet.bIsBoss)
			{
				XPMult *= FMath::Max(0.f, BossXPMultiplier);
			}

			const float SafeMult = FMath::Max(0.f, XPMult);
			if (SafeMult > 0.f && !FMath::IsNearlyEqual(SafeMult, 1.f))
			{
				if (FPooledEnemyRuntimeState* PoolState = PooledEnemyStates.Find(TWeakObjectPtr<APawn>(SpawnedPawn)))
				{
					PoolState->BaselineAttributeValues.FindOrAdd(TEXT("AeyerjiRewardAttributeSet.XPRewardBase"), CurrentXP);
				}
				ASC->SetNumericAttributeBase(XPAttr, CurrentXP * SafeMult);
			}
		}
	}
}

void AAeyerjiSpawnerGroup::ApplyAffixVFX(APawn* SpawnedPawn, const FEliteAffixDefinition& Affix)
{
	if (!IsValid(SpawnedPawn) || !Affix.VFXSystem)
	{
		if (!Affix.VFXSystem)
		{
			UE_LOG(LogTemp, Warning, TEXT("Affix %s has no VFXSystem; skipping VFX for %s"),
				*Affix.AffixTag.ToString(),
				*GetNameSafe(SpawnedPawn));
		}
		return;
	}

	if (SpawnedPawn->GetNetMode() == NM_DedicatedServer && !bReplicateEliteVFX)
	{
		return;
	}

	USceneComponent* AttachParent = nullptr;
	if (ACharacter* CharacterOwner = Cast<ACharacter>(SpawnedPawn))
	{
		AttachParent = CharacterOwner->GetMesh();
	}

	if (!AttachParent)
	{
		AttachParent = SpawnedPawn->GetRootComponent();
	}

	if (!AttachParent || !AttachParent->IsRegistered())
	{
		UE_LOG(LogTemp, Warning, TEXT("Affix VFX skipped: invalid attach parent for %s (Affix=%s Parent=%s Registered=%d)"),
			*GetNameSafe(SpawnedPawn),
			*Affix.AffixTag.ToString(),
			*GetNameSafe(AttachParent),
			AttachParent ? (AttachParent->IsRegistered() ? 1 : 0) : 0);
		return;
	}

	UNiagaraComponent* NiagaraComp = UNiagaraFunctionLibrary::SpawnSystemAttached(
		Affix.VFXSystem,
		AttachParent,
		Affix.VFXSocket,
		Affix.VFXOffset,
		FRotator::ZeroRotator,
		EAttachLocation::KeepRelativeOffset,
		true);

	if (!NiagaraComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("Affix VFX failed for %s (Affix=%s AttachParent=%s)"),
			*GetNameSafe(SpawnedPawn),
			*Affix.AffixTag.ToString(),
			*GetNameSafe(AttachParent));
		if (UWorld* World = SpawnedPawn->GetWorld())
		{
			const FTransform OwnerTransform = SpawnedPawn->GetActorTransform();
			const FVector WorldOffset = OwnerTransform.TransformVector(Affix.VFXOffset);
			NiagaraComp = UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				World,
				Affix.VFXSystem,
				OwnerTransform.GetLocation() + WorldOffset,
				OwnerTransform.Rotator());
		}
	}

	if (!NiagaraComp)
	{
		UE_LOG(LogTemp, Warning, TEXT("Affix VFX failed at world spawn for %s (Affix=%s)"), *GetNameSafe(SpawnedPawn), *Affix.AffixTag.ToString());
		return;
	}

	NiagaraComp->SetAutoDestroy(true);
	NiagaraComp->SetUsingAbsoluteScale(false);

	if (bReplicateEliteVFX && SpawnedPawn->GetLocalRole() == ROLE_Authority)
	{
		NiagaraComp->SetIsReplicated(true);
	}
	TrackPooledNiagara(SpawnedPawn, NiagaraComp);
}

void AAeyerjiSpawnerGroup::ApplyElitePackage(APawn* SpawnedPawn, const FEnemySet& EnemySet)
{
	if (!HasAuthority() || !IsValid(SpawnedPawn))
	{
		return;
	}

	FEnemySet RuntimeSet = EnemySet;
	RuntimeSet.bIsElite = RuntimeSet.bIsElite || RuntimeSet.bIsMiniBoss || RuntimeSet.bIsBoss;
	const bool bApplyMiniBossBonuses = RuntimeSet.bIsMiniBoss && !RuntimeSet.bIsBoss;

	if (!RuntimeSet.bIsElite)
	{
		return;
	}

	if (!EliteActorTag.IsNone())
	{
		SpawnedPawn->Tags.AddUnique(EliteActorTag);
		TrackPooledActorTag(SpawnedPawn, EliteActorTag);
	}

	if (bApplyMiniBossBonuses && !MiniBossActorTag.IsNone())
	{
		SpawnedPawn->Tags.AddUnique(MiniBossActorTag);
		TrackPooledActorTag(SpawnedPawn, MiniBossActorTag);
	}

	if (RuntimeSet.bIsBoss && !BossActorTag.IsNone())
	{
		SpawnedPawn->Tags.AddUnique(BossActorTag);
		TrackPooledActorTag(SpawnedPawn, BossActorTag);
	}

	if (RuntimeSet.bSkipEliteAutoScaling)
	{
		// Keep authored elite stats, but still apply affix gameplay (tags/abilities/effects) and visuals.
		TArray<const FEliteAffixDefinition*> Affixes = BuildEliteAffixLoadout(RuntimeSet);
		ApplyEliteGameplay(SpawnedPawn, RuntimeSet, Affixes, /*bApplyXPMultipliers=*/true);

		TArray<FGameplayTag> AffixTags;
		for (const FEliteAffixDefinition* Affix : Affixes)
		{
			if (Affix && Affix->AffixTag.IsValid())
			{
				AffixTags.Add(Affix->AffixTag);
			}
		}

		// Preserve elite visual marking but do not change actor scale when authored stats are used.
		MulticastApplyElitePresentation(SpawnedPawn, 1.f, AffixTags, ResolvePooledOriginalScale(SpawnedPawn));
		AJ_LOG(this, TEXT("ApplyElitePackage bypassed auto stat scaling only: Pawn=%s Elite=%d SkipEliteAutoScaling=%d AffixCount=%d"),
			*GetNameSafe(SpawnedPawn),
			RuntimeSet.bIsElite ? 1 : 0,
			RuntimeSet.bSkipEliteAutoScaling ? 1 : 0,
			AffixTags.Num());
		RefreshSpawnedPawnStatusBar(SpawnedPawn);
		return;
	}

	TArray<const FEliteAffixDefinition*> Affixes = BuildEliteAffixLoadout(RuntimeSet);

	float HealthMult = RuntimeSet.EliteHealthMultiplierOverride > 0.f ? RuntimeSet.EliteHealthMultiplierOverride : EliteHealthMultiplier;
	float DamageMult = RuntimeSet.EliteDamageMultiplierOverride > 0.f ? RuntimeSet.EliteDamageMultiplierOverride : EliteDamageMultiplier;
	float RangeMult = RuntimeSet.EliteRangeMultiplierOverride > 0.f ? RuntimeSet.EliteRangeMultiplierOverride : EliteRangeMultiplier;

	if (bApplyMiniBossBonuses)
	{
		HealthMult *= MiniBossHealthMultiplier;
		DamageMult *= MiniBossDamageMultiplier;
	}

	if (RuntimeSet.bIsBoss)
	{
		HealthMult *= BossHealthMultiplier;
		DamageMult *= BossDamageMultiplier;
		RangeMult *= BossRangeMultiplier;
	}

	for (const FEliteAffixDefinition* Affix : Affixes)
	{
		if (!Affix)
		{
			continue;
		}

		HealthMult *= FMath::Max(KINDA_SMALL_NUMBER, Affix->HealthMultiplier);
		DamageMult *= FMath::Max(KINDA_SMALL_NUMBER, Affix->DamageMultiplier);
		RangeMult *= FMath::Max(KINDA_SMALL_NUMBER, Affix->RangeMultiplier);
	}

	const float ScaleMult = ComputeEliteScale(RuntimeSet, Affixes);

	ApplyEliteStats(SpawnedPawn, HealthMult, DamageMult, RangeMult);
	ApplyEliteGameplay(SpawnedPawn, RuntimeSet, Affixes, /*bApplyXPMultipliers=*/true);

	TArray<FGameplayTag> AffixTags;
	for (const FEliteAffixDefinition* Affix : Affixes)
	{
		if (Affix && Affix->AffixTag.IsValid())
		{
			AffixTags.Add(Affix->AffixTag);
		}
	}

	// Cosmetic FX are multicast so dedicated servers can show them to clients even though they cannot render.
	MulticastApplyElitePresentation(SpawnedPawn, ScaleMult, AffixTags, ResolvePooledOriginalScale(SpawnedPawn));
	AJ_LOG(this, TEXT("ApplyElitePackage applied: Pawn=%s Elite=%d MiniBoss=%d Boss=%d AffixCount=%d ScaleMult=%.2f"),
		*GetNameSafe(SpawnedPawn),
		RuntimeSet.bIsElite ? 1 : 0,
		RuntimeSet.bIsMiniBoss ? 1 : 0,
		RuntimeSet.bIsBoss ? 1 : 0,
		AffixTags.Num(),
		ScaleMult);
	RefreshSpawnedPawnStatusBar(SpawnedPawn);
}

void AAeyerjiSpawnerGroup::HandleActivationEvent(const FGameplayTag& EventTag, const FGameplayEventData& Payload)
{
	if (!HasAuthority())
	{
		return;
	}

	if (!bDisableActivationEvent && EventTag == ActivationEventTag)
	{
		const AActor* InstigatorSource = Payload.Instigator.Get();
		AActor* InstigatorActor = InstigatorSource ? const_cast<AActor*>(InstigatorSource) : nullptr;

		AController* InstigatorController = nullptr;
		if (const UObject* OptionalObj = Payload.OptionalObject2.Get())
		{
			InstigatorController = const_cast<AController*>(Cast<AController>(OptionalObj));
		}

		ActivateEncounter(InstigatorActor, InstigatorController);
	}
}

void AAeyerjiSpawnerGroup::CacheActivationStimulus(AActor* InstigatorActor, AController* InstigatorController)
{
	ClearAggroCache();

	if (InstigatorActor)
	{
		if (AController* ControllerFromActor = Cast<AController>(InstigatorActor))
		{
			InstigatorController = ControllerFromActor;
			CachedAggroActor = ControllerFromActor->GetPawn();
		}
		else
		{
			CachedAggroActor = InstigatorActor;

			if (!InstigatorController)
			{
				if (APawn* Pawn = Cast<APawn>(InstigatorActor))
				{
					InstigatorController = Pawn->GetController();
				}
			}
		}
	}

	if (InstigatorController)
	{
		CachedAggroController = InstigatorController;

		if (!CachedAggroActor.IsValid())
		{
			CachedAggroActor = InstigatorController->GetPawn();
		}
	}
}

AActor* AAeyerjiSpawnerGroup::ResolveAggroTargetActor() const
{
	if (CachedAggroActor.IsValid())
	{
		return CachedAggroActor.Get();
	}

	if (CachedAggroController.IsValid())
	{
		return CachedAggroController->GetPawn();
	}

	return nullptr;
}

APawn* AAeyerjiSpawnerGroup::ResolveAggroTargetPawn() const
{
	if (AActor* TargetActor = ResolveAggroTargetActor())
	{
		return Cast<APawn>(TargetActor);
	}

	if (CachedAggroController.IsValid())
	{
		return CachedAggroController->GetPawn();
	}

	return nullptr;
}

AController* AAeyerjiSpawnerGroup::ResolveAggroController() const
{
	if (CachedAggroController.IsValid())
	{
		return CachedAggroController.Get();
	}

	if (CachedAggroActor.IsValid())
	{
		if (APawn* Pawn = Cast<APawn>(CachedAggroActor.Get()))
		{
			return Pawn->GetController();
		}
	}

	return nullptr;
}

void AAeyerjiSpawnerGroup::ApplyAggroToSpawnedPawn(APawn* SpawnedPawn)
{
	if (!AggroSettings.bEnableAggro || !SpawnedPawn)
	{
		return;
	}

	AActor* AggroActor = ResolveAggroTargetActor();
	AController* AggroController = ResolveAggroController();
	APawn* AggroPawn = ResolveAggroTargetPawn();

	if (!AggroActor && !AggroController && !AggroPawn)
	{
		return;
	}

	APawn* InstigatorPawn = AggroPawn;
	if (!InstigatorPawn && AggroController)
	{
		InstigatorPawn = AggroController->GetPawn();
	}

	if (InstigatorPawn)
	{
		SpawnedPawn->SetInstigator(InstigatorPawn);
	}

	if (AggroSettings.bEnsureController && !SpawnedPawn->GetController())
	{
		SpawnedPawn->SpawnDefaultController();
	}

	AController* SpawnedController = SpawnedPawn->GetController();

	if (AEnemyAIController* EnemyAI = Cast<AEnemyAIController>(SpawnedController))
	{
		EnemyAI->SetPermanentRiftPursuit(bPermanentRiftPursuit);
		EnemyAI->ConfigureDefenseObjectiveTargeting(DefenseObjectiveTargetActor.Get(), DefenseTargetingSettings);
		if (IsValid(DefenseObjectiveTargetActor.Get()))
		{
			// Defense waves are target-arbitrated by the enemy controller/StateTree.
			// Generic encounter aggro would otherwise periodically force MoveToActor(player)
			// and fight the "return to the objective" rule.
			EnemyAI->RefreshDefenseObjectiveTarget();
			return;
		}
		if (bPermanentRiftPursuit && IsValid(AggroActor)
			&& !EnemyAI->TryAcquireTarget(AggroActor, /*bBroadcastAllyAlert=*/false)
			&& EnemyAI->GetTargetActor() != AggroActor)
		{
			EnemyAI->SetTargetActor(AggroActor);
		}
	}

	if (AggroSettings.bSetFocusOnInstigator && SpawnedController && AggroActor)
	{
		if (AAIController* AIController = Cast<AAIController>(SpawnedController))
		{
			AIController->SetFocus(AggroActor);
		}
	}

	if (AggroSettings.bIssueMoveCommand && AggroActor)
	{
		if (AAIController* AIController = Cast<AAIController>(SpawnedController))
		{
			FAeyerjiNavSafetyResolveParams NavParams;
			NavParams.ProjectionExtent = SpawnNavProjectionExtent;
			FVector SafePawnLocation = SpawnedPawn->GetActorLocation();
			if (!UAeyerjiNavSafetyLibrary::EnsurePawnOnSafeNav(
					SpawnedPawn,
					NavParams,
					/*bRecoverIfOffNav=*/true,
					SafePawnLocation))
			{
				UE_LOG(LogAeyerji, Warning,
					TEXT("[SpawnerGroup] Aggro move skipped because spawned pawn could not resolve to nav. Spawner=%s Pawn=%s"),
					*GetNameSafe(this),
					*GetNameSafe(SpawnedPawn));
				return;
			}

			AIController->MoveToActor(AggroActor, AggroSettings.MoveAcceptanceRadius, true, true, true, nullptr, false);
		}
	}
}

void AAeyerjiSpawnerGroup::StartAggroReissueTimer()
{
	if (!HasAuthority() || !AggroSettings.bEnableAggro
		|| (!AggroSettings.bReissueAggroWhileActive && !bPermanentRiftPursuit))
	{
		return;
	}

	if (!GetWorld() || (AggroSettings.ReissueAggroIntervalSeconds <= 0.f && !bPermanentRiftPursuit))
	{
		return;
	}

	GetWorldTimerManager().ClearTimer(AggroReissueTimerHandle);
	GetWorldTimerManager().SetTimer(
		AggroReissueTimerHandle,
		this,
		&AAeyerjiSpawnerGroup::ReissueAggroToTrackedEnemies,
		bPermanentRiftPursuit ? 0.75f : FMath::Max(0.1f, AggroSettings.ReissueAggroIntervalSeconds),
		true);
}

void AAeyerjiSpawnerGroup::StopAggroReissueTimer()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AggroReissueTimerHandle);
	}
}

void AAeyerjiSpawnerGroup::ReissueAggroToTrackedEnemies()
{
	if (!HasAuthority() || !bActive || !AggroSettings.bEnableAggro)
	{
		StopAggroReissueTimer();
		return;
	}

	if (!bPermanentRiftPursuit && !ResolveAggroTargetActor() && !ResolveAggroController() && !ResolveAggroTargetPawn())
	{
		return;
	}

	for (auto It = TrackedLiveEnemies.CreateIterator(); It; ++It)
	{
		AActor* EnemyActor = It->Get();
		if (!IsValid(EnemyActor))
		{
			It.RemoveCurrent();
			continue;
		}

		if (APawn* EnemyPawn = Cast<APawn>(EnemyActor))
		{
			if (bPermanentRiftPursuit)
			{
				APawn* NearestPlayer = ResolveNearestLivePlayer(EnemyPawn->GetActorLocation());
				if (!NearestPlayer)
				{
					continue;
				}
				CachedAggroActor = NearestPlayer;
				CachedAggroController = NearestPlayer->GetController();
			}
			ApplyAggroToSpawnedPawn(EnemyPawn);
		}
	}

	LiveEnemies = TrackedLiveEnemies.Num();
}

APawn* AAeyerjiSpawnerGroup::ResolveNearestLivePlayer(const FVector& FromLocation) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	const FGameplayTag DeadTag = FGameplayTag::RequestGameplayTag(TEXT("State.Dead"), /*ErrorIfNotFound=*/false);
	APawn* BestPawn = nullptr;
	float BestDistanceSquared = TNumericLimits<float>::Max();
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		AController* PlayerController = It->Get();
		APawn* PlayerPawn = PlayerController ? PlayerController->GetPawn() : nullptr;
		if (!IsValid(PlayerPawn))
		{
			continue;
		}
		if (DeadTag.IsValid())
		{
			if (const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PlayerPawn, true))
			{
				if (ASC->HasMatchingGameplayTag(DeadTag))
				{
					continue;
				}
			}
		}

		const float DistanceSquared = FVector::DistSquared(FromLocation, PlayerPawn->GetActorLocation());
		if (!BestPawn || DistanceSquared < BestDistanceSquared)
		{
			BestPawn = PlayerPawn;
			BestDistanceSquared = DistanceSquared;
		}
	}
	return BestPawn;
}

void AAeyerjiSpawnerGroup::ClearAggroCache()
{
	CachedAggroActor.Reset();
	CachedAggroController.Reset();
}

void AAeyerjiSpawnerGroup::RebuildSpawnPointOrder()
{
	SpawnPointOrder.Reset();
	SpawnPointCursor = 0;

	if (SpawnPointMode == EAeyerjiSpawnPointMode::Random)
	{
		return;
	}

	const int32 NumPoints = SpawnPoints.Num();
	if (NumPoints <= 0)
	{
		return;
	}

	switch (SpawnPointMode)
	{
	case EAeyerjiSpawnPointMode::Sequential:
		for (int32 Index = 0; Index < NumPoints; ++Index)
		{
			if (IsValid(SpawnPoints[Index]))
			{
				SpawnPointOrder.Add(Index);
			}
		}
		break;

	case EAeyerjiSpawnPointMode::Symmetrical:
		{
			int32 Left = 0;
			int32 Right = NumPoints - 1;
			while (Left <= Right)
			{
				if (IsValid(SpawnPoints[Left]))
				{
					SpawnPointOrder.Add(Left);
				}

				if (Right != Left && IsValid(SpawnPoints[Right]))
				{
					SpawnPointOrder.Add(Right);
				}

				++Left;
				--Right;
			}
		}
		break;

	default:
		break;
	}
}

int32 AAeyerjiSpawnerGroup::GetNextSpawnPointIndex()
{
	const int32 NumPoints = SpawnPoints.Num();
	if (NumPoints <= 0)
	{
		return INDEX_NONE;
	}

	if (SpawnPointMode == EAeyerjiSpawnPointMode::Random)
	{
		for (int32 Attempt = 0; Attempt < NumPoints; ++Attempt)
		{
			const int32 Candidate = FMath::RandHelper(NumPoints);
			if (SpawnPoints.IsValidIndex(Candidate) && IsValid(SpawnPoints[Candidate]))
			{
				return Candidate;
			}
		}

		return INDEX_NONE;
	}

	if (SpawnPointOrder.Num() == 0)
	{
		RebuildSpawnPointOrder();
	}

	const int32 OrderCount = SpawnPointOrder.Num();
	if (OrderCount == 0)
	{
		return INDEX_NONE;
	}

	for (int32 Attempt = 0; Attempt < OrderCount; ++Attempt)
	{
		if (SpawnPointCursor >= OrderCount)
		{
			SpawnPointCursor = 0;
		}

		const int32 Candidate = SpawnPointOrder[SpawnPointCursor];
		SpawnPointCursor = (SpawnPointCursor + 1) % OrderCount;

		if (SpawnPoints.IsValidIndex(Candidate) && IsValid(SpawnPoints[Candidate]))
		{
			return Candidate;
		}
	}

	return INDEX_NONE;
}

void AAeyerjiSpawnerGroup::ResetSpawnPointCycle()
{
	SpawnPointOrder.Reset();
	SpawnPointCursor = 0;
}

void AAeyerjiSpawnerGroup::RebuildSpawnDiscoveryCache()
{
	CachedSpawnRegions.Reset();
	CachedFallbackPlayerStart = nullptr;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AAeyerjiSpawnRegion> It(World); It; ++It)
	{
		AAeyerjiSpawnRegion* Region = *It;
		if (!IsValid(Region) || Region->RegionWeight <= 0.f)
		{
			continue;
		}

		const FBox Bounds = Region->GetRegionBounds();
		if (!Bounds.IsValid)
		{
			continue;
		}

		FCachedAeyerjiSpawnRegion& CachedRegion = CachedSpawnRegions.AddDefaulted_GetRef();
		CachedRegion.Region = Region;
		CachedRegion.Bounds = Bounds;
		CachedRegion.Weight = Region->RegionWeight;
	}

	if (bFallbackReachabilityTargetToPlayerStart)
	{
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			if (APlayerStart* PlayerStart = *It)
			{
				CachedFallbackPlayerStart = PlayerStart;
				break;
			}
		}
	}
}

void AAeyerjiSpawnerGroup::BeginEnemyScalingTablePreload()
{
	CachedEnemyScalingTable = EnemyScalingTable.Get();
	CachedScalingRows.Reset();
	CachedMissingScalingRows.Reset();

	if (CachedEnemyScalingTable || EnemyScalingTable.IsNull())
	{
		return;
	}

	EnemyScalingTableHandle = UAssetManager::GetStreamableManager().RequestAsyncLoad(
		EnemyScalingTable.ToSoftObjectPath(),
		FStreamableDelegate::CreateUObject(this, &AAeyerjiSpawnerGroup::HandleEnemyScalingTableLoaded),
		FStreamableManager::AsyncLoadHighPriority);
}

void AAeyerjiSpawnerGroup::HandleEnemyScalingTableLoaded()
{
	CachedEnemyScalingTable = EnemyScalingTable.Get();
	CachedScalingRows.Reset();
	CachedMissingScalingRows.Reset();
}

float AAeyerjiSpawnerGroup::GetCachedSpawnHalfHeight(TSubclassOf<APawn> EnemyClass)
{
	UClass* Class = EnemyClass.Get();
	if (!Class)
	{
		return 100.f;
	}

	const TObjectKey<UClass> ClassKey(Class);
	if (const float* CachedHalfHeight = CachedSpawnHalfHeights.Find(ClassKey))
	{
		return *CachedHalfHeight;
	}

	float SpawnHalfHeight = 100.f;
	if (const ACharacter* CharacterCDO = Cast<ACharacter>(Class->GetDefaultObject()))
	{
		if (const UCapsuleComponent* Capsule = CharacterCDO->GetCapsuleComponent())
		{
			SpawnHalfHeight = FMath::Max(1.f, Capsule->GetScaledCapsuleHalfHeight());
		}
	}

	CachedSpawnHalfHeights.Add(ClassKey, SpawnHalfHeight);
	return SpawnHalfHeight;
}

float AAeyerjiSpawnerGroup::ResolveSurvivalRoundAttributeMultiplier(const FGameplayAttribute& Attribute) const
{
	if (!LevelDirector || !Attribute.IsValid())
	{
		return 1.f;
	}

	if (Attribute == UAeyerjiAttributeSet::GetHPMaxAttribute())
	{
		return FMath::Max(0.f, LevelDirector->GetSurvivalEnemyHealthMultiplier());
	}

	if (Attribute == UAeyerjiAttributeSet::GetAttackDamageAttribute()
		|| Attribute == UAeyerjiAttributeSet::GetSpellPowerAttribute())
	{
		return FMath::Max(0.f, LevelDirector->GetSurvivalEnemyDamageMultiplier());
	}

	return 1.f;
}

void AAeyerjiSpawnerGroup::ApplyEnemyScaling(APawn* SpawnedPawn, const FEnemySet& EnemySet)
{
	if (!HasAuthority() || !IsValid(SpawnedPawn))
	{
		return;
	}

	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(SpawnedPawn, /*LookForComponent*/ true);
	if (!ASC)
	{
		return;
	}

	// Make sure archetype defaults are applied before we read base attribute values.
	AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(SpawnedPawn);
	if (Enemy)
	{
		Enemy->ApplyArchetypeData();
	}

	const TWeakObjectPtr<AActor> EnemyKey(SpawnedPawn);
	FTrackedEnemyScalingState* ScalingState = TrackedEnemyScalingStates.Find(EnemyKey);
	const bool bPreserveResourceRatio = ScalingState && ScalingState->BaseAttributeValues.Num() > 0;
	const float OldHP = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetHPAttribute());
	const float OldHPMax = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetHPMaxAttribute());
	const float OldMana = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetManaAttribute());
	const float OldManaMax = ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetManaMaxAttribute());

	const int32 PlayerLevel = ResolvePlayerLevelForScaling();
	const int32 EnemyLevel = LevelDirector
		? LevelDirector->GetEffectiveEnemyLevelForPlayerLevel(PlayerLevel)
		: UAeyerjiDifficultySettings::Get()->EvaluateEnemyLevel(PlayerLevel);
	const float DifficultyAlpha = LevelDirector
		? LevelDirector->GetDerivedDifficultyAlpha()
		: UAeyerjiDifficultySettings::Get()->EvaluateDifficultyAlpha(UAeyerjiDifficultySettings::GetNormalWorldTier());
	const float GlobalStatBudgetMultiplier = LevelDirector
		? LevelDirector->GetGlobalStatBudgetMultiplier()
		: UAeyerjiDifficultySettings::Get()->EvaluateStatBudget(UAeyerjiDifficultySettings::GetNormalWorldTier());
	const float RewardQualityMultiplier = LevelDirector
		? LevelDirector->GetActiveRiftRewardQualityMultiplier()
		: 1.f;

	const FEnemyScalingRow* Row = FindScalingRow(EnemySet.EnemyArchetypeTag);
	if (!Row)
	{
		ASC->SetNumericAttributeBase(UAeyerjiAttributeSet::GetLevelAttribute(), static_cast<float>(EnemyLevel));

		auto ApplyDirectScalingMultiplier = [&](const FGameplayAttribute& Attribute, const FName AttributeName)
		{
			const float RiftMultiplier = LevelDirector ? LevelDirector->GetRiftAttributeMultiplier(Attribute) : 1.f;
			const float Multiplier = ResolveSurvivalRoundAttributeMultiplier(Attribute) * RiftMultiplier;
			if (FMath::IsNearlyEqual(Multiplier, 1.f))
			{
				return;
			}

			float BaseValue = ASC->GetNumericAttribute(Attribute);
			if (ScalingState)
			{
				if (const float* CachedBaseValue = ScalingState->BaseAttributeValues.Find(AttributeName))
				{
					BaseValue = *CachedBaseValue;
				}
				else
				{
					ScalingState->BaseAttributeValues.Add(AttributeName, BaseValue);
					if (FPooledEnemyRuntimeState* PoolState = PooledEnemyStates.Find(TWeakObjectPtr<APawn>(SpawnedPawn)))
					{
						PoolState->BaselineAttributeValues.FindOrAdd(AttributeName, BaseValue);
					}
				}
			}

			const float NewValue = BaseValue * Multiplier;
			ASC->SetNumericAttributeBase(Attribute, NewValue);
			if (Attribute == UAeyerjiAttributeSet::GetHPMaxAttribute())
			{
				ASC->SetNumericAttributeBase(
					UAeyerjiAttributeSet::GetHPAttribute(),
					ResolvePreservedResourceValue(OldHP, OldHPMax, NewValue, bPreserveResourceRatio));
			}
		};

		ApplyDirectScalingMultiplier(UAeyerjiAttributeSet::GetHPMaxAttribute(), TEXT("AeyerjiAttributeSet.HPMax"));
		ApplyDirectScalingMultiplier(UAeyerjiAttributeSet::GetHPRegenAttribute(), TEXT("AeyerjiAttributeSet.HPRegen"));
		ApplyDirectScalingMultiplier(UAeyerjiAttributeSet::GetAttackDamageAttribute(), TEXT("AeyerjiAttributeSet.AttackDamage"));
		ApplyDirectScalingMultiplier(UAeyerjiAttributeSet::GetSpellPowerAttribute(), TEXT("AeyerjiAttributeSet.SpellPower"));
		ApplyDirectScalingMultiplier(UAeyerjiAttributeSet::GetStaggerPowerAttribute(), TEXT("AeyerjiAttributeSet.StaggerPower"));
		ApplyDirectScalingMultiplier(UAeyerjiAttributeSet::GetArmorAttribute(), TEXT("AeyerjiAttributeSet.Armor"));
		ApplyDirectScalingMultiplier(UAeyerjiAttributeSet::GetPoiseMaxAttribute(), TEXT("AeyerjiAttributeSet.PoiseMax"));
		ApplyDirectScalingMultiplier(UAeyerjiAttributeSet::GetStaggerResistanceAttribute(), TEXT("AeyerjiAttributeSet.StaggerResistance"));

		if (Enemy)
		{
			Enemy->SetScalingSnapshot(EnemyLevel, DifficultyAlpha, Enemy->GetScalingSourceTag(), RewardQualityMultiplier);
		}

		RefreshSpawnedPawnStatusBar(SpawnedPawn);
		return;
	}

	ASC->SetNumericAttributeBase(UAeyerjiAttributeSet::GetLevelAttribute(), static_cast<float>(EnemyLevel));

	for (const FEnemyAttributeScalingEntry& Entry : Row->Attributes)
	{
		const FGameplayAttribute Attr = ResolveAttribute(Entry.AttributeName);
		if (!Attr.IsValid())
		{
			continue;
		}

		const int32 LevelDelta = FMath::Max(EnemyLevel - 1, 0);
		float PerLevelAdd = Entry.PerLevelAdd;
		float MinValue = Entry.MinValue;
		float MaxValue = Entry.MaxValue;
		if (Attr == UAeyerjiAttributeSet::GetAttackSpeedAttribute() && MaxValue > 0.f && MaxValue <= 10.f)
		{
			// Legacy EnemyScaling rows stored AttackSpeed clamps as attacks/sec. Runtime attributes use rating units.
			PerLevelAdd *= 100.f;
			MinValue *= 100.f;
			MaxValue *= 100.f;
		}

		float BaseValue = ASC->GetNumericAttribute(Attr);
		if (ScalingState)
		{
			if (const float* CachedBaseValue = ScalingState->BaseAttributeValues.Find(Entry.AttributeName))
			{
				BaseValue = *CachedBaseValue;
			}
			else
			{
				ScalingState->BaseAttributeValues.Add(Entry.AttributeName, BaseValue);
				if (FPooledEnemyRuntimeState* PoolState = PooledEnemyStates.Find(TWeakObjectPtr<APawn>(SpawnedPawn)))
				{
					PoolState->BaselineAttributeValues.FindOrAdd(Entry.AttributeName, BaseValue);
				}
			}
		}

		float Value = BaseValue;
		Value = (Value * (1.f + Entry.PerLevelMultiplier * LevelDelta)) + (PerLevelAdd * LevelDelta);

		if (Enemy)
		{
			Enemy->ApplyArchetypeStatMultipliers(Entry.AttributeName, Value);
		}

		Value *= GlobalStatBudgetMultiplier;
		Value *= LevelDirector ? LevelDirector->GetRiftAttributeMultiplier(Attr) : 1.f;

		const bool bClampMin = !FMath::IsNearlyZero(MinValue);
		const bool bClampMax = !FMath::IsNearlyZero(MaxValue);
		if (bClampMin || bClampMax)
		{
			const float Min = bClampMin ? MinValue : Value;
			const float Max = bClampMax ? MaxValue : Value;
			Value = FMath::Clamp(Value, Min, Max);
		}

		Value *= ResolveSurvivalRoundAttributeMultiplier(Attr);

		ASC->SetNumericAttributeBase(Attr, Value);

		if (Attr == UAeyerjiAttributeSet::GetHPMaxAttribute())
		{
			ASC->SetNumericAttributeBase(
				UAeyerjiAttributeSet::GetHPAttribute(),
				ResolvePreservedResourceValue(OldHP, OldHPMax, Value, bPreserveResourceRatio));
		}
		else if (Attr == UAeyerjiAttributeSet::GetManaMaxAttribute())
		{
			ASC->SetNumericAttributeBase(
				UAeyerjiAttributeSet::GetManaAttribute(),
				ResolvePreservedResourceValue(OldMana, OldManaMax, Value, bPreserveResourceRatio));
		}
	}

	if (Enemy)
	{
		Enemy->SetScalingSnapshot(EnemyLevel, DifficultyAlpha, Row->SourceTag, RewardQualityMultiplier);
	}

	RefreshSpawnedPawnStatusBar(SpawnedPawn);
}

const FEnemyScalingRow* AAeyerjiSpawnerGroup::FindScalingRow(const FGameplayTag& ArchetypeTag) const
{
	if (!ArchetypeTag.IsValid())
	{
		return nullptr;
	}

	if (const FEnemyScalingRow* const* CachedRow = CachedScalingRows.Find(ArchetypeTag))
	{
		return *CachedRow;
	}

	if (CachedMissingScalingRows.Contains(ArchetypeTag))
	{
		return nullptr;
	}

	auto GetTagDepth = [](const FGameplayTag& Tag) -> int32
	{
		if (!Tag.IsValid())
		{
			return 0;
		}

		const FString TagString = Tag.ToString();
		int32 Depth = 1;
		for (const TCHAR Char : TagString)
		{
			if (Char == TEXT('.'))
			{
				++Depth;
			}
		}

		return Depth;
	};

	const FEnemyScalingRow* BestRow = nullptr;
	int32 BestDepth = -1;

	UDataTable* Table = CachedEnemyScalingTable.Get();
	if (!Table)
	{
		return nullptr;
	}

	if (Table)
	{
		for (const TPair<FName, uint8*>& Pair : Table->GetRowMap())
		{
			if (const FEnemyScalingRow* Row = reinterpret_cast<const FEnemyScalingRow*>(Pair.Value))
			{
				if (!Row->ArchetypeTag.IsValid())
				{
					continue;
				}

				if (Row->ArchetypeTag == ArchetypeTag)
				{
					CachedScalingRows.Add(ArchetypeTag, Row);
					return Row;
				}

				if (ArchetypeTag.MatchesTag(Row->ArchetypeTag))
				{
					const int32 Depth = GetTagDepth(Row->ArchetypeTag);
					if (Depth > BestDepth)
					{
						BestRow = Row;
						BestDepth = Depth;
					}
				}
			}
		}
	}

	if (BestRow)
	{
		CachedScalingRows.Add(ArchetypeTag, BestRow);
		return BestRow;
	}

	CachedMissingScalingRows.Add(ArchetypeTag);
	if (!BestRow && GEngine)
	{
		static TSet<FName> WarnedTags;
		const FName TagName = ArchetypeTag.GetTagName();
		if (!WarnedTags.Contains(TagName))
		{
			WarnedTags.Add(TagName);
			GEngine->AddOnScreenDebugMessage(
				-1,
				6.0f,
				FColor::Red,
				FString::Printf(TEXT("EnemyScalingTable: no scaling row found for %s"), *ArchetypeTag.ToString()));
		}
	}

	return nullptr;
}

int32 AAeyerjiSpawnerGroup::ResolvePlayerLevelForScaling() const
{
	if (LevelDirector)
	{
		return LevelDirector->GetEnemyScalingPlayerLevel();
	}

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

FGameplayAttribute AAeyerjiSpawnerGroup::ResolveAttribute(const FName& AttributeName) const
{
	FString NameString = AttributeName.ToString();
	int32 DotIndex = INDEX_NONE;
	if (NameString.FindChar('.', DotIndex))
	{
		NameString = NameString.Mid(DotIndex + 1);
	}

	const FName StrippedName(*NameString);
	if (FProperty* Prop = FindFProperty<FProperty>(UAeyerjiAttributeSet::StaticClass(), StrippedName))
	{
		return FGameplayAttribute(Prop);
	}

	return FGameplayAttribute();
}
