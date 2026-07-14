#include "Frontend/AeyerjiFrontendSubsystem.h"

#include "Aeyerji/AeyerjiGameState.h"
#include "Aeyerji/AeyerjiPlayerState.h"
#include "Aeyerji/AeyerjiSaveGame.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Frontend/AeyerjiSessionSubsystem.h"
#include "Frontend/AeyerjiFrontendRules.h"
#include "GUI/AeyerjiStringLibrary.h"
#include "Progression/AeyerjiProgressionLibrary.h"
#include "Systems/AeyerjiSaveManagerSubsystem.h"
#include "UObject/UObjectGlobals.h"

void UAeyerjiFrontendSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Collection.InitializeDependency<UAeyerjiSaveManagerSubsystem>();
	Collection.InitializeDependency<UAeyerjiSessionSubsystem>();

	if (UAeyerjiSaveManagerSubsystem* SaveManager = GetGameInstance()->GetSubsystem<UAeyerjiSaveManagerSubsystem>())
	{
		SaveManager->OnProfileChanged.AddUObject(this, &ThisClass::HandleProfileChanged);
	}
	if (UAeyerjiSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UAeyerjiSessionSubsystem>())
	{
		Sessions->OnOperationChanged.AddUObject(this, &ThisClass::HandleSessionOperation);
		Sessions->OnFailure.AddUObject(this, &ThisClass::HandleSessionFailure);
		Sessions->OnSearchResultsChanged.AddUObject(this, &ThisClass::HandleSessionResults);
	}
	PostLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this, &ThisClass::HandlePostLoadMap);
	ResolveLocalProfile();
}

void UAeyerjiFrontendSubsystem::Deinitialize()
{
	FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadMapHandle);
	if (UAeyerjiSaveManagerSubsystem* SaveManager = GetGameInstance()->GetSubsystem<UAeyerjiSaveManagerSubsystem>())
	{
		SaveManager->OnProfileChanged.RemoveAll(this);
	}
	if (UAeyerjiSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UAeyerjiSessionSubsystem>())
	{
		Sessions->OnOperationChanged.RemoveAll(this);
		Sessions->OnFailure.RemoveAll(this);
		Sessions->OnSearchResultsChanged.RemoveAll(this);
	}
	if (BoundGameState.IsValid())
	{
		BoundGameState->OnLobbySnapshotChangedNative.RemoveAll(this);
	}
	if (BoundPlayerState.IsValid())
	{
		BoundPlayerState->OnFrontendRequestRejectedNative.RemoveAll(this);
	}
	BoundGameState.Reset();
	BoundPlayerState.Reset();
	ResolvedProfile = nullptr;
	Super::Deinitialize();
}

void UAeyerjiFrontendSubsystem::ResolveLocalProfile()
{
	UAeyerjiSaveManagerSubsystem* SaveManager = GetGameInstance()->GetSubsystem<UAeyerjiSaveManagerSubsystem>();
	if (!SaveManager)
	{
		HandleSessionFailure(EAeyerjiFrontendFailure::ProfileResolveFailed);
		return;
	}
	FrontendSnapshot.ProfileState = EAeyerjiFrontendProfileState::Resolving;
	FrontendSnapshot.OperationState = EAeyerjiFrontendOperationState::ResolvingProfile;
	OnFrontendSnapshotChanged.Broadcast(FrontendSnapshot);
	SaveManager->ResolveProfileForLocalOwner(
		FAeyerjiOnProfileResolved::CreateUObject(this, &ThisClass::HandleProfileResolved), GetLocalAeyerjiPlayerState());
}

void UAeyerjiFrontendSubsystem::HandleProfileResolved(const bool bSuccess, const bool bHadPersistedData, UAeyerjiSaveGame* SaveData)
{
	ResolvedProfile = bSuccess ? SaveData : nullptr;
	FrontendSnapshot.ProfileState = bSuccess ? EAeyerjiFrontendProfileState::Ready : EAeyerjiFrontendProfileState::Failed;
	FrontendSnapshot.OperationState = bSuccess ? EAeyerjiFrontendOperationState::Idle : EAeyerjiFrontendOperationState::Failed;
	RebuildFrontendSnapshot(ResolvedProfile);
	if (!bSuccess)
	{
		HandleSessionFailure(EAeyerjiFrontendFailure::ProfileResolveFailed);
	}
	else
	{
		SubmitResolvedProfileToLobby();
	}
	UE_LOG(LogTemp, Display, TEXT("[Frontend] ProfileResolved Result=%d Existing=%d Revision=%lld Level=%d"),
		bSuccess, bHadPersistedData, FrontendSnapshot.ProfileRevision, FrontendSnapshot.CharacterLevel);
}

