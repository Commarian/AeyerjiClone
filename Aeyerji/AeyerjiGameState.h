// Copyright (c) 2025 Aeyerji.
#pragma once

#include "AeyerjiObjectiveTypes.h"
#include "AeyerjiRunTypes.h"
#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Systems/AeyerjiRiftTypes.h"
#include "Systems/AeyerjiWorldStateTypes.h"
#include "Frontend/AeyerjiFrontendTypes.h"
#include "AeyerjiGameState.generated.h"

class AAeyerjiLevelDirector;
class AAeyerjiEncounterDirector;
class AAeyerjiEndRunPortal;
class AAeyerjiSpawnerGroup;
class UAeyerjiStreamingSubsystem;
class APlayerController;
class APlayerState;
class APlayerStart;
class AAeyerjiPlayerState;
class AAeyerjiRewardPresentationActor;

UENUM(BlueprintType)
enum class EAeyerjiRunState : uint8
{
	PreRun        UMETA(DisplayName="PreRun"),
	InRun         UMETA(DisplayName="InRun"),
	BossDefeated  UMETA(DisplayName="BossDefeated"),
	ObjectiveComplete UMETA(DisplayName="ObjectiveComplete"),
	RunComplete   UMETA(DisplayName="RunComplete"),
	ReturnToMenu  UMETA(DisplayName="ReturnToMenu")
};

UENUM(BlueprintType)
enum class EAeyerjiWorldFlowPhase : uint8
{
	Menu               UMETA(DisplayName="Menu"),
	TransitionLoading  UMETA(DisplayName="TransitionLoading"),
	Gameplay           UMETA(DisplayName="Gameplay")
};

UENUM(BlueprintType)
enum class EAeyerjiObjectiveEvent : uint8
{
	None                     UMETA(DisplayName="None"),
	PrimaryObjectiveComplete UMETA(DisplayName="PrimaryObjectiveComplete"),
	BossObjectiveComplete    UMETA(DisplayName="BossObjectiveComplete"),
	MainObjectiveComplete    UMETA(DisplayName="MainObjectiveComplete"),
	RunCompleted             UMETA(DisplayName="RunCompleted"),
	RunFailedTimeExpired     UMETA(DisplayName="RunFailedTimeExpired"),
	RunAbandoned             UMETA(DisplayName="RunAbandoned"),
	RunFailedDefenseObjectiveDestroyed UMETA(DisplayName="RunFailedDefenseObjectiveDestroyed")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FAeyerjiRunStateChangedSignature, EAeyerjiRunState, NewState, EAeyerjiRunState, OldState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAeyerjiRunResultsReadySignature, const FAeyerjiRunResults&, Results);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAeyerjiRiftRunStateChangedSignature, const FAeyerjiRiftRunState&, RiftState);
DECLARE_MULTICAST_DELEGATE_OneParam(FAeyerjiObjectiveStateChangedNativeSignature, const FAeyerjiObjectiveState&);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAeyerjiObjectiveStateChangedSignature, const FAeyerjiObjectiveState&, ObjectiveState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAeyerjiObjectiveEventCompletedSignature, EAeyerjiObjectiveEvent, CompletedEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAeyerjiSurvivalRoundStateChangedSignature, const FAeyerjiSurvivalRoundState&, SurvivalState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAeyerjiSurvivalRoundMessageSignature, FName, MessageKey);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAeyerjiSurvivalUpgradeOfferChangedSignature, const FAeyerjiSurvivalUpgradeOfferState&, OfferState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FAeyerjiWorldFlowPhaseChangedSignature, EAeyerjiWorldFlowPhase, NewPhase, EAeyerjiWorldFlowPhase, OldPhase, int32, TransitionId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FAeyerjiWorldFlowLoadingStateChangedSignature, int32, PendingLoaderCount, int32, TransitionId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FAeyerjiZoneGameplayReadySignature, FName, ZoneId, int32, TransitionId);
DECLARE_MULTICAST_DELEGATE_OneParam(FAeyerjiLobbySnapshotChangedNativeSignature, const FAeyerjiLobbySnapshot&);

USTRUCT()
struct FAeyerjiReplicatedWorldStateItem : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY()
	FAeyerjiWorldStateEntry Entry;

	void PreReplicatedRemove(const struct FAeyerjiReplicatedWorldStateArray& InArraySerializer);
	void PostReplicatedAdd(const struct FAeyerjiReplicatedWorldStateArray& InArraySerializer);
	void PostReplicatedChange(const struct FAeyerjiReplicatedWorldStateArray& InArraySerializer);
};

