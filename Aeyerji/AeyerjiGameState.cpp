// Copyright (c) 2025 Aeyerji.

#include "AeyerjiGameState.h"

#include "AeyerjiGameplayTags.h"
#include "AeyerjiPlayerController.h"
#include "AeyerjiPlayerState.h"
#include "CharacterStatsLibrary.h"
#include "AIController.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Components/CapsuleComponent.h"
#include "Director/AeyerjiEncounterDirector.h"
#include "Director/AeyerjiLevelDirector.h"
#include "Director/AeyerjiSpawnerGroup.h"
#include "Director/AeyerjiWorldSpawnProfile.h"
#include "Enemy/EnemyParentNative.h"
#include "Inventory/AeyerjiLootPickup.h"
#include "Inventory/AeyerjiInventoryBPFL.h"
#include "Inventory/AeyerjiRewardPresentationActor.h"
#include "World/AeyerjiEndRunPortal.h"
#include "Systems/AeyerjiStreamingManifest.h"
#include "Systems/AeyerjiStreamingSubsystem.h"
#include "Systems/AeyerjiRiftRules.h"
#include "Systems/AeyerjiDifficultyTuning.h"
#include "Systems/AeyerjiWorldStateSubsystem.h"
#include "Frontend/AeyerjiSessionSubsystem.h"
#include "Frontend/AeyerjiFrontendRules.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/Engine.h"
#include "Engine/LevelScriptActor.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Info.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/IConsoleManager.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavigationSystem.h"
#include "Navigation/AeyerjiNavSafetyLibrary.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogAeyerjiWorldFlow, Log, All);

namespace
{
	FAeyerjiRiftRewardLayerDefinition BuildRiftRewardLayer(
		const int32 Drops,
		const int32 Variance,
		const EItemRarity MinimumRarity,
		const FGameplayTag SourceTag)
	{
		FAeyerjiRiftRewardLayerDefinition Layer;
		Layer.SourceTag = SourceTag;
		Layer.MultiDropConfig.TotalBaseDrops = FMath::Max(Drops, 0);
		Layer.MultiDropConfig.TotalVariance = FMath::Max(Variance, 0);
		Layer.MinimumRarity = MinimumRarity;
		return Layer;
	}

	bool FindProjectedPortalSpawnLocation(
		UNavigationSystemV1* NavigationSystem,
		const APawn* PlayerPawn,
		const float SpawnDistance,
		FVector& OutSpawnLocation)
	{
		if (!NavigationSystem || !IsValid(PlayerPawn))
		{
			return false;
		}

		FVector Forward2D = PlayerPawn->GetActorForwardVector().GetSafeNormal2D();
		if (Forward2D.IsNearlyZero())
		{
			Forward2D = FVector::ForwardVector;
		}

		const FVector PawnLocation = PlayerPawn->GetActorLocation();
		const FVector ProjectionExtent(200.f, 200.f, 500.f);
		static const float DirectionAnglesDeg[] = { 0.f, 45.f, -45.f, 90.f, -90.f, 135.f, -135.f, 180.f };

		for (const float DirectionAngleDeg : DirectionAnglesDeg)
		{
			const FVector CandidateDirection = Forward2D.RotateAngleAxis(DirectionAngleDeg, FVector::UpVector).GetSafeNormal();
			if (CandidateDirection.IsNearlyZero())
			{
				continue;
			}

			const FVector CandidateLocation = PawnLocation + (CandidateDirection * SpawnDistance);
			FNavLocation ProjectedLocation;
			if (NavigationSystem->ProjectPointToNavigation(CandidateLocation, ProjectedLocation, ProjectionExtent))
			{
				OutSpawnLocation = ProjectedLocation.Location;
				return true;
			}
		}

		FNavLocation ProjectedPawnLocation;
		if (NavigationSystem->ProjectPointToNavigation(PawnLocation, ProjectedPawnLocation, ProjectionExtent))
		{
			OutSpawnLocation = ProjectedPawnLocation.Location;
			return true;
		}

		return false;
	}

	const TCHAR* RunStateToString(EAeyerjiRunState State)
	{
		switch (State)
		{
		case EAeyerjiRunState::PreRun:
			return TEXT("PreRun");
		case EAeyerjiRunState::InRun:
			return TEXT("InRun");
		case EAeyerjiRunState::BossDefeated:
			return TEXT("BossDefeated");
		case EAeyerjiRunState::ObjectiveComplete:
			return TEXT("ObjectiveComplete");
		case EAeyerjiRunState::RunComplete:
			return TEXT("RunComplete");
		case EAeyerjiRunState::ReturnToMenu:
			return TEXT("ReturnToMenu");
		default:
			return TEXT("Unknown");
		}
	}

	const TCHAR* WorldFlowPhaseToString(const EAeyerjiWorldFlowPhase Phase)
	{
		switch (Phase)
		{
		case EAeyerjiWorldFlowPhase::Menu:
			return TEXT("Menu");
		case EAeyerjiWorldFlowPhase::TransitionLoading:
			return TEXT("TransitionLoading");
		case EAeyerjiWorldFlowPhase::Gameplay:
			return TEXT("Gameplay");
		default:
			return TEXT("Unknown");
		}
	}

	void LogRunDebugForWorld(UWorld* World)
	{
		if (!World)
		{
			UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("aeyerji.Run.Debug failed: no world context."));
			return;
		}

		FString DebugText(TEXT("Aeyerji Run Debug\n"));

		if (const AAeyerjiGameState* GameState = World->GetGameState<AAeyerjiGameState>())
		{
			DebugText += FString::Printf(TEXT("GameState: %s\n"), *GameState->GetRunLifecycleDebugString());
		}
		else
		{
			DebugText += TEXT("GameState: None\n");
		}

		const AAeyerjiLevelDirector* LevelDirector = nullptr;
		for (TActorIterator<AAeyerjiLevelDirector> It(World); It; ++It)
		{
			LevelDirector = *It;
			break;
		}

		DebugText += FString::Printf(TEXT("Level: %s\n"),
			LevelDirector ? *LevelDirector->GetRunDefinitionDebugString() : TEXT("None"));

		const AAeyerjiEncounterDirector* EncounterDirector = nullptr;
		for (TActorIterator<AAeyerjiEncounterDirector> It(World); It; ++It)
		{
			EncounterDirector = *It;
			break;
		}

		DebugText += FString::Printf(TEXT("Encounter: %s\n"),
			EncounterDirector ? *EncounterDirector->GetEncounterDirectorDebugString() : TEXT("None"));

		const AAeyerjiSpawnerGroup* FirstSpawner = nullptr;
		for (TActorIterator<AAeyerjiSpawnerGroup> It(World); It; ++It)
		{
			FirstSpawner = *It;
			break;
		}

		DebugText += FString::Printf(TEXT("FirstSpawner: %s Active=%d Cleared=%d Live=%d\n"),
			*GetNameSafe(FirstSpawner),
			FirstSpawner && FirstSpawner->IsActive() ? 1 : 0,
			FirstSpawner && FirstSpawner->IsCleared() ? 1 : 0,
			FirstSpawner ? FirstSpawner->GetLiveEnemyCount() : 0);

		if (const UAeyerjiWorldStateSubsystem* WorldStateSubsystem = UAeyerjiWorldStateSubsystem::Get(World))
		{
			TArray<FString> RunFacts;
			WorldStateSubsystem->GetRunFactDebugStrings(RunFacts);
			DebugText += TEXT("RunFacts:\n");
			if (RunFacts.Num() == 0)
			{
				DebugText += TEXT("  None\n");
			}
			else
			{
				for (const FString& Fact : RunFacts)
				{
					DebugText += FString::Printf(TEXT("  %s\n"), *Fact);
				}
			}

			TArray<FString> PersistentFacts;
			WorldStateSubsystem->GetPersistentFactDebugStrings(PersistentFacts);
			DebugText += TEXT("PersistentFacts:\n");
			if (PersistentFacts.Num() == 0)
			{
				DebugText += TEXT("  None\n");
			}
			else
			{
				constexpr int32 MaxFactsToPrint = 40;
				for (int32 Index = 0; Index < PersistentFacts.Num() && Index < MaxFactsToPrint; ++Index)
				{
					DebugText += FString::Printf(TEXT("  %s\n"), *PersistentFacts[Index]);
				}

				if (PersistentFacts.Num() > MaxFactsToPrint)
				{
					DebugText += FString::Printf(TEXT("  ... %d more persistent facts\n"), PersistentFacts.Num() - MaxFactsToPrint);
				}
			}
		}

		UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("%s"), *DebugText);
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 8.f, FColor::Cyan, DebugText);
		}
	}

	static FAutoConsoleCommandWithWorld GRunDebugCommand(
		TEXT("aeyerji.Run.Debug"),
		TEXT("Prints current run state, active zone, director ownership, active spawner, and run facts."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&LogRunDebugForWorld));
}

void FAeyerjiReplicatedWorldStateItem::PreReplicatedRemove(const FAeyerjiReplicatedWorldStateArray& InArraySerializer)
{
	if (AAeyerjiGameState* GameState = InArraySerializer.Owner.Get())
	{
		GameState->HandleReplicatedWorldStateEntryRemoved(Entry.Key);
	}
}

void FAeyerjiReplicatedWorldStateItem::PostReplicatedAdd(const FAeyerjiReplicatedWorldStateArray& InArraySerializer)
{
	if (AAeyerjiGameState* GameState = InArraySerializer.Owner.Get())
	{
		GameState->HandleReplicatedWorldStateEntryChanged(Entry);
	}
}

void FAeyerjiReplicatedWorldStateItem::PostReplicatedChange(const FAeyerjiReplicatedWorldStateArray& InArraySerializer)
{
	if (AAeyerjiGameState* GameState = InArraySerializer.Owner.Get())
	{
		GameState->HandleReplicatedWorldStateEntryChanged(Entry);
	}
}

void FAeyerjiReplicatedWorldStateArray::UpsertEntry(const FAeyerjiWorldStateEntry& Entry)
{
	const int32 ExistingIndex = FindIndexByKey(Entry.Key);
	if (ExistingIndex != INDEX_NONE)
	{
		Items[ExistingIndex].Entry = Entry.MakeDataOnlyCopy();
		MarkItemDirty(Items[ExistingIndex]);
		return;
	}

	FAeyerjiReplicatedWorldStateItem& NewItem = Items.AddDefaulted_GetRef();
	NewItem.Entry = Entry.MakeDataOnlyCopy();
	MarkItemDirty(NewItem);
}

void FAeyerjiReplicatedWorldStateArray::RemoveEntry(const FAeyerjiWorldStateKey& Key)
{
	const int32 ExistingIndex = FindIndexByKey(Key);
	if (ExistingIndex == INDEX_NONE)
	{
		return;
	}

	Items.RemoveAt(ExistingIndex);
	MarkArrayDirty();
}

void FAeyerjiReplicatedWorldStateArray::ResetFromEntries(const TArray<FAeyerjiWorldStateEntry>& Entries)
{
	Items.Reset();
	for (const FAeyerjiWorldStateEntry& Entry : Entries)
	{
		if (Entry.Key.IsValid() && Entry.Replication == EAeyerjiWorldStateReplication::PublicReplicated)
		{
			FAeyerjiReplicatedWorldStateItem& NewItem = Items.AddDefaulted_GetRef();
			NewItem.Entry = Entry.MakeDataOnlyCopy();
		}
	}

	MarkArrayDirty();
}

int32 FAeyerjiReplicatedWorldStateArray::FindIndexByKey(const FAeyerjiWorldStateKey& Key) const
{
	for (int32 Index = 0; Index < Items.Num(); ++Index)
	{
		if (Items[Index].Entry.Key == Key)
		{
			return Index;
		}
	}

	return INDEX_NONE;
}

AAeyerjiGameState::AAeyerjiGameState()
{
	bReplicates = true;
	ReplicatedWorldStateEntries.SetOwner(this);
}

void AAeyerjiGameState::BeginPlay()
{
	Super::BeginPlay();

	ReplicatedWorldStateEntries.SetOwner(this);

	if (UAeyerjiStreamingSubsystem* StreamingSubsystem = GetStreamingSubsystem())
	{
		StreamingSubsystem->OnZoneReady.RemoveDynamic(this, &AAeyerjiGameState::HandleStreamingZoneReady);
		StreamingSubsystem->OnZoneReady.AddDynamic(this, &AAeyerjiGameState::HandleStreamingZoneReady);
	}

	if (HasAuthority())
	{
		BindToLevelDirector();

		const bool bTransitionAlreadyQueued = (WorldFlowPhase == EAeyerjiWorldFlowPhase::TransitionLoading)
			|| (TransitionId > 0)
			|| !ActiveZoneId.IsNone();
		if (!bTransitionAlreadyQueued)
		{
			SetRunState(EAeyerjiRunState::PreRun);
			SetWorldFlowPhase(EAeyerjiWorldFlowPhase::Menu);
		}
		else
		{
			UE_LOG(LogAeyerjiWorldFlow, Display,
				TEXT("AAeyerjiGameState: BeginPlay preserving queued world-flow state (Phase=%s TransitionId=%d Zone=%s RunState=%s)."),
				WorldFlowPhaseToString(WorldFlowPhase),
				TransitionId,
				*ActiveZoneId.ToString(),
				RunStateToString(RunState));
		}

		if (UAeyerjiWorldStateSubsystem* WorldStateSubsystem = UAeyerjiWorldStateSubsystem::Get(this))
		{
			WorldStateSubsystem->PublishReplicatedEntriesToGameState(this);
		}

		FAeyerjiPendingRunLaunchRequest PendingLaunch;
		const bool bHasPendingLaunch = GetStreamingSubsystem()
			&& GetStreamingSubsystem()->GetPendingFrontendRunLaunch(PendingLaunch);
		const FString RuntimeMapName = GetWorld() ? GetWorld()->GetMapName() : FString();
		const bool bMenuWorld = !bHasPendingLaunch
			&& (RuntimeMapName.Contains(TEXT("L_MainMenu")) || RuntimeMapName.Contains(TEXT("L_PersistentRoot")));
		if (bMenuWorld)
		{
			ClearFrontendReadiness();
			LobbySnapshot.Phase = EAeyerjiLobbyPhase::Waiting;
			LobbySnapshot.LaunchAtServerTimeSeconds = 0.f;
			if (UGameInstance* GI = GetGameInstance())
			{
				if (UAeyerjiSessionSubsystem* Sessions = GI->GetSubsystem<UAeyerjiSessionSubsystem>())
				{
					LobbySnapshot.ActivityType = Sessions->GetLastPartyActivity();
					LobbySnapshot.SelectedExcursionTier = Sessions->GetLastPartyExcursionTier();
					Sessions->MarkPartyReturnedToLobby();
				}
			}
		}
		else
		{
			LobbySnapshot.Phase = EAeyerjiLobbyPhase::InGameplay;
		}
		RebuildFrontendLobbySnapshot(true);
	}

	HandleReplicatedWorldFlowState();
}

void AAeyerjiGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME_CONDITION_NOTIFY(AAeyerjiGameState, RunState, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(AAeyerjiGameState, RunResults, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(AAeyerjiGameState, RiftRunState, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(AAeyerjiGameState, LobbySnapshot, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(AAeyerjiGameState, CurrentObjectiveState, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(AAeyerjiGameState, CurrentSurvivalRoundState, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(AAeyerjiGameState, CurrentSurvivalUpgradeOfferState, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME(AAeyerjiGameState, LastCompletedObjectiveEvent);
	DOREPLIFETIME_CONDITION_NOTIFY(AAeyerjiGameState, ObjectiveEventVersion, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(AAeyerjiGameState, WorldFlowPhase, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(AAeyerjiGameState, ActiveZoneId, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(AAeyerjiGameState, TransitionId, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(AAeyerjiGameState, PendingWorldFlowLoaderCount, COND_None, REPNOTIFY_Always);
	DOREPLIFETIME(AAeyerjiGameState, ReplicatedWorldStateEntries);
}

void AAeyerjiGameState::OnRep_RunState(EAeyerjiRunState OldState)
{
	HandleRunStateChanged(OldState);
}

void AAeyerjiGameState::OnRep_RunResults()
{
	MaybeBroadcastRunResults();
}

void AAeyerjiGameState::OnRep_RiftRunState()
{
	OnRiftRunStateChanged.Broadcast(RiftRunState);
}

void AAeyerjiGameState::OnRep_LobbySnapshot()
{
	OnLobbySnapshotChangedNative.Broadcast(LobbySnapshot);
}

bool AAeyerjiGameState::IsFrontendLobbyAcceptingConnections() const
{
	return LobbySnapshot.Phase == EAeyerjiLobbyPhase::Waiting && PlayerArray.Num() < 4;
}

void AAeyerjiGameState::Server_NotifyFrontendRosterChanged()
{
	if (HasAuthority())
	{
		RebuildFrontendLobbySnapshot(true);
	}
}

void AAeyerjiGameState::Server_NotifyFrontendProfileChanged(AAeyerjiPlayerState* PlayerState, const bool bRevisionChanged)
{
	if (!HasAuthority() || !PlayerState)
	{
		return;
	}
	if (bRevisionChanged)
	{
		PlayerState->SetFrontendReadyFromServer(false);
	}
	RebuildFrontendLobbySnapshot(false);
}

void AAeyerjiGameState::ClearFrontendReadiness()
{
	if (!HasAuthority())
	{
		return;
	}
	for (APlayerState* PlayerState : PlayerArray)
	{
		if (AAeyerjiPlayerState* Member = Cast<AAeyerjiPlayerState>(PlayerState))
		{
			Member->SetFrontendReadyFromServer(false);
		}
	}
}

void AAeyerjiGameState::RebuildFrontendLobbySnapshot(const bool bDetectRosterChange)
{
	if (!HasAuthority())
	{
		return;
	}

	TArray<AAeyerjiPlayerState*> Members;
	TArray<int32> RosterIds;
	for (APlayerState* PlayerState : PlayerArray)
	{
		if (AAeyerjiPlayerState* Member = Cast<AAeyerjiPlayerState>(PlayerState))
		{
			Members.Add(Member);
			RosterIds.Add(Member->GetPlayerId());
		}
	}
	Members.Sort([](const AAeyerjiPlayerState& Left, const AAeyerjiPlayerState& Right)
	{
		if (Left.GetPlayerId() != Right.GetPlayerId())
		{
			return Left.GetPlayerId() < Right.GetPlayerId();
		}
		return Left.GetPlayerName() < Right.GetPlayerName();
	});
	RosterIds.Sort();
	const int32 NewLeaderId = AeyerjiFrontendRules::ResolveLeaderPlayerId(RosterIds);
	if (bDetectRosterChange && AeyerjiFrontendRules::ShouldResetAllReadiness(
		LastFrontendRosterPlayerIds, RosterIds, LobbySnapshot.LeaderPlayerId, NewLeaderId, false))
	{
		ClearFrontendReadiness();
	}
	LastFrontendRosterPlayerIds = RosterIds;

	LobbySnapshot.LeaderPlayerId = NewLeaderId;
	LobbySnapshot.Members.Reset(Members.Num());
	TArray<int32> VerifiedCaps;
	bool bAllProfilesVerified = !Members.IsEmpty();
	for (AAeyerjiPlayerState* Member : Members)
	{
		FAeyerjiLobbyMemberView& View = LobbySnapshot.Members.AddDefaulted_GetRef();
		View.PlayerId = Member->GetPlayerId();
		View.DisplayName = Member->GetPlayerName();
		View.CharacterLevel = Member->GetFrontendCharacterLevel();
		View.HighestUnlockedExcursionTier = Member->GetFrontendHighestTier();
		View.ProfileState = Member->GetFrontendProfileState();
		View.bReady = Member->IsFrontendReady();
		View.bLeader = View.PlayerId == NewLeaderId;
		if (Member->IsFrontendProfileVerified())
		{
			VerifiedCaps.Add(Member->GetFrontendHighestTier());
		}
		else
		{
			bAllProfilesVerified = false;
		}
	}
	LobbySnapshot.CommonExcursionTierCap = bAllProfilesVerified
		? AeyerjiRiftRules::ResolveCommonTierCap(VerifiedCaps) : 0;
	const int32 PreviousSelectedTier = LobbySnapshot.SelectedExcursionTier;
	if (LobbySnapshot.ActivityType == EAeyerjiRiftActivityType::StandardRift)
	{
		LobbySnapshot.SelectedExcursionTier = 0;
	}
	else if (LobbySnapshot.CommonExcursionTierCap > 0)
	{
		LobbySnapshot.SelectedExcursionTier = FMath::Clamp(
			FMath::Max(LobbySnapshot.SelectedExcursionTier, 1), 1, LobbySnapshot.CommonExcursionTierCap);
	}
	if (LobbySnapshot.SelectedExcursionTier != PreviousSelectedTier && PreviousSelectedTier > 0)
	{
		ClearFrontendReadiness();
		for (FAeyerjiLobbyMemberView& Member : LobbySnapshot.Members)
		{
			Member.bReady = false;
		}
	}
	LobbySnapshot.Revision = FMath::Max(LobbySnapshot.Revision + 1, 1);
	OnRep_LobbySnapshot();
	ForceNetUpdate();
	UE_LOG(LogTemp, Display, TEXT("[Lobby] Revision=%d Phase=%d Members=%d Leader=%d Activity=%d Tier=%d CommonCap=%d"),
		LobbySnapshot.Revision, static_cast<int32>(LobbySnapshot.Phase), LobbySnapshot.Members.Num(),
		LobbySnapshot.LeaderPlayerId, static_cast<int32>(LobbySnapshot.ActivityType),
		LobbySnapshot.SelectedExcursionTier, LobbySnapshot.CommonExcursionTierCap);
}

bool AAeyerjiGameState::Server_SetFrontendReady(AAeyerjiPlayerState* Requester, const bool bReady)
{
	if (!HasAuthority() || !Requester || LobbySnapshot.Phase != EAeyerjiLobbyPhase::Waiting
		|| (bReady && !Requester->IsFrontendProfileVerified()))
	{
		UE_LOG(LogTemp, Warning, TEXT("[Lobby] ReadyRejected Player=%s Ready=%d Phase=%d Profile=%d"),
			*GetNameSafe(Requester), bReady, static_cast<int32>(LobbySnapshot.Phase),
			Requester ? static_cast<int32>(Requester->GetFrontendProfileState()) : -1);
		return false;
	}
	Requester->SetFrontendReadyFromServer(bReady);
	RebuildFrontendLobbySnapshot(false);
	return true;
}

bool AAeyerjiGameState::Server_SetFrontendActivity(AAeyerjiPlayerState* Requester, const EAeyerjiRiftActivityType ActivityType)
{
	if (!HasAuthority() || !Requester || LobbySnapshot.Phase != EAeyerjiLobbyPhase::Waiting
		|| Requester->GetPlayerId() != LobbySnapshot.LeaderPlayerId)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Lobby] ActivityRejected Player=%s Reason=NotLeaderOrWrongPhase"), *GetNameSafe(Requester));
		return false;
	}
	if (LobbySnapshot.ActivityType == ActivityType)
	{
		return true;
	}
	LobbySnapshot.ActivityType = ActivityType;
	LobbySnapshot.SelectedExcursionTier = ActivityType == EAeyerjiRiftActivityType::Excursion ? 1 : 0;
	ClearFrontendReadiness();
	RebuildFrontendLobbySnapshot(false);
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAeyerjiSessionSubsystem* Sessions = GI->GetSubsystem<UAeyerjiSessionSubsystem>())
		{
			Sessions->UpdateWaitingPartySelection(LobbySnapshot.ActivityType, LobbySnapshot.SelectedExcursionTier);
		}
	}
	return true;
}

const FAeyerjiRiftTierRow* AAeyerjiGameState::FindFrontendTierRow(const int32 Tier) const
{
	const UDataTable* TierTable = UAeyerjiDifficultySettings::GetRiftTierTable();
	return TierTable && Tier > 0
		? TierTable->FindRow<FAeyerjiRiftTierRow>(FName(*FString::Printf(TEXT("Tier_%d"), Tier)), TEXT("Frontend lobby tier lookup"), false)
		: nullptr;
}

bool AAeyerjiGameState::Server_SetFrontendTier(AAeyerjiPlayerState* Requester, const int32 Tier)
{
	if (!HasAuthority() || !Requester || LobbySnapshot.Phase != EAeyerjiLobbyPhase::Waiting
		|| LobbySnapshot.ActivityType != EAeyerjiRiftActivityType::Excursion
		|| Requester->GetPlayerId() != LobbySnapshot.LeaderPlayerId)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Lobby] TierRejected Player=%s Tier=%d Reason=NotLeaderActivityOrPhase"), *GetNameSafe(Requester), Tier);
		return false;
	}
	if (!FindFrontendTierRow(Tier) || Tier > LobbySnapshot.CommonExcursionTierCap)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Lobby] TierRejected Player=%s Tier=%d Reason=UndefinedOrAboveCommonCap CommonCap=%d"),
			*GetNameSafe(Requester), Tier, LobbySnapshot.CommonExcursionTierCap);
		return false;
	}
	if (LobbySnapshot.SelectedExcursionTier == Tier)
	{
		return true;
	}
	LobbySnapshot.SelectedExcursionTier = Tier;
	ClearFrontendReadiness();
	RebuildFrontendLobbySnapshot(false);
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAeyerjiSessionSubsystem* Sessions = GI->GetSubsystem<UAeyerjiSessionSubsystem>())
		{
			Sessions->UpdateWaitingPartySelection(LobbySnapshot.ActivityType, LobbySnapshot.SelectedExcursionTier);
		}
	}
	return true;
}

