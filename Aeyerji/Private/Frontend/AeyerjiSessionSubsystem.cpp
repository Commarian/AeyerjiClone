#include "Frontend/AeyerjiSessionSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/NetworkVersion.h"
#include "Online/OnlineSessionNames.h"
#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"

namespace
{
	const FName AeyerjiGameKey(TEXT("AEY_GAME"));
	const FName AeyerjiBuildKey(TEXT("AEY_BUILD"));
	const FName AeyerjiPhaseKey(TEXT("AEY_PHASE"));
	const FName AeyerjiPartyKey(TEXT("AEY_PARTY"));
	const FName AeyerjiActivityKey(TEXT("AEY_ACTIVITY"));
	const FName AeyerjiTierKey(TEXT("AEY_TIER"));
	const FString AeyerjiGameId(TEXT("Aeyerji"));
	const FString LobbyPhaseWaiting(TEXT("Waiting"));

	FString GetAeyerjiNetworkBuildId()
	{
		return LexToString(FNetworkVersion::GetLocalNetworkVersion());
	}
}

void UAeyerjiSessionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		InviteHandle = Sessions->AddOnSessionUserInviteAcceptedDelegate_Handle(
			FOnSessionUserInviteAcceptedDelegate::CreateUObject(this, &ThisClass::HandleInviteAccepted));
	}

	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &ThisClass::HandleNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &ThisClass::HandleTravelFailure);
	}
}

void UAeyerjiSessionSubsystem::Deinitialize()
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface())
	{
		if (CreateHandle.IsValid()) Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
		if (FindHandle.IsValid()) Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
		if (JoinHandle.IsValid()) Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
		if (DestroyHandle.IsValid()) Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
		if (UpdateHandle.IsValid()) Sessions->ClearOnUpdateSessionCompleteDelegate_Handle(UpdateHandle);
		if (StartHandle.IsValid()) Sessions->ClearOnStartSessionCompleteDelegate_Handle(StartHandle);
		if (EndHandle.IsValid()) Sessions->ClearOnEndSessionCompleteDelegate_Handle(EndHandle);
		if (InviteHandle.IsValid()) Sessions->ClearOnSessionUserInviteAcceptedDelegate_Handle(InviteHandle);
	}
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}

	ActiveSearch.Reset();
	NativeSearchResults.Reset();
	PendingInviteResult = FOnlineSessionSearchResult();
	bInviteJoinPending = false;
	SearchResultViews.Reset();
	Super::Deinitialize();
}

IOnlineSessionPtr UAeyerjiSessionSubsystem::GetSessionInterface() const
{
	IOnlineSubsystem* OSS = IOnlineSubsystem::Get();
	return OSS ? OSS->GetSessionInterface() : nullptr;
}

bool UAeyerjiSessionSubsystem::BeginOperation(const EAeyerjiFrontendOperationState NewState)
{
	if (OperationState != EAeyerjiFrontendOperationState::Idle && OperationState != EAeyerjiFrontendOperationState::Failed)
	{
		OnFailure.Broadcast(EAeyerjiFrontendFailure::Busy);
		return false;
	}
	OperationState = NewState;
	OnOperationChanged.Broadcast(OperationState, true);
	return true;
}

void UAeyerjiSessionSubsystem::FinishOperation(const bool bSuccess, const EAeyerjiFrontendFailure Failure)
{
	const EAeyerjiFrontendOperationState CompletedOperation = OperationState;
	OperationState = bSuccess ? EAeyerjiFrontendOperationState::Idle : EAeyerjiFrontendOperationState::Failed;
	OnOperationChanged.Broadcast(CompletedOperation, bSuccess);
	if (!bSuccess && Failure != EAeyerjiFrontendFailure::None)
	{
		OnFailure.Broadcast(Failure);
	}
}