USTRUCT()
struct FAeyerjiReplicatedWorldStateArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FAeyerjiReplicatedWorldStateItem> Items;

	TWeakObjectPtr<class AAeyerjiGameState> Owner;

	/** Assigns the GameState that consumes client-side fast-array callbacks. */
	void SetOwner(class AAeyerjiGameState* InOwner) { Owner = InOwner; }

	/** Adds or updates a public replicated entry. */
	void UpsertEntry(const FAeyerjiWorldStateEntry& Entry);

	/** Removes a public replicated entry by key. */
	void RemoveEntry(const FAeyerjiWorldStateKey& Key);

	/** Replaces the replicated list with a full authority snapshot. */
	void ResetFromEntries(const TArray<FAeyerjiWorldStateEntry>& Entries);

	/** Net delta serializer used by the replicated GameState property. */
	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FAeyerjiReplicatedWorldStateItem, FAeyerjiReplicatedWorldStateArray>(Items, DeltaParms, *this);
	}

private:
	int32 FindIndexByKey(const FAeyerjiWorldStateKey& Key) const;
};

template<>
struct TStructOpsTypeTraits<FAeyerjiReplicatedWorldStateArray> : public TStructOpsTypeTraitsBase2<FAeyerjiReplicatedWorldStateArray>
{
	enum
	{
		WithNetDeltaSerializer = true
	};
};

/**
 * Networked run state machine (authoritative on the server, replicated to clients).
 * This is the MVP spine for starting/ending a run, showing results, and returning to the main menu cleanly.
 */