bool AAeyerjiGameState::Server_RequestFrontendLaunch(AAeyerjiPlayerState* Requester)
{
	if (!HasAuthority() || !Requester || LobbySnapshot.Phase != EAeyerjiLobbyPhase::Waiting
		|| Requester->GetPlayerId() != LobbySnapshot.LeaderPlayerId)
	{
		UE_LOG(LogTemp, Warning, TEXT("[LobbyLaunch] Rejected Player=%s Reason=NotLeaderOrWrongPhase"), *GetNameSafe(Requester));
		return false;
	}
	const int32 Tier = LobbySnapshot.ActivityType == EAeyerjiRiftActivityType::Excursion
		? LobbySnapshot.SelectedExcursionTier : 0;
	const FAeyerjiRiftTierRow* TierRow = LobbySnapshot.ActivityType == EAeyerjiRiftActivityType::Excursion
		? FindFrontendTierRow(Tier) : nullptr;
	const EAeyerjiFrontendFailure ValidationFailure = AeyerjiFrontendRules::ValidateLaunch(
		LobbySnapshot, Requester->GetPlayerId(), TierRow);
	if (ValidationFailure != EAeyerjiFrontendFailure::None)
	{
		UE_LOG(LogTemp, Warning, TEXT("[LobbyLaunch] Rejected Player=%s Failure=%d Activity=%d Tier=%d CommonCap=%d"),
			*GetNameSafe(Requester), static_cast<int32>(ValidationFailure),
			static_cast<int32>(LobbySnapshot.ActivityType), Tier, LobbySnapshot.CommonExcursionTierCap);
		return false;
	}

	UAeyerjiStreamingSubsystem* Streaming = GetStreamingSubsystem();
	FAeyerjiPendingRunLaunchRequest Request;
	if (!Streaming || !Streaming->PrepareFrontendRunLaunch(LobbySnapshot.ActivityType, Tier, Request))
	{
		UE_LOG(LogTemp, Warning, TEXT("[LobbyLaunch] Rejected Reason=MapSelectionFailed Activity=%d Tier=%d"),
			static_cast<int32>(LobbySnapshot.ActivityType), Tier);
		return false;
	}

	LobbySnapshot.Phase = EAeyerjiLobbyPhase::Launching;
	LobbySnapshot.LaunchAtServerTimeSeconds = GetServerWorldTimeSeconds() + FMath::Max(0.f, FrontendLaunchCountdownSeconds);
	RebuildFrontendLobbySnapshot(false);
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAeyerjiSessionSubsystem* Sessions = GI->GetSubsystem<UAeyerjiSessionSubsystem>())
		{
			Sessions->MarkPartyLaunching(LobbySnapshot.ActivityType, Tier, Request.MapId);
		}
	}
	UE_LOG(LogTemp, Display, TEXT("[LobbyLaunch] Accepted RequestId=%d Activity=%d Tier=%d Map=%s Countdown=%.2f"),
		Request.RequestId, static_cast<int32>(Request.ActivityType), Request.ExcursionTier,
		*Request.MapPackageName.ToString(), FrontendLaunchCountdownSeconds);
	if (FrontendLaunchCountdownSeconds <= 0.f)
	{
		HandleFrontendLaunchCountdownElapsed();
	}
	else if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(FrontendLaunchCountdownHandle, this,
			&ThisClass::HandleFrontendLaunchCountdownElapsed, FrontendLaunchCountdownSeconds, false);
	}
	return true;
}

void AAeyerjiGameState::HandleFrontendLaunchCountdownElapsed()
{
	if (!HasAuthority() || LobbySnapshot.Phase != EAeyerjiLobbyPhase::Launching)
	{
		return;
	}
	UAeyerjiStreamingSubsystem* Streaming = GetStreamingSubsystem();
	if (!Streaming || !Streaming->ExecutePendingFrontendRunLaunch())
	{
		LobbySnapshot.Phase = EAeyerjiLobbyPhase::Waiting;
		LobbySnapshot.LaunchAtServerTimeSeconds = 0.f;
		ClearFrontendReadiness();
		RebuildFrontendLobbySnapshot(false);
		UE_LOG(LogTemp, Error, TEXT("[LobbyLaunch] TravelFailed"));
	}
}

float AAeyerjiGameState::GetAuthoritativeRunElapsedSeconds() const
{
	if (RiftRunState.RunSerial <= 0 || RiftRunState.StartServerTimeSeconds <= 0.f)
	{
		return 0.f;
	}

	return FMath::Max(GetServerWorldTimeSeconds() - RiftRunState.StartServerTimeSeconds, 0.f);
}

float AAeyerjiGameState::GetAuthoritativeRunRemainingSeconds() const
{
	return FMath::Max(RiftRunState.TimeLimitSeconds - GetAuthoritativeRunElapsedSeconds(), 0.f);
}

FString AAeyerjiGameState::GetRunLifecycleDebugString() const
{
	FString WorldStateSummary(TEXT("WorldStateUnavailable"));
	if (const UAeyerjiWorldStateSubsystem* WorldStateSubsystem = UAeyerjiWorldStateSubsystem::Get(this))
	{
		WorldStateSummary = WorldStateSubsystem->GetWorldStateDebugSummary();
	}

	return FString::Printf(
		TEXT("RunState=%s Result=%d ResultsVersion=%d Zone=%s TransitionId=%d ObjectiveReady=%d SurvivalRound=%d Cycle=%d Phase=%d BossRound=%d %s"),
		RunStateToString(RunState),
		static_cast<int32>(RunResults.Resolution),
		RunResults.ResultsVersion,
		*ActiveZoneId.ToString(),
		TransitionId,
		CurrentObjectiveState.bObjectiveReady ? 1 : 0,
		CurrentSurvivalRoundState.RoundNumber,
		CurrentSurvivalRoundState.CycleNumber,
		static_cast<int32>(CurrentSurvivalRoundState.Phase),
		CurrentSurvivalRoundState.bBossRound ? 1 : 0,
		*WorldStateSummary);
}

void AAeyerjiGameState::OnRep_CurrentObjectiveState()
{
	BroadcastCurrentObjectiveState();
}

void AAeyerjiGameState::OnRep_CurrentSurvivalRoundState()
{
	BroadcastCurrentSurvivalRoundState();
}

void AAeyerjiGameState::OnRep_CurrentSurvivalUpgradeOfferState()
{
	BroadcastCurrentSurvivalUpgradeOfferState();
}

void AAeyerjiGameState::OnRep_ObjectiveEventVersion()
{
	if (LastCompletedObjectiveEvent != EAeyerjiObjectiveEvent::None)
	{
		OnObjectiveEventCompleted.Broadcast(LastCompletedObjectiveEvent);
	}
}

void AAeyerjiGameState::OnRep_WorldFlowPhase(const EAeyerjiWorldFlowPhase OldPhase)
{
	OnWorldFlowPhaseChanged.Broadcast(WorldFlowPhase, OldPhase, TransitionId);
	HandleReplicatedWorldFlowState();
	MaybeBroadcastZoneGameplayReady();
}

void AAeyerjiGameState::OnRep_ActiveZoneId()
{
	HandleReplicatedWorldFlowState();
	MaybeBroadcastZoneGameplayReady();
}

void AAeyerjiGameState::OnRep_TransitionId()
{
	HandleReplicatedWorldFlowState();
	MaybeBroadcastZoneGameplayReady();
}

void AAeyerjiGameState::OnRep_PendingWorldFlowLoaderCount()
{
	OnWorldFlowLoadingStateChanged.Broadcast(PendingWorldFlowLoaderCount, TransitionId);
}

void AAeyerjiGameState::SetObjectiveStateFromServer(const FAeyerjiObjectiveState& NewState)
{
	if (!HasAuthority())
	{
		return;
	}

	FAeyerjiObjectiveState MutatedState = NewState;
	MutatedState.ObjectiveRevision = FMath::Max(CurrentObjectiveState.ObjectiveRevision + 1, 1);
	CurrentObjectiveState = MutatedState;
	BroadcastCurrentObjectiveState();
	ForceNetUpdate();
}

void AAeyerjiGameState::ClearObjectiveStateFromServer()
{
	if (!HasAuthority())
	{
		return;
	}

	FAeyerjiObjectiveState ClearedState;
	ClearedState.ObjectiveRevision = FMath::Max(CurrentObjectiveState.ObjectiveRevision + 1, 1);
	CurrentObjectiveState = ClearedState;
	BroadcastCurrentObjectiveState();
	ForceNetUpdate();
}

void AAeyerjiGameState::SetSurvivalRoundStateFromServer(const FAeyerjiSurvivalRoundState& NewState)
{
	if (!HasAuthority())
	{
		return;
	}

	FAeyerjiSurvivalRoundState MutatedState = NewState;
	MutatedState.Revision = FMath::Max(CurrentSurvivalRoundState.Revision + 1, 1);
	CurrentSurvivalRoundState = MutatedState;
	BroadcastCurrentSurvivalRoundState();
	ForceNetUpdate();
}

void AAeyerjiGameState::ClearSurvivalRoundStateFromServer()
{
	if (!HasAuthority())
	{
		return;
	}

	FAeyerjiSurvivalRoundState ClearedState;
	ClearedState.Revision = FMath::Max(CurrentSurvivalRoundState.Revision + 1, 1);
	CurrentSurvivalRoundState = ClearedState;
	BroadcastCurrentSurvivalRoundState();
	ForceNetUpdate();
}

void AAeyerjiGameState::SetSurvivalUpgradeOfferStateFromServer(const FAeyerjiSurvivalUpgradeOfferState& NewState)
{
	if (!HasAuthority())
	{
		return;
	}

	CurrentSurvivalUpgradeOfferState = NewState;
	BroadcastCurrentSurvivalUpgradeOfferState();
	ForceNetUpdate();
}

void AAeyerjiGameState::ClearSurvivalUpgradeOfferStateFromServer()
{
	if (!HasAuthority())
	{
		return;
	}

	FAeyerjiSurvivalUpgradeOfferState ClearedState;
	ClearedState.Revision = FMath::Max(CurrentSurvivalUpgradeOfferState.Revision + 1, 1);
	CurrentSurvivalUpgradeOfferState = ClearedState;
	BroadcastCurrentSurvivalUpgradeOfferState();
	ForceNetUpdate();
}

void AAeyerjiGameState::PublishWorldStateEntryFromServer(const FAeyerjiWorldStateEntry& Entry)
{
	if (!HasAuthority() || Entry.Replication != EAeyerjiWorldStateReplication::PublicReplicated)
	{
		return;
	}

	ReplicatedWorldStateEntries.UpsertEntry(Entry);
	ForceNetUpdate();
}

void AAeyerjiGameState::RemoveWorldStateEntryFromServer(const FAeyerjiWorldStateKey& Key)
{
	if (!HasAuthority())
	{
		return;
	}

	ReplicatedWorldStateEntries.RemoveEntry(Key);
	ForceNetUpdate();
}

void AAeyerjiGameState::RepublishWorldStateFromServer(const TArray<FAeyerjiWorldStateEntry>& Entries)
{
	if (!HasAuthority())
	{
		return;
	}

	ReplicatedWorldStateEntries.ResetFromEntries(Entries);
	ForceNetUpdate();
}

void AAeyerjiGameState::HandleReplicatedWorldStateEntryChanged(const FAeyerjiWorldStateEntry& Entry)
{
	if (UAeyerjiWorldStateSubsystem* WorldStateSubsystem = UAeyerjiWorldStateSubsystem::Get(this))
	{
		WorldStateSubsystem->ApplyReplicatedEntry(Entry);
	}
}

void AAeyerjiGameState::HandleReplicatedWorldStateEntryRemoved(const FAeyerjiWorldStateKey& Key)
{
	if (UAeyerjiWorldStateSubsystem* WorldStateSubsystem = UAeyerjiWorldStateSubsystem::Get(this))
	{
		WorldStateSubsystem->RemoveReplicatedEntry(Key);
	}
}

void AAeyerjiGameState::BroadcastCurrentObjectiveState()
{
	OnObjectiveStateChangedNative.Broadcast(CurrentObjectiveState);
	OnObjectiveStateChanged.Broadcast(CurrentObjectiveState);
}

void AAeyerjiGameState::BroadcastCurrentSurvivalRoundState()
{
	OnSurvivalRoundStateChanged.Broadcast(CurrentSurvivalRoundState);
	if (!CurrentSurvivalRoundState.MessageKey.IsNone())
	{
		OnSurvivalRoundMessage.Broadcast(CurrentSurvivalRoundState.MessageKey);
	}
}

void AAeyerjiGameState::BroadcastCurrentSurvivalUpgradeOfferState()
{
	OnSurvivalUpgradeOfferChanged.Broadcast(CurrentSurvivalUpgradeOfferState);
}

void AAeyerjiGameState::SetPendingWorldFlowLoaderCount(const int32 NewPendingCount)
{
	const int32 ClampedPendingCount = FMath::Max(0, NewPendingCount);
	if (PendingWorldFlowLoaderCount == ClampedPendingCount)
	{
		return;
	}

	PendingWorldFlowLoaderCount = ClampedPendingCount;
	OnWorldFlowLoadingStateChanged.Broadcast(PendingWorldFlowLoaderCount, TransitionId);

	if (HasAuthority())
	{
		ForceNetUpdate();
	}
}

void AAeyerjiGameState::ClearWorldFlowLoadingRequirements()
{
	if (AAeyerjiEncounterDirector* EncounterDirector = CachedLoadingEncounterDirector.Get())
	{
		EncounterDirector->OnFixedPopulationInitialSpawnComplete.RemoveDynamic(this, &AAeyerjiGameState::HandleEncounterDirectorInitialSpawnComplete);
	}

	CachedLoadingEncounterDirector.Reset();
	LastPreparedWorldFlowLoadingTransitionId = 0;
	SetPendingWorldFlowLoaderCount(0);
}

void AAeyerjiGameState::PrepareWorldFlowLoadingRequirements()
{
	if (!HasAuthority() || !bServerZoneReady || WorldFlowPhase != EAeyerjiWorldFlowPhase::TransitionLoading || TransitionId <= 0)
	{
		return;
	}

	if (LastPreparedWorldFlowLoadingTransitionId == TransitionId)
	{
		return;
	}

	SetPendingWorldFlowLoaderCount(0);

	if (!ResolveGameplayActorsForActiveZone())
	{
		return;
	}

	AAeyerjiLevelDirector* LevelDirector = CachedLevelDirector.Get();
	if (!IsValid(LevelDirector) || LevelDirector->SpawnMode != EAeyerjiLevelSpawnMode::FixedWorldPopulation)
	{
		LastPreparedWorldFlowLoadingTransitionId = TransitionId;
		return;
	}

	AAeyerjiEncounterDirector* EncounterDirector = LevelDirector->GetEncounterDirector();
	if (!IsValid(EncounterDirector))
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning,
			TEXT("AAeyerjiGameState: Fixed-world loading gate skipped for Zone=%s because no EncounterDirector was found."),
			*ActiveZoneId.ToString());
		LastPreparedWorldFlowLoadingTransitionId = TransitionId;
		return;
	}

	if (CachedLoadingEncounterDirector.Get() != EncounterDirector)
	{
		if (AAeyerjiEncounterDirector* PreviousEncounterDirector = CachedLoadingEncounterDirector.Get())
		{
			PreviousEncounterDirector->OnFixedPopulationInitialSpawnComplete.RemoveDynamic(this, &AAeyerjiGameState::HandleEncounterDirectorInitialSpawnComplete);
		}

		CachedLoadingEncounterDirector = EncounterDirector;
		EncounterDirector->OnFixedPopulationInitialSpawnComplete.RemoveDynamic(this, &AAeyerjiGameState::HandleEncounterDirectorInitialSpawnComplete);
		EncounterDirector->OnFixedPopulationInitialSpawnComplete.AddDynamic(this, &AAeyerjiGameState::HandleEncounterDirectorInitialSpawnComplete);
	}

	if (!EncounterDirector->IsFixedWorldPopulationActive())
	{
		const bool bStartedFixedPopulation = EncounterDirector->StartFixedWorldPopulation(
			LevelDirector->WorldSpawnProfile,
			LevelDirector->WorldPopulationSpawner,
			LevelDirector);
		UE_LOG(LogAeyerjiWorldFlow, Display,
			TEXT("AAeyerjiGameState: Pre-spawn fixed population for Zone=%s TransitionId=%d Result=%s"),
			*ActiveZoneId.ToString(),
			TransitionId,
			bStartedFixedPopulation ? TEXT("started") : TEXT("skipped"));
	}

	if (EncounterDirector->IsFixedWorldPopulationActive() && !EncounterDirector->IsFixedWorldPopulationInitialSpawnComplete())
	{
		SetPendingWorldFlowLoaderCount(1);
		UE_LOG(LogAeyerjiWorldFlow, Display,
			TEXT("AAeyerjiGameState: Waiting on EncounterDirector=%s initial spawn (Remaining=%d TransitionId=%d)"),
			*GetNameSafe(EncounterDirector),
			EncounterDirector->GetFixedPopulationRemainingToSpawn(),
			TransitionId);
	}

	LastPreparedWorldFlowLoadingTransitionId = TransitionId;
}

