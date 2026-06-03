#include "Director/AeyerjiWorldDirector.h"

#include "../AeyerjiGameState.h"
#include "AeyerjiGameplayTags.h"
#include "Components/StateTreeComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "StateTree.h"
#include "Systems/AeyerjiWorldStateSubsystem.h"
#include "Systems/AeyerjiStreamingSubsystem.h"
#include "TimerManager.h"

AAeyerjiWorldDirector::AAeyerjiWorldDirector()
{
	PrimaryActorTick.bCanEverTick = false;

	RunDirectorStateTree = CreateDefaultSubobject<UStateTreeComponent>(TEXT("RunDirectorStateTree"));
	RunDirectorStateTree->SetStartLogicAutomatically(false);
	RunDirectorStateTreeAsset = TSoftObjectPtr<UStateTree>(FSoftObjectPath(TEXT("/Game/StateTrees/ST_RunDirector.ST_RunDirector")));

	WatchedRunDirectorTags.AddTag(AeyerjiTags::World_Boss_Map1_Defeated);
}

void AAeyerjiWorldDirector::BeginPlay()
{
	Super::BeginPlay();

	HandleRunDirectorStateTreeBeginPlay();

	if (!bEnterZoneOnBeginPlay)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (World->GetNetMode() == NM_Client)
	{
		return;
	}

	if (bOnlyRunOnAuthority && !HasAuthority())
	{
		return;
	}

	World->GetTimerManager().SetTimerForNextTick(this, &AAeyerjiWorldDirector::ExecuteDeferredStartupFlow);
}

void AAeyerjiWorldDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	UnbindRunDirectorWorldStateEvents();

	if (RunDirectorStateTree)
	{
		RunDirectorStateTree->StopLogic(TEXT("World director end play"));
	}

	Super::EndPlay(EndPlayReason);
}

void AAeyerjiWorldDirector::HandleRunDirectorStateTreeBeginPlay()
{
	if (!RunDirectorStateTree)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (World->GetNetMode() == NM_Client || !HasAuthority())
	{
		RunDirectorStateTree->StopLogic(TEXT("Run director StateTree is server-owned"));
		return;
	}

	if (!AssignRunDirectorStateTreeAsset())
	{
		return;
	}

	BindRunDirectorWorldStateEvents();

	if (bSeedLevelProofState)
	{
		UAeyerjiWorldStateSubsystem::SetWorldStateInt(
			this,
			AeyerjiTags::Run_Level,
			FMath::Max(1, ProofRunLevel),
			NAME_None,
			NAME_None,
			EAeyerjiWorldStatePersistence::RuntimeOnly,
			EAeyerjiWorldStateReplication::ServerOnly,
			EAeyerjiWorldStateScope::Run);
	}

	EvaluateRunDirectorStateTree(FGameplayTag(), TEXT("BeginPlay"));
}

void AAeyerjiWorldDirector::EvaluateRunDirectorStateTree(const FGameplayTag TriggerTag, const FString& Reason)
{
	LastRunDirectorEvaluationTag = TriggerTag;
	LastRunDirectorEvaluationReason = Reason.IsEmpty() ? TEXT("Manual") : Reason;

	if (!RunDirectorStateTree)
	{
		return;
	}

	const UWorld* World = GetWorld();
	if (!World || World->GetNetMode() == NM_Client || !HasAuthority())
	{
		RunDirectorStateTree->StopLogic(TEXT("Run director StateTree is server-owned"));
		return;
	}

	if (bUseLevelProofGate && !ShouldEvaluateRunDirectorLevel60Test())
	{
		return;
	}

	if (bLogRunDirectorEvaluation)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[RunDirector] Evaluate Reason=%s TriggerTag=%s"),
			*LastRunDirectorEvaluationReason,
			*LastRunDirectorEvaluationTag.ToString());
	}

	if (RunDirectorStateTree->IsRunning())
	{
		RunDirectorStateTree->StopLogic(TEXT("Run director throttled evaluation restart"));
	}

	RunDirectorStateTree->StartLogic();
	if (RunDirectorStateTree->IsRunning())
	{
		RunDirectorStateTree->StopLogic(TEXT("Run director event-driven evaluation complete"));
	}

	if (bUseLevelProofGate && !ShouldEvaluateRunDirectorLevel60Test())
	{
		if (bLogRunDirectorEvaluation)
		{
			UE_LOG(LogTemp, Display, TEXT("AeyerjiWorldDirector: Run.Level proof gate fired and gated as done."));
		}
	}
}

bool AAeyerjiWorldDirector::AssignRunDirectorStateTreeAsset()
{
	if (!RunDirectorStateTree)
	{
		return false;
	}

	if (!RunDirectorStateTreeAsset.IsNull())
	{
		UStateTree* StateTreeAsset = RunDirectorStateTreeAsset.LoadSynchronous();
		if (!StateTreeAsset)
		{
			return false;
		}

		RunDirectorStateTree->SetStateTree(StateTreeAsset);
	}

	return true;
}

void AAeyerjiWorldDirector::HandleRunDirectorWorldStateChanged(const FAeyerjiWorldStateEntry& Entry)
{
	if (ShouldEvaluateForWorldStateEntry(Entry))
	{
		EvaluateRunDirectorStateTree(Entry.Key.StateTag, Entry.Key.ToString());
	}
}