UCLASS(BlueprintType)
class AEYERJI_API AAeyerjiGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AAeyerjiGameState();

	/** Initializes the run state machine and binds to the LevelDirector when running on the server. */
	virtual void BeginPlay() override;

	/** Replication descriptor for RunState and RunResults. */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Current run state (replicated). */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Run")
	EAeyerjiRunState GetRunState() const { return RunState; }

	/** Latest run results snapshot (replicated). */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Run")
	const FAeyerjiRunResults& GetRunResults() const { return RunResults; }

	/** Shared authoritative Greater Rift state; clients use this instead of a local LevelDirector timer. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Rift")
	const FAeyerjiRiftRunState& GetRiftRunState() const { return RiftRunState; }

	/** Elapsed run time derived from synchronized GameState server time. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Rift")
	float GetAuthoritativeRunElapsedSeconds() const;

	/** Remaining timed-success window derived from synchronized GameState server time. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Rift")
	float GetAuthoritativeRunRemainingSeconds() const;

	/** Elected leader for the next/active run; the lowest stable PlayerId wins. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Rift")
	AAeyerjiPlayerState* GetRunLeaderPlayerState() const { return RunLeaderPlayerState.Get(); }

	/** Replicated presentation-safe party staging state used by the frontend shell. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Frontend|Lobby")
	const FAeyerjiLobbySnapshot& GetLobbySnapshot() const { return LobbySnapshot; }

	/** True only while another connection may be admitted to this four-player lobby. */
	bool IsFrontendLobbyAcceptingConnections() const;

	/** GameMode roster hook. Readiness is cleared when stable membership changes. */
	void Server_NotifyFrontendRosterChanged();

	/** Rebuilds verified member data after one profile submission changes. */
	void Server_NotifyFrontendProfileChanged(AAeyerjiPlayerState* PlayerState, bool bRevisionChanged);

	/** Owned PlayerState command endpoints; never callable directly by an unowned widget actor. */
	bool Server_SetFrontendReady(AAeyerjiPlayerState* Requester, bool bReady);
	bool Server_SetFrontendActivity(AAeyerjiPlayerState* Requester, EAeyerjiRiftActivityType ActivityType);
	bool Server_SetFrontendTier(AAeyerjiPlayerState* Requester, int32 Tier);
	bool Server_RequestFrontendLaunch(AAeyerjiPlayerState* Requester);

	/** Server validation endpoint used by the leader-facing PlayerState RPC. */
	bool Server_TrySelectRiftTier(AAeyerjiPlayerState* Requester, int32 RequestedTier, EAeyerjiRiftTierSelectionFailure& OutFailure);

	/** Validates leader authority and stages a same-tier or newly-earned-tier retry before travel. */
	bool Server_RetryRiftRunForRequester(
		AAeyerjiPlayerState* Requester,
		bool bSelectEarnedTier,
		EAeyerjiRiftTierSelectionFailure& OutFailure);

	/** Records a player death immediately; the first boss-phase death permanently removes flawless eligibility. */
	void Server_NotifyPlayerDeath(AAeyerjiPlayerState* DeadPlayerState);

	/** True while deaths should respawn at the configured boss-arena PlayerStart without resetting the boss. */
	bool IsBossArenaRespawnActive() const;

	/** PlayerStart tag configured by the active boss definition. */
	FName GetBossArenaRespawnPlayerStartTag() const;

	/** Compact debug string for the replicated run lifecycle and current world-state run facts. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Run|Debug")
	FString GetRunLifecycleDebugString() const;

	/** Latest shared objective snapshot used by local HUDs. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Run|Objective")
	const FAeyerjiObjectiveState& GetCurrentObjectiveState() const { return CurrentObjectiveState; }

	/** Returns true when the replicated objective snapshot is safe for UI consumption. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Run|Objective")
	bool IsObjectiveStateReady() const { return CurrentObjectiveState.bObjectiveReady; }

	/** Latest shared survival-round snapshot used by local HUDs. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Run|Survival")
	const FAeyerjiSurvivalRoundState& GetCurrentSurvivalRoundState() const { return CurrentSurvivalRoundState; }

	/** Latest shared survival upgrade offer used by local HUDs. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Run|Survival")
	const FAeyerjiSurvivalUpgradeOfferState& GetCurrentSurvivalUpgradeOfferState() const { return CurrentSurvivalUpgradeOfferState; }

	/** Server-only: replaces the shared replicated survival-round snapshot. */
	void SetSurvivalRoundStateFromServer(const FAeyerjiSurvivalRoundState& NewState);

	/** Server-only: clears the shared replicated survival-round snapshot. */
	void ClearSurvivalRoundStateFromServer();

	/** Server-only: replaces the shared replicated survival upgrade offer snapshot. */
	void SetSurvivalUpgradeOfferStateFromServer(const FAeyerjiSurvivalUpgradeOfferState& NewState);

	/** Server-only: clears the shared replicated survival upgrade offer snapshot. */
	void ClearSurvivalUpgradeOfferStateFromServer();

	/** Current world-flow phase used by streaming transition orchestration. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|World|Flow")
	EAeyerjiWorldFlowPhase GetWorldFlowPhase() const { return WorldFlowPhase; }

	/** Current target zone id for the active world transition. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|World|Flow")
	FName GetActiveZoneId() const { return ActiveZoneId; }

	/** Monotonic transition id used to correlate zone-ready handshakes. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|World|Flow")
	int32 GetTransitionId() const { return TransitionId; }

	/** Number of loading participants still blocking the current world transition from finishing. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|World|Flow")
	int32 GetPendingWorldFlowLoaderCount() const { return PendingWorldFlowLoaderCount; }

	/** Returns true while extra world-flow loading participants are still blocking gameplay start. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|World|Flow")
	bool IsWaitingOnWorldFlowLoaders() const { return PendingWorldFlowLoaderCount > 0; }

	/** Server-only: starts a server-authoritative transition into a target zone. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|World|Flow")
	bool Server_BeginWorldTransition(FName TargetZoneId);

	/** Server-only: records a player's zone-ready acknowledgement for a transition id. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|World|Flow")
	bool Server_ReportPlayerZoneReady(APlayerState* PlayerState, int32 ReportedTransitionId);

	/** Server-only: replaces the shared replicated objective snapshot with a coherent server-authored state. */
	void SetObjectiveStateFromServer(const FAeyerjiObjectiveState& NewState);

	/** Server-only: clears the shared replicated objective snapshot when objective data is no longer valid. */
	void ClearObjectiveStateFromServer();

	/** Server-only: publishes one public world-state entry to all clients. */
	void PublishWorldStateEntryFromServer(const FAeyerjiWorldStateEntry& Entry);

	/** Server-only: removes one public world-state entry from all clients. */
	void RemoveWorldStateEntryFromServer(const FAeyerjiWorldStateKey& Key);

	/** Server-only: replaces the public replicated world-state mirror. */
	void RepublishWorldStateFromServer(const TArray<FAeyerjiWorldStateEntry>& Entries);

	/** Client-side fast-array callback for changed public world-state entries. */
	void HandleReplicatedWorldStateEntryChanged(const FAeyerjiWorldStateEntry& Entry);

	/** Client-side fast-array callback for removed public world-state entries. */
	void HandleReplicatedWorldStateEntryRemoved(const FAeyerjiWorldStateKey& Key);

	/** Broadcasts the current objective snapshot to local listeners on this instance. */
	void BroadcastCurrentObjectiveState();

	/**
	 * Server-only: starts the run and transitions PreRun -> InRun.
	 * If a LevelDirector is present, it also calls StartRun() on it.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Run")
	bool Server_StartRun();

	/** Idempotently commits weighted-progress completion and requests exactly one boss encounter. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Rift")
	bool Server_BeginBossPhase();

	/**
	 * Server-only: marks the boss as defeated and transitions InRun -> BossDefeated.
	 * This typically comes from the boss spawner clearing.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Run")
	bool Server_NotifyBossDefeated();

	/**
	 * Server-only: marks the objective as complete and transitions BossDefeated/InRun -> ObjectiveComplete.
	 * Intended to be called after the boss stinger or when a kill-target objective is met.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Run")
	bool Server_BeginObjectiveComplete();

	/**
	 * Server-only: finalizes a victory after the extraction portal is used and transitions ObjectiveComplete -> RunComplete.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Run")
	bool Server_CompleteExtraction();

	/**
	 * Server-only: finalizes the run as a time-expired failure and transitions InRun -> RunComplete.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Run")
	bool Server_FailRunTimeExpired();

	/**
	 * Server-only: finalizes the run because the survival defense objective was destroyed.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Run")
	bool Server_FailRunDefenseObjectiveDestroyed();

	/**
	 * Server-only: finalizes an immediate manual end and transitions the current run state into RunComplete.
	 * This remains the compatibility wrapper used by RequestEndRun().
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Run")
	bool Server_MarkRunComplete();

	/**
	 * Server-only: transitions RunComplete -> ReturnToMenu.
	 * Clients will remove UI and travel back to the main menu map.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Run")
	bool Server_ReturnToMenu();

	/**
	 * Server-only: deterministic escape hatch for the MVP.
	 * From any state, ends the run (if needed), marks results, and returns to menu.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Run")
	bool Server_ForceEndRunAndReturnToMenu();

	/**
	 * Server-only: restarts the current gameplay mission via full gameplay-map travel after RunComplete.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Run")
	bool Server_RetryRun();

public:
	/** Fired whenever the run state changes (server + clients). */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|Run|Events")
	FAeyerjiRunStateChangedSignature OnRunStateChanged;

	/** Fired when a RunComplete results snapshot is available (server + clients). */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|Run|Events")
	FAeyerjiRunResultsReadySignature OnRunResultsReady;

	/** Fired whenever the replicated Greater Rift snapshot changes. */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|Rift|Events")
	FAeyerjiRiftRunStateChangedSignature OnRiftRunStateChanged;

	/** Fired whenever the replicated shared objective snapshot changes. */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|Run|Events")
	FAeyerjiObjectiveStateChangedSignature OnObjectiveStateChanged;

	/** Fired when a run objective-related event completes so UI can react in Blueprint. */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|Run|Events")
	FAeyerjiObjectiveEventCompletedSignature OnObjectiveEventCompleted;

	/** Fired whenever the replicated survival-round snapshot changes. */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|Run|Events")
	FAeyerjiSurvivalRoundStateChangedSignature OnSurvivalRoundStateChanged;

	/** Fired when a survival-round state carries a non-empty message key. */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|Run|Events")
	FAeyerjiSurvivalRoundMessageSignature OnSurvivalRoundMessage;

	/** Fired whenever the replicated between-round survival upgrade offer changes. */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|Run|Events")
	FAeyerjiSurvivalUpgradeOfferChangedSignature OnSurvivalUpgradeOfferChanged;

	/** Native objective-state signal for C++ systems that bridge replicated state into local UI. */
	FAeyerjiObjectiveStateChangedNativeSignature OnObjectiveStateChangedNative;

	/** Fired whenever world-flow phase transitions for menu/loading/gameplay orchestration. */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|World|Flow|Events")
	FAeyerjiWorldFlowPhaseChangedSignature OnWorldFlowPhaseChanged;

	/** Fired when extra world-flow loading blockers are added or cleared. */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|World|Flow|Events")
	FAeyerjiWorldFlowLoadingStateChangedSignature OnWorldFlowLoadingStateChanged;

	/** Fired after a gameplay zone is fully active and post-stream activation work has completed. */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|World|Flow|Events")
	FAeyerjiZoneGameplayReadySignature OnZoneGameplayReady;

	/** Native signal consumed by the frontend subsystem; Blueprint receives the shell presentation hook. */
	FAeyerjiLobbySnapshotChangedNativeSignature OnLobbySnapshotChangedNative;

	/** Travel URL for the main menu map (e.g. /Game/Maps/MainMenu). If empty, uses GameInstance->ReturnToMainMenu(). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Run|Travel")
	FString MainMenuTravelURL = "/Game/Levels/L_MainMenu";

	/** Optional delay (seconds) between BossDefeated and ObjectiveComplete. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Run|Flow", meta=(ClampMin="0.0"))
	float BossDefeatedToCompleteDelay = 1.25f;

	/** Optional delay (seconds) after RunComplete before auto returning to menu. Set 0 to require manual return. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Run|Flow", meta=(ClampMin="0.0"))
	float AutoReturnToMenuDelay = 0.f;

	/** Time reserved for the frontend portal animation after all launch validation succeeds. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Frontend|Lobby", meta=(ClampMin="0.0"))
	float FrontendLaunchCountdownSeconds = 1.5f;

	/** Failsafe timeout for server-side world-flow transitions before fallback handling is applied. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|World|Flow", meta=(ClampMin="1.0"))
	float WorldTransitionTimeoutSeconds = 15.f;

	/** Optional fixed run seed used by deterministic automation. Zero generates a fresh server seed. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Rift|Automation", meta=(ClampMin="0"))
	int32 FixedRiftRunSeed = 0;

protected:
	/** Replicated run state; drives UI and travel. */
	UPROPERTY(ReplicatedUsing=OnRep_RunState, VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Run|State")
	EAeyerjiRunState RunState = EAeyerjiRunState::PreRun;

	/** RepNotify for RunState; broadcasts delegates and triggers client travel where appropriate. */
	UFUNCTION()
	void OnRep_RunState(EAeyerjiRunState OldState);

	/** Replicated results snapshot; updated at BossDefeated/RunComplete. */
	UPROPERTY(ReplicatedUsing=OnRep_RunResults, VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Run|State")
	FAeyerjiRunResults RunResults;

	/** RepNotify for RunResults; fires OnRunResultsReady on clients when results replicate. */
	UFUNCTION()
	void OnRep_RunResults();

	/** Replicated Greater Rift run facts and synchronized timer origin. */
	UPROPERTY(ReplicatedUsing=OnRep_RiftRunState, VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Rift")
	FAeyerjiRiftRunState RiftRunState;

	/** Server-authored party staging state. Clients never derive readiness or tier authority locally. */
	UPROPERTY(ReplicatedUsing=OnRep_LobbySnapshot, VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Lobby")
	FAeyerjiLobbySnapshot LobbySnapshot;

	UFUNCTION()
	void OnRep_LobbySnapshot();

	/** RepNotify hook used to refresh event-driven Blueprint presentation. */
	UFUNCTION()
	void OnRep_RiftRunState();

	/** Replicated shared objective snapshot used by local HUDs. */
	UPROPERTY(ReplicatedUsing=OnRep_CurrentObjectiveState, VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Run|State")
	FAeyerjiObjectiveState CurrentObjectiveState;

	/** RepNotify for the shared objective snapshot; broadcasts OnObjectiveStateChanged on clients. */
	UFUNCTION()
	void OnRep_CurrentObjectiveState();

	/** Replicated shared survival-round snapshot used by local HUDs. */
	UPROPERTY(ReplicatedUsing=OnRep_CurrentSurvivalRoundState, VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Run|State")
	FAeyerjiSurvivalRoundState CurrentSurvivalRoundState;

	/** RepNotify for the shared survival-round snapshot; broadcasts OnSurvivalRoundStateChanged on clients. */
	UFUNCTION()
	void OnRep_CurrentSurvivalRoundState();

	/** Replicated shared survival upgrade offer used by local HUDs. */
	UPROPERTY(ReplicatedUsing=OnRep_CurrentSurvivalUpgradeOfferState, VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Run|State")
	FAeyerjiSurvivalUpgradeOfferState CurrentSurvivalUpgradeOfferState;

	/** RepNotify for the shared upgrade offer snapshot; broadcasts OnSurvivalUpgradeOfferChanged on clients. */
	UFUNCTION()
	void OnRep_CurrentSurvivalUpgradeOfferState();

	/** Replicated last objective event type for UI event fan-out on clients. */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Run|State")
	EAeyerjiObjectiveEvent LastCompletedObjectiveEvent = EAeyerjiObjectiveEvent::None;

	/** Replicated objective event version used to trigger OnRep for repeated event types. */
	UPROPERTY(ReplicatedUsing=OnRep_ObjectiveEventVersion, VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Run|State")
	int32 ObjectiveEventVersion = 0;

	/** RepNotify for objective event completion; broadcasts OnObjectiveEventCompleted on clients. */
	UFUNCTION()
	void OnRep_ObjectiveEventVersion();

	/** Replicated world-flow phase used to drive menu/loading/gameplay behavior. */
	UPROPERTY(ReplicatedUsing=OnRep_WorldFlowPhase, VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|World|Flow|State")
	EAeyerjiWorldFlowPhase WorldFlowPhase = EAeyerjiWorldFlowPhase::Menu;

	/** RepNotify for world-flow phase transitions. */
	UFUNCTION()
	void OnRep_WorldFlowPhase(EAeyerjiWorldFlowPhase OldPhase);

	/** Replicated active target zone id for the current world transition request. */
	UPROPERTY(ReplicatedUsing=OnRep_ActiveZoneId, VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|World|Flow|State")
	FName ActiveZoneId = NAME_None;

	/** RepNotify for active target zone id. */
	UFUNCTION()
	void OnRep_ActiveZoneId();

	/** Replicated transition id that increments on each server transition request. */
	UPROPERTY(ReplicatedUsing=OnRep_TransitionId, VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|World|Flow|State")
	int32 TransitionId = 0;

	/** RepNotify for transition id replication. */
	UFUNCTION()
	void OnRep_TransitionId();

	/** Replicated count of extra world-flow loading blockers still delaying gameplay start. */
	UPROPERTY(ReplicatedUsing=OnRep_PendingWorldFlowLoaderCount, VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|World|Flow|State")
	int32 PendingWorldFlowLoaderCount = 0;

	/** RepNotify for external world-flow loading blocker changes. */
	UFUNCTION()
	void OnRep_PendingWorldFlowLoaderCount();

	/** Replicated public world-state mirror consumed by client subsystems. */
	UPROPERTY(Replicated, VisibleAnywhere, Category="Aeyerji|World|State")
	FAeyerjiReplicatedWorldStateArray ReplicatedWorldStateEntries;

protected:
	/** Binds to LevelDirector delegates (run start and boss spawner clear) if one exists in the world. */
	void BindToLevelDirector();

	/** Rebuilds the authoritative objective snapshot from currently bound gameplay actors. */
	void RefreshObjectiveStateFromAuthority();

	/** Captures a results snapshot from the LevelDirector, if present. */
	void SnapshotRunResults(EAeyerjiRunResolution Resolution, bool bBossDefeated);

	/** Server-only helper that emits a replicated objective-complete event for Blueprint UI listeners. */
	void BroadcastObjectiveEventCompleted(EAeyerjiObjectiveEvent CompletedEvent);

	/** Broadcasts OnRunResultsReady once when in RunComplete and results are available. */
	void MaybeBroadcastRunResults();

	/** Broadcasts the current survival-round snapshot to local listeners on this instance. */
	void BroadcastCurrentSurvivalRoundState();

	/** Broadcasts the current survival upgrade offer snapshot to local listeners on this instance. */
	void BroadcastCurrentSurvivalUpgradeOfferState();

	/** Moves the state machine to a new state (server-only) and runs state entry side effects. */
	bool SetRunState(EAeyerjiRunState NewState);

	/** Returns true if the transition is allowed for the MVP flow. */
	bool CanTransitionTo(EAeyerjiRunState NewState) const;

	/** Runs state entry effects and broadcasts OnRunStateChanged for local listeners. */
	void HandleRunStateChanged(EAeyerjiRunState OldState);

	/** Client-side cleanup + travel used when entering ReturnToMenu. */
	void CleanupUIAndReturnToMenu();

	/** Spawns the extraction portal after a successful objective completion. */
	void SpawnEndRunPortal();

	/** Cleans up any active extraction portal for the current run. */
	void ClearEndRunPortal();

	/** Writes the completed run and character state to local save data. */
	void PersistRunResultsForPlayers();

	/** Stops active run systems, freezes players, and removes live enemies after RunComplete is saved. */
	void FinalizeCompletedRunWorld();

	/** Stops enemy combat immediately at run end without destroying actors inside active GAS callbacks. */
	void StopRemainingRunEnemiesForCompletedRun();

	/** Defers enemy actor destruction until current damage/ability callbacks have unwound. */
	void ScheduleDeferredDestroyRemainingRunEnemies();

	/** Deletes any remaining live enemies without rewarding the player. */
	void DestroyRemainingRunEnemies();

	/** Removes runtime-spawned persistent-world actors so a streamed gameplay session starts from a clean state. */
	void DestroyPersistentRuntimeActorsForFreshSession();

	/** Resolves the streaming subsystem for this world context. */
	UAeyerjiStreamingSubsystem* GetStreamingSubsystem() const;

	/** Returns the configured menu zone used for streamed return/retry staging. */
	FName ResolveMenuZoneId() const;

	/** Applies world-flow replication updates and kicks client-side streaming requests. */
	void HandleReplicatedWorldFlowState();

	/** Broadcasts gameplay-ready once per transition after a gameplay zone becomes fully active. */
	void MaybeBroadcastZoneGameplayReady();

	/** Assigns a new world-flow phase and emits local phase-change events. */
	void SetWorldFlowPhase(EAeyerjiWorldFlowPhase NewPhase);

	/** Server helper that tries to complete a transition once all ready requirements are met. */
	void TryCompleteWorldTransition();

	/** Returns true when server streaming and all connected players have acknowledged readiness. */
	bool AreAllPlayersReadyForTransition() const;

	/** Marks listen-server local players as ready once server streaming is ready. */
	void MarkLocalPlayersReadyForTransition();

	/** Server-only: starts any extra loading work that should finish before players spawn. */
	void PrepareWorldFlowLoadingRequirements();

	/** Clears bound loading participants and resets the replicated loading-blocker count. */
	void ClearWorldFlowLoadingRequirements();

	/** Updates the replicated loading-blocker count and notifies local listeners. */
	void SetPendingWorldFlowLoaderCount(int32 NewPendingCount);

	/** Respawns or despawns players according to the active zone's spawn policy. */
	bool ApplyZoneSpawnPolicy();

	/** Re-resolves gameplay actors after a streamed gameplay zone becomes active. */
	bool ResolveGameplayActorsForActiveZone();

	/** Finds PlayerStart candidates for the zone tag and returns a best match for the player index. */
	APlayerStart* SelectPlayerStartForZone(const FName DesiredTag, int32 PlayerIndex) const;

	/** Server-only: mirrors run lifecycle into world-state facts for persistence bridges. */
	void PublishRunLifecycleWorldState(EAeyerjiRunState OldState);

protected:
	/** Server hook: LevelDirector notified that it started/stopped a run. */
	UFUNCTION()
	void HandleLevelDirectorRunActiveChanged(bool bIsRunning);

	/** Server hook: boss spawner group finished. */
	UFUNCTION()
	void HandleBossSpawnerCleared(AAeyerjiSpawnerGroup* Spawner);

	/** Server hook: a tracked boss died inside the configured boss spawner. */
	UFUNCTION()
	void HandleBossSpawnerBossDefeated(AAeyerjiSpawnerGroup* Spawner, AActor* BossEnemy);

	/** Streaming subsystem callback fired when the active zone reaches ready state locally. */
	UFUNCTION()
	void HandleStreamingZoneReady(FName ZoneId);

	/** Server callback fired when the encounter director finishes its initial fixed-population spawn. */
	UFUNCTION()
	void HandleEncounterDirectorInitialSpawnComplete(AAeyerjiEncounterDirector* Director);

	/** Server hook fired when the active LevelDirector run timer expires. */
	UFUNCTION()
	void HandleLevelDirectorRunTimerExpired();

	/** Server hook fired when encounter objective progress changes. */
	UFUNCTION()
	void HandleEncounterProgressChanged(float Progress01, int32 Killed, int32 Total);

	/** Server hook fired when the LevelDirector advances or resets the primary objective phase. */
	UFUNCTION()
	void HandlePrimaryObjectiveStateChanged(bool bIsComplete);

	/** Timer callback to advance BossDefeated -> ObjectiveComplete. */
	void HandleBossDefeatedDelayElapsed();

	/** Timer callback to auto-advance RunComplete -> ReturnToMenu. */
	void HandleAutoReturnDelayElapsed();

	/** Timer callback used as a failsafe when a world transition does not complete in time. */
	void HandleWorldTransitionTimeout();

	/** Continues a staged retry once the gameplay zone has been safely unloaded in PIE. */
	void HandleDeferredRetryTravel();

	/** Deferred auto-start check; retries while player profile/pawn replication is still settling. */
	void HandleDeferredAutoStartRun();

	/** Completes the validated one-shot lobby launch after the replicated presentation countdown. */
	void HandleFrontendLaunchCountdownElapsed();

	/** Validates all authoritative actors, possessed pawns, and applied profiles required by run start. */
	bool ValidateRunStartReadiness(FString& OutReason) const;

	/** Freezes the current participant set and elects the lowest stable PlayerId as leader. */
	bool SnapshotRunParticipantsAndElectLeader(FString& OutReason);

	/** Resolves the launch party's immutable activity snapshot and applies the optional Excursion tier before any encounter can activate. */
	bool FreezeRiftConfigurationForNewRun(const FAeyerjiRiftTierRow* TierRow, FString& OutReason);

	/** Re-elects the lowest stable loaded participant before pre-run leader actions. */
	void ElectRunLeaderFromCurrentPlayers();

	/** Rolls immutable per-player layer ledgers, releases base pickups, and initializes the shared private cache. */
	bool FinalizeRiftRewards();
	bool RollRiftRewardLayer(AAeyerjiPlayerState* PlayerState, const FAeyerjiRiftRewardLayerDefinition& Layer,
		FGameplayTag FallbackSourceTag, TArray<FLootDropResult>& OutResults) const;
	bool ReleaseRiftBaseRewards(AAeyerjiPlayerState* PlayerState, TArray<FLootDropResult>& Results,
		TSet<int32>& ReleasedIndices);

private:
	TWeakObjectPtr<AAeyerjiLevelDirector> CachedLevelDirector;
	TWeakObjectPtr<AAeyerjiEncounterDirector> CachedEncounterDirector;
	TWeakObjectPtr<AAeyerjiEncounterDirector> CachedLoadingEncounterDirector;
	TWeakObjectPtr<AAeyerjiEndRunPortal> CachedEndRunPortal;
	TWeakObjectPtr<AAeyerjiSpawnerGroup> CachedBossSpawner;

	FTimerHandle BossDefeatedDelayHandle;
	FTimerHandle AutoReturnDelayHandle;
	FTimerHandle WorldTransitionTimeoutHandle;
	FTimerHandle DeferredRetryTravelHandle;
	FTimerHandle DeferredDestroyRunEnemiesHandle;
	FTimerHandle DeferredAutoStartRunHandle;
	FTimerHandle FrontendLaunchCountdownHandle;

	/** Rebuilds the replicated roster, stable leader, and common verified-profile cap. */
	void RebuildFrontendLobbySnapshot(bool bDetectRosterChange);

	/** Clears every manual ready bit after shared inputs, leadership, or membership changes. */
	void ClearFrontendReadiness();

	/** Finds a configured Excursion row by its stable Tier_N row identity. */
	const FAeyerjiRiftTierRow* FindFrontendTierRow(int32 Tier) const;

	/** Last stable membership projection used to detect joins/leaves without relying on actor order. */
	TArray<int32> LastFrontendRosterPlayerIds;

	/** Server-only immutable participant snapshot for rewards, progression, and retry. */
	TArray<TWeakObjectPtr<AAeyerjiPlayerState>> RunParticipants;

	/** Pre-run/active run leader elected from stable PlayerIds. */
	TWeakObjectPtr<AAeyerjiPlayerState> RunLeaderPlayerState;

	/** Tier requested for the next run. The active value is frozen independently in RiftRunState. */
	int32 PendingSelectedRiftTier = 1;

	/** Next authority-issued run serial; zero is reserved for no run. */
	int32 NextRunSerial = 1;

	/** One-shot guards stamped with the serial that completed each authority action. */
	int32 StartedRunSerial = 0;
	int32 BossPhaseStartedRunSerial = 0;
	int32 BossDefeatedRunSerial = 0;
	int32 ResultsFinalizedRunSerial = 0;
	int32 ExtractionCompletedRunSerial = 0;
	int32 PersistedRunSerial = 0;

	/** Prevents readiness retries from flooding logs while profiles settle. */
	FString LastAutoStartReadinessReason;

	struct FRiftPlayerRewardLedger
	{
		TArray<FLootDropResult> BaseResults;
		TArray<FLootDropResult> TimedResults;
		TArray<FLootDropResult> FlawlessResults;
		TSet<int32> ReleasedBaseIndices;
		bool bBaseRolled = false;
		bool bTimedRolled = false;
		bool bFlawlessRolled = false;
		bool bRolled = false;
		bool bBonusBundleInstalled = false;
	};

	/** Server-only immutable rolls and release markers keyed by the snapshotted run participant. */
	TMap<TWeakObjectPtr<AAeyerjiPlayerState>, FRiftPlayerRewardLedger> RiftRewardLedger;
	TWeakObjectPtr<AAeyerjiRewardPresentationActor> RiftBonusRewardCache;

	/** Reward inputs copied at run start so live DataAsset edits cannot change eligibility or rerolls. */
	FAeyerjiRiftRewardLayerDefinition FrozenRiftBaseReward;
	FAeyerjiRiftRewardLayerDefinition FrozenRiftTimedReward;
	FAeyerjiRiftRewardLayerDefinition FrozenRiftFlawlessReward;
	TSubclassOf<AAeyerjiRewardPresentationActor> FrozenRiftBonusRewardPresentationClass;
	bool bHasFrozenRiftRewardConfiguration = false;

	/** Server-only counter used to stamp RunResults.ResultsVersion. */
	int32 NextResultsVersion = 1;

	/** Local gate to prevent duplicate results broadcasts. */
	int32 LastBroadcastResultsVersion = 0;

	/** Local gate to prevent duplicate gameplay-ready broadcasts for the same transition. */
	int32 LastBroadcastGameplayReadyTransitionId = 0;

	/** Server-side set of players that acknowledged readiness for the current transition id. */
	TSet<TObjectPtr<APlayerState>> ZoneReadyPlayers;

	/** Server-side gate set when the streaming subsystem reports the active zone ready. */
	bool bServerZoneReady = false;

	/** Client-side guard to avoid duplicate EnterZone() requests for the same transition id. */
	int32 LastRequestedClientTransitionId = 0;

	/** Client-side guard to avoid duplicate Server_ReportZoneReady RPCs for the same transition id. */
	int32 LastReportedReadyTransitionId = 0;

	/** Server-side guard to avoid re-preparing extra loading requirements for the same transition. */
	int32 LastPreparedWorldFlowLoadingTransitionId = 0;

	/** PIE-only gate used to stage retry map travel after the gameplay zone has been unloaded. */
	bool bPendingRetryAfterMenuTransition = false;

	/** Target zone restored once the staged retry map restart begins. */
	FName PendingRetryZoneId = NAME_None;
};