bool UAeyerjiSessionSubsystem::HostPublicParty(const FString& PartyName, const int32 PublicConnections)
{
	IOnlineSubsystem* OSS = IOnlineSubsystem::Get();
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!OSS || !Sessions.IsValid())
	{
		OnFailure.Broadcast(EAeyerjiFrontendFailure::OnlineUnavailable);
		return false;
	}
	if (!BeginOperation(EAeyerjiFrontendOperationState::CreatingSession))
	{
		return false;
	}

	FOnlineSessionSettings Settings;
	Settings.bIsLANMatch = OSS->GetSubsystemName() == NULL_SUBSYSTEM;
	Settings.NumPublicConnections = FMath::Clamp(PublicConnections, 1, 4);
	Settings.NumPrivateConnections = 0;
	Settings.bShouldAdvertise = true;
	Settings.bAllowJoinInProgress = false;
	Settings.bAllowInvites = true;
	Settings.bUsesPresence = true;
	Settings.bAllowJoinViaPresence = true;
	Settings.bAllowJoinViaPresenceFriendsOnly = false;
	Settings.bUseLobbiesIfAvailable = true;
	Settings.bUseLobbiesVoiceChatIfAvailable = false;
	Settings.BuildUniqueId = GetBuildUniqueId();
	Settings.Set(AeyerjiGameKey, AeyerjiGameId, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(AeyerjiBuildKey, GetAeyerjiNetworkBuildId(), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(SETTING_MAPNAME, FString(TEXT("L_MainMenu")), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(AeyerjiPhaseKey, LobbyPhaseWaiting, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(AeyerjiPartyKey, PartyName.IsEmpty() ? FString(TEXT("Aeyerji Party")) : PartyName.Left(64), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(AeyerjiActivityKey, static_cast<int32>(EAeyerjiRiftActivityType::StandardRift), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Settings.Set(AeyerjiTierKey, 0, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);

	CreateHandle = Sessions->AddOnCreateSessionCompleteDelegate_Handle(
		FOnCreateSessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleCreateSessionComplete));
	UE_LOG(LogTemp, Display, TEXT("[Session] Operation=Host Backend=%s LAN=%d Capacity=%d"), *OSS->GetSubsystemName().ToString(), Settings.bIsLANMatch, Settings.NumPublicConnections);
	if (!Sessions->CreateSession(0, NAME_GameSession, Settings))
	{
		Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
		CreateHandle.Reset();
		FinishOperation(false, EAeyerjiFrontendFailure::SessionCreateFailed);
		return false;
	}
	return true;
}

bool UAeyerjiSessionSubsystem::SearchPublicParties()
{
	IOnlineSubsystem* OSS = IOnlineSubsystem::Get();
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!OSS || !Sessions.IsValid())
	{
		OnFailure.Broadcast(EAeyerjiFrontendFailure::OnlineUnavailable);
		return false;
	}
	if (!BeginOperation(EAeyerjiFrontendOperationState::SearchingSessions))
	{
		return false;
	}

	ActiveSearch = MakeShared<FOnlineSessionSearch>();
	ActiveSearch->MaxSearchResults = 100;
	ActiveSearch->PingBucketSize = 50;
	ActiveSearch->bIsLanQuery = OSS->GetSubsystemName() == NULL_SUBSYSTEM;
	if (!ActiveSearch->bIsLanQuery)
	{
		ActiveSearch->QuerySettings.Set(SEARCH_LOBBIES, true, EOnlineComparisonOp::Equals);
	}
	FindHandle = Sessions->AddOnFindSessionsCompleteDelegate_Handle(
		FOnFindSessionsCompleteDelegate::CreateUObject(this, &ThisClass::HandleFindSessionsComplete));
	UE_LOG(LogTemp, Display, TEXT("[Session] Operation=Search Backend=%s LAN=%d"), *OSS->GetSubsystemName().ToString(), ActiveSearch->bIsLanQuery);
	if (!Sessions->FindSessions(0, ActiveSearch.ToSharedRef()))
	{
		Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
		FindHandle.Reset();
		FinishOperation(false, EAeyerjiFrontendFailure::SessionSearchFailed);
		return false;
	}
	return true;
}

bool UAeyerjiSessionSubsystem::JoinPublicParty(const int32 ResultId)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid() || !NativeSearchResults.IsValidIndex(ResultId))
	{
		OnFailure.Broadcast(EAeyerjiFrontendFailure::SessionJoinFailed);
		return false;
	}
	if (!BeginOperation(EAeyerjiFrontendOperationState::JoiningSession))
	{
		return false;
	}
	int32 ActivityValue = static_cast<int32>(EAeyerjiRiftActivityType::StandardRift);
	NativeSearchResults[ResultId].Session.SessionSettings.Get(AeyerjiActivityKey, ActivityValue);
	NativeSearchResults[ResultId].Session.SessionSettings.Get(AeyerjiTierKey, LastPartyExcursionTier);
	LastPartyActivity = static_cast<EAeyerjiRiftActivityType>(ActivityValue);
	if (LastPartyActivity != EAeyerjiRiftActivityType::Excursion)
	{
		LastPartyExcursionTier = 0;
	}
	JoinHandle = Sessions->AddOnJoinSessionCompleteDelegate_Handle(
		FOnJoinSessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleJoinSessionComplete));
	UE_LOG(LogTemp, Display, TEXT("[Session] Operation=Join ResultId=%d"), ResultId);
	if (!Sessions->JoinSession(0, NAME_GameSession, NativeSearchResults[ResultId]))
	{
		Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
		JoinHandle.Reset();
		FinishOperation(false, EAeyerjiFrontendFailure::SessionJoinFailed);
		return false;
	}
	return true;
}

