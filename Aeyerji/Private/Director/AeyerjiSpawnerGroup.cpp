#include "Director/AeyerjiSpawnerGroup.h"

#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Engine/World.h"
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
#include "GameFramework/PlayerStart.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "Enemy/AeyerjiEnemyManagementBPFL.h"
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
	if (ActivationEventHandle.IsValid() && ActivationEventTag.IsValid())
	{
		if (UAeyerjiGameplayEventSubsystem* EventSubsystem = UAeyerjiGameplayEventSubsystem::Get(this))
		{
			EventSubsystem->UnregisterListener(ActivationEventTag, ActivationEventHandle);
		}
	}

	ClearAggroCache();

	Super::EndPlay(EndPlayReason);
}

void AAeyerjiSpawnerGroup::HandleActivationOverlap(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
                                                   UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
                                                   bool bFromSweep, const FHitResult& SweepResult)
{
	if (!bActive && OtherActor && OtherActor == UGameplayStatics::GetPlayerPawn(this, 0))
	{
		APawn* PawnInstigator = Cast<APawn>(OtherActor);
		AController* InstigatorController = PawnInstigator ? PawnInstigator->GetController() : nullptr;
		ActivateEncounter(OtherActor, InstigatorController);
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

	const bool bUseEncounterAsset = EncounterDefinition && (bPreferEncounterAsset || Waves.Num() == 0);
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
	ResetSpawnPointCycle();
	if (SpawnPointMode != EAeyerjiSpawnPointMode::Random)
	{
		RebuildSpawnPointOrder();
	}

	bActive = true;
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

				// Designers should not author bosses/mini-bosses into wave data; those are spawned manually.
				if (EnemySet.bIsBoss || EnemySet.bIsMiniBoss)
				{
					PendingSpawnCounts[WaveIdx][SetIdx] = 0;
					if (EnemySet.bIsBoss)
					{
						UE_LOG(LogTemp, Warning, TEXT("Spawner %s suppressing boss set in Waves[%d] (SetIdx=%d). Bosses must be spawned manually via RegisterExternalEnemy."),
							*GetNameSafe(this), WaveIdx, SetIdx);
					}
					else
					{
						UE_LOG(LogTemp, Warning, TEXT("Spawner %s suppressing mini-boss set in Waves[%d] (SetIdx=%d). Mini-bosses must be spawned manually via RegisterExternalEnemy."),
							*GetNameSafe(this), WaveIdx, SetIdx);
					}
					continue;
				}

				PendingSpawnCounts[WaveIdx][SetIdx] = EnemySet.Count;
			}
		}
	}

	OnEncounterStarted.Broadcast(this);

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
			PendingSpawnCounts[WaveIdx][SetIdx] = (EnemySet.bIsBoss || EnemySet.bIsMiniBoss) ? 0 : EnemySet.Count;
			TotalPendingSpawns += PendingSpawnCounts[WaveIdx][SetIdx];
		}
	}

	UE_LOG(LogTemp, Log, TEXT("SurvivalSpawner %s activated with %d waves, %d pending spawns, %d spawn points, aggro actor=%s controller=%s."),
		*GetNameSafe(this),
		EncounterWavesRuntime.Num(),
		TotalPendingSpawns,
		SpawnPoints.Num(),
		*GetNameSafe(ResolveAggroTargetActor()),
		*GetNameSafe(ResolveAggroController()));

	OnEncounterStarted.Broadcast(this);
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