void AAeyerjiGameState::ElectRunLeaderFromCurrentPlayers()
{
	RunLeaderPlayerState.Reset();
	AAeyerjiPlayerState* BestPlayerState = nullptr;
	int32 BestPlayerId = MAX_int32;
	for (APlayerState* PlayerState : PlayerArray)
	{
		AAeyerjiPlayerState* Candidate = Cast<AAeyerjiPlayerState>(PlayerState);
		if (!IsValid(Candidate) || !Candidate->IsProfileLoadApplied())
		{
			continue;
		}

		const int32 CandidateId = Candidate->GetPlayerId() >= 0 ? Candidate->GetPlayerId() : MAX_int32 - 1;
		if (!BestPlayerState || CandidateId < BestPlayerId
			|| (CandidateId == BestPlayerId && Candidate->GetName().Compare(BestPlayerState->GetName()) < 0))
		{
			BestPlayerState = Candidate;
			BestPlayerId = CandidateId;
		}
	}

	RunLeaderPlayerState = BestPlayerState;
}

bool AAeyerjiGameState::SnapshotRunParticipantsAndElectLeader(FString& OutReason)
{
	OutReason.Reset();
	RunParticipants.Reset();
	ElectRunLeaderFromCurrentPlayers();

	for (APlayerState* PlayerState : PlayerArray)
	{
		AAeyerjiPlayerState* Participant = Cast<AAeyerjiPlayerState>(PlayerState);
		if (!IsValid(Participant))
		{
			OutReason = FString::Printf(TEXT("Participating PlayerState %s is not AAeyerjiPlayerState"), *GetNameSafe(PlayerState));
			return false;
		}

		if (!Participant->IsProfileLoadApplied())
		{
			OutReason = FString::Printf(TEXT("Profile not applied for %s"), *GetNameSafe(Participant));
			return false;
		}

		APawn* Pawn = Participant->GetPawn();
		if (!IsValid(Pawn) || !IsValid(Pawn->GetController()) || Pawn->GetController()->PlayerState != Participant)
		{
			OutReason = FString::Printf(TEXT("Possessed pawn not ready for %s"), *GetNameSafe(Participant));
			return false;
		}

		RunParticipants.Add(Participant);
	}

	if (RunParticipants.IsEmpty() || !RunLeaderPlayerState.IsValid())
	{
		OutReason = TEXT("No loaded run participants or leader");
		return false;
	}

	RunParticipants.Sort([](const TWeakObjectPtr<AAeyerjiPlayerState>& Left, const TWeakObjectPtr<AAeyerjiPlayerState>& Right)
	{
		const AAeyerjiPlayerState* LeftPlayer = Left.Get();
		const AAeyerjiPlayerState* RightPlayer = Right.Get();
		if (!LeftPlayer || !RightPlayer)
		{
			return LeftPlayer != nullptr;
		}
		if (LeftPlayer->GetPlayerId() != RightPlayer->GetPlayerId())
		{
			return LeftPlayer->GetPlayerId() < RightPlayer->GetPlayerId();
		}
		return LeftPlayer->GetName() < RightPlayer->GetName();
	});

	TArray<int32> HighestUnlockedTiers;
	HighestUnlockedTiers.Reserve(RunParticipants.Num());
	for (const TWeakObjectPtr<AAeyerjiPlayerState>& ParticipantPtr : RunParticipants)
	{
		if (const AAeyerjiPlayerState* Participant = ParticipantPtr.Get())
		{
			HighestUnlockedTiers.Add(Participant->GetHighestUnlockedRiftTier());
		}
	}
	const int32 CommonCap = AeyerjiRiftRules::ResolveCommonTierCap(HighestUnlockedTiers);
	if (CommonCap <= 0)
	{
		OutReason = TEXT("Participant Rift Tier progression is invalid");
		return false;
	}

	FAeyerjiPendingRunLaunchRequest LaunchRequest;
	if (UAeyerjiStreamingSubsystem* Streaming = GetStreamingSubsystem();
		Streaming && Streaming->GetPendingFrontendRunLaunch(LaunchRequest))
	{
		if (LaunchRequest.ActivityType == EAeyerjiRiftActivityType::Excursion
			&& (LaunchRequest.ExcursionTier <= 0 || LaunchRequest.ExcursionTier > CommonCap))
		{
			OutReason = FString::Printf(TEXT("Launch request Tier_%d exceeds hydrated party cap %d"),
				LaunchRequest.ExcursionTier, CommonCap);
			return false;
		}
		PendingSelectedRiftTier = LaunchRequest.ActivityType == EAeyerjiRiftActivityType::Excursion
			? LaunchRequest.ExcursionTier : 1;
	}
	else
	{
		// Legacy/direct-map fallback restores the leader's save-backed selection.
		PendingSelectedRiftTier = FMath::Clamp(
			RunLeaderPlayerState->GetLastSelectedRiftTier(), 1, CommonCap);
	}
	return true;
}

bool AAeyerjiGameState::ValidateRunStartReadiness(FString& OutReason) const
{
	OutReason.Reset();
	if (!HasAuthority())
	{
		OutReason = TEXT("GameState is not authoritative");
		return false;
	}
	if (WorldFlowPhase != EAeyerjiWorldFlowPhase::Gameplay)
	{
		OutReason = TEXT("World flow has not reached Gameplay");
		return false;
	}
	if (RunState != EAeyerjiRunState::PreRun)
	{
		OutReason = FString::Printf(TEXT("RunState is %s, not PreRun"), RunStateToString(RunState));
		return false;
	}

	AAeyerjiLevelDirector* LevelDirector = CachedLevelDirector.Get();
	if (!IsValid(LevelDirector))
	{
		OutReason = TEXT("LevelDirector is missing");
		return false;
	}

	// The director owns the placed encounter/boss endpoint validation because those actors
	// may be Blueprint subclasses loaded from the streamed gameplay level.
	if (!const_cast<AAeyerjiLevelDirector*>(LevelDirector)->ValidateRunStartReadiness(OutReason))
	{
		return false;
	}

	if (PlayerArray.IsEmpty())
	{
		OutReason = TEXT("No participating PlayerStates");
		return false;
	}
	for (APlayerState* PlayerState : PlayerArray)
	{
		const AAeyerjiPlayerState* Participant = Cast<AAeyerjiPlayerState>(PlayerState);
		if (!Participant || !Participant->IsProfileLoadApplied())
		{
			OutReason = FString::Printf(TEXT("Profile not ready for %s"), *GetNameSafe(PlayerState));
			return false;
		}
		const APawn* Pawn = Participant->GetPawn();
		if (!IsValid(Pawn) || !IsValid(Pawn->GetController()))
		{
			OutReason = FString::Printf(TEXT("Possessed pawn not ready for %s"), *GetNameSafe(Participant));
			return false;
		}
	}

	return true;
}

bool AAeyerjiGameState::FreezeRiftConfigurationForNewRun(const FAeyerjiRiftTierRow* TierRow, FString& OutReason)
{
	OutReason.Reset();
	AAeyerjiLevelDirector* LevelDirector = CachedLevelDirector.Get();
	EAeyerjiRiftActivityType ActivityType = LevelDirector
		? LevelDirector->GetRiftActivityType()
		: EAeyerjiRiftActivityType::StandardRift;
	const TCHAR* ActivitySource = LevelDirector ? TEXT("ZoneFallback") : TEXT("LegacyFallback");
	int32 LaunchRequestId = 0;
	if (UAeyerjiStreamingSubsystem* StreamingSubsystem = UAeyerjiStreamingSubsystem::GetStreamingSubsystem(this))
	{
		FAeyerjiPendingRunLaunchRequest LaunchRequest;
		if (StreamingSubsystem->GetPendingFrontendRunLaunch(LaunchRequest))
		{
			ActivityType = LaunchRequest.ActivityType;
			PendingSelectedRiftTier = LaunchRequest.ActivityType == EAeyerjiRiftActivityType::Excursion
				? LaunchRequest.ExcursionTier : 1;
			LaunchRequestId = LaunchRequest.RequestId;
			ActivitySource = TEXT("FrontendLobbyRequest");
		}
		// Main-menu Play commits Campaign/Excursion into the persistent streaming
		// session. Prefer that server-owned handoff over an editor default whenever a
		// gameplay session was actually launched through the menu.
		else if (!StreamingSubsystem->GetCurrentGameplayMapId().IsNone())
		{
			ActivityType = StreamingSubsystem->IsCampaignModeEnabled()
				? EAeyerjiRiftActivityType::StandardRift
				: EAeyerjiRiftActivityType::Excursion;
			ActivitySource = StreamingSubsystem->IsCampaignModeEnabled()
				? TEXT("MainMenuCampaign")
				: TEXT("MainMenuExcursion");
		}
	}
	if (ActivityType == EAeyerjiRiftActivityType::Excursion && !TierRow)
	{
		OutReason = FString::Printf(TEXT("Excursion Tier_%d is not defined"), FMath::Max(PendingSelectedRiftTier, 1));
		return false;
	}

	int32 HighestParticipantLevel = 1;
	for (const TWeakObjectPtr<AAeyerjiPlayerState>& ParticipantPtr : RunParticipants)
	{
		const AAeyerjiPlayerState* Participant = ParticipantPtr.Get();
		const APawn* Pawn = Participant ? Participant->GetPawn() : nullptr;
		const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn, true);
		const UAeyerjiAttributeSet* Attributes = ASC ? ASC->GetSet<UAeyerjiAttributeSet>() : nullptr;
		const int32 CharacterLevel = Attributes
			? UAeyerjiDifficultySettings::ClampGameplayLevel(FMath::RoundToInt(Attributes->GetLevel()))
			: 1;
		if (ActivityType == EAeyerjiRiftActivityType::Excursion
			&& CharacterLevel < FMath::Max(TierRow->MinimumCharacterLevel, 1))
		{
			OutReason = FString::Printf(TEXT("%s is Character Level %d; Tier_%d requires Character Level %d"),
				*GetNameSafe(Participant), CharacterLevel, FMath::Max(PendingSelectedRiftTier, 1),
				FMath::Max(TierRow->MinimumCharacterLevel, 1));
			return false;
		}
		HighestParticipantLevel = FMath::Max(HighestParticipantLevel, CharacterLevel);
	}

	RiftRewardLedger.Reset();
	bHasFrozenRiftRewardConfiguration = false;
	FrozenRiftBaseReward = FAeyerjiRiftRewardLayerDefinition();
	FrozenRiftTimedReward = FAeyerjiRiftRewardLayerDefinition();
	FrozenRiftFlawlessReward = FAeyerjiRiftRewardLayerDefinition();
	FrozenRiftBonusRewardPresentationClass = nullptr;
	if (RiftBonusRewardCache.IsValid())
	{
		RiftBonusRewardCache->Destroy();
	}
	RiftBonusRewardCache.Reset();

	FAeyerjiRiftRunState NewState;
	NewState.RunSerial = NextRunSerial++;
	NewState.RunSeed = FixedRiftRunSeed > 0 ? FixedRiftRunSeed : FMath::RandRange(1, MAX_int32 - 1);
	NewState.Activity.ActivityType = ActivityType;
	NewState.Activity.ExcursionTier = ActivityType == EAeyerjiRiftActivityType::Excursion
		? FMath::Max(PendingSelectedRiftTier, 1)
		: 0;
	NewState.Activity.ActivityLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(
		ActivityType == EAeyerjiRiftActivityType::Excursion
			? FMath::Min(HighestParticipantLevel, FMath::Max(TierRow->MaxActivityLevel, 1))
			: HighestParticipantLevel);
	NewState.SelectedRiftTier = NewState.Activity.ExcursionTier;

	if (ActivityType == EAeyerjiRiftActivityType::Excursion && TierRow)
	{
		bHasFrozenRiftRewardConfiguration = true;
		FrozenRiftBaseReward = BuildRiftRewardLayer(
			TierRow->BaseRewardDrops, TierRow->BaseRewardVariance,
			TierRow->BaseMinimumRarity, AeyerjiTags::Loot_Source_RiftBase);
		FrozenRiftTimedReward = BuildRiftRewardLayer(
			TierRow->TimedRewardDrops, TierRow->TimedRewardVariance,
			TierRow->TimedMinimumRarity, AeyerjiTags::Loot_Source_RiftTimed);
		FrozenRiftFlawlessReward = BuildRiftRewardLayer(
			TierRow->FlawlessRewardDrops, TierRow->FlawlessRewardVariance,
			TierRow->FlawlessMinimumRarity, AeyerjiTags::Loot_Source_RiftFlawless);
		FrozenRiftBonusRewardPresentationClass = TierRow->BonusRewardPresentationClass.LoadSynchronous();
		NewState.MonsterPower.MonsterPowerIndex = NewState.Activity.ExcursionTier;
		NewState.MonsterPower.HealthMultiplier = FMath::Max(TierRow->HealthMultiplier, 0.f);
		NewState.MonsterPower.DamageMultiplier = FMath::Max(TierRow->DamageMultiplier, 0.f);
		NewState.MonsterPower.DefenseMultiplier = FMath::Max(TierRow->DefenseMultiplier, 0.f);
		NewState.MonsterPower.RewardQualityMultiplier = FMath::Max(TierRow->RewardQualityMultiplier, 0.f);
		NewState.TimeLimitSeconds = FMath::Max(TierRow->TimeLimitSeconds, 1.f);
		if (TierRow->FixedRunSeed != 0)
		{
			NewState.RunSeed = TierRow->FixedRunSeed;
		}
		if (LevelDirector && !LevelDirector->ApplyRiftActivityForNextRun(NewState.Activity,
			ActivityType == EAeyerjiRiftActivityType::Excursion ? TierRow : nullptr))
		{
			OutReason = TEXT("LevelDirector rejected the frozen Rift activity snapshot");
			return false;
		}
	}
	else if (LevelDirector)
	{
		// Standard Rifts deliberately have no tier modifiers, but still freeze their
		// level from the launch party so later character level-ups cannot rescale enemies.
		if (!LevelDirector->ApplyRiftActivityForNextRun(NewState.Activity, nullptr))
		{
			OutReason = TEXT("LevelDirector rejected the frozen Standard Rift activity snapshot");
			return false;
		}
		NewState.TimeLimitSeconds = LevelDirector->RunTimeLimitSeconds > 0.f ? LevelDirector->RunTimeLimitSeconds : 900.f;
	}
	else
	{
		// This is an explicit legacy fallback for zones that do not provide a LevelDirector.
		NewState.Activity.ActivityLevel = UAeyerjiDifficultySettings::GetRiftEnemyReferenceLevel();
		UE_LOG(LogAeyerjiWorldFlow, Warning,
			TEXT("[RiftRun][Activity] Legacy fallback ActivityLevel=%d because no LevelDirector is bound"),
			NewState.Activity.ActivityLevel);
	}

	NewState.StartServerTimeSeconds = GetServerWorldTimeSeconds();
	NewState.Revision = FMath::Max(RiftRunState.Revision + 1, 1);
	RiftRunState = NewState;
	OnRep_RiftRunState();
	ForceNetUpdate();
	UE_LOG(LogAeyerjiWorldFlow, Display,
		TEXT("[RiftRun][Activity] RunSerial=%d Source=%s Type=%s ActivityLevel=%d ExcursionTier=%d HighestLaunchLevel=%d"),
		NewState.RunSerial,
		ActivitySource,
		*StaticEnum<EAeyerjiRiftActivityType>()->GetNameStringByValue(static_cast<int64>(NewState.Activity.ActivityType)),
		NewState.Activity.ActivityLevel, NewState.Activity.ExcursionTier, HighestParticipantLevel);
	if (LaunchRequestId > 0)
	{
		if (UAeyerjiStreamingSubsystem* StreamingSubsystem = GetStreamingSubsystem())
		{
			if (!StreamingSubsystem->ConsumePendingFrontendRunLaunch(LaunchRequestId))
			{
				UE_LOG(LogAeyerjiWorldFlow, Error, TEXT("[LobbyLaunch] Failed one-shot consume RequestId=%d"), LaunchRequestId);
			}
		}
	}
	return true;
}

bool AAeyerjiGameState::Server_StartRun()
{
	if (!HasAuthority())
	{
		return false;
	}
	if (RunState == EAeyerjiRunState::InRun && RiftRunState.RunSerial > 0 && StartedRunSerial == RiftRunState.RunSerial)
	{
		UE_LOG(LogAeyerjiWorldFlow, Display,
			TEXT("[RiftRun][Start] Duplicate request ignored RunSerial=%d Zone=%s"),
			RiftRunState.RunSerial, *ActiveZoneId.ToString());
		return true;
	}

	ResolveGameplayActorsForActiveZone();
	FString ReadinessReason;
	if (!ValidateRunStartReadiness(ReadinessReason) || !SnapshotRunParticipantsAndElectLeader(ReadinessReason))
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning,
			TEXT("[RiftRun][Start] Rejected Zone=%s TransitionId=%d Reason=%s"),
			*ActiveZoneId.ToString(), TransitionId, *ReadinessReason);
		return false;
	}

	AAeyerjiLevelDirector* LevelDirector = CachedLevelDirector.Get();
	const FAeyerjiRiftTierRow* TierRow = LevelDirector
		? LevelDirector->FindRiftTierRow(PendingSelectedRiftTier)
		: nullptr;
	if (LevelDirector && LevelDirector->GetRiftActivityType() == EAeyerjiRiftActivityType::Excursion && !TierRow)
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning,
			TEXT("[RiftRun][Start] Rejected undefined Tier=%d Zone=%s"),
			PendingSelectedRiftTier, *ActiveZoneId.ToString());
		return false;
	}
	if (!FreezeRiftConfigurationForNewRun(TierRow, ReadinessReason))
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning,
			TEXT("[RiftRun][Start] Rejected Zone=%s Tier=%d Reason=%s"),
			*ActiveZoneId.ToString(), PendingSelectedRiftTier, *ReadinessReason);
		return false;
	}
	if (LevelDirector && LevelDirector->SpawnMode == EAeyerjiLevelSpawnMode::ProximityEncounterRegions)
	{
		const int32 ProgressTarget = (RiftRunState.Activity.ActivityType == EAeyerjiRiftActivityType::Excursion && TierRow)
			? FMath::Max(TierRow->ProgressTargetPoints, 1)
			: FMath::Max(LevelDirector->GetEffectiveObjectiveKillTargetRaw(), 1);
		if (!LevelDirector->PrepareRiftRegionEncounterPlan(
			RiftRunState.RunSerial, RiftRunState.RunSeed, ProgressTarget, ReadinessReason))
		{
			UE_LOG(LogAeyerjiWorldFlow, Error,
				TEXT("[RiftRun][Start] Plan rejected RunSerial=%d Reason=%s"),
				RiftRunState.RunSerial, *ReadinessReason);
			RiftRunState.RunSerial = 0;
			RiftRunState.StartServerTimeSeconds = 0.f;
			RiftRunState.Revision++;
			OnRep_RiftRunState();
			ForceNetUpdate();
			return false;
		}
	}

	if (UAeyerjiWorldStateSubsystem* WorldStateSubsystem = UAeyerjiWorldStateSubsystem::Get(this))
	{
		const FName RunId = !ActiveZoneId.IsNone()
			? FName(*FString::Printf(TEXT("%s_%d"), *ActiveZoneId.ToString(), TransitionId))
			: FName(*FString::Printf(TEXT("Run_%d"), FMath::Max(TransitionId, 1)));
		WorldStateSubsystem->BeginRun(RunId);
	}

	if (!SetRunState(EAeyerjiRunState::InRun))
	{
		return false;
	}
	StartedRunSerial = RiftRunState.RunSerial;

	LastCompletedObjectiveEvent = EAeyerjiObjectiveEvent::None;
	ObjectiveEventVersion = FMath::Max(1, ObjectiveEventVersion + 1);
	ClearSurvivalRoundStateFromServer();
	ClearSurvivalUpgradeOfferStateFromServer();
	ForceNetUpdate();

	if (LevelDirector)
	{
		UE_LOG(LogAeyerjiWorldFlow, Display,
			TEXT("[RiftRun][Start] RunSerial=%d Seed=%d Tier=%d Leader=%s Participants=%d Director=%s"),
			RiftRunState.RunSerial, RiftRunState.RunSeed, RiftRunState.SelectedRiftTier,
			*GetNameSafe(RunLeaderPlayerState.Get()), RunParticipants.Num(), *GetNameSafe(LevelDirector));
		LevelDirector->StartRun();
	}
	else
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: Server_StartRun has no LevelDirector bound for Zone=%s"),
			*ActiveZoneId.ToString());
	}

	RefreshObjectiveStateFromAuthority();

	return true;
}