bool UAeyerjiSessionSubsystem::LeaveCurrentParty()
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid() || !Sessions->GetNamedSession(NAME_GameSession))
	{
		TravelToOfflineMenu();
		return true;
	}
	if (!BeginOperation(EAeyerjiFrontendOperationState::LeavingSession))
	{
		return false;
	}
	bLeaveTravelPending = true;
	DestroyHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
		FOnDestroySessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleDestroySessionComplete));
	if (!Sessions->DestroySession(NAME_GameSession))
	{
		Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
		DestroyHandle.Reset();
		bLeaveTravelPending = false;
		FinishOperation(false, EAeyerjiFrontendFailure::SessionLeaveFailed);
		return false;
	}
	return true;
}

bool UAeyerjiSessionSubsystem::HasOnlineParty() const
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	return Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession) != nullptr;
}

bool UAeyerjiSessionSubsystem::UpdatePartyAdvertisement(const FString& Phase, const bool bJoinable,
	const EAeyerjiRiftActivityType ActivityType, const int32 ExcursionTier, const FName MapId)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	FNamedOnlineSession* Named = Sessions.IsValid() ? Sessions->GetNamedSession(NAME_GameSession) : nullptr;
	if (!Named)
	{
		return true;
	}
	if (UpdateHandle.IsValid())
	{
		bQueuedAdvertisementUpdate = true;
		QueuedAdvertisementPhase = Phase;
		bQueuedAdvertisementJoinable = bJoinable;
		QueuedAdvertisementActivity = ActivityType;
		QueuedAdvertisementTier = ExcursionTier;
		QueuedAdvertisementMapId = MapId;
		return true;
	}
	FOnlineSessionSettings Updated = Named->SessionSettings;
	Updated.bShouldAdvertise = bJoinable;
	Updated.bAllowJoinViaPresence = bJoinable;
	Updated.bAllowJoinInProgress = false;
	Updated.Set(AeyerjiPhaseKey, Phase, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Updated.Set(AeyerjiActivityKey, static_cast<int32>(ActivityType), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Updated.Set(AeyerjiTierKey, ExcursionTier, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	Updated.Set(SETTING_MAPNAME, MapId.ToString(), EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
	UpdateHandle = Sessions->AddOnUpdateSessionCompleteDelegate_Handle(
		FOnUpdateSessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleUpdateSessionComplete));
	if (!Sessions->UpdateSession(NAME_GameSession, Updated, true))
	{
		Sessions->ClearOnUpdateSessionCompleteDelegate_Handle(UpdateHandle);
		UpdateHandle.Reset();
		return false;
	}
	return true;
}

bool UAeyerjiSessionSubsystem::MarkPartyLaunching(const EAeyerjiRiftActivityType ActivityType, const int32 ExcursionTier, const FName GameplayMapId)
{
	LastPartyActivity = ActivityType;
	LastPartyExcursionTier = ActivityType == EAeyerjiRiftActivityType::Excursion ? ExcursionTier : 0;
	UE_LOG(LogTemp, Display, TEXT("[Session] Phase=Launching Activity=%d Tier=%d Map=%s"), static_cast<int32>(ActivityType), ExcursionTier, *GameplayMapId.ToString());
	IOnlineSessionPtr Sessions = GetSessionInterface();
	bStartAfterUpdate = Sessions.IsValid()
		&& Sessions->GetSessionState(NAME_GameSession) == EOnlineSessionState::Pending;
	if (!UpdatePartyAdvertisement(TEXT("Launching"), false, ActivityType, ExcursionTier, GameplayMapId))
	{
		bStartAfterUpdate = false;
		return false;
	}
	return true;
}

bool UAeyerjiSessionSubsystem::MarkPartyReturnedToLobby()
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (!Sessions.IsValid() || !Sessions->GetNamedSession(NAME_GameSession))
	{
		return true;
	}
	if (Sessions->GetSessionState(NAME_GameSession) == EOnlineSessionState::InProgress)
	{
		bUpdateLobbyAfterEnd = true;
		EndHandle = Sessions->AddOnEndSessionCompleteDelegate_Handle(
			FOnEndSessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleEndSessionComplete));
		if (Sessions->EndSession(NAME_GameSession))
		{
			return true;
		}
		Sessions->ClearOnEndSessionCompleteDelegate_Handle(EndHandle);
		EndHandle.Reset();
		bUpdateLobbyAfterEnd = false;
	}
	UE_LOG(LogTemp, Display, TEXT("[Session] Phase=Waiting Joinable=1"));
	return UpdatePartyAdvertisement(LobbyPhaseWaiting, true, LastPartyActivity, LastPartyExcursionTier, FName(TEXT("L_MainMenu")));
}

