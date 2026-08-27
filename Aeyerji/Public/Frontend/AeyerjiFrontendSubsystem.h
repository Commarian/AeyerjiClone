#pragma once

#include "CoreMinimal.h"
#include "Frontend/AeyerjiFrontendTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "AeyerjiFrontendSubsystem.generated.h"

class AAeyerjiGameState;
class AAeyerjiPlayerState;
class UAeyerjiSaveGame;
class UAeyerjiSaveManagerSubsystem;
class UAeyerjiSessionSubsystem;

DECLARE_MULTICAST_DELEGATE_OneParam(FAeyerjiFrontendSnapshotNative, const FAeyerjiFrontendSnapshot&);
DECLARE_MULTICAST_DELEGATE_OneParam(FAeyerjiLobbySnapshotNative, const FAeyerjiLobbySnapshot&);
DECLARE_MULTICAST_DELEGATE_OneParam(FAeyerjiFrontendResultsNative, const TArray<FAeyerjiSessionSearchResultView>&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FAeyerjiFrontendFeedbackNative, EAeyerjiFrontendFailure, const FText&);

/** Event-driven frontend read model and command facade. Gameplay state remains owned by existing systems. */
UCLASS()
class AEYERJI_API UAeyerjiFrontendSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Resolves/reconciles the local profile and updates the immutable presentation snapshot. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Frontend")
	void ResolveLocalProfile();

	/** Rebinds replicated lobby state after travel and republishes all current frontend views. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Frontend")
	void RefreshCurrentState();

	/**
	 * Retries profile submission when seamless travel finally assigns the local PlayerState.
	 * The controller calls this from its possession/PlayerState replication hooks so submission
	 * does not depend on an unrelated later lobby snapshot.
	 */
	void NotifyLocalPlayerStateReady(AAeyerjiPlayerState* PlayerState);

	UFUNCTION(BlueprintPure, Category="Aeyerji|Frontend")
	const FAeyerjiFrontendSnapshot& GetFrontendSnapshot() const { return FrontendSnapshot; }

	UFUNCTION(BlueprintPure, Category="Aeyerji|Frontend")
	const FAeyerjiLobbySnapshot& GetLobbySnapshot() const { return LobbySnapshot; }

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Frontend|Session")
	bool HostPublicParty(const FString& PartyName);

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Frontend|Session")
	bool SearchPublicParties();

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Frontend|Session")
	bool JoinPublicParty(int32 ResultId);

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Frontend|Session")
	bool LeaveCurrentParty();

	/** Opens the provider invite overlay for the retained online party, when supported. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Frontend|Session")
	bool OpenPartyInviteOverlay();

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Frontend|Lobby")
	bool SetReady(bool bReady);

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Frontend|Lobby")
	bool SelectActivity(EAeyerjiRiftActivityType ActivityType);

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Frontend|Lobby")
	bool SelectExcursionTier(int32 Tier);

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Frontend|Lobby")
	bool LaunchSelectedActivity();

	FAeyerjiFrontendSnapshotNative OnFrontendSnapshotChanged;
	FAeyerjiLobbySnapshotNative OnLobbySnapshotChanged;
	FAeyerjiFrontendResultsNative OnSessionResultsChanged;
	FAeyerjiFrontendFeedbackNative OnFeedback;

private:
	void HandleProfileResolved(bool bSuccess, bool bHadPersistedData, UAeyerjiSaveGame* SaveData);
	void HandleProfileChanged(const FString& OwnerKey, int64 Revision);
	void RebuildFrontendSnapshot(UAeyerjiSaveGame* SaveData);
	void HandleSessionOperation(EAeyerjiFrontendOperationState CompletedOperation, bool bSuccess);
	void HandleSessionFailure(EAeyerjiFrontendFailure Failure);
	void HandleSessionResults(const TArray<FAeyerjiSessionSearchResultView>& Results);
	void HandleLobbySnapshot(const FAeyerjiLobbySnapshot& Snapshot);
	void HandlePostLoadMap(UWorld* LoadedWorld);
	void BindLobbyState();
	AAeyerjiPlayerState* GetLocalAeyerjiPlayerState() const;
	bool SubmitResolvedProfileToLobby();
	static FText ResolveFailureText(EAeyerjiFrontendFailure Failure);

	UPROPERTY(Transient)
	FAeyerjiFrontendSnapshot FrontendSnapshot;

	UPROPERTY(Transient)
	FAeyerjiLobbySnapshot LobbySnapshot;

	UPROPERTY(Transient)
	TObjectPtr<UAeyerjiSaveGame> ResolvedProfile;

	TWeakObjectPtr<AAeyerjiGameState> BoundGameState;
	TWeakObjectPtr<AAeyerjiPlayerState> BoundPlayerState;

	/**
	 * Tracks the asynchronous profile submission made for the current local PlayerState.
	 * Joining creates a new PlayerState, so the same resolved profile must be submitted once again.
	 */
	TWeakObjectPtr<AAeyerjiPlayerState> ProfileSubmissionPlayerState;
	int64 ProfileSubmissionRevision = 0;
	bool bProfileSubmissionPending = false;

	FDelegateHandle PostLoadMapHandle;
};