bool AAeyerjiGameState::Server_TrySelectRiftTier(
	AAeyerjiPlayerState* Requester,
	const int32 RequestedTier,
	EAeyerjiRiftTierSelectionFailure& OutFailure)
{
	OutFailure = EAeyerjiRiftTierSelectionFailure::None;
	if (!HasAuthority())
	{
		OutFailure = EAeyerjiRiftTierSelectionFailure::NotAuthority;
		return false;
	}
	if (RunState != EAeyerjiRunState::PreRun)
	{
		OutFailure = EAeyerjiRiftTierSelectionFailure::RunAlreadyActive;
		return false;
	}
	if (WorldFlowPhase != EAeyerjiWorldFlowPhase::Gameplay)
	{
		OutFailure = EAeyerjiRiftTierSelectionFailure::RunNotReady;
		return false;
	}

	ElectRunLeaderFromCurrentPlayers();
	if (!IsValid(Requester) || RunLeaderPlayerState.Get() != Requester)
	{
		OutFailure = EAeyerjiRiftTierSelectionFailure::RequesterNotLeader;
		return false;
	}

	TArray<int32> HighestUnlockedTiers;
	HighestUnlockedTiers.Reserve(PlayerArray.Num());
	for (APlayerState* PlayerState : PlayerArray)
	{
		AAeyerjiPlayerState* Participant = Cast<AAeyerjiPlayerState>(PlayerState);
		if (!Participant || !Participant->IsProfileLoadApplied())
		{
			OutFailure = EAeyerjiRiftTierSelectionFailure::ProfileNotReady;
			return false;
		}
		HighestUnlockedTiers.Add(Participant->GetHighestUnlockedRiftTier());
	}
	const int32 CommonHighestTier = AeyerjiRiftRules::ResolveCommonTierCap(HighestUnlockedTiers);
	if (CommonHighestTier <= 0)
	{
		OutFailure = EAeyerjiRiftTierSelectionFailure::ProfileNotReady;
		return false;
	}
	if (RequestedTier < 1 || RequestedTier > CommonHighestTier)
	{
		OutFailure = EAeyerjiRiftTierSelectionFailure::TierLockedForParty;
		return false;
	}

	AAeyerjiLevelDirector* LevelDirector = CachedLevelDirector.Get();
	if (!LevelDirector || !LevelDirector->FindRiftTierRow(RequestedTier))
	{
		OutFailure = EAeyerjiRiftTierSelectionFailure::TierNotDefined;
		return false;
	}

	PendingSelectedRiftTier = RequestedTier;
	for (APlayerState* PlayerState : PlayerArray)
	{
		if (AAeyerjiPlayerState* Participant = Cast<AAeyerjiPlayerState>(PlayerState))
		{
			Participant->SetRiftProgressionFromServer(Participant->GetHighestUnlockedRiftTier(), RequestedTier);
		}
	}

	RiftRunState.SelectedRiftTier = RequestedTier;
	RiftRunState.Revision = FMath::Max(RiftRunState.Revision + 1, 1);
	OnRep_RiftRunState();
	ForceNetUpdate();
	UE_LOG(LogAeyerjiWorldFlow, Display,
		TEXT("[RiftRun][TierSelection] Leader=%s Tier=%d CommonCap=%d"),
		*GetNameSafe(Requester), RequestedTier, CommonHighestTier);
	return true;
}

bool AAeyerjiGameState::Server_BeginBossPhase()
{
	if (!HasAuthority() || RunState != EAeyerjiRunState::InRun || RiftRunState.RunSerial <= 0)
	{
		return false;
	}
	if (BossPhaseStartedRunSerial == RiftRunState.RunSerial || RiftRunState.bBossPhaseStarted)
	{
		return true;
	}

	BossPhaseStartedRunSerial = RiftRunState.RunSerial;
	RiftRunState.bBossPhaseStarted = true;
	RiftRunState.Revision = FMath::Max(RiftRunState.Revision + 1, 1);
	if (CachedEncounterDirector.IsValid())
	{
		CachedEncounterDirector->FreezeWeightedProgress();
	}
	if (CachedLevelDirector.IsValid())
	{
		CachedLevelDirector->DisableUnopenedRiftEncounterRegions();
		if (!CachedLevelDirector->IsPrimaryObjectiveComplete())
		{
			BroadcastObjectiveEventCompleted(EAeyerjiObjectiveEvent::PrimaryObjectiveComplete);
			CachedLevelDirector->MarkPrimaryObjectiveComplete();
		}
		else
		{
			CachedLevelDirector->OpenBossGate();
		}
	}
	OnRep_RiftRunState();
	ForceNetUpdate();
	UE_LOG(LogAeyerjiWorldFlow, Display,
		TEXT("[RiftRun][BossPhase] Began RunSerial=%d Progress=%d/%d"),
		RiftRunState.RunSerial, CurrentObjectiveState.ProgressPoints, CurrentObjectiveState.ProgressPointTarget);
	return true;
}

bool AAeyerjiGameState::RollRiftRewardLayer(
	AAeyerjiPlayerState* PlayerState,
	const FAeyerjiRiftRewardLayerDefinition& Layer,
	const FGameplayTag FallbackSourceTag,
	TArray<FLootDropResult>& OutResults) const
{
	OutResults.Reset();
	if (!IsValid(PlayerState))
	{
		return false;
	}

	bool bHasRolls = Layer.MultiDropConfig.TotalBaseDrops > 0;
	for (const FLootMultiDropBucket& Bucket : Layer.MultiDropConfig.Buckets)
	{
		bHasRolls |= Bucket.BaseDrops > 0;
	}
	if (!bHasRolls)
	{
		return true;
	}

	ULootService* LootService = UCharacterStatsLibrary::GetLootService(const_cast<AAeyerjiGameState*>(this));
	if (!LootService)
	{
		return false;
	}

	AActor* PlayerActor = PlayerState->GetPawn();
	int32 PlayerLevel = RiftRunState.Activity.ActivityLevel > 0
		? RiftRunState.Activity.ActivityLevel
		: UAeyerjiDifficultySettings::GetRiftEnemyReferenceLevel();
	if (const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(PlayerActor, true))
	{
		if (const UAeyerjiAttributeSet* Attributes = ASC->GetSet<UAeyerjiAttributeSet>())
		{
			PlayerLevel = FMath::Max(FMath::RoundToInt(Attributes->GetLevel()), 1);
		}
	}

	FLootContext Context;
	Context.PlayerActor = PlayerActor;
	Context.EnemyLevel = RiftRunState.Activity.ActivityLevel > 0
		? RiftRunState.Activity.ActivityLevel
		: UAeyerjiDifficultySettings::GetRiftEnemyReferenceLevel();
	Context.PlayerLevel = PlayerLevel;
	Context.WorldTier = UAeyerjiDifficultySettings::GetNormalWorldTier();
	Context.SourceTag = Layer.SourceTag.IsValid() ? Layer.SourceTag : FallbackSourceTag;
	Context.PityGroup = Layer.PityGroup;
	Context.MinimumRarity = Layer.MinimumRarity;
	Context.DifficultyScale = 1.f;
	Context.RewardQualityMultiplier = RiftRunState.MonsterPower.RewardQualityMultiplier;
	return LootService->RollMultiDrop(Context, Layer.MultiDropConfig, OutResults);
}

bool AAeyerjiGameState::ReleaseRiftBaseRewards(
	AAeyerjiPlayerState* PlayerState,
	TArray<FLootDropResult>& Results,
	TSet<int32>& ReleasedIndices)
{
	if (!IsValid(PlayerState))
	{
		return false;
	}
	AActor* RecipientActor = PlayerState->GetPawn();
	const FVector Origin = CachedLevelDirector.IsValid() && CachedLevelDirector->BossSpawnMarker
		? CachedLevelDirector->BossSpawnMarker->GetActorLocation()
		: (RecipientActor ? RecipientActor->GetActorLocation() : FVector::ZeroVector);
	bool bAllReleased = true;
	for (int32 Index = 0; Index < Results.Num(); ++Index)
	{
		if (ReleasedIndices.Contains(Index))
		{
			continue;
		}

		const float Angle = Results.Num() > 0 ? 360.f * static_cast<float>(Index) / Results.Num() : 0.f;
		const FVector Offset = FVector(100.f, 0.f, 35.f).RotateAngleAxis(Angle, FVector::UpVector);
		const FAeyerjiLootSpawnSummary SpawnSummary = UAeyerjiInventoryBPFL::SpawnLootResults(
			this, TArray<FLootDropResult>{Results[Index]}, Origin + Offset, FRotator::ZeroRotator,
			Results[Index].Seed, EItemDropDistributionMode::DropOnlyForInstigator, RecipientActor);
		if (SpawnSummary.SpawnedPickupCount > 0)
		{
			for (AAeyerjiLootPickup* Pickup : SpawnSummary.SpawnedPickups)
			{
				if (IsValid(Pickup))
				{
					Pickup->ReserveForPlayerState(PlayerState);
				}
			}
			ReleasedIndices.Add(Index);
		}
		else
		{
			bAllReleased = false;
		}
	}
	return bAllReleased && ReleasedIndices.Num() == Results.Num();
}

bool AAeyerjiGameState::FinalizeRiftRewards()
{
	if (!HasAuthority() || !RiftRunState.bBossDefeated)
	{
		return false;
	}
	if (RiftRunState.bRewardsFinalized)
	{
		return true;
	}

	AAeyerjiLevelDirector* LevelDirector = CachedLevelDirector.Get();
	if (!bHasFrozenRiftRewardConfiguration)
	{
		// Legacy/non-Rift boss runs have no layered reward definition, but still complete
		// the orchestration barrier so their existing boss loot path remains compatible.
		RiftRunState.bRewardsFinalized = true;
		RiftRunState.Revision++;
		OnRep_RiftRunState();
		ForceNetUpdate();
		return true;
	}
	const EAeyerjiRiftRewardEligibility RewardEligibility = AeyerjiRiftRules::ResolveRewardEligibility(
		RiftRunState.bBossDefeated,
		RiftRunState.bCompletedInTime,
		RiftRunState.bBossPhaseDeathOccurred);

	// Each layer has its own rolled bit. A transient failure can therefore resume
	// this loop without rerolling layers that already succeeded for another player.
	for (const TWeakObjectPtr<AAeyerjiPlayerState>& ParticipantPtr : RunParticipants)
	{
		AAeyerjiPlayerState* Participant = ParticipantPtr.Get();
		if (!IsValid(Participant))
		{
			continue;
		}
		FRiftPlayerRewardLedger& Ledger = RiftRewardLedger.FindOrAdd(Participant);
		if (!Ledger.bRolled)
		{
			if (!Ledger.bBaseRolled)
			{
				if (!RollRiftRewardLayer(Participant, FrozenRiftBaseReward, AeyerjiTags::Loot_Source_RiftBase, Ledger.BaseResults))
				{
					return false;
				}
				Ledger.bBaseRolled = true;
			}
			if (!Ledger.bTimedRolled)
			{
				if (EnumHasAnyFlags(RewardEligibility, EAeyerjiRiftRewardEligibility::Timed)
					&& !RollRiftRewardLayer(Participant, FrozenRiftTimedReward, AeyerjiTags::Loot_Source_RiftTimed, Ledger.TimedResults))
				{
					return false;
				}
				Ledger.bTimedRolled = true;
			}
			if (!Ledger.bFlawlessRolled)
			{
				if (EnumHasAnyFlags(RewardEligibility, EAeyerjiRiftRewardEligibility::Flawless)
					&& !RollRiftRewardLayer(Participant, FrozenRiftFlawlessReward, AeyerjiTags::Loot_Source_RiftFlawless, Ledger.FlawlessResults))
				{
					return false;
				}
				Ledger.bFlawlessRolled = true;
			}
			Ledger.bRolled = Ledger.bBaseRolled && Ledger.bTimedRolled && Ledger.bFlawlessRolled;
		}
	}

	FVector CacheLocation = LevelDirector && LevelDirector->BossSpawnMarker
		? LevelDirector->BossSpawnMarker->GetActorLocation()
		: FVector::ZeroVector;
	for (TPair<TWeakObjectPtr<AAeyerjiPlayerState>, FRiftPlayerRewardLedger>& Pair : RiftRewardLedger)
	{
		AAeyerjiPlayerState* Participant = Pair.Key.Get();
		FRiftPlayerRewardLedger& Ledger = Pair.Value;
		if (!IsValid(Participant))
		{
			continue;
		}
		ReleaseRiftBaseRewards(Participant, Ledger.BaseResults, Ledger.ReleasedBaseIndices);

		TArray<FLootDropResult> BonusResults = Ledger.TimedResults;
		BonusResults.Append(Ledger.FlawlessResults);
		if (!BonusResults.IsEmpty() && !Ledger.bBonusBundleInstalled)
		{
			if (!RiftBonusRewardCache.IsValid())
			{
				UClass* CacheClass = FrozenRiftBonusRewardPresentationClass
					? FrozenRiftBonusRewardPresentationClass.Get()
					: AAeyerjiRewardPresentationActor::StaticClass();
				RiftBonusRewardCache = GetWorld()->SpawnActor<AAeyerjiRewardPresentationActor>(
					CacheClass, CacheLocation, FRotator::ZeroRotator);
			}
			if (!RiftBonusRewardCache.IsValid())
			{
				return false;
			}
			RiftBonusRewardCache->AddPrivateRewardBundle(Participant, BonusResults, AeyerjiTags::Loot_Source_RiftTimed);
			Ledger.bBonusBundleInstalled = true;
		}
	}

	// Pickup spawn failures stay retryable in the immutable base ledger. They do not
	// reroll, and a later FinalizeRiftRewards call can create the missing actors.
	bool bAllBaseReleased = true;
	for (const TPair<TWeakObjectPtr<AAeyerjiPlayerState>, FRiftPlayerRewardLedger>& Pair : RiftRewardLedger)
	{
		bAllBaseReleased &= Pair.Value.ReleasedBaseIndices.Num() == Pair.Value.BaseResults.Num();
	}
	if (!bAllBaseReleased)
	{
		return false;
	}

	RiftRunState.bRewardsFinalized = true;
	RiftRunState.Revision++;
	OnRep_RiftRunState();
	ForceNetUpdate();
	UE_LOG(LogAeyerjiWorldFlow, Display,
		TEXT("[RiftRun][Rewards] Finalized RunSerial=%d Participants=%d InTime=%d Flawless=%d Cache=%s"),
		RiftRunState.RunSerial, RiftRewardLedger.Num(), RiftRunState.bCompletedInTime ? 1 : 0,
		(!RiftRunState.bBossPhaseDeathOccurred && RiftRunState.bCompletedInTime) ? 1 : 0,
		*GetNameSafe(RiftBonusRewardCache.Get()));
	return true;
}

void AAeyerjiGameState::Server_NotifyPlayerDeath(AAeyerjiPlayerState* DeadPlayerState)
{
	if (!HasAuthority() || !RiftRunState.bBossPhaseStarted || RiftRunState.bBossDefeated
		|| RiftRunState.bBossPhaseDeathOccurred || !IsValid(DeadPlayerState))
	{
		return;
	}

	RiftRunState.bBossPhaseDeathOccurred = true;
	RiftRunState.Revision = FMath::Max(RiftRunState.Revision + 1, 1);
	OnRep_RiftRunState();
	ForceNetUpdate();
	UE_LOG(LogAeyerjiWorldFlow, Display,
		TEXT("[RiftRun][BossDeath] RunSerial=%d Player=%s FlawlessEligible=0"),
		RiftRunState.RunSerial, *GetNameSafe(DeadPlayerState));
}

bool AAeyerjiGameState::IsBossArenaRespawnActive() const
{
	return RiftRunState.RunSerial > 0 && RiftRunState.bBossPhaseStarted && !RiftRunState.bBossDefeated;
}

FName AAeyerjiGameState::GetBossArenaRespawnPlayerStartTag() const
{
	return CachedLevelDirector.IsValid()
		? CachedLevelDirector->GetBossArenaRespawnPlayerStartTag()
		: NAME_None;
}

bool AAeyerjiGameState::Server_NotifyBossDefeated()
{
	if (!HasAuthority())
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: Server_NotifyBossDefeated rejected on non-authority."));
		return false;
	}
	if (RiftRunState.RunSerial > 0 && BossDefeatedRunSerial == RiftRunState.RunSerial)
	{
		UE_LOG(LogAeyerjiWorldFlow, Display,
			TEXT("[RiftRun][BossDefeated] Duplicate ignored RunSerial=%d"), RiftRunState.RunSerial);
		return FinalizeRiftRewards();
	}

	UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: Server_NotifyBossDefeated requested (RunState=%s BossSpawner=%s)."),
		RunStateToString(RunState),
		*GetNameSafe(CachedBossSpawner.Get()));

	if (AAeyerjiLevelDirector* LevelDirector = CachedLevelDirector.Get())
	{
		if (LevelDirector->HandleSurvivalBossDefeated())
		{
			return true;
		}
	}

	if (!SetRunState(EAeyerjiRunState::BossDefeated))
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: Server_NotifyBossDefeated failed to set RunState -> BossDefeated from %s."),
			RunStateToString(RunState));
		return false;
	}

	BossDefeatedRunSerial = RiftRunState.RunSerial;
	RiftRunState.bBossDefeated = true;
	const float AcceptedElapsedSeconds = GetAuthoritativeRunElapsedSeconds();
	RiftRunState.bCompletedInTime = AeyerjiRiftRules::IsCompletedInTime(
		AcceptedElapsedSeconds, RiftRunState.TimeLimitSeconds);
	RiftRunState.bOvertime = !RiftRunState.bCompletedInTime;
	RiftRunState.EarnedNextRiftTier = RiftRunState.bCompletedInTime
		? RiftRunState.SelectedRiftTier + 1
		: 0;
	RiftRunState.Revision = FMath::Max(RiftRunState.Revision + 1, 1);
	OnRep_RiftRunState();
	ForceNetUpdate();
	UE_LOG(LogAeyerjiWorldFlow, Display,
		TEXT("[RiftRun][BossDefeated] RunSerial=%d Elapsed=%.3f Limit=%.3f InTime=%d BossDeath=%d EarnedTier=%d"),
		RiftRunState.RunSerial, AcceptedElapsedSeconds, RiftRunState.TimeLimitSeconds,
		RiftRunState.bCompletedInTime ? 1 : 0, RiftRunState.bBossPhaseDeathOccurred ? 1 : 0,
		RiftRunState.EarnedNextRiftTier);
	if (!FinalizeRiftRewards())
	{
		UE_LOG(LogAeyerjiWorldFlow, Error,
			TEXT("[RiftRun][Rewards] Finalization deferred RunSerial=%d; extraction remains disabled"),
			RiftRunState.RunSerial);
	}

	BroadcastObjectiveEventCompleted(EAeyerjiObjectiveEvent::BossObjectiveComplete);

	if (AAeyerjiLevelDirector* LevelDirector = CachedLevelDirector.Get())
	{
		LevelDirector->WritePersistentFactsForTrigger(EAeyerjiPersistentFactWriteTrigger::BossDefeated);
		LevelDirector->EndRun();
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BossDefeatedDelayHandle);

		if (BossDefeatedToCompleteDelay <= 0.f)
		{
			UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: BossDefeated delay <= 0 (%.2fs), advancing immediately to ObjectiveComplete."),
				BossDefeatedToCompleteDelay);
			HandleBossDefeatedDelayElapsed();
		}
		else
		{
			UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: Scheduling BossDefeated -> ObjectiveComplete delay %.2fs."),
				BossDefeatedToCompleteDelay);
			World->GetTimerManager().SetTimer(BossDefeatedDelayHandle, this, &AAeyerjiGameState::HandleBossDefeatedDelayElapsed, BossDefeatedToCompleteDelay, false);
		}
	}
	else
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: Server_NotifyBossDefeated has no World; delay timer not scheduled."));
	}

	return true;
}