bool UAeyerjiSessionSubsystem::UpdateWaitingPartySelection(const EAeyerjiRiftActivityType ActivityType,
	const int32 ExcursionTier)
{
	LastPartyActivity = ActivityType;
	LastPartyExcursionTier = ActivityType == EAeyerjiRiftActivityType::Excursion ? ExcursionTier : 0;
	return UpdatePartyAdvertisement(LobbyPhaseWaiting, true, ActivityType,
		ActivityType == EAeyerjiRiftActivityType::Excursion ? ExcursionTier : 0,
		FName(TEXT("L_MainMenu")));
}

void UAeyerjiSessionSubsystem::HandleCreateSessionComplete(const FName SessionName, const bool bWasSuccessful)
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface()) Sessions->ClearOnCreateSessionCompleteDelegate_Handle(CreateHandle);
	CreateHandle.Reset();
	FinishOperation(bWasSuccessful, EAeyerjiFrontendFailure::SessionCreateFailed);
	UE_LOG(LogTemp, Display, TEXT("[Session] Operation=Host Result=%d"), bWasSuccessful);
	if (bWasSuccessful)
	{
		UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/Levels/L_MainMenu")), true, TEXT("listen"));
	}
}

void UAeyerjiSessionSubsystem::HandleFindSessionsComplete(const bool bWasSuccessful)
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface()) Sessions->ClearOnFindSessionsCompleteDelegate_Handle(FindHandle);
	FindHandle.Reset();
	NativeSearchResults.Reset();
	SearchResultViews.Reset();
	if (bWasSuccessful && ActiveSearch.IsValid())
	{
		for (const FOnlineSessionSearchResult& Result : ActiveSearch->SearchResults)
		{
			FString GameId, BuildId, Phase, PartyName;
			Result.Session.SessionSettings.Get(AeyerjiGameKey, GameId);
			Result.Session.SessionSettings.Get(AeyerjiBuildKey, BuildId);
			Result.Session.SessionSettings.Get(AeyerjiPhaseKey, Phase);
			if (GameId != AeyerjiGameId || BuildId != GetAeyerjiNetworkBuildId() || Phase != LobbyPhaseWaiting)
			{
				continue;
			}
			Result.Session.SessionSettings.Get(AeyerjiPartyKey, PartyName);
			int32 ActivityValue = 0;
			int32 Tier = 0;
			Result.Session.SessionSettings.Get(AeyerjiActivityKey, ActivityValue);
			Result.Session.SessionSettings.Get(AeyerjiTierKey, Tier);
			const int32 ResultId = NativeSearchResults.Add(Result);
			FAeyerjiSessionSearchResultView& View = SearchResultViews.AddDefaulted_GetRef();
			View.ResultId = ResultId;
			View.PartyName = PartyName;
			View.HostName = Result.Session.OwningUserName;
			View.MaximumPlayers = Result.Session.SessionSettings.NumPublicConnections;
			View.CurrentPlayers = FMath::Max(0, View.MaximumPlayers - Result.Session.NumOpenPublicConnections);
			View.PingMilliseconds = Result.PingInMs;
			View.ActivityType = static_cast<EAeyerjiRiftActivityType>(ActivityValue);
			View.ExcursionTier = Tier;
			View.bJoinable = Result.Session.NumOpenPublicConnections > 0;
		}
	}
	OnSearchResultsChanged.Broadcast(SearchResultViews);
	FinishOperation(bWasSuccessful, EAeyerjiFrontendFailure::SessionSearchFailed);
	UE_LOG(LogTemp, Display, TEXT("[Session] Operation=Search Result=%d Accepted=%d Raw=%d"), bWasSuccessful, SearchResultViews.Num(), ActiveSearch.IsValid() ? ActiveSearch->SearchResults.Num() : 0);
}