void AAeyerjiSpawnerGroup::ResetEncounter()
{
	GetWorldTimerManager().ClearAllTimersForObject(this);

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
		}

		if (BossGameplayTag.IsValid())
		{
			if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(SpawnedPawn))
			{
				ASC->AddLooseGameplayTag(BossGameplayTag);
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
	}

	RegisterProgressEnemy(SpawnedPawn);

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

	float SpawnHalfHeight = 100.f;
	if (const ACharacter* CharacterCDO = EnemyClass ? Cast<ACharacter>(EnemyClass->GetDefaultObject()) : nullptr)
	{
		if (const UCapsuleComponent* Capsule = CharacterCDO->GetCapsuleComponent())
		{
			SpawnHalfHeight = FMath::Max(1.f, Capsule->GetScaledCapsuleHalfHeight());
		}
	}

	if (UWorld* World = GetWorld())
	{
		const float MaxDistanceFromReference = FMath::Max(0.f, SpawnMaxDistanceFromTarget);
		const int32 MaxRegionSpawnAttempts = FMath::Max(1, SpawnRegionSearchAttempts);

		TArray<AAeyerjiSpawnRegion*> Regions;
		for (TActorIterator<AAeyerjiSpawnRegion> It(World); It; ++It)
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

			if (Region->RegionWeight > 0.f)
			{
				Regions.Add(Region);
			}
		}

		if (!Regions.IsEmpty())
		{
			TArray<AAeyerjiSpawnRegion*> PreferredRegions;
			for (AAeyerjiSpawnRegion* Region : Regions)
			{
				const FBox Bounds = Region->GetRegionBounds();
				const float DistSq = Bounds.ComputeSquaredDistanceToPoint(ReferenceLocation);
				if (!bHasReferenceLocation || MaxDistanceFromReference <= 0.f || DistSq <= FMath::Square(MaxDistanceFromReference))
				{
					PreferredRegions.Add(Region);
				}
			}

			const TArray<AAeyerjiSpawnRegion*>& CandidateRegions = PreferredRegions.IsEmpty() ? Regions : PreferredRegions;
			const UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
			for (int32 Attempt = 0; Attempt < MaxRegionSpawnAttempts; ++Attempt)
			{
				AAeyerjiSpawnRegion* Region = CandidateRegions[FMath::RandHelper(CandidateRegions.Num())];
				const FBox Bounds = Region->GetRegionBounds();
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
		if (UWorld* World = GetWorld())
		{
			for (TActorIterator<APlayerStart> It(World); It; ++It)
			{
				if (const APlayerStart* PlayerStart = *It)
				{
					OutLocation = PlayerStart->GetActorLocation();
					return true;
				}
			}
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

	// Safety: bosses/mini-bosses should never flow through wave spawns; only manual RegisterExternalEnemy.
	if (EnemySet.bIsBoss || EnemySet.bIsMiniBoss)
	{
		UE_LOG(LogTemp, Warning, TEXT("SpawnOneFromSet aborted: boss/mini-boss found in wave on %s. Remove from wave data and spawn manually."), *GetNameSafe(this));
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

	UE_LOG(LogTemp, Log, TEXT("SurvivalSpawner %s spawning wave=%d set=%d class=%s location=%s aggro=%s."),
		*GetNameSafe(this),
		WaveIndex,
		SetIndex,
		*GetNameSafe(ResolvedEnemySet.EnemyClass),
		*SpawnTransform.GetLocation().ToCompactString(),
		*GetNameSafe(AggroActor));

	APawn* SpawnedPawn = UAeyerjiEnemyManagementBPFL::SpawnAndRegisterEnemyFromSet(
		this,
		ResolvedEnemySet,
		SpawnTransform,
		this,
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
	}
	return true;
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

	bool bRemovedTrackedEnemy = false;
	for (auto It = TrackedLiveEnemies.CreateIterator(); It; ++It)
	{
		const TWeakObjectPtr<AActor>& TrackedEnemy = *It;
		if (!TrackedEnemy.IsValid() || TrackedEnemy.Get() == EnemyActor)
		{
			bRemovedTrackedEnemy = true;
			It.RemoveCurrent();
		}
	}

	if (!bRemovedTrackedEnemy)
	{
		return;
	}

	TrackedEnemyScalingStates.Remove(TWeakObjectPtr<AActor>(EnemyActor));
	const bool bRemovedBossEnemy = TrackedBossEnemies.Remove(TWeakObjectPtr<AActor>(EnemyActor)) > 0;
	LiveEnemies = TrackedLiveEnemies.Num();
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

void AAeyerjiSpawnerGroup::RegisterProgressEnemy(APawn* SpawnedPawn)
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

	if (!EncounterDirector || !EncounterDirector->IsFixedWorldPopulationActive())
	{
		return;
	}

	if (AEnemyParentNative* Enemy = Cast<AEnemyParentNative>(SpawnedPawn))
	{
		EncounterDirector->RegisterProgressEnemy(Enemy);
	}
}

void AAeyerjiSpawnerGroup::MulticastApplyElitePresentation_Implementation(APawn* SpawnedPawn, float ScaleMultiplier, const TArray<FGameplayTag>& AffixTags)
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
		const FVector NewScale = SpawnedPawn->GetActorScale3D() * SafeScale;
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
		const FVector NewScale = SpawnedPawn->GetActorScale3D() * SafeScale;
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
		}

		if (bApplyMiniBossBonuses && MiniBossGameplayTag.IsValid())
		{
			ASC->AddLooseGameplayTag(MiniBossGameplayTag);
		}

		if (EnemySet.bIsBoss && BossGameplayTag.IsValid())
		{
			ASC->AddLooseGameplayTag(BossGameplayTag);
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
		}

		if (ASC && Affix->GameplayEffect)
		{
			const UGameplayEffect* EffectCDO = Affix->GameplayEffect->GetDefaultObject<UGameplayEffect>();
			if (EffectCDO)
			{
				ASC->ApplyGameplayEffectToSelf(EffectCDO, 1.f, ASC->MakeEffectContext());
			}
		}

		if (ASC)
		{
			for (TSubclassOf<UGameplayAbility> AbilityClass : Affix->GrantedAbilities)
			{
				if (*AbilityClass)
				{
					ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass));
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

			ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass));
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

			ASC->GiveAbility(FGameplayAbilitySpec(AbilityClass));
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
	}

	if (bApplyMiniBossBonuses && !MiniBossActorTag.IsNone())
	{
		SpawnedPawn->Tags.AddUnique(MiniBossActorTag);
	}

	if (RuntimeSet.bIsBoss && !BossActorTag.IsNone())
	{
		SpawnedPawn->Tags.AddUnique(BossActorTag);
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
		MulticastApplyElitePresentation(SpawnedPawn, 1.f, AffixTags);
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
	MulticastApplyElitePresentation(SpawnedPawn, ScaleMult, AffixTags);
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

	if (EventTag == ActivationEventTag)
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
			AIController->MoveToActor(AggroActor, AggroSettings.MoveAcceptanceRadius, true, true, true, nullptr, true);
		}
	}
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

	const FEnemyScalingRow* Row = FindScalingRow(EnemySet.EnemyArchetypeTag);
	if (!Row)
	{
		ASC->SetNumericAttributeBase(UAeyerjiAttributeSet::GetLevelAttribute(), static_cast<float>(EnemyLevel));

		if (Enemy)
		{
			Enemy->SetScalingSnapshot(EnemyLevel, DifficultyAlpha, Enemy->GetScalingSourceTag());
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
			}
		}

		float Value = BaseValue;
		Value = (Value * (1.f + Entry.PerLevelMultiplier * LevelDelta)) + (PerLevelAdd * LevelDelta);

		if (Enemy)
		{
			Enemy->ApplyArchetypeStatMultipliers(Entry.AttributeName, Value);
		}

		Value *= GlobalStatBudgetMultiplier;

		const bool bClampMin = !FMath::IsNearlyZero(MinValue);
		const bool bClampMax = !FMath::IsNearlyZero(MaxValue);
		if (bClampMin || bClampMax)
		{
			const float Min = bClampMin ? MinValue : Value;
			const float Max = bClampMax ? MaxValue : Value;
			Value = FMath::Clamp(Value, Min, Max);
		}

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
		Enemy->SetScalingSnapshot(EnemyLevel, DifficultyAlpha, Row->SourceTag);
	}

	RefreshSpawnedPawnStatusBar(SpawnedPawn);
}

const FEnemyScalingRow* AAeyerjiSpawnerGroup::FindScalingRow(const FGameplayTag& ArchetypeTag) const
{
	if (!ArchetypeTag.IsValid() || EnemyScalingTable.IsNull())
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

	if (UDataTable* Table = EnemyScalingTable.LoadSynchronous())
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

	return BestRow;
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