bool AAeyerjiGameState::Server_BeginObjectiveComplete()
{
	if (!HasAuthority())
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: Server_BeginObjectiveComplete rejected on non-authority."));
		return false;
	}
	if (RiftRunState.bBossDefeated && !RiftRunState.bRewardsFinalized && !FinalizeRiftRewards())
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning,
			TEXT("[RiftRun][Extraction] Objective completion blocked until rewards finalize RunSerial=%d"),
			RiftRunState.RunSerial);
		return false;
	}

	UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: Server_BeginObjectiveComplete requested (RunState=%s CachedPortal=%s)."),
		RunStateToString(RunState),
		*GetNameSafe(CachedEndRunPortal.Get()));

	if (RunState == EAeyerjiRunState::ObjectiveComplete)
	{
		UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: ObjectiveComplete already active, forcing SpawnEndRunPortal re-check."));
		SpawnEndRunPortal();
		return true;
	}

	if (RunState != EAeyerjiRunState::InRun && RunState != EAeyerjiRunState::BossDefeated)
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: Server_BeginObjectiveComplete rejected invalid RunState=%s."),
			RunStateToString(RunState));
		return false;
	}

	const bool bBossDefeated = (RunState == EAeyerjiRunState::BossDefeated);
	if (!SetRunState(EAeyerjiRunState::ObjectiveComplete))
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: Failed to transition RunState to ObjectiveComplete from %s."),
			RunStateToString(RunState));
		return false;
	}

	BroadcastObjectiveEventCompleted(EAeyerjiObjectiveEvent::MainObjectiveComplete);

	if (RunResults.ResultsVersion <= 0 || RunResults.Resolution != EAeyerjiRunResolution::Victory)
	{
		SnapshotRunResults(EAeyerjiRunResolution::Victory, bBossDefeated);
	}

	if (AAeyerjiLevelDirector* LevelDirector = CachedLevelDirector.Get())
	{
		LevelDirector->EndRun();
	}

	return true;
}

bool AAeyerjiGameState::Server_CompleteExtraction()
{
	if (!HasAuthority())
	{
		return false;
	}
	if (RiftRunState.RunSerial > 0 && RiftRunState.bBossDefeated && !RiftRunState.bRewardsFinalized)
	{
		return false;
	}
	if (RiftRunState.RunSerial > 0 && ExtractionCompletedRunSerial == RiftRunState.RunSerial)
	{
		UE_LOG(LogAeyerjiWorldFlow, Display,
			TEXT("[RiftRun][Extraction] Duplicate ignored RunSerial=%d"), RiftRunState.RunSerial);
		return true;
	}

	if (RunState == EAeyerjiRunState::RunComplete)
	{
		MaybeBroadcastRunResults();
		return true;
	}

	if (RunResults.ResultsVersion <= 0 || RunResults.Resolution != EAeyerjiRunResolution::Victory)
	{
		const bool bBossDefeated = (RunState == EAeyerjiRunState::BossDefeated || RunState == EAeyerjiRunState::ObjectiveComplete);
		const bool bShouldMarkBossDefeated = RunResults.bBossDefeated
			|| bBossDefeated
			|| (CachedLevelDirector.IsValid() && CachedLevelDirector->GetRunWinCondition() == EAeyerjiRunWinCondition::BossCleared);
		SnapshotRunResults(EAeyerjiRunResolution::Victory, bShouldMarkBossDefeated);
	}

	if (!SetRunState(EAeyerjiRunState::RunComplete))
	{
		return false;
	}
	ExtractionCompletedRunSerial = RiftRunState.RunSerial;

	BroadcastObjectiveEventCompleted(EAeyerjiObjectiveEvent::RunCompleted);

	if (AAeyerjiLevelDirector* LevelDirector = CachedLevelDirector.Get())
	{
		LevelDirector->WritePersistentFactsForTrigger(EAeyerjiPersistentFactWriteTrigger::ZoneCompleted);
		LevelDirector->WritePersistentFactsForTrigger(EAeyerjiPersistentFactWriteTrigger::Unlock);
	}

	PersistRunResultsForPlayers();
	FinalizeCompletedRunWorld();
	if (UAeyerjiWorldStateSubsystem* WorldStateSubsystem = UAeyerjiWorldStateSubsystem::Get(this))
	{
		WorldStateSubsystem->EndRun();
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BossDefeatedDelayHandle);
		World->GetTimerManager().ClearTimer(AutoReturnDelayHandle);

		if (AutoReturnToMenuDelay > 0.f)
		{
			World->GetTimerManager().SetTimer(AutoReturnDelayHandle, this, &AAeyerjiGameState::HandleAutoReturnDelayElapsed, AutoReturnToMenuDelay, false);
		}
	}

	MaybeBroadcastRunResults();
	return true;
}

bool AAeyerjiGameState::Server_FailRunTimeExpired()
{
	if (!HasAuthority())
	{
		return false;
	}

	if (RunState == EAeyerjiRunState::RunComplete)
	{
		MaybeBroadcastRunResults();
		return true;
	}

	if (RunState != EAeyerjiRunState::InRun)
	{
		return false;
	}

	SnapshotRunResults(EAeyerjiRunResolution::TimeExpired, /*bBossDefeated=*/false);

	if (!SetRunState(EAeyerjiRunState::RunComplete))
	{
		return false;
	}

	BroadcastObjectiveEventCompleted(EAeyerjiObjectiveEvent::RunFailedTimeExpired);

	PersistRunResultsForPlayers();
	FinalizeCompletedRunWorld();
	if (UAeyerjiWorldStateSubsystem* WorldStateSubsystem = UAeyerjiWorldStateSubsystem::Get(this))
	{
		WorldStateSubsystem->EndRun();
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BossDefeatedDelayHandle);
		World->GetTimerManager().ClearTimer(AutoReturnDelayHandle);

		if (AutoReturnToMenuDelay > 0.f)
		{
			World->GetTimerManager().SetTimer(AutoReturnDelayHandle, this, &AAeyerjiGameState::HandleAutoReturnDelayElapsed, AutoReturnToMenuDelay, false);
		}
	}

	MaybeBroadcastRunResults();
	return true;
}

bool AAeyerjiGameState::Server_FailRunDefenseObjectiveDestroyed()
{
	if (!HasAuthority())
	{
		return false;
	}

	if (RunState == EAeyerjiRunState::RunComplete)
	{
		MaybeBroadcastRunResults();
		return true;
	}

	if (RunState != EAeyerjiRunState::InRun)
	{
		return false;
	}

	SnapshotRunResults(EAeyerjiRunResolution::DefenseObjectiveDestroyed, /*bBossDefeated=*/false);

	if (!SetRunState(EAeyerjiRunState::RunComplete))
	{
		return false;
	}

	BroadcastObjectiveEventCompleted(EAeyerjiObjectiveEvent::RunFailedDefenseObjectiveDestroyed);

	PersistRunResultsForPlayers();
	FinalizeCompletedRunWorld();
	if (UAeyerjiWorldStateSubsystem* WorldStateSubsystem = UAeyerjiWorldStateSubsystem::Get(this))
	{
		WorldStateSubsystem->EndRun();
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BossDefeatedDelayHandle);
		World->GetTimerManager().ClearTimer(AutoReturnDelayHandle);

		if (AutoReturnToMenuDelay > 0.f)
		{
			World->GetTimerManager().SetTimer(AutoReturnDelayHandle, this, &AAeyerjiGameState::HandleAutoReturnDelayElapsed, AutoReturnToMenuDelay, false);
		}
	}

	MaybeBroadcastRunResults();
	return true;
}

bool AAeyerjiGameState::Server_MarkRunComplete()
{
	if (!HasAuthority())
	{
		return false;
	}

	if (RunState == EAeyerjiRunState::RunComplete)
	{
		MaybeBroadcastRunResults();
		return true;
	}

	const bool bBossDefeated = (RunState == EAeyerjiRunState::BossDefeated || RunState == EAeyerjiRunState::ObjectiveComplete);

	if (RunState != EAeyerjiRunState::InRun
		&& RunState != EAeyerjiRunState::BossDefeated
		&& RunState != EAeyerjiRunState::ObjectiveComplete)
	{
		return false;
	}

	SnapshotRunResults(EAeyerjiRunResolution::Abandoned, bBossDefeated);

	if (!SetRunState(EAeyerjiRunState::RunComplete))
	{
		return false;
	}

	BroadcastObjectiveEventCompleted(EAeyerjiObjectiveEvent::RunAbandoned);

	PersistRunResultsForPlayers();
	FinalizeCompletedRunWorld();
	if (UAeyerjiWorldStateSubsystem* WorldStateSubsystem = UAeyerjiWorldStateSubsystem::Get(this))
	{
		WorldStateSubsystem->EndRun();
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BossDefeatedDelayHandle);
		World->GetTimerManager().ClearTimer(AutoReturnDelayHandle);

		if (AutoReturnToMenuDelay > 0.f)
		{
			World->GetTimerManager().SetTimer(AutoReturnDelayHandle, this, &AAeyerjiGameState::HandleAutoReturnDelayElapsed, AutoReturnToMenuDelay, false);
		}
	}

	MaybeBroadcastRunResults();
	return true;
}

bool AAeyerjiGameState::Server_ReturnToMenu()
{
	if (!HasAuthority())
	{
		return false;
	}

	bPendingRetryAfterMenuTransition = false;
	PendingRetryZoneId = NAME_None;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DeferredRetryTravelHandle);
	}

	return SetRunState(EAeyerjiRunState::ReturnToMenu);
}

bool AAeyerjiGameState::Server_ForceEndRunAndReturnToMenu()
{
	if (!HasAuthority())
	{
		return false;
	}

	if (RunState == EAeyerjiRunState::ReturnToMenu)
	{
		return true;
	}

	if (RunState != EAeyerjiRunState::RunComplete)
	{
		if (!Server_MarkRunComplete())
		{
			return false;
		}
	}

	bPendingRetryAfterMenuTransition = false;
	PendingRetryZoneId = NAME_None;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DeferredRetryTravelHandle);
	}

	return SetRunState(EAeyerjiRunState::ReturnToMenu);
}

bool AAeyerjiGameState::Server_RetryRiftRunForRequester(
	AAeyerjiPlayerState* Requester,
	const bool bSelectEarnedTier,
	EAeyerjiRiftTierSelectionFailure& OutFailure)
{
	OutFailure = EAeyerjiRiftTierSelectionFailure::None;
	if (!HasAuthority())
	{
		OutFailure = EAeyerjiRiftTierSelectionFailure::NotAuthority;
		return false;
	}
	if (RunState != EAeyerjiRunState::RunComplete)
	{
		OutFailure = EAeyerjiRiftTierSelectionFailure::RunNotReady;
		return false;
	}

	// Re-elect from currently connected, hydrated profiles so a departed original
	// leader cannot permanently block replay for the remaining party.
	ElectRunLeaderFromCurrentPlayers();
	if (!IsValid(Requester) || RunLeaderPlayerState.Get() != Requester)
	{
		OutFailure = EAeyerjiRiftTierSelectionFailure::RequesterNotLeader;
		return false;
	}

	const int32 RequestedTier = bSelectEarnedTier
		? RiftRunState.EarnedNextRiftTier
		: FMath::Max(RunResults.SelectedRiftTier, 1);
	if (RequestedTier < 1)
	{
		OutFailure = EAeyerjiRiftTierSelectionFailure::RunNotReady;
		return false;
	}

	TArray<AAeyerjiPlayerState*> LoadedParticipants;
	TArray<int32> HighestUnlockedTiers;
	LoadedParticipants.Reserve(PlayerArray.Num());
	HighestUnlockedTiers.Reserve(PlayerArray.Num());
	for (APlayerState* PlayerState : PlayerArray)
	{
		AAeyerjiPlayerState* Participant = Cast<AAeyerjiPlayerState>(PlayerState);
		if (!IsValid(Participant) || !Participant->IsProfileLoadApplied())
		{
			OutFailure = EAeyerjiRiftTierSelectionFailure::ProfileNotReady;
			return false;
		}
		LoadedParticipants.Add(Participant);
		HighestUnlockedTiers.Add(Participant->GetHighestUnlockedRiftTier());
	}

	const int32 CommonHighestTier = AeyerjiRiftRules::ResolveCommonTierCap(HighestUnlockedTiers);
	if (CommonHighestTier <= 0)
	{
		OutFailure = EAeyerjiRiftTierSelectionFailure::ProfileNotReady;
		return false;
	}
	if (RequestedTier > CommonHighestTier)
	{
		OutFailure = EAeyerjiRiftTierSelectionFailure::TierLockedForParty;
		return false;
	}

	AAeyerjiLevelDirector* LevelDirector = CachedLevelDirector.Get();
	if (!LevelDirector || !LevelDirector->FindRiftTierRow(RequestedTier))
	{
		OutFailure = EAeyerjiRiftTierSelectionFailure::TierNotDefined;
		return false;
	}

	PendingSelectedRiftTier = RequestedTier;
	for (AAeyerjiPlayerState* Participant : LoadedParticipants)
	{
		Participant->SetRiftProgressionFromServer(Participant->GetHighestUnlockedRiftTier(), RequestedTier);
		Participant->CommitCheckpointProfile(EAeyerjiSaveCheckpointReason::RetryRun);
	}

	// During results, RunResults remains the immutable completed-run snapshot while
	// this replicated field tells Blueprint which tier the authority staged next.
	RiftRunState.SelectedRiftTier = RequestedTier;
	RiftRunState.Revision = FMath::Max(RiftRunState.Revision + 1, 1);
	OnRep_RiftRunState();
	ForceNetUpdate();
	UE_LOG(LogAeyerjiWorldFlow, Display,
		TEXT("[RiftRun][Retry] Leader=%s PreviousRunSerial=%d RequestedTier=%d EarnedSelection=%d CommonCap=%d"),
		*GetNameSafe(Requester), RunResults.RunSerial, RequestedTier, bSelectEarnedTier ? 1 : 0, CommonHighestTier);

	if (!Server_RetryRun())
	{
		OutFailure = EAeyerjiRiftTierSelectionFailure::RunNotReady;
		return false;
	}
	return true;
}

bool AAeyerjiGameState::Server_RetryRun()
{
	if (!HasAuthority() || RunState != EAeyerjiRunState::RunComplete)
	{
		return false;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(BossDefeatedDelayHandle);
		World->GetTimerManager().ClearTimer(AutoReturnDelayHandle);
	}

	ClearEndRunPortal();

	UAeyerjiStreamingSubsystem* StreamingSubsystem = GetStreamingSubsystem();
	if (!StreamingSubsystem)
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: Server_RetryRun failed - StreamingSubsystem missing."));
		return false;
	}

	const FName RetryZoneId = !RunResults.CompletedZoneId.IsNone()
		? RunResults.CompletedZoneId
		: ActiveZoneId;
	if (RetryZoneId.IsNone())
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: Server_RetryRun failed - no retry zone could be resolved."));
		return false;
	}

	bPendingRetryAfterMenuTransition = false;
	PendingRetryZoneId = NAME_None;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DeferredRetryTravelHandle);
	}

	const FName MenuZoneId = ResolveMenuZoneId();
	if (!MenuZoneId.IsNone() && MenuZoneId != ActiveZoneId)
	{
		bPendingRetryAfterMenuTransition = true;
		PendingRetryZoneId = RetryZoneId;

		UE_LOG(LogAeyerjiWorldFlow, Display,
			TEXT("AAeyerjiGameState: Staging retry via menu transition (RetryZone=%s MenuZone=%s)."),
			*RetryZoneId.ToString(),
			*MenuZoneId.ToString());
		return Server_BeginWorldTransition(MenuZoneId);
	}

	return StreamingSubsystem->RestartCurrentGameplaySession(RetryZoneId, true);
}

UAeyerjiStreamingSubsystem* AAeyerjiGameState::GetStreamingSubsystem() const
{
	if (UGameInstance* GI = GetGameInstance())
	{
		return GI->GetSubsystem<UAeyerjiStreamingSubsystem>();
	}

	return nullptr;
}

FName AAeyerjiGameState::ResolveMenuZoneId() const
{
	if (UAeyerjiStreamingSubsystem* StreamingSubsystem = GetStreamingSubsystem())
	{
		FZoneDef MenuZoneDefinition;
		if (StreamingSubsystem->GetZoneDefinition(FName(TEXT("Zone.Menu")), MenuZoneDefinition))
		{
			return MenuZoneDefinition.ZoneId;
		}

		if (const UAeyerjiStreamingManifest* Manifest = StreamingSubsystem->GetManifest())
		{
			return Manifest->DefaultZoneId;
		}
	}

	return NAME_None;
}

void AAeyerjiGameState::HandleReplicatedWorldFlowState()
{
	if (HasAuthority())
	{
		return;
	}

	if (WorldFlowPhase != EAeyerjiWorldFlowPhase::TransitionLoading)
	{
		return;
	}

	if (ActiveZoneId.IsNone() || TransitionId <= 0 || TransitionId == LastRequestedClientTransitionId)
	{
		return;
	}

	if (UAeyerjiStreamingSubsystem* StreamingSubsystem = GetStreamingSubsystem())
	{
		const bool bRequested = StreamingSubsystem->EnterZone(ActiveZoneId);
		if (bRequested)
		{
			LastRequestedClientTransitionId = TransitionId;
			UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: client requested EnterZone(%s) for TransitionId=%d"),
				*ActiveZoneId.ToString(),
				TransitionId);
		}
		else
		{
			UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: client failed EnterZone(%s) for TransitionId=%d"),
				*ActiveZoneId.ToString(),
				TransitionId);
		}
	}
}

void AAeyerjiGameState::MaybeBroadcastZoneGameplayReady()
{
	if (WorldFlowPhase != EAeyerjiWorldFlowPhase::Gameplay)
	{
		return;
	}

	if (ActiveZoneId.IsNone() || TransitionId <= 0 || TransitionId == LastBroadcastGameplayReadyTransitionId)
	{
		return;
	}

	LastBroadcastGameplayReadyTransitionId = TransitionId;
	UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: Broadcasting gameplay-ready Zone=%s TransitionId=%d"),
		*ActiveZoneId.ToString(),
		TransitionId);
	OnZoneGameplayReady.Broadcast(ActiveZoneId, TransitionId);
}

void AAeyerjiGameState::SetWorldFlowPhase(const EAeyerjiWorldFlowPhase NewPhase)
{
	if (WorldFlowPhase == NewPhase)
	{
		return;
	}

	const EAeyerjiWorldFlowPhase OldPhase = WorldFlowPhase;
	WorldFlowPhase = NewPhase;
	OnWorldFlowPhaseChanged.Broadcast(WorldFlowPhase, OldPhase, TransitionId);

	if (WorldFlowPhase != EAeyerjiWorldFlowPhase::TransitionLoading)
	{
		ClearWorldFlowLoadingRequirements();
		ZoneReadyPlayers.Reset();
		bServerZoneReady = false;
	}

	UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: WorldFlowPhase %s -> %s (TransitionId=%d Zone=%s)"),
		WorldFlowPhaseToString(OldPhase),
		WorldFlowPhaseToString(WorldFlowPhase),
		TransitionId,
		*ActiveZoneId.ToString());

	if (HasAuthority())
	{
		if (WorldFlowPhase != EAeyerjiWorldFlowPhase::Gameplay)
		{
			ClearObjectiveStateFromServer();
		}
		else
		{
			RefreshObjectiveStateFromAuthority();
		}

		ForceNetUpdate();
	}
}

bool AAeyerjiGameState::Server_BeginWorldTransition(const FName TargetZoneId)
{
	if (!HasAuthority() || TargetZoneId.IsNone())
	{
		return false;
	}

	UAeyerjiStreamingSubsystem* StreamingSubsystem = GetStreamingSubsystem();
	if (!StreamingSubsystem)
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: Server_BeginWorldTransition failed - StreamingSubsystem missing."));
		return false;
	}

	FZoneDef ValidatedZone;
	if (!StreamingSubsystem->GetZoneDefinition(TargetZoneId, ValidatedZone))
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: Server_BeginWorldTransition rejected invalid ZoneId=%s"),
			*TargetZoneId.ToString());
		return false;
	}

	const EAeyerjiWorldFlowPhase PreviousPhase = WorldFlowPhase;
	const FName PreviousZoneId = ActiveZoneId;
	const int32 PreviousTransitionId = TransitionId;

	ActiveZoneId = ValidatedZone.ZoneId;
	TransitionId = FMath::Max(TransitionId + 1, 1);
	ZoneReadyPlayers.Reset();
	bServerZoneReady = false;
	ClearWorldFlowLoadingRequirements();

	SetWorldFlowPhase(EAeyerjiWorldFlowPhase::TransitionLoading);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WorldTransitionTimeoutHandle);
		World->GetTimerManager().SetTimer(
			WorldTransitionTimeoutHandle,
			this,
			&AAeyerjiGameState::HandleWorldTransitionTimeout,
			FMath::Max(1.f, WorldTransitionTimeoutSeconds),
			false);
	}

	const bool bEnterIssued = StreamingSubsystem->EnterZone(ActiveZoneId);
	if (!bEnterIssued)
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: EnterZone failed for Zone=%s TransitionId=%d"),
			*ActiveZoneId.ToString(),
			TransitionId);
		ActiveZoneId = PreviousZoneId;
		TransitionId = PreviousTransitionId;
		SetWorldFlowPhase(PreviousPhase);
		ZoneReadyPlayers.Reset();
		bServerZoneReady = false;
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(WorldTransitionTimeoutHandle);
		}
		ForceNetUpdate();
		return false;
	}

	if (UWorld* World = GetWorld();
		World
		&& World->WorldType == EWorldType::PIE
		&& ValidatedZone.bSpawnPlayerAfterReady)
	{
		DestroyPersistentRuntimeActorsForFreshSession();
	}

	UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: Started world transition Zone=%s TransitionId=%d"),
		*ActiveZoneId.ToString(),
		TransitionId);
	ForceNetUpdate();
	return true;
}