bool AAeyerjiWorldDirector::ShouldEvaluateForWorldStateEntry(const FAeyerjiWorldStateEntry& Entry) const
{
	const FGameplayTag& ChangedTag = Entry.Key.StateTag;
	if (!ChangedTag.IsValid())
	{
		return false;
	}

	if (bUseLevelProofGate)
	{
		if (ChangedTag == AeyerjiTags::Run_Level
			|| ChangedTag == AeyerjiTags::Run_Event_SpawnMoreEnemies_Done)
		{
			return true;
		}
	}

	if (WatchedRunDirectorTags.HasTagExact(ChangedTag))
	{
		return true;
	}

	return bEvaluateOnAnyRunWorldState && ChangedTag.ToString().StartsWith(TEXT("Run."));
}

void AAeyerjiWorldDirector::BindRunDirectorWorldStateEvents()
{
	if (RunDirectorWorldStateChangedHandle.IsValid())
	{
		return;
	}

	if (UAeyerjiWorldStateSubsystem* WorldStateSubsystem = UAeyerjiWorldStateSubsystem::Get(this))
	{
		RunDirectorWorldStateChangedHandle = WorldStateSubsystem->OnWorldStateChangedNative.AddUObject(
			this,
			&AAeyerjiWorldDirector::HandleRunDirectorWorldStateChanged);
	}
}

void AAeyerjiWorldDirector::UnbindRunDirectorWorldStateEvents()
{
	if (!RunDirectorWorldStateChangedHandle.IsValid())
	{
		return;
	}

	if (UAeyerjiWorldStateSubsystem* WorldStateSubsystem = UAeyerjiWorldStateSubsystem::Get(this))
	{
		WorldStateSubsystem->OnWorldStateChangedNative.Remove(RunDirectorWorldStateChangedHandle);
	}
	RunDirectorWorldStateChangedHandle.Reset();
}

bool AAeyerjiWorldDirector::ShouldEvaluateRunDirectorLevel60Test() const
{
	const UAeyerjiWorldStateSubsystem* WorldStateSubsystem = UAeyerjiWorldStateSubsystem::Get(this);
	if (!WorldStateSubsystem)
	{
		return false;
	}

	FAeyerjiWorldStateEntry DoneEntry;
	if (WorldStateSubsystem->GetEntry(
		FAeyerjiWorldStateKey(AeyerjiTags::Run_Event_SpawnMoreEnemies_Done, NAME_None, NAME_None),
		DoneEntry))
	{
		return false;
	}

	int32 RunLevel = 0;
	if (!UAeyerjiWorldStateSubsystem::GetWorldStateInt(this, AeyerjiTags::Run_Level, RunLevel))
	{
		if (bLogRunDirectorEvaluation)
		{
			UE_LOG(LogTemp, Verbose, TEXT("AeyerjiWorldDirector: Waiting for Run.Level before evaluating run director StateTree."));
		}
		return false;
	}

	const int32 Threshold = FMath::Max(1, ProofRunLevelThreshold);
	const bool bShouldEvaluate = RunLevel >= Threshold;
	if (bLogRunDirectorEvaluation && !bShouldEvaluate)
	{
		UE_LOG(LogTemp, Verbose, TEXT("AeyerjiWorldDirector: Run.Level=%d is below the proof threshold %d."), RunLevel, Threshold);
	}

	return bShouldEvaluate;
}

void AAeyerjiWorldDirector::ExecuteDeferredStartupFlow()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (World->GetNetMode() == NM_Client)
	{
		return;
	}

	if (bOnlyRunOnAuthority && !HasAuthority())
	{
		return;
	}

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (UAeyerjiStreamingSubsystem* StreamingSubsystem = GameInstance->GetSubsystem<UAeyerjiStreamingSubsystem>())
		{
			FName StartupZoneId = StartZoneId;
			const bool bUsedPendingStartupOverride = StreamingSubsystem->TakePendingStartupZoneOverride(StartupZoneId);
			if (!bUsedPendingStartupOverride
				&& bPreferSavedZone
				&& !StreamingSubsystem->GetCurrentZoneId().IsNone())
			{
				StartupZoneId = StreamingSubsystem->GetCurrentZoneId();
			}

			UE_LOG(LogTemp, Display,
				TEXT("AeyerjiWorldDirector: StartupZone=%s UsedPendingOverride=%d PreferSavedZone=%d"),
				*StartupZoneId.ToString(),
				bUsedPendingStartupOverride ? 1 : 0,
				bPreferSavedZone ? 1 : 0);

			if (StartupZoneId.IsNone())
			{
				FZoneDef DefaultZoneDef;
				if (StreamingSubsystem->GetZoneDefinition(FName(TEXT("Zone.Menu")), DefaultZoneDef))
				{
					StartupZoneId = DefaultZoneDef.ZoneId;
				}
			}

			if (bUseServerWorldFlow)
			{
				if (AAeyerjiGameState* AeyerjiGameState = GetWorld()->GetGameState<AAeyerjiGameState>())
				{
					if (!StartupZoneId.IsNone())
					{
						if (AeyerjiGameState->Server_BeginWorldTransition(StartupZoneId))
						{
							return;
						}
					}
				}
			}

			StreamingSubsystem->EnterStartupZone(StartupZoneId, bPreferSavedZone);
		}
	}
}