void UAeyerjiFrontendSubsystem::HandleProfileChanged(const FString& OwnerKey, const int64 Revision)
{
	UAeyerjiSaveGame* SaveData = nullptr;
	if (UAeyerjiSaveManagerSubsystem* SaveManager = GetGameInstance()->GetSubsystem<UAeyerjiSaveManagerSubsystem>())
	{
		SaveManager->GetCachedOrLocalProfileForOwner(SaveData, GetLocalAeyerjiPlayerState());
	}
	if (SaveData)
	{
		ResolvedProfile = SaveData;
		FrontendSnapshot.ProfileState = EAeyerjiFrontendProfileState::Ready;
		RebuildFrontendSnapshot(SaveData);
		SubmitResolvedProfileToLobby();
	}
}

void UAeyerjiFrontendSubsystem::RebuildFrontendSnapshot(UAeyerjiSaveGame* SaveData)
{
	EAeyerjiFrontendOperationState OperationState = FrontendSnapshot.OperationState;
	if (UAeyerjiSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UAeyerjiSessionSubsystem>())
	{
		OperationState = Sessions->GetOperationState();
	}
	FrontendSnapshot = AeyerjiFrontendRules::BuildSnapshot(
		SaveData, FrontendSnapshot.ProfileState, OperationState);
	OnFrontendSnapshotChanged.Broadcast(FrontendSnapshot);
}

void UAeyerjiFrontendSubsystem::RefreshCurrentState()
{
	BindLobbyState();
	RebuildFrontendSnapshot(ResolvedProfile);
	OnLobbySnapshotChanged.Broadcast(LobbySnapshot);
	if (UAeyerjiSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UAeyerjiSessionSubsystem>())
	{
		OnSessionResultsChanged.Broadcast(Sessions->GetSearchResultViews());
	}
	SubmitResolvedProfileToLobby();
}

bool UAeyerjiFrontendSubsystem::HostPublicParty(const FString& PartyName)
{
	UAeyerjiSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UAeyerjiSessionSubsystem>();
	return Sessions && Sessions->HostPublicParty(PartyName, 4);
}

bool UAeyerjiFrontendSubsystem::SearchPublicParties()
{
	UAeyerjiSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UAeyerjiSessionSubsystem>();
	return Sessions && Sessions->SearchPublicParties();
}

bool UAeyerjiFrontendSubsystem::JoinPublicParty(const int32 ResultId)
{
	UAeyerjiSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UAeyerjiSessionSubsystem>();
	return Sessions && Sessions->JoinPublicParty(ResultId);
}

bool UAeyerjiFrontendSubsystem::LeaveCurrentParty()
{
	UAeyerjiSessionSubsystem* Sessions = GetGameInstance()->GetSubsystem<UAeyerjiSessionSubsystem>();
	return Sessions && Sessions->LeaveCurrentParty();
}

bool UAeyerjiFrontendSubsystem::SetReady(const bool bReady)
{
	if (AAeyerjiPlayerState* PS = GetLocalAeyerjiPlayerState())
	{
		PS->RequestFrontendReady(bReady);
		return true;
	}
	return false;
}

bool UAeyerjiFrontendSubsystem::SelectActivity(const EAeyerjiRiftActivityType ActivityType)
{
	if (AAeyerjiPlayerState* PS = GetLocalAeyerjiPlayerState())
	{
		PS->RequestFrontendActivity(ActivityType);
		return true;
	}
	return false;
}

bool UAeyerjiFrontendSubsystem::SelectExcursionTier(const int32 Tier)
{
	if (AAeyerjiPlayerState* PS = GetLocalAeyerjiPlayerState())
	{
		PS->RequestFrontendTier(Tier);
		return true;
	}
	return false;
}

bool UAeyerjiFrontendSubsystem::LaunchSelectedActivity()
{
	if (AAeyerjiPlayerState* PS = GetLocalAeyerjiPlayerState())
	{
		PS->RequestFrontendLaunch();
		return true;
	}
	return false;
}

void UAeyerjiFrontendSubsystem::HandleSessionOperation(EAeyerjiFrontendOperationState CompletedOperation, bool bSuccess)
{
	RebuildFrontendSnapshot(ResolvedProfile);
}

void UAeyerjiFrontendSubsystem::HandleSessionFailure(const EAeyerjiFrontendFailure Failure)
{
	FrontendSnapshot.OperationState = EAeyerjiFrontendOperationState::Failed;
	OnFrontendSnapshotChanged.Broadcast(FrontendSnapshot);
	OnFeedback.Broadcast(Failure, ResolveFailureText(Failure));
}

void UAeyerjiFrontendSubsystem::HandleSessionResults(const TArray<FAeyerjiSessionSearchResultView>& Results)
{
	OnSessionResultsChanged.Broadcast(Results);
}

void UAeyerjiFrontendSubsystem::HandleLobbySnapshot(const FAeyerjiLobbySnapshot& Snapshot)
{
	LobbySnapshot = Snapshot;
	OnLobbySnapshotChanged.Broadcast(LobbySnapshot);
}

void UAeyerjiFrontendSubsystem::HandlePostLoadMap(UWorld* LoadedWorld)
{
	if (LoadedWorld && LoadedWorld->GetGameInstance() == GetGameInstance())
	{
		RefreshCurrentState();
	}
}