bool AAeyerjiGameState::Server_ReportPlayerZoneReady(APlayerState* PlayerState, const int32 ReportedTransitionId)
{
	if (!HasAuthority() || !IsValid(PlayerState))
	{
		return false;
	}

	if (WorldFlowPhase != EAeyerjiWorldFlowPhase::TransitionLoading)
	{
		return false;
	}

	if (ReportedTransitionId != TransitionId)
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: Ignoring stale ready ack from %s (Reported=%d Current=%d)"),
			*GetNameSafe(PlayerState),
			ReportedTransitionId,
			TransitionId);
		return false;
	}

	ZoneReadyPlayers.Add(PlayerState);
	int32 ExpectedReadyCount = 0;
	if (const UWorld* World = GetWorld())
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (IsValid(It->Get()))
			{
				++ExpectedReadyCount;
			}
		}
	}
	UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: Ready ack from %s for TransitionId=%d (%d/%d)"),
		*GetNameSafe(PlayerState),
		TransitionId,
		ZoneReadyPlayers.Num(),
		ExpectedReadyCount);

	TryCompleteWorldTransition();
	return true;
}

void AAeyerjiGameState::TryCompleteWorldTransition()
{
	if (!HasAuthority() || WorldFlowPhase != EAeyerjiWorldFlowPhase::TransitionLoading)
	{
		return;
	}

	PrepareWorldFlowLoadingRequirements();

	if (!AreAllPlayersReadyForTransition())
	{
		return;
	}

	if (PendingWorldFlowLoaderCount > 0)
	{
		return;
	}

	if (!ApplyZoneSpawnPolicy())
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(WorldTransitionTimeoutHandle);
	}

	ForceNetUpdate();
}

bool AAeyerjiGameState::AreAllPlayersReadyForTransition() const
{
	if (!bServerZoneReady)
	{
		return false;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		const APlayerController* PC = It->Get();
		if (!IsValid(PC))
		{
			continue;
		}

		const APlayerState* PS = PC->PlayerState;
		if (!IsValid(PS))
		{
			return false;
		}

		if (!ZoneReadyPlayers.Contains(PS))
		{
			return false;
		}
	}

	return true;
}

void AAeyerjiGameState::MarkLocalPlayersReadyForTransition()
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

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			if (PC->IsLocalController())
			{
				if (APlayerState* PS = PC->PlayerState)
				{
					ZoneReadyPlayers.Add(PS);
				}
			}
		}
	}
}

APlayerStart* AAeyerjiGameState::SelectPlayerStartForZone(const FName DesiredTag, const int32 PlayerIndex) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	TArray<APlayerStart*> TaggedStarts;
	TArray<APlayerStart*> AllStarts;

	for (TActorIterator<APlayerStart> It(World); It; ++It)
	{
		if (APlayerStart* Start = *It)
		{
			AllStarts.Add(Start);
			if (!DesiredTag.IsNone() && Start->PlayerStartTag == DesiredTag)
			{
				TaggedStarts.Add(Start);
			}
		}
	}

	if (TaggedStarts.Num() > 0)
	{
		const int32 SafeIndex = FMath::Abs(PlayerIndex) % TaggedStarts.Num();
		return TaggedStarts[SafeIndex];
	}

	if (AllStarts.Num() <= 0)
	{
		return nullptr;
	}

	if (!DesiredTag.IsNone())
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: Missing PlayerStart tag %s. Falling back to first available PlayerStart."),
			*DesiredTag.ToString());
	}

	return AllStarts[0];
}

bool AAeyerjiGameState::ApplyZoneSpawnPolicy()
{
	UAeyerjiStreamingSubsystem* StreamingSubsystem = GetStreamingSubsystem();
	if (!StreamingSubsystem)
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: ApplyZoneSpawnPolicy failed - StreamingSubsystem missing."));
		return false;
	}

	FZoneDef ZoneDef;
	const bool bHasZoneDef = StreamingSubsystem->GetZoneDefinition(ActiveZoneId, ZoneDef);

	if (!bHasZoneDef)
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: Zone definition missing for %s; defaulting to gameplay spawn policy."),
			*ActiveZoneId.ToString());
		ZoneDef.bSpawnPlayerAfterReady = true;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	if (!ZoneDef.bSpawnPlayerAfterReady)
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (APlayerController* PC = It->Get())
			{
				if (APawn* ExistingPawn = PC->GetPawn())
				{
					PC->UnPossess();
					ExistingPawn->Destroy();
				}
			}
		}

		if (RunState != EAeyerjiRunState::PreRun)
		{
			SetRunState(EAeyerjiRunState::PreRun);
		}

		SetWorldFlowPhase(EAeyerjiWorldFlowPhase::Menu);
		UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: Transition completed for menu zone %s"), *ActiveZoneId.ToString());

		if (bPendingRetryAfterMenuTransition)
		{
			UE_LOG(LogAeyerjiWorldFlow, Display,
				TEXT("AAeyerjiGameState: Menu staging complete; scheduling deferred retry travel for Zone=%s."),
				*PendingRetryZoneId.ToString());
			World->GetTimerManager().ClearTimer(DeferredRetryTravelHandle);
			World->GetTimerManager().SetTimerForNextTick(this, &AAeyerjiGameState::HandleDeferredRetryTravel);
		}
		return true;
	}

	AGameModeBase* GameMode = UGameplayStatics::GetGameMode(this);
	if (!GameMode)
	{
		UE_LOG(LogAeyerjiWorldFlow, Error, TEXT("AAeyerjiGameState: Cannot spawn players - GameMode missing."));
		return false;
	}

	int32 SpawnPlayerIndex = 0;
	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		if (!PC)
		{
			continue;
		}

		APlayerStart* DesiredStart = SelectPlayerStartForZone(ZoneDef.EntryPlayerStartTag, SpawnPlayerIndex);
		if (!DesiredStart)
		{
			UE_LOG(LogAeyerjiWorldFlow, Error, TEXT("AAeyerjiGameState: No PlayerStart found for Zone=%s Tag=%s"),
				*ActiveZoneId.ToString(),
				*ZoneDef.EntryPlayerStartTag.ToString());
			return false;
		}

		if (APawn* ExistingPawn = PC->GetPawn())
		{
			PC->UnPossess();
			ExistingPawn->Destroy();
		}

		if (APlayerState* PS = PC->PlayerState)
		{
			PS->SetIsOnlyASpectator(false);
			PS->SetIsSpectator(false);
		}

		PC->ChangeState(NAME_Playing);
		PC->ClientGotoState(NAME_Playing);

		GameMode->RestartPlayerAtPlayerStart(PC, DesiredStart);
		APawn* SpawnedPawn = PC->GetPawn();
		UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: RestartPlayerAtPlayerStart controller=%s start=%s pawn=%s"),
			*GetNameSafe(PC),
			*GetNameSafe(DesiredStart),
			*GetNameSafe(SpawnedPawn));
		if (!SpawnedPawn)
		{
			SpawnedPawn = GameMode->SpawnDefaultPawnAtTransform(PC, DesiredStart->GetActorTransform());
			if (SpawnedPawn)
			{
				PC->Possess(SpawnedPawn);
				UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: Fallback SpawnDefaultPawnAtTransform created %s for %s"),
					*GetNameSafe(SpawnedPawn),
					*GetNameSafe(PC));
			}
		}

		if (!SpawnedPawn)
		{
			UE_LOG(LogAeyerjiWorldFlow, Error, TEXT("AAeyerjiGameState: Failed to spawn pawn for %s at %s in Zone=%s"),
				*GetNameSafe(PC),
				*GetNameSafe(DesiredStart),
				*ActiveZoneId.ToString());
			return false;
		}

		if (PC->GetPawn() != SpawnedPawn)
		{
			UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: Controller %s did not retain possession of %s after spawn. CurrentPawn=%s"),
				*GetNameSafe(PC),
				*GetNameSafe(SpawnedPawn),
				*GetNameSafe(PC->GetPawn()));
			SpawnedPawn = PC->GetPawn();
		}

		if (!SpawnedPawn)
		{
			UE_LOG(LogAeyerjiWorldFlow, Error, TEXT("AAeyerjiGameState: Spawn succeeded but possession failed for %s in Zone=%s"),
				*GetNameSafe(PC),
				*ActiveZoneId.ToString());
			return false;
		}

		FAeyerjiNavSafetyResolveParams SpawnNavParams;
		SpawnNavParams.ProjectionExtent = FVector(250.f, 250.f, 700.f);
		SpawnNavParams.SearchRadius = 600.f;
		SpawnNavParams.GroundTraceHeight = 400.f;
		SpawnNavParams.GroundTraceDepth = 800.f;

		FAeyerjiNavSafetyResult SpawnNavResult;
		if (!UAeyerjiNavSafetyLibrary::ResolveSafeNavLocationForPawn(this, DesiredStart->GetActorLocation(), SpawnedPawn, SpawnNavParams, SpawnNavResult))
		{
			UE_LOG(LogAeyerjiWorldFlow, Error, TEXT("AAeyerjiGameState: PlayerStart %s for Zone=%s is not on safe nav. Reason=%s Location=%s"),
				*GetNameSafe(DesiredStart),
				*ActiveZoneId.ToString(),
				*SpawnNavResult.FailureReason.ToString(),
				*DesiredStart->GetActorLocation().ToCompactString());
			SpawnedPawn->Destroy();
			return false;
		}

		SpawnedPawn->SetActorLocationAndRotation(
			SpawnNavResult.GroundedLocation,
			DesiredStart->GetActorRotation(),
			false,
			nullptr,
			ETeleportType::TeleportPhysics);

		UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: Spawned %s at %s for Zone=%s"),
			*GetNameSafe(SpawnedPawn),
			*GetNameSafe(DesiredStart),
			*ActiveZoneId.ToString());
		UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: Possessed pawn class=%s controller=%s"),
			*GetNameSafe(SpawnedPawn->GetClass()),
			*GetNameSafe(PC));

		++SpawnPlayerIndex;
	}

	const bool bResolvedGameplayActors = ResolveGameplayActorsForActiveZone();
	UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: ResolveGameplayActorsForActiveZone Zone=%s Result=%s"),
		*ActiveZoneId.ToString(),
		bResolvedGameplayActors ? TEXT("resolved") : TEXT("missing"));

	if (AAeyerjiLevelDirector* LevelDirector = CachedLevelDirector.Get())
	{
		LevelDirector->HandleGameplayZoneActivated();
		if (!CachedEncounterDirector.IsValid())
		{
			BindToLevelDirector();
		}
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		if (AAeyerjiPlayerController* AeyerjiPC = Cast<AAeyerjiPlayerController>(It->Get()))
		{
			AeyerjiPC->RefreshZoneRuntimeReferences();
		}
	}

	if (RunState != EAeyerjiRunState::PreRun)
	{
		SetRunState(EAeyerjiRunState::PreRun);
	}

	if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World))
	{
		for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
		{
			if (ANavMeshBoundsVolume* BoundsVolume = *It)
			{
				NavSys->OnNavigationBoundsUpdated(BoundsVolume);
			}
		}

		ANavigationData* NavData = NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate);
		if (!NavData)
		{
			NavData = NavSys->GetDefaultNavDataInstance(FNavigationSystem::Create);
		}

		const bool bNavBuildPending = UNavigationSystemV1::IsNavigationBeingBuiltOrLocked(this)
			|| NavSys->IsNavigationBuildInProgress()
			|| NavSys->IsNavigationDirty();
		const bool bNavDataMissing = (NavData == nullptr);
		if (bNavBuildPending || bNavDataMissing)
		{
			UE_LOG(LogAeyerjiWorldFlow, Display,
				TEXT("AAeyerjiGameState: Requesting navigation build for Zone=%s (NavBuildPending=%d NavDataMissing=%d)"),
				*ActiveZoneId.ToString(),
				bNavBuildPending ? 1 : 0,
				bNavDataMissing ? 1 : 0);
			NavSys->Build();
		}
	}
	else
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: NavigationSystem missing while activating Zone=%s"),
			*ActiveZoneId.ToString());
	}

	// Gameplay is replicated before auto-start. Starting during spawn setup allowed the
	// director to run while profiles, possession, and streamed boss endpoints were incomplete.
	SetWorldFlowPhase(EAeyerjiWorldFlowPhase::Gameplay);
	MaybeBroadcastZoneGameplayReady();

	const bool bShouldAutoStartRun = ZoneDef.bSpawnPlayerAfterReady && ZoneDef.bAutoStartRun;
	if (bShouldAutoStartRun)
	{
		if (!bResolvedGameplayActors || !CachedLevelDirector.IsValid())
		{
			UE_LOG(LogAeyerjiWorldFlow, Error, TEXT("AAeyerjiGameState: Auto-start run skipped for Zone=%s because LevelDirector could not be resolved."),
				*ActiveZoneId.ToString());
		}
		else
		{
			UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: Scheduling deferred auto-start for Zone=%s TransitionId=%d"),
				*ActiveZoneId.ToString(),
				TransitionId);
			World->GetTimerManager().ClearTimer(DeferredAutoStartRunHandle);
			World->GetTimerManager().SetTimerForNextTick(this, &AAeyerjiGameState::HandleDeferredAutoStartRun);
		}
	}
	else
	{
		UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: Auto-start run skipped for Zone=%s (SpawnAfterReady=%d AutoStart=%d)"),
			*ActiveZoneId.ToString(),
			ZoneDef.bSpawnPlayerAfterReady ? 1 : 0,
			ZoneDef.bAutoStartRun ? 1 : 0);
	}

	UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: Transition completed for gameplay zone %s"), *ActiveZoneId.ToString());
	return true;
}

void AAeyerjiGameState::HandleDeferredAutoStartRun()
{
	if (!HasAuthority() || WorldFlowPhase != EAeyerjiWorldFlowPhase::Gameplay || RunState != EAeyerjiRunState::PreRun)
	{
		return;
	}

	ResolveGameplayActorsForActiveZone();
	FString ReadinessReason;
	if (!ValidateRunStartReadiness(ReadinessReason))
	{
		if (LastAutoStartReadinessReason != ReadinessReason)
		{
			LastAutoStartReadinessReason = ReadinessReason;
			UE_LOG(LogAeyerjiWorldFlow, Display,
				TEXT("[RiftRun][Readiness] Waiting Zone=%s TransitionId=%d Reason=%s"),
				*ActiveZoneId.ToString(), TransitionId, *ReadinessReason);
		}
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(DeferredAutoStartRunHandle, this,
				&AAeyerjiGameState::HandleDeferredAutoStartRun, 0.1f, false);
		}
		return;
	}

	LastAutoStartReadinessReason.Reset();
	if (!Server_StartRun())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(DeferredAutoStartRunHandle, this,
				&AAeyerjiGameState::HandleDeferredAutoStartRun, 0.1f, false);
		}
	}
}

bool AAeyerjiGameState::ResolveGameplayActorsForActiveZone()
{
	if (!HasAuthority())
	{
		return false;
	}

	BindToLevelDirector();

	const bool bResolved = CachedLevelDirector.IsValid();
	if (!bResolved)
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: ResolveGameplayActorsForActiveZone found no LevelDirector for Zone=%s"),
			*ActiveZoneId.ToString());
	}

	return bResolved;
}

void AAeyerjiGameState::BindToLevelDirector()
{
	if (!HasAuthority())
	{
		return;
	}

	AAeyerjiLevelDirector* PreviousLevelDirector = CachedLevelDirector.Get();
	if (PreviousLevelDirector)
	{
		PreviousLevelDirector->OnRunStateChanged.RemoveDynamic(this, &AAeyerjiGameState::HandleLevelDirectorRunActiveChanged);
		PreviousLevelDirector->OnRunTimerExpired.RemoveDynamic(this, &AAeyerjiGameState::HandleLevelDirectorRunTimerExpired);
		PreviousLevelDirector->OnPrimaryObjectiveStateChanged.RemoveDynamic(this, &AAeyerjiGameState::HandlePrimaryObjectiveStateChanged);
	}

	if (AAeyerjiEncounterDirector* PreviousEncounterDirector = CachedEncounterDirector.Get())
	{
		PreviousEncounterDirector->OnProgressChanged.RemoveDynamic(this, &AAeyerjiGameState::HandleEncounterProgressChanged);
	}

	if (AAeyerjiSpawnerGroup* PreviousBossSpawner = CachedBossSpawner.Get())
	{
		PreviousBossSpawner->OnEncounterCleared.RemoveDynamic(this, &AAeyerjiGameState::HandleBossSpawnerCleared);
		PreviousBossSpawner->OnBossDefeated.RemoveDynamic(this, &AAeyerjiGameState::HandleBossSpawnerBossDefeated);
	}

	CachedLevelDirector.Reset();
	CachedEncounterDirector.Reset();
	CachedBossSpawner.Reset();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	for (TActorIterator<AAeyerjiLevelDirector> It(World); It; ++It)
	{
		CachedLevelDirector = *It;
		break;
	}

	AAeyerjiLevelDirector* LevelDirector = CachedLevelDirector.Get();
	if (!IsValid(LevelDirector))
	{
		if (WorldFlowPhase == EAeyerjiWorldFlowPhase::Gameplay)
		{
			UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: BindToLevelDirector could not find a LevelDirector for Zone=%s"),
				*ActiveZoneId.ToString());
		}
		else
		{
			UE_LOG(LogAeyerjiWorldFlow, Verbose, TEXT("AAeyerjiGameState: BindToLevelDirector found no LevelDirector while in %s."),
				WorldFlowPhaseToString(WorldFlowPhase));
		}

		ClearObjectiveStateFromServer();
		return;
	}

	LevelDirector->OnRunStateChanged.RemoveDynamic(this, &AAeyerjiGameState::HandleLevelDirectorRunActiveChanged);
	LevelDirector->OnRunStateChanged.AddDynamic(this, &AAeyerjiGameState::HandleLevelDirectorRunActiveChanged);
	LevelDirector->OnRunTimerExpired.RemoveDynamic(this, &AAeyerjiGameState::HandleLevelDirectorRunTimerExpired);
	LevelDirector->OnRunTimerExpired.AddDynamic(this, &AAeyerjiGameState::HandleLevelDirectorRunTimerExpired);
	LevelDirector->OnPrimaryObjectiveStateChanged.RemoveDynamic(this, &AAeyerjiGameState::HandlePrimaryObjectiveStateChanged);
	LevelDirector->OnPrimaryObjectiveStateChanged.AddDynamic(this, &AAeyerjiGameState::HandlePrimaryObjectiveStateChanged);

	if (AAeyerjiEncounterDirector* EncounterDirector = LevelDirector->GetEncounterDirector())
	{
		CachedEncounterDirector = EncounterDirector;
		EncounterDirector->OnProgressChanged.RemoveDynamic(this, &AAeyerjiGameState::HandleEncounterProgressChanged);
		EncounterDirector->OnProgressChanged.AddDynamic(this, &AAeyerjiGameState::HandleEncounterProgressChanged);
	}

	if (AAeyerjiSpawnerGroup* BossSpawner = LevelDirector->BossSpawner)
	{
		CachedBossSpawner = BossSpawner;
		BossSpawner->OnEncounterCleared.RemoveDynamic(this, &AAeyerjiGameState::HandleBossSpawnerCleared);
		BossSpawner->OnEncounterCleared.AddDynamic(this, &AAeyerjiGameState::HandleBossSpawnerCleared);
		BossSpawner->OnBossDefeated.RemoveDynamic(this, &AAeyerjiGameState::HandleBossSpawnerBossDefeated);
		BossSpawner->OnBossDefeated.AddDynamic(this, &AAeyerjiGameState::HandleBossSpawnerBossDefeated);
	}

	UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: Bound LevelDirector=%s EncounterDirector=%s BossSpawner=%s for Zone=%s"),
		*GetNameSafe(LevelDirector),
		*GetNameSafe(CachedEncounterDirector.Get()),
		*GetNameSafe(CachedBossSpawner.Get()),
		*ActiveZoneId.ToString());

	RefreshObjectiveStateFromAuthority();
}

