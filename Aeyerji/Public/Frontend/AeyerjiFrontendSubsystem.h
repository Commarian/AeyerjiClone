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
	FDelegateHandle PostLoadMapHandle;
};