void UAeyerjiSessionSubsystem::HandleJoinSessionComplete(const FName SessionName, const EOnJoinSessionCompleteResult::Type Result)
{
	IOnlineSessionPtr Sessions = GetSessionInterface();
	if (Sessions.IsValid()) Sessions->ClearOnJoinSessionCompleteDelegate_Handle(JoinHandle);
	JoinHandle.Reset();
	FString ConnectString;
	const bool bSuccess = Result == EOnJoinSessionCompleteResult::Success && Sessions.IsValid()
		&& Sessions->GetResolvedConnectString(SessionName, ConnectString);
	FinishOperation(bSuccess, Result == EOnJoinSessionCompleteResult::SessionIsFull ? EAeyerjiFrontendFailure::SessionFull : EAeyerjiFrontendFailure::SessionJoinFailed);
	if (bSuccess)
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (APlayerController* PC = GI->GetFirstLocalPlayerController())
			{
				PC->ClientTravel(ConnectString, TRAVEL_Absolute);
			}
		}
	}
}

void UAeyerjiSessionSubsystem::HandleDestroySessionComplete(const FName SessionName, const bool bWasSuccessful)
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface()) Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
	DestroyHandle.Reset();
	FinishOperation(bWasSuccessful, EAeyerjiFrontendFailure::SessionLeaveFailed);
	if (bInviteJoinPending)
	{
		const FOnlineSessionSearchResult InviteResult = PendingInviteResult;
		PendingInviteResult = FOnlineSessionSearchResult();
		bInviteJoinPending = false;
		if (bWasSuccessful)
		{
			NativeSearchResults = { InviteResult };
			JoinPublicParty(0);
		}
		return;
	}
	if (bLeaveTravelPending)
	{
		bLeaveTravelPending = false;
		TravelToOfflineMenu();
	}
}

void UAeyerjiSessionSubsystem::HandleUpdateSessionComplete(const FName SessionName, const bool bWasSuccessful)
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface()) Sessions->ClearOnUpdateSessionCompleteDelegate_Handle(UpdateHandle);
	UpdateHandle.Reset();
	UE_LOG(LogTemp, Display, TEXT("[Session] Operation=Update Result=%d"), bWasSuccessful);
	if (bQueuedAdvertisementUpdate)
	{
		const FString Phase = QueuedAdvertisementPhase;
		const bool bJoinable = bQueuedAdvertisementJoinable;
		const EAeyerjiRiftActivityType ActivityType = QueuedAdvertisementActivity;
		const int32 Tier = QueuedAdvertisementTier;
		const FName MapId = QueuedAdvertisementMapId;
		bQueuedAdvertisementUpdate = false;
		QueuedAdvertisementPhase.Reset();
		QueuedAdvertisementMapId = NAME_None;
		UpdatePartyAdvertisement(Phase, bJoinable, ActivityType, Tier, MapId);
		return;
	}
	if (bStartAfterUpdate)
	{
		bStartAfterUpdate = false;
		if (bWasSuccessful)
		{
			if (IOnlineSessionPtr Sessions = GetSessionInterface())
			{
				StartHandle = Sessions->AddOnStartSessionCompleteDelegate_Handle(
					FOnStartSessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleStartSessionComplete));
				Sessions->StartSession(NAME_GameSession);
			}
		}
	}
}

void UAeyerjiSessionSubsystem::HandleStartSessionComplete(const FName SessionName, const bool bWasSuccessful)
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface()) Sessions->ClearOnStartSessionCompleteDelegate_Handle(StartHandle);
	StartHandle.Reset();
}

