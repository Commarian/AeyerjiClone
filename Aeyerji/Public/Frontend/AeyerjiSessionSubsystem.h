#pragma once

#include "CoreMinimal.h"
#include "Frontend/AeyerjiFrontendTypes.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "OnlineSessionSettings.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AeyerjiSessionSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE_OneParam(FAeyerjiSessionResultsNative, const TArray<FAeyerjiSessionSearchResultView>&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FAeyerjiSessionOperationNative, EAeyerjiFrontendOperationState, bool);
DECLARE_MULTICAST_DELEGATE_OneParam(FAeyerjiSessionFailureNative, EAeyerjiFrontendFailure);

/**
 * Sole owner of classic Online Subsystem party-session operations.
 * Native online result objects never cross the Blueprint boundary.
 */
UCLASS()
class AEYERJI_API UAeyerjiSessionSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Hosts a public four-player listen party through Steam or the Null/LAN fallback. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Frontend|Session")
	bool HostPublicParty(const FString& PartyName, int32 PublicConnections = 4);

	/** Refreshes public parties, replacing every previous opaque result identifier. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Frontend|Session")
	bool SearchPublicParties();

	/** Joins a result from the latest search by its opaque frontend identifier. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Frontend|Session")
	bool JoinPublicParty(int32 ResultId);

	/** Leaves/destroys the current party and returns the local process to the menu. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Frontend|Session")
	bool LeaveCurrentParty();

	/** Opens the active online provider's friend-invite UI for the current lobby. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Frontend|Session")
	bool OpenPartyInviteOverlay();

	/** Closes joining and starts/updates the online session before authoritative gameplay travel. */
	bool MarkPartyLaunching(EAeyerjiRiftActivityType ActivityType, int32 ExcursionTier, FName GameplayMapId);

	/** Ends gameplay presence and reopens the retained party after server travel back to the lobby. */
	bool MarkPartyReturnedToLobby();

	/** Refreshes browser-visible shared activity/tier while the party remains joinable. */
	bool UpdateWaitingPartySelection(EAeyerjiRiftActivityType ActivityType, int32 ExcursionTier);

	UFUNCTION(BlueprintPure, Category="Aeyerji|Frontend|Session")
	EAeyerjiFrontendOperationState GetOperationState() const { return OperationState; }

	UFUNCTION(BlueprintPure, Category="Aeyerji|Frontend|Session")
	const TArray<FAeyerjiSessionSearchResultView>& GetSearchResultViews() const { return SearchResultViews; }

	UFUNCTION(BlueprintPure, Category="Aeyerji|Frontend|Session")
	bool HasOnlineParty() const;
	EAeyerjiRiftActivityType GetLastPartyActivity() const { return LastPartyActivity; }
	int32 GetLastPartyExcursionTier() const { return LastPartyExcursionTier; }

	FAeyerjiSessionResultsNative OnSearchResultsChanged;
	FAeyerjiSessionOperationNative OnOperationChanged;
	FAeyerjiSessionFailureNative OnFailure;

private:
	IOnlineSessionPtr GetSessionInterface() const;
	bool BeginOperation(EAeyerjiFrontendOperationState NewState);
	void FinishOperation(bool bSuccess, EAeyerjiFrontendFailure Failure = EAeyerjiFrontendFailure::None);
	bool UpdatePartyAdvertisement(const FString& Phase, bool bJoinable, EAeyerjiRiftActivityType ActivityType, int32 ExcursionTier, FName MapId);
	void TravelToOfflineMenu() const;

	void HandleCreateSessionComplete(FName SessionName, bool bWasSuccessful);
	void HandleFindSessionsComplete(bool bWasSuccessful);
	void HandleJoinSessionComplete(FName SessionName, EOnJoinSessionCompleteResult::Type Result);
	void HandleDestroySessionComplete(FName SessionName, bool bWasSuccessful);
	void HandleUpdateSessionComplete(FName SessionName, bool bWasSuccessful);
	void HandleStartSessionComplete(FName SessionName, bool bWasSuccessful);
	void HandleEndSessionComplete(FName SessionName, bool bWasSuccessful);
	void HandleInviteAccepted(bool bWasSuccessful, int32 LocalUserNum, TSharedPtr<const FUniqueNetId> UserId, const FOnlineSessionSearchResult& InviteResult);
	void HandleNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& ErrorString);
	void HandleTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& ErrorString);

	TSharedPtr<FOnlineSessionSearch> ActiveSearch;
	TArray<FOnlineSessionSearchResult> NativeSearchResults;
	FOnlineSessionSearchResult PendingInviteResult;

	UPROPERTY(Transient)
	TArray<FAeyerjiSessionSearchResultView> SearchResultViews;

	EAeyerjiFrontendOperationState OperationState = EAeyerjiFrontendOperationState::Idle;
	bool bLeaveTravelPending = false;
	bool bInviteJoinPending = false;
	bool bStartAfterUpdate = false;
	bool bUpdateLobbyAfterEnd = false;
	bool bQueuedAdvertisementUpdate = false;
	FString QueuedAdvertisementPhase;
	bool bQueuedAdvertisementJoinable = false;
	EAeyerjiRiftActivityType QueuedAdvertisementActivity = EAeyerjiRiftActivityType::StandardRift;
	int32 QueuedAdvertisementTier = 0;
	FName QueuedAdvertisementMapId = NAME_None;
	EAeyerjiRiftActivityType LastPartyActivity = EAeyerjiRiftActivityType::StandardRift;
	int32 LastPartyExcursionTier = 0;

	FDelegateHandle CreateHandle;
	FDelegateHandle FindHandle;
	FDelegateHandle JoinHandle;
	FDelegateHandle DestroyHandle;
	FDelegateHandle UpdateHandle;
	FDelegateHandle StartHandle;
	FDelegateHandle EndHandle;
	FDelegateHandle InviteHandle;
	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;
};