void AAeyerjiGameState::RefreshObjectiveStateFromAuthority()
{
	if (!HasAuthority())
	{
		return;
	}

	if (WorldFlowPhase != EAeyerjiWorldFlowPhase::Gameplay)
	{
		ClearObjectiveStateFromServer();
		return;
	}

	if (AAeyerjiEncounterDirector* EncounterDirector = CachedEncounterDirector.Get())
	{
		EncounterDirector->PushObjectiveStateToGameState();
		return;
	}

	ClearObjectiveStateFromServer();
}

void AAeyerjiGameState::SnapshotRunResults(const EAeyerjiRunResolution Resolution, const bool bBossDefeated)
{
	if (RiftRunState.RunSerial > 0 && ResultsFinalizedRunSerial == RiftRunState.RunSerial)
	{
		UE_LOG(LogAeyerjiWorldFlow, Display,
			TEXT("[RiftRun][Results] Duplicate snapshot ignored RunSerial=%d"), RiftRunState.RunSerial);
		return;
	}

	FAeyerjiRunResults NewResults;
	NewResults.ResultsVersion = NextResultsVersion++;
	NewResults.RunSerial = RiftRunState.RunSerial;
	NewResults.bBossDefeated = bBossDefeated;
	NewResults.Resolution = Resolution;
	NewResults.CompletedZoneId = ActiveZoneId;
	NewResults.SelectedRiftTier = RiftRunState.SelectedRiftTier;
	NewResults.bCompletedInTime = RiftRunState.bCompletedInTime;
	NewResults.bOvertime = RiftRunState.bOvertime;
	NewResults.bBossPhaseDeathOccurred = RiftRunState.bBossPhaseDeathOccurred;
	NewResults.bFlawlessRewardEarned = RiftRunState.bCompletedInTime && !RiftRunState.bBossPhaseDeathOccurred;
	NewResults.EarnedNextRiftTier = RiftRunState.EarnedNextRiftTier;

	if (AAeyerjiLevelDirector* LevelDirector = CachedLevelDirector.Get())
	{
		NewResults.RunTimeSeconds = LevelDirector->GetRunTimeSeconds();
		NewResults.DifficultySlider = LevelDirector->GetDifficultySlider();
		NewResults.ShardsCollected = LevelDirector->GetShardCount();
		NewResults.TimeLimitSeconds = LevelDirector->RunTimeLimitSeconds;
		NewResults.TimeRemainingSeconds = LevelDirector->GetRemainingRunTimeSeconds();
		NewResults.UnitsKillTarget = LevelDirector->GetEffectiveObjectiveKillTargetRaw();
	}

	if (AAeyerjiEncounterDirector* EncounterDirector = CachedEncounterDirector.Get())
	{
		NewResults.UnitsKilled = EncounterDirector->GetKilledCount();
		NewResults.EnemiesDefeated = CurrentObjectiveState.EnemiesDefeated;
		NewResults.ProgressPoints = CurrentObjectiveState.ProgressPoints;
		NewResults.ProgressPointTarget = CurrentObjectiveState.ProgressPointTarget;
		if (NewResults.UnitsKillTarget <= 0)
		{
			NewResults.UnitsKillTarget = EncounterDirector->GetTotalToKillRaw();
		}
	}

	if (NewResults.TimeLimitSeconds > 0.f)
	{
		NewResults.TimeRemainingSeconds = FMath::Max(NewResults.TimeLimitSeconds - NewResults.RunTimeSeconds, 0.f);
		NewResults.SpeedBonusPercent = FMath::Clamp(NewResults.TimeRemainingSeconds / NewResults.TimeLimitSeconds, 0.f, 1.f) * 100.f;
	}

	RunResults = NewResults;
	ResultsFinalizedRunSerial = RiftRunState.RunSerial;
	UE_LOG(LogAeyerjiWorldFlow, Display,
		TEXT("[RiftRun][Results] Finalized RunSerial=%d Result=%d Tier=%d Progress=%d/%d InTime=%d Flawless=%d"),
		RiftRunState.RunSerial, static_cast<int32>(Resolution), NewResults.SelectedRiftTier,
		NewResults.ProgressPoints, NewResults.ProgressPointTarget, NewResults.bCompletedInTime ? 1 : 0,
		NewResults.bFlawlessRewardEarned ? 1 : 0);
	ForceNetUpdate();
}

void AAeyerjiGameState::BroadcastObjectiveEventCompleted(const EAeyerjiObjectiveEvent CompletedEvent)
{
	if (!HasAuthority() || CompletedEvent == EAeyerjiObjectiveEvent::None)
	{
		return;
	}

	LastCompletedObjectiveEvent = CompletedEvent;
	ObjectiveEventVersion = FMath::Max(1, ObjectiveEventVersion + 1);
	OnObjectiveEventCompleted.Broadcast(LastCompletedObjectiveEvent);
	ForceNetUpdate();
}

void AAeyerjiGameState::MaybeBroadcastRunResults()
{
	if (RunState != EAeyerjiRunState::RunComplete)
	{
		return;
	}

	if (RunResults.ResultsVersion <= 0 || RunResults.ResultsVersion == LastBroadcastResultsVersion)
	{
		return;
	}

	LastBroadcastResultsVersion = RunResults.ResultsVersion;
	OnRunResultsReady.Broadcast(RunResults);
}

bool AAeyerjiGameState::SetRunState(EAeyerjiRunState NewState)
{
	if (!HasAuthority())
	{
		return false;
	}

	if (RunState == NewState)
	{
		return true;
	}

	if (!CanTransitionTo(NewState))
	{
		UE_LOG(LogTemp, Warning, TEXT("AAeyerjiGameState: invalid run state transition %s -> %s"),
		       RunStateToString(RunState),
		       RunStateToString(NewState));
		return false;
	}

	const EAeyerjiRunState OldState = RunState;
	RunState = NewState;
	HandleRunStateChanged(OldState);
	ForceNetUpdate();
	return true;
}

bool AAeyerjiGameState::CanTransitionTo(EAeyerjiRunState NewState) const
{
	if (RunState == NewState)
	{
		return true;
	}

	if (NewState == EAeyerjiRunState::PreRun)
	{
		return true;
	}

	switch (RunState)
	{
	case EAeyerjiRunState::PreRun:
		return (NewState == EAeyerjiRunState::InRun) || (NewState == EAeyerjiRunState::RunComplete) || (NewState == EAeyerjiRunState::ReturnToMenu);
	case EAeyerjiRunState::InRun:
		return (NewState == EAeyerjiRunState::BossDefeated) || (NewState == EAeyerjiRunState::ObjectiveComplete) || (NewState == EAeyerjiRunState::RunComplete);
	case EAeyerjiRunState::BossDefeated:
		return (NewState == EAeyerjiRunState::ObjectiveComplete) || (NewState == EAeyerjiRunState::RunComplete);
	case EAeyerjiRunState::ObjectiveComplete:
		return (NewState == EAeyerjiRunState::RunComplete);
	case EAeyerjiRunState::RunComplete:
		return (NewState == EAeyerjiRunState::ReturnToMenu);
	case EAeyerjiRunState::ReturnToMenu:
		return false;
	default:
		return false;
	}
}

void AAeyerjiGameState::HandleRunStateChanged(EAeyerjiRunState OldState)
{
	UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: RunState changed %s -> %s (HasAuthority=%d)."),
		RunStateToString(OldState),
		RunStateToString(RunState),
		HasAuthority() ? 1 : 0);

	OnRunStateChanged.Broadcast(RunState, OldState);

	if (HasAuthority())
	{
		PublishRunLifecycleWorldState(OldState);
	}

	if (RunState == EAeyerjiRunState::InRun && HasAuthority())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(BossDefeatedDelayHandle);
			World->GetTimerManager().ClearTimer(AutoReturnDelayHandle);
		}

		ClearEndRunPortal();
		RunResults = FAeyerjiRunResults();
		LastBroadcastResultsVersion = 0;
	}

	if (RunState == EAeyerjiRunState::ObjectiveComplete && HasAuthority())
	{
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(BossDefeatedDelayHandle);
			World->GetTimerManager().ClearTimer(AutoReturnDelayHandle);
		}

		UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: RunState ObjectiveComplete reached; attempting portal spawn."));
		SpawnEndRunPortal();
	}

	if (RunState == EAeyerjiRunState::RunComplete && HasAuthority())
	{
		ClearEndRunPortal();
	}

	if (RunState == EAeyerjiRunState::ReturnToMenu)
	{
		if (HasAuthority())
		{
			if (UWorld* World = GetWorld())
			{
				World->GetTimerManager().ClearTimer(BossDefeatedDelayHandle);
				World->GetTimerManager().ClearTimer(AutoReturnDelayHandle);
			}

			ClearEndRunPortal();
		}

		CleanupUIAndReturnToMenu();
	}

	if (!(HasAuthority() && RunState == EAeyerjiRunState::RunComplete))
	{
		MaybeBroadcastRunResults();
	}

	if (HasAuthority())
	{
		RefreshObjectiveStateFromAuthority();
	}
}

void AAeyerjiGameState::PublishRunLifecycleWorldState(const EAeyerjiRunState OldState)
{
	if (!HasAuthority())
	{
		return;
	}

	UAeyerjiWorldStateSubsystem* WorldStateSubsystem = UAeyerjiWorldStateSubsystem::Get(this);
	if (!WorldStateSubsystem)
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: Cannot publish run lifecycle facts because WorldStateSubsystem is missing."));
		return;
	}

	const FName StateName(RunStateToString(RunState));
	WorldStateSubsystem->SetValue(
		FAeyerjiWorldStateKey(AeyerjiTags::Run_State),
		FAeyerjiWorldStateValue::FromName(StateName),
		EAeyerjiWorldStatePersistence::RuntimeOnly,
		EAeyerjiWorldStateReplication::PublicReplicated,
		EAeyerjiWorldStateScope::Run);

	if (!ActiveZoneId.IsNone())
	{
		WorldStateSubsystem->SetValue(
			FAeyerjiWorldStateKey(AeyerjiTags::Run_Zone),
			FAeyerjiWorldStateValue::FromName(ActiveZoneId),
			EAeyerjiWorldStatePersistence::RuntimeOnly,
			EAeyerjiWorldStateReplication::PublicReplicated,
			EAeyerjiWorldStateScope::Run);
	}

	switch (RunState)
	{
	case EAeyerjiRunState::InRun:
		WorldStateSubsystem->MarkEventHappened(
			AeyerjiTags::Run_Event_Started,
			NAME_None,
			EAeyerjiWorldStatePersistence::RuntimeOnly,
			EAeyerjiWorldStateReplication::PublicReplicated,
			EAeyerjiWorldStateScope::Run);
		break;
	case EAeyerjiRunState::BossDefeated:
		WorldStateSubsystem->MarkEventHappened(
			AeyerjiTags::Run_Event_BossDefeated,
			NAME_None,
			EAeyerjiWorldStatePersistence::RuntimeOnly,
			EAeyerjiWorldStateReplication::PublicReplicated,
			EAeyerjiWorldStateScope::Run);
		break;
	case EAeyerjiRunState::ObjectiveComplete:
		WorldStateSubsystem->MarkEventHappened(
			AeyerjiTags::Run_Event_ObjectiveComplete,
			NAME_None,
			EAeyerjiWorldStatePersistence::RuntimeOnly,
			EAeyerjiWorldStateReplication::PublicReplicated,
			EAeyerjiWorldStateScope::Run);
		break;
	case EAeyerjiRunState::RunComplete:
	{
		FName ResultName(TEXT("Unknown"));
		switch (RunResults.Resolution)
		{
		case EAeyerjiRunResolution::Victory:
			ResultName = TEXT("Victory");
			WorldStateSubsystem->MarkEventHappened(
				AeyerjiTags::Run_Event_Completed,
				NAME_None,
				EAeyerjiWorldStatePersistence::RuntimeOnly,
				EAeyerjiWorldStateReplication::PublicReplicated,
				EAeyerjiWorldStateScope::Run);
			break;
		case EAeyerjiRunResolution::TimeExpired:
			ResultName = TEXT("TimeExpired");
			WorldStateSubsystem->MarkEventHappened(
				AeyerjiTags::Run_Event_Failed,
				NAME_None,
				EAeyerjiWorldStatePersistence::RuntimeOnly,
				EAeyerjiWorldStateReplication::PublicReplicated,
				EAeyerjiWorldStateScope::Run);
			break;
		case EAeyerjiRunResolution::DefenseObjectiveDestroyed:
			ResultName = TEXT("DefenseObjectiveDestroyed");
			WorldStateSubsystem->MarkEventHappened(
				AeyerjiTags::Run_Event_Failed,
				NAME_None,
				EAeyerjiWorldStatePersistence::RuntimeOnly,
				EAeyerjiWorldStateReplication::PublicReplicated,
				EAeyerjiWorldStateScope::Run);
			break;
		case EAeyerjiRunResolution::Abandoned:
			ResultName = TEXT("Abandoned");
			WorldStateSubsystem->MarkEventHappened(
				AeyerjiTags::Run_Event_Abandoned,
				NAME_None,
				EAeyerjiWorldStatePersistence::RuntimeOnly,
				EAeyerjiWorldStateReplication::PublicReplicated,
				EAeyerjiWorldStateScope::Run);
			break;
		default:
			break;
		}

		WorldStateSubsystem->SetValue(
			FAeyerjiWorldStateKey(AeyerjiTags::Run_Result),
			FAeyerjiWorldStateValue::FromName(ResultName),
			EAeyerjiWorldStatePersistence::RuntimeOnly,
			EAeyerjiWorldStateReplication::PublicReplicated,
			EAeyerjiWorldStateScope::Run);
		break;
	}
	default:
		break;
	}

	UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: Published run world-state facts Old=%s New=%s Zone=%s."),
		RunStateToString(OldState),
		RunStateToString(RunState),
		*ActiveZoneId.ToString());
}

void AAeyerjiGameState::CleanupUIAndReturnToMenu()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (UAeyerjiStreamingSubsystem* StreamingSubsystem = GetStreamingSubsystem())
	{
		const FName MenuZoneId = ResolveMenuZoneId();

		if (!MenuZoneId.IsNone())
		{
			if (HasAuthority())
			{
				Server_BeginWorldTransition(MenuZoneId);
			}
			return;
		}
	}

	if (World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	// Streaming menu returns keep the existing menu widget tree alive; only hard travel needs a full widget purge.
	UWidgetLayoutLibrary::RemoveAllWidgets(this);

	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
	{
		return;
	}

	if (!MainMenuTravelURL.IsEmpty())
	{
		PC->ClientTravel(MainMenuTravelURL, TRAVEL_Absolute);
		return;
	}

	if (UGameInstance* GI = GetGameInstance())
	{
		GI->ReturnToMainMenu();
	}
}

void AAeyerjiGameState::SpawnEndRunPortal()
{
	if (!HasAuthority())
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: SpawnEndRunPortal rejected on non-authority."));
		return;
	}

	if (CachedEndRunPortal.IsValid())
	{
		UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: SpawnEndRunPortal skipped - portal already exists (%s)."),
			*GetNameSafe(CachedEndRunPortal.Get()));
		return;
	}

	UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: SpawnEndRunPortal begin (RunState=%s Zone=%s)."),
		RunStateToString(RunState),
		*ActiveZoneId.ToString());

	AAeyerjiLevelDirector* LevelDirector = CachedLevelDirector.Get();
	if (!IsValid(LevelDirector))
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: SpawnEndRunPortal skipped - LevelDirector missing."));
		return;
	}

	if (!LevelDirector->EndRunPortalClass)
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: SpawnEndRunPortal skipped - EndRunPortalClass is not configured on %s."),
			*GetNameSafe(LevelDirector));
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: SpawnEndRunPortal skipped - World is null."));
		return;
	}

	UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	FTransform SpawnTransform = FTransform::Identity;
	bool bHasSpawnTransform = false;

	if (AActor* SpawnPoint = LevelDirector->EndRunPortalSpawnPoint)
	{
		SpawnTransform = SpawnPoint->GetActorTransform();
		bHasSpawnTransform = true;

		if (NavigationSystem)
		{
			FNavLocation ProjectedLocation;
			if (NavigationSystem->ProjectPointToNavigation(SpawnTransform.GetLocation(), ProjectedLocation, FVector(200.f, 200.f, 500.f)))
			{
				SpawnTransform.SetLocation(ProjectedLocation.Location);
			}
			else
			{
				UE_LOG(LogAeyerjiWorldFlow, Warning,
					TEXT("AAeyerjiGameState: EndRunPortalSpawnPoint %s is not on navigable ground. Falling back to dynamic portal placement."),
					*GetNameSafe(SpawnPoint));
				bHasSpawnTransform = false;
			}
		}
	}

	if (!bHasSpawnTransform)
	{
		if (APawn* PlayerPawn = UGameplayStatics::GetPlayerPawn(this, 0))
		{
			SpawnTransform = PlayerPawn->GetActorTransform();

			FVector ProjectedSpawnLocation = PlayerPawn->GetActorLocation();
			if (FindProjectedPortalSpawnLocation(NavigationSystem, PlayerPawn, 750.f, ProjectedSpawnLocation))
			{
				SpawnTransform.SetLocation(ProjectedSpawnLocation);
				bHasSpawnTransform = true;
			}
			else
			{
				FVector Forward = PlayerPawn->GetActorForwardVector().GetSafeNormal2D();
				if (Forward.IsNearlyZero())
				{
					Forward = FVector::ForwardVector;
				}

				SpawnTransform.SetLocation(PlayerPawn->GetActorLocation() + (Forward * 750.f));
				bHasSpawnTransform = true;

				UE_LOG(LogAeyerjiWorldFlow, Warning,
					TEXT("AAeyerjiGameState: Failed to project a navigable portal spawn location for %s. Using a raw fallback position."),
					*GetNameSafe(PlayerPawn));
			}
		}
		else
		{
			UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: SpawnEndRunPortal could not resolve PlayerPawn for dynamic placement."));
		}
	}

	if (!bHasSpawnTransform)
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: SpawnEndRunPortal skipped - no valid spawn transform could be resolved."));
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	if (AAeyerjiEndRunPortal* SpawnedPortal = World->SpawnActor<AAeyerjiEndRunPortal>(LevelDirector->EndRunPortalClass, SpawnTransform, SpawnParams))
	{
		CachedEndRunPortal = SpawnedPortal;
		UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: SpawnEndRunPortal spawned %s at %s."),
			*GetNameSafe(SpawnedPortal),
			*SpawnTransform.GetLocation().ToCompactString());
	}
	else
	{
		UE_LOG(LogAeyerjiWorldFlow, Error, TEXT("AAeyerjiGameState: SpawnEndRunPortal failed - SpawnActor returned null (Class=%s Location=%s)."),
			*GetNameSafe(LevelDirector->EndRunPortalClass),
			*SpawnTransform.GetLocation().ToCompactString());
	}
}

void AAeyerjiGameState::ClearEndRunPortal()
{
	if (AAeyerjiEndRunPortal* Portal = CachedEndRunPortal.Get())
	{
		Portal->Destroy();
	}

	CachedEndRunPortal.Reset();
}

void AAeyerjiGameState::PersistRunResultsForPlayers()
{
	if (!HasAuthority() || RunResults.ResultsVersion <= 0)
	{
		return;
	}
	if (RunResults.RunSerial > 0 && PersistedRunSerial == RunResults.RunSerial)
	{
		UE_LOG(LogAeyerjiWorldFlow, Display,
			TEXT("[RiftRun][Save] Duplicate commit ignored RunSerial=%d"), RunResults.RunSerial);
		return;
	}

	RunResults.BestTimeForDifficultySeconds = 0.f;

	if (RunParticipants.IsEmpty())
	{
		for (APlayerState* PlayerState : PlayerArray)
		{
			if (AAeyerjiPlayerState* Participant = Cast<AAeyerjiPlayerState>(PlayerState))
			{
				RunParticipants.Add(Participant);
			}
		}
	}

	for (const TWeakObjectPtr<AAeyerjiPlayerState>& ParticipantPtr : RunParticipants)
	{
		AAeyerjiPlayerState* AeyerjiPlayerState = ParticipantPtr.Get();
		if (!IsValid(AeyerjiPlayerState))
		{
			UE_LOG(LogAeyerjiWorldFlow, Warning,
				TEXT("[RiftRun][Save] Participant disconnected before commit RunSerial=%d"), RunResults.RunSerial);
			continue;
		}

		FAeyerjiRunResults PersonalResults = RunResults;
		const int32 PreviousHighestTier = AeyerjiPlayerState->GetHighestUnlockedRiftTier();
		const int32 AdvancedHighestTier = AeyerjiRiftRules::ResolveHighestUnlockedTier(
			PreviousHighestTier,
			RunResults.SelectedRiftTier,
			RunResults.Resolution == EAeyerjiRunResolution::Victory,
			RunResults.bCompletedInTime);
		AeyerjiPlayerState->SetRiftProgressionFromServer(AdvancedHighestTier, RunResults.SelectedRiftTier);
		PersonalResults.HighestUnlockedRiftTier = AdvancedHighestTier;
		PersonalResults.bNewRiftTierUnlockedForProfile = AdvancedHighestTier > PreviousHighestTier;
		if (const FRiftPlayerRewardLedger* RewardLedger = RiftRewardLedger.Find(ParticipantPtr))
		{
			PersonalResults.BaseRewardRolls = RewardLedger->BaseResults.Num();
			PersonalResults.TimedRewardRolls = RewardLedger->TimedResults.Num();
			PersonalResults.FlawlessRewardRolls = RewardLedger->FlawlessResults.Num();
		}
		AeyerjiPlayerState->SetPersonalRunResultsFromServer(PersonalResults);

		const bool bSaved = UCharacterStatsLibrary::RecordCompletedRunAndSaveCharacter(AeyerjiPlayerState, PersonalResults);
		UE_LOG(LogAeyerjiWorldFlow, Display,
			TEXT("[RiftRun][Save] RunSerial=%d Player=%s Saved=%d Tier=%d Highest=%d"),
			RunResults.RunSerial, *GetNameSafe(AeyerjiPlayerState), bSaved ? 1 : 0,
			RunResults.SelectedRiftTier, AdvancedHighestTier);

		float BestTimeForDifficulty = 0.f;
		if (RunResults.BestTimeForDifficultySeconds <= 0.f
			&& UCharacterStatsLibrary::GetBestRunTimeSecondsForDifficulty(AeyerjiPlayerState, RunResults.DifficultySlider, BestTimeForDifficulty))
		{
			RunResults.BestTimeForDifficultySeconds = BestTimeForDifficulty;
		}
	}
	PersistedRunSerial = RunResults.RunSerial;

	if (RunState == EAeyerjiRunState::RunComplete)
	{
		RunResults.ResultsVersion = NextResultsVersion++;
	}

	ForceNetUpdate();
}