void UAeyerjiSessionSubsystem::HandleEndSessionComplete(const FName SessionName, const bool bWasSuccessful)
{
	if (IOnlineSessionPtr Sessions = GetSessionInterface()) Sessions->ClearOnEndSessionCompleteDelegate_Handle(EndHandle);
	EndHandle.Reset();
	if (bUpdateLobbyAfterEnd)
	{
		bUpdateLobbyAfterEnd = false;
		if (bWasSuccessful)
		{
			UE_LOG(LogTemp, Display, TEXT("[Session] Phase=Waiting Joinable=1"));
			UpdatePartyAdvertisement(LobbyPhaseWaiting, true, LastPartyActivity, LastPartyExcursionTier, FName(TEXT("L_MainMenu")));
		}
	}
}

void UAeyerjiSessionSubsystem::HandleInviteAccepted(const bool bWasSuccessful, const int32 LocalUserNum,
	TSharedPtr<const FUniqueNetId> UserId, const FOnlineSessionSearchResult& InviteResult)
{
	if (!bWasSuccessful || !InviteResult.IsValid())
	{
		OnFailure.Broadcast(EAeyerjiFrontendFailure::SessionJoinFailed);
		return;
	}
	FString GameId, BuildId, Phase;
	InviteResult.Session.SessionSettings.Get(AeyerjiGameKey, GameId);
	InviteResult.Session.SessionSettings.Get(AeyerjiBuildKey, BuildId);
	InviteResult.Session.SessionSettings.Get(AeyerjiPhaseKey, Phase);
	if (GameId != AeyerjiGameId || BuildId != GetAeyerjiNetworkBuildId() || Phase != LobbyPhaseWaiting)
	{
		OnFailure.Broadcast(EAeyerjiFrontendFailure::SessionInProgress);
		return;
	}
	if (IOnlineSessionPtr Sessions = GetSessionInterface(); Sessions.IsValid() && Sessions->GetNamedSession(NAME_GameSession))
	{
		if (!BeginOperation(EAeyerjiFrontendOperationState::LeavingSession))
		{
			return;
		}
		PendingInviteResult = InviteResult;
		bInviteJoinPending = true;
		bLeaveTravelPending = false;
		DestroyHandle = Sessions->AddOnDestroySessionCompleteDelegate_Handle(
			FOnDestroySessionCompleteDelegate::CreateUObject(this, &ThisClass::HandleDestroySessionComplete));
		if (!Sessions->DestroySession(NAME_GameSession))
		{
			Sessions->ClearOnDestroySessionCompleteDelegate_Handle(DestroyHandle);
			DestroyHandle.Reset();
			bInviteJoinPending = false;
			PendingInviteResult = FOnlineSessionSearchResult();
			FinishOperation(false, EAeyerjiFrontendFailure::SessionLeaveFailed);
		}
		return;
	}
	NativeSearchResults = { InviteResult };
	JoinPublicParty(0);
}

void UAeyerjiSessionSubsystem::HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver,
	const ENetworkFailure::Type FailureType, const FString& ErrorString)
{
	UE_LOG(LogTemp, Warning, TEXT("[Session] NetworkFailure Type=%d Error=%s"), static_cast<int32>(FailureType), *ErrorString);
	OperationState = EAeyerjiFrontendOperationState::Failed;
	OnFailure.Broadcast(EAeyerjiFrontendFailure::NetworkFailure);
	if (!LeaveCurrentParty())
	{
		TravelToOfflineMenu();
	}
}

void UAeyerjiSessionSubsystem::HandleTravelFailure(UWorld* World, const ETravelFailure::Type FailureType, const FString& ErrorString)
{
	UE_LOG(LogTemp, Warning, TEXT("[Session] TravelFailure Type=%d Error=%s"), static_cast<int32>(FailureType), *ErrorString);
	OperationState = EAeyerjiFrontendOperationState::Failed;
	OnFailure.Broadcast(EAeyerjiFrontendFailure::NetworkFailure);
	if (!LeaveCurrentParty())
	{
		TravelToOfflineMenu();
	}
}

void UAeyerjiSessionSubsystem::TravelToOfflineMenu() const
{
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Game/Levels/L_MainMenu")), true);
}