void UAeyerjiFrontendSubsystem::BindLobbyState()
{
	UWorld* World = GetGameInstance()->GetWorld();
	AAeyerjiGameState* NewGameState = World ? World->GetGameState<AAeyerjiGameState>() : nullptr;
	if (BoundGameState.Get() == NewGameState)
	{
		if (NewGameState)
		{
			LobbySnapshot = NewGameState->GetLobbySnapshot();
		}
	}
	else
	{
		if (BoundGameState.IsValid())
		{
			BoundGameState->OnLobbySnapshotChangedNative.RemoveAll(this);
		}
		BoundGameState = NewGameState;
		if (NewGameState)
		{
			NewGameState->OnLobbySnapshotChangedNative.AddUObject(this, &ThisClass::HandleLobbySnapshot);
			LobbySnapshot = NewGameState->GetLobbySnapshot();
		}
	}

	AAeyerjiPlayerState* NewPlayerState = GetLocalAeyerjiPlayerState();
	if (BoundPlayerState.Get() != NewPlayerState)
	{
		if (BoundPlayerState.IsValid())
		{
			BoundPlayerState->OnFrontendRequestRejectedNative.RemoveAll(this);
		}
		BoundPlayerState = NewPlayerState;
		if (NewPlayerState)
		{
			NewPlayerState->OnFrontendRequestRejectedNative.AddUObject(this, &ThisClass::HandleSessionFailure);
		}
	}
}

AAeyerjiPlayerState* UAeyerjiFrontendSubsystem::GetLocalAeyerjiPlayerState() const
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (APlayerController* PC = GI->GetFirstLocalPlayerController())
		{
			return PC->GetPlayerState<AAeyerjiPlayerState>();
		}
	}
	return nullptr;
}

bool UAeyerjiFrontendSubsystem::SubmitResolvedProfileToLobby()
{
	AAeyerjiPlayerState* PS = GetLocalAeyerjiPlayerState();
	UAeyerjiSaveManagerSubsystem* SaveManager = GetGameInstance()->GetSubsystem<UAeyerjiSaveManagerSubsystem>();
	if (!PS || !ResolvedProfile || !SaveManager)
	{
		return false;
	}
	if (PS->GetFrontendProfileRevision() == ResolvedProfile->Revision && PS->IsFrontendProfileVerified())
	{
		return true;
	}
	FAeyerjiSaveTransportHeader Header;
	TArray<uint8> Bytes;
	if (!SaveManager->BuildTransportFromProfile(ResolvedProfile, Header, Bytes))
	{
		return false;
	}
	return PS->SubmitFrontendProfile(Header, Bytes);
}

FText UAeyerjiFrontendSubsystem::ResolveFailureText(const EAeyerjiFrontendFailure Failure)
{
	const TCHAR* Key = TEXT("Frontend_Error_Unknown");
	switch (Failure)
	{
	case EAeyerjiFrontendFailure::Busy: Key = TEXT("Frontend_Error_Busy"); break;
	case EAeyerjiFrontendFailure::OnlineUnavailable: Key = TEXT("Frontend_Error_OnlineUnavailable"); break;
	case EAeyerjiFrontendFailure::ProfileResolveFailed: Key = TEXT("Frontend_Error_ProfileResolve"); break;
	case EAeyerjiFrontendFailure::ProfileTransferRejected: Key = TEXT("Frontend_Error_ProfileTransfer"); break;
	case EAeyerjiFrontendFailure::SessionCreateFailed: Key = TEXT("Frontend_Error_Host"); break;
	case EAeyerjiFrontendFailure::SessionSearchFailed: Key = TEXT("Frontend_Error_Search"); break;
	case EAeyerjiFrontendFailure::SessionJoinFailed: Key = TEXT("Frontend_Error_Join"); break;
	case EAeyerjiFrontendFailure::SessionLeaveFailed: Key = TEXT("Frontend_Error_Leave"); break;
	case EAeyerjiFrontendFailure::SessionFull: Key = TEXT("Frontend_Error_Full"); break;
	case EAeyerjiFrontendFailure::SessionInProgress: Key = TEXT("Frontend_Error_InProgress"); break;
	case EAeyerjiFrontendFailure::NetworkFailure: Key = TEXT("Frontend_Error_Network"); break;
	case EAeyerjiFrontendFailure::NotLeader: Key = TEXT("Frontend_Error_NotLeader"); break;
	case EAeyerjiFrontendFailure::PartyNotReady: Key = TEXT("Frontend_Error_PartyNotReady"); break;
	case EAeyerjiFrontendFailure::TierNotDefined: Key = TEXT("Frontend_Error_TierNotDefined"); break;
	case EAeyerjiFrontendFailure::TierLockedForParty: Key = TEXT("Frontend_Error_TierLocked"); break;
	case EAeyerjiFrontendFailure::TierLevelRequirement: Key = TEXT("Frontend_Error_TierLevel"); break;
	case EAeyerjiFrontendFailure::LaunchFailed: Key = TEXT("Frontend_Error_Launch"); break;
	default: break;
	}
	return AeyerjiStringLibrary::GetGlobalStringTableText(Key);
}