void AAeyerjiGameState::FinalizeCompletedRunWorld()
{
	if (!HasAuthority())
	{
		return;
	}

	if (AAeyerjiLevelDirector* LevelDirector = CachedLevelDirector.Get())
	{
		LevelDirector->EndRun();
	}

	if (UWorld* World = GetWorld())
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			AAeyerjiPlayerController* PlayerController = Cast<AAeyerjiPlayerController>(It->Get());
			if (!IsValid(PlayerController))
			{
				continue;
			}

			PlayerController->AbortMovement_Both();

			if (APawn* Pawn = PlayerController->GetPawn())
			{
				if (UCharacterMovementComponent* MovementComponent = Cast<UCharacterMovementComponent>(Pawn->GetMovementComponent()))
				{
					MovementComponent->DisableMovement();
					MovementComponent->StopMovementImmediately();
				}
			}
		}
	}

	StopRemainingRunEnemiesForCompletedRun();
	ScheduleDeferredDestroyRemainingRunEnemies();
}

void AAeyerjiGameState::StopRemainingRunEnemiesForCompletedRun()
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

	for (TActorIterator<AEnemyParentNative> It(World); It; ++It)
	{
		AEnemyParentNative* Enemy = *It;
		if (!IsValid(Enemy) || Enemy->IsActorBeingDestroyed())
		{
			continue;
		}

		if (AAIController* AIController = Cast<AAIController>(Enemy->GetController()))
		{
			AIController->StopMovement();
			AIController->ClearFocus(EAIFocusPriority::Gameplay);
			AIController->ClearFocus(EAIFocusPriority::Move);
		}
		else if (AController* Controller = Enemy->GetController())
		{
			Controller->StopMovement();
		}

		if (UCharacterMovementComponent* MovementComponent = Enemy->GetCharacterMovement())
		{
			MovementComponent->StopMovementImmediately();
			MovementComponent->DisableMovement();
		}

		if (UAbilitySystemComponent* ASC = Enemy->GetAbilitySystemComponent())
		{
			ASC->CancelAllAbilities();
		}

		Enemy->SetActorTickEnabled(false);
		Enemy->SetActorEnableCollision(false);
		if (UCapsuleComponent* Capsule = Enemy->GetCapsuleComponent())
		{
			Capsule->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
	}
}

void AAeyerjiGameState::ScheduleDeferredDestroyRemainingRunEnemies()
{
	if (!HasAuthority())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || World->GetTimerManager().IsTimerActive(DeferredDestroyRunEnemiesHandle))
	{
		return;
	}

	DeferredDestroyRunEnemiesHandle = World->GetTimerManager().SetTimerForNextTick(this, &AAeyerjiGameState::DestroyRemainingRunEnemies);
}

void AAeyerjiGameState::DestroyRemainingRunEnemies()
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

	World->GetTimerManager().ClearTimer(DeferredDestroyRunEnemiesHandle);

	for (TActorIterator<AEnemyParentNative> It(World); It; ++It)
	{
		AEnemyParentNative* Enemy = *It;
		if (!IsValid(Enemy) || Enemy->IsActorBeingDestroyed())
		{
			continue;
		}

		Enemy->Destroy();
	}
}

void AAeyerjiGameState::DestroyPersistentRuntimeActorsForFreshSession()
{
	if (!HasAuthority())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !World->PersistentLevel)
	{
		return;
	}

	TArray<TWeakObjectPtr<AActor>> ActorsToDestroy;
	int32 DestroyedLootPickups = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor) || Actor->IsPendingKillPending())
		{
			continue;
		}

		if (Actor == this
			|| Actor == World->GetAuthGameMode()
			|| Actor == World->GetGameState()
			|| Actor->GetLevel() != World->PersistentLevel
			|| Actor->IsNetStartupActor())
		{
			continue;
		}

		if (Actor->IsA<AController>()
			|| Actor->IsA<APawn>()
			|| Actor->IsA<AInfo>()
			|| Actor->IsA<ALevelScriptActor>()
			|| Actor->IsA<AWorldSettings>())
		{
			continue;
		}

		if (Actor->IsA<AAeyerjiLootPickup>())
		{
			++DestroyedLootPickups;
		}

		ActorsToDestroy.Add(Actor);
	}

	for (const TWeakObjectPtr<AActor>& ActorPtr : ActorsToDestroy)
	{
		if (AActor* Actor = ActorPtr.Get())
		{
			Actor->Destroy();
		}
	}

	UE_LOG(LogAeyerjiWorldFlow, Display,
		TEXT("AAeyerjiGameState: DestroyPersistentRuntimeActorsForFreshSession destroyed %d actors (LootPickups=%d)."),
		ActorsToDestroy.Num(),
		DestroyedLootPickups);
}

void AAeyerjiGameState::HandleLevelDirectorRunActiveChanged(bool bIsRunning)
{
	if (!HasAuthority())
	{
		return;
	}

	if (bIsRunning)
	{
		SetRunState(EAeyerjiRunState::InRun);
	}
}

void AAeyerjiGameState::HandleBossSpawnerCleared(AAeyerjiSpawnerGroup* Spawner)
{
	if (!HasAuthority())
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: HandleBossSpawnerCleared ignored on non-authority (Spawner=%s)."),
			*GetNameSafe(Spawner));
		return;
	}

	if (!CachedBossSpawner.IsValid() || Spawner != CachedBossSpawner.Get())
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: HandleBossSpawnerCleared ignored (Incoming=%s Cached=%s CachedValid=%d)."),
			*GetNameSafe(Spawner),
			*GetNameSafe(CachedBossSpawner.Get()),
			CachedBossSpawner.IsValid() ? 1 : 0);
		return;
	}

	if (RunState == EAeyerjiRunState::InRun
		&& CachedLevelDirector.IsValid())
	{
		const EAeyerjiRunWinCondition WinCondition = CachedLevelDirector->GetRunWinCondition();
		const bool bBossClearCanCompleteRun = WinCondition == EAeyerjiRunWinCondition::BossCleared
			|| (WinCondition == EAeyerjiRunWinCondition::KillTargetThenBoss && CachedLevelDirector->IsPrimaryObjectiveComplete());
		if (bBossClearCanCompleteRun)
		{
			UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: Boss spawner cleared; notifying boss defeated (RunState=%s WinCondition=%d)."),
				RunStateToString(RunState),
				static_cast<int32>(WinCondition));
			Server_NotifyBossDefeated();
			return;
		}
	}

	const EAeyerjiRunWinCondition WinCondition = CachedLevelDirector.IsValid()
		? CachedLevelDirector->GetRunWinCondition()
		: EAeyerjiRunWinCondition::BossCleared;
	UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: Boss spawner cleared but conditions not met (RunState=%s LevelDirectorValid=%d WinCondition=%d PrimaryObjectiveComplete=%d)."),
		RunStateToString(RunState),
		CachedLevelDirector.IsValid() ? 1 : 0,
		static_cast<int32>(WinCondition),
		CachedLevelDirector.IsValid() && CachedLevelDirector->IsPrimaryObjectiveComplete() ? 1 : 0);
}

void AAeyerjiGameState::HandleBossSpawnerBossDefeated(AAeyerjiSpawnerGroup* Spawner, AActor* BossEnemy)
{
	if (!HasAuthority())
	{
		return;
	}

	if (!CachedBossSpawner.IsValid() || Spawner != CachedBossSpawner.Get())
	{
		return;
	}

	if (RunState != EAeyerjiRunState::InRun || !CachedLevelDirector.IsValid())
	{
		return;
	}

	const EAeyerjiRunWinCondition WinCondition = CachedLevelDirector->GetRunWinCondition();
	const bool bBossDeathCanCompleteRun = WinCondition == EAeyerjiRunWinCondition::BossCleared
		|| (WinCondition == EAeyerjiRunWinCondition::KillTargetThenBoss && CachedLevelDirector->IsPrimaryObjectiveComplete());
	if (!bBossDeathCanCompleteRun)
	{
		return;
	}

	UE_LOG(LogAeyerjiWorldFlow, Display,
		TEXT("AAeyerjiGameState: Boss defeated via spawner boss-death event (Spawner=%s Boss=%s RunState=%s WinCondition=%d)."),
		*GetNameSafe(Spawner),
		*GetNameSafe(BossEnemy),
		RunStateToString(RunState),
		static_cast<int32>(WinCondition));
	Server_NotifyBossDefeated();
}

void AAeyerjiGameState::HandleLevelDirectorRunTimerExpired()
{
	if (!HasAuthority() || RunState != EAeyerjiRunState::InRun)
	{
		return;
	}

	if (RiftRunState.RunSerial > 0)
	{
		if (!RiftRunState.bOvertime)
		{
			RiftRunState.bOvertime = true;
			RiftRunState.bCompletedInTime = false;
			RiftRunState.Revision = FMath::Max(RiftRunState.Revision + 1, 1);
			OnRep_RiftRunState();
			ForceNetUpdate();
			UE_LOG(LogAeyerjiWorldFlow, Display,
				TEXT("[RiftRun][Timer] Overtime began RunSerial=%d Elapsed=%.3f Limit=%.3f; run continues"),
				RiftRunState.RunSerial, GetAuthoritativeRunElapsedSeconds(), RiftRunState.TimeLimitSeconds);
		}
		return;
	}

	Server_FailRunTimeExpired();
}

void AAeyerjiGameState::HandleEncounterProgressChanged(const float Progress01, const int32 Killed, const int32 Total)
{
	static_cast<void>(Progress01);

	if (!HasAuthority() || RunState != EAeyerjiRunState::InRun || !CachedLevelDirector.IsValid())
	{
		return;
	}

	const EAeyerjiRunWinCondition WinCondition = CachedLevelDirector->GetRunWinCondition();
	if (WinCondition != EAeyerjiRunWinCondition::KillTarget
		&& WinCondition != EAeyerjiRunWinCondition::KillTargetThenBoss)
	{
		return;
	}

	const int32 EffectiveObjectiveTarget = CachedLevelDirector->GetEffectiveObjectiveKillTargetRaw();
	const int32 RequiredKills = EffectiveObjectiveTarget > 0 ? EffectiveObjectiveTarget : Total;
	if (RequiredKills <= 0 || Killed < RequiredKills)
	{
		return;
	}
	if (CachedLevelDirector->SpawnMode == EAeyerjiLevelSpawnMode::ProximityEncounterRegions)
	{
		if (CachedEncounterDirector.IsValid())
		{
			CachedEncounterDirector->FreezeWeightedProgress();
		}
		CachedLevelDirector->DisableUnopenedRiftEncounterRegions();
		UE_LOG(LogAeyerjiWorldFlow, Display,
			TEXT("[RiftRun][Progress] Complete RunSerial=%d Points=%d/%d; unused regions disabled"),
			RiftRunState.RunSerial, Killed, RequiredKills);
		Server_BeginBossPhase();
		return;
	}

	if (WinCondition == EAeyerjiRunWinCondition::KillTarget)
	{
		Server_BeginObjectiveComplete();
		return;
	}

	UE_LOG(LogAeyerjiWorldFlow, Display,
		TEXT("AAeyerjiGameState: KillTargetThenBoss threshold reached (Killed=%d Required=%d Total=%d)."),
		Killed,
		RequiredKills,
		Total);

	if (!CachedLevelDirector->IsPrimaryObjectiveComplete())
	{
		BroadcastObjectiveEventCompleted(EAeyerjiObjectiveEvent::PrimaryObjectiveComplete);
		CachedLevelDirector->MarkPrimaryObjectiveComplete();
	}
	else
	{
		const bool bBossAlreadySpawned = CachedEncounterDirector.IsValid() && CachedEncounterDirector->IsBossSpawned();
		if (!CachedLevelDirector->HasBossEncounterBeenTriggered() && !bBossAlreadySpawned)
		{
			CachedLevelDirector->OpenBossGate();
		}
	}

	if (!CachedBossSpawner.IsValid() && CachedLevelDirector->BossSpawner)
	{
		CachedBossSpawner = CachedLevelDirector->BossSpawner;
		CachedBossSpawner->OnEncounterCleared.RemoveDynamic(this, &AAeyerjiGameState::HandleBossSpawnerCleared);
		CachedBossSpawner->OnEncounterCleared.AddDynamic(this, &AAeyerjiGameState::HandleBossSpawnerCleared);
		CachedBossSpawner->OnBossDefeated.RemoveDynamic(this, &AAeyerjiGameState::HandleBossSpawnerBossDefeated);
		CachedBossSpawner->OnBossDefeated.AddDynamic(this, &AAeyerjiGameState::HandleBossSpawnerBossDefeated);
	}

	if (!CachedBossSpawner.IsValid())
	{
		UE_LOG(LogAeyerjiWorldFlow, Error,
			TEXT("AAeyerjiGameState: KillTargetThenBoss objective reached but no boss spawner is configured/available after opening the boss gate."));
		return;
	}

	if (CachedBossSpawner->IsCleared())
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning,
			TEXT("AAeyerjiGameState: KillTargetThenBoss boss spawner was already cleared after opening the boss gate; treating this as boss defeated."));
		Server_NotifyBossDefeated();
	}
}

void AAeyerjiGameState::HandlePrimaryObjectiveStateChanged(const bool bIsComplete)
{
	static_cast<void>(bIsComplete);

	if (!HasAuthority())
	{
		return;
	}

	RefreshObjectiveStateFromAuthority();
}

void AAeyerjiGameState::HandleStreamingZoneReady(const FName ZoneId)
{
	if (WorldFlowPhase != EAeyerjiWorldFlowPhase::TransitionLoading)
	{
		return;
	}

	if (ZoneId != ActiveZoneId)
	{
		return;
	}

	UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: Local zone ready for Zone=%s TransitionId=%d NetMode=%d"),
		*ZoneId.ToString(),
		TransitionId,
		GetWorld() ? static_cast<int32>(GetWorld()->GetNetMode()) : -1);

	if (HasAuthority())
	{
		bServerZoneReady = true;
		MarkLocalPlayersReadyForTransition();
		TryCompleteWorldTransition();
		return;
	}

	if (TransitionId <= 0 || TransitionId == LastReportedReadyTransitionId)
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		if (AAeyerjiPlayerController* LocalPC = Cast<AAeyerjiPlayerController>(World->GetFirstPlayerController()))
		{
			LocalPC->Server_ReportZoneReady(TransitionId);
			LastReportedReadyTransitionId = TransitionId;
			UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: Reported zone ready to server for TransitionId=%d"), TransitionId);
		}
	}
}

void AAeyerjiGameState::HandleEncounterDirectorInitialSpawnComplete(AAeyerjiEncounterDirector* Director)
{
	if (!HasAuthority() || WorldFlowPhase != EAeyerjiWorldFlowPhase::TransitionLoading)
	{
		return;
	}

	if (!CachedLoadingEncounterDirector.IsValid() || Director != CachedLoadingEncounterDirector.Get())
	{
		return;
	}

	UE_LOG(LogAeyerjiWorldFlow, Display,
		TEXT("AAeyerjiGameState: EncounterDirector initial spawn complete for Zone=%s TransitionId=%d"),
		*ActiveZoneId.ToString(),
		TransitionId);

	SetPendingWorldFlowLoaderCount(0);
	TryCompleteWorldTransition();
}

void AAeyerjiGameState::HandleBossDefeatedDelayElapsed()
{
	if (HasAuthority())
	{
		UE_LOG(LogAeyerjiWorldFlow, Display, TEXT("AAeyerjiGameState: BossDefeated delay elapsed (RunState=%s); requesting ObjectiveComplete."),
			RunStateToString(RunState));
		if (!Server_BeginObjectiveComplete() && RiftRunState.bBossDefeated && !RiftRunState.bRewardsFinalized)
		{
			if (UWorld* World = GetWorld())
			{
				World->GetTimerManager().SetTimer(BossDefeatedDelayHandle, this,
					&AAeyerjiGameState::HandleBossDefeatedDelayElapsed, 0.25f, false);
			}
		}
	}
}

void AAeyerjiGameState::HandleAutoReturnDelayElapsed()
{
	if (HasAuthority())
	{
		Server_ReturnToMenu();
	}
}

void AAeyerjiGameState::HandleWorldTransitionTimeout()
{
	if (!HasAuthority() || WorldFlowPhase != EAeyerjiWorldFlowPhase::TransitionLoading)
	{
		return;
	}

	UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: World transition timed out (Zone=%s TransitionId=%d)."),
		*ActiveZoneId.ToString(),
		TransitionId);

	FName FallbackZone = NAME_None;
	if (UAeyerjiStreamingSubsystem* StreamingSubsystem = GetStreamingSubsystem())
	{
		if (const UAeyerjiStreamingManifest* Manifest = StreamingSubsystem->GetManifest())
		{
			FallbackZone = Manifest->DefaultZoneId;
		}
		else
		{
			FZoneDef MenuZoneDefinition;
			if (StreamingSubsystem->GetZoneDefinition(FName(TEXT("Zone.Menu")), MenuZoneDefinition))
			{
				FallbackZone = MenuZoneDefinition.ZoneId;
			}
		}
	}

	if (!FallbackZone.IsNone() && FallbackZone != ActiveZoneId)
	{
		Server_BeginWorldTransition(FallbackZone);
		return;
	}

	SetWorldFlowPhase(EAeyerjiWorldFlowPhase::Menu);
	ForceNetUpdate();
}

void AAeyerjiGameState::HandleDeferredRetryTravel()
{
	if (!HasAuthority() || !bPendingRetryAfterMenuTransition)
	{
		return;
	}

	UAeyerjiStreamingSubsystem* StreamingSubsystem = GetStreamingSubsystem();
	if (!StreamingSubsystem)
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning, TEXT("AAeyerjiGameState: Deferred retry failed - StreamingSubsystem missing."));
		bPendingRetryAfterMenuTransition = false;
		PendingRetryZoneId = NAME_None;
		return;
	}

	const FName RetryZoneId = PendingRetryZoneId;
	bPendingRetryAfterMenuTransition = false;
	PendingRetryZoneId = NAME_None;

	if (UWorld* World = GetWorld(); World && World->WorldType == EWorldType::PIE)
	{
		UE_LOG(LogAeyerjiWorldFlow, Display,
			TEXT("AAeyerjiGameState: Executing deferred PIE retry via streamed transition for Zone=%s."),
			*RetryZoneId.ToString());

		if (!Server_BeginWorldTransition(RetryZoneId))
		{
			UE_LOG(LogAeyerjiWorldFlow, Warning,
				TEXT("AAeyerjiGameState: Deferred PIE retry transition failed for Zone=%s."),
				*RetryZoneId.ToString());
		}
		return;
	}

	UE_LOG(LogAeyerjiWorldFlow, Display,
		TEXT("AAeyerjiGameState: Executing deferred retry travel for Zone=%s."),
		*RetryZoneId.ToString());

	if (!StreamingSubsystem->RestartCurrentGameplaySession(RetryZoneId, true))
	{
		UE_LOG(LogAeyerjiWorldFlow, Warning,
			TEXT("AAeyerjiGameState: Deferred retry travel failed for Zone=%s."),
			*RetryZoneId.ToString());
	}
}
