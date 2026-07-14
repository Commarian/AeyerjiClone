// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/AeyerjiAbilityProgression.h"
#include "GameFramework/PlayerState.h"
#include "Abilities/AeyerjiAbilitySlot.h"
#include "AeyerjiRunTypes.h"
#include "Systems/AeyerjiRiftTypes.h"
#include "Systems/AeyerjiSaveTypes.h"
#include "Frontend/AeyerjiFrontendTypes.h"
#include "AeyerjiPlayerState.generated.h"

class UGameplayAbility;
class UPlayerStatsTrackingComponent;
class UAeyerjiSaveGame;
class APawn;

DECLARE_MULTICAST_DELEGATE_OneParam(FAeyerjiFrontendRequestRejectedNative, EAeyerjiFrontendFailure);

/** Replicated profile hydration state used to keep UI widgets from mutating save-backed runtime before load finishes. */
UENUM(BlueprintType)
enum class EAeyerjiProfileLoadState : uint8
{
	Pending = 0,
	Applying,
	Applied,
	Failed
};

/**
 * 
 */
UCLASS()
class AEYERJI_API AAeyerjiPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	/** Lifetime loot stats holder for this player. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aeyerji|Stats")
	TObjectPtr<UPlayerStatsTrackingComponent> PlayerStatsTracking = nullptr;

	/** 7 slots (including potion slot) - replicated and saved. */
	UPROPERTY(ReplicatedUsing = OnRep_ActionBar, SaveGame, BlueprintReadWrite)
	TArray<FAeyerjiAbilitySlot> ActionBar;
	
	/** Called on every client *after* ActionBar is updated. */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
		FOnActionBarChanged, const TArray<FAeyerjiAbilitySlot>&, NewBar);

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
		FOnActionBarSwapBlocked, FText, Reason, TSubclassOf<UGameplayAbility>, AbilityClass);

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
		FOnSaveSlotOverrideChanged, const FString&, NewSlot);

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(
		FOnAbilityProgressionChanged, const TArray<FAeyerjiAbilityProgressEntry>&, ProgressEntries, int32, RemainingPoints, int32, TotalPointSpends);

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
		FOnGoldChanged, int64, NewGold, int64, Delta);

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
		FOnRiftTierProgressionChanged, int32, HighestUnlockedTier, int32, LastSelectedTier);

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
		FOnRiftTierSelectionRejected, EAeyerjiRiftTierSelectionFailure, Reason, int32, RequestedTier);

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
		FOnPersonalRunResultsChanged, const FAeyerjiRunResults&, Results);

	AAeyerjiPlayerState();

	/** Blueprint-assignable notification. */
	UPROPERTY(EditAnywhere, Category = "Aeyerji|Events")
	FOnActionBarChanged OnActionBarChanged;

	UPROPERTY(BlueprintAssignable, Category = "Aeyerji|ActionBar")
	FOnActionBarSwapBlocked OnActionBarSwapBlocked;

	UPROPERTY(BlueprintAssignable, Category = "Aeyerji|SaveGame")
	FOnSaveSlotOverrideChanged OnSaveSlotOverrideChanged;

	UPROPERTY(BlueprintAssignable, Category = "Aeyerji|Abilities")
	FOnAbilityProgressionChanged OnAbilityProgressionChanged;

	/** Blueprint-assignable notification fired after replicated or authority-side gold changes. */
	UPROPERTY(BlueprintAssignable, Category = "Aeyerji|Currency")
	FOnGoldChanged OnGoldChanged;

	/** Fired after profile hydration or an authoritative Greater Rift unlock/selection change. */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|Rift")
	FOnRiftTierProgressionChanged OnRiftTierProgressionChanged;

	/** Owning-client rejection hook; Blueprint maps the enum to localized string-table text. */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|Rift")
	FOnRiftTierSelectionRejected OnRiftTierSelectionRejected;

	/** Owner-specific results, including personal unlock and reward eligibility. */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|Rift")
	FOnPersonalRunResultsChanged OnPersonalRunResultsChanged;

	/** Public read-only accessor for C++ callers. */
	const TArray<FAeyerjiAbilitySlot>& GetActionBar() const { return ActionBar; }

	/** Returns true after the authoritative character profile has been applied. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|SaveGame")
	bool IsProfileLoadApplied() const { return ProfileLoadState == EAeyerjiProfileLoadState::Applied; }

	/** Current replicated profile load state for UI gating and diagnostics. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|SaveGame")
	EAeyerjiProfileLoadState GetProfileLoadState() const { return ProfileLoadState; }

	UFUNCTION(BlueprintPure, Category="Aeyerji|Abilities")
	const TArray<FAeyerjiAbilityProgressEntry>& GetAbilityProgressEntries() const { return AbilityProgressEntries; }

	UFUNCTION(BlueprintPure, Category="Aeyerji|Abilities")
	int32 GetUnspentAbilityPoints() const { return UnspentAbilityPoints; }

	UFUNCTION(BlueprintPure, Category="Aeyerji|Abilities")
	int32 GetTotalAbilityPointSpends() const { return TotalAbilityPointSpends; }

	/** Current profile-persistent gold balance replicated from the server. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Currency")
	int64 GetGold() const { return Gold; }

	/** Highest Greater Rift tier this loaded profile may select. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Rift")
	int32 GetHighestUnlockedRiftTier() const { return HighestUnlockedRiftTier; }

	/** Last Greater Rift tier selected by this profile. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Rift")
	int32 GetLastSelectedRiftTier() const { return LastSelectedRiftTier; }

	/** Owner-specific run results replicated independently of the shared party result. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Rift")
	const FAeyerjiRunResults& GetPersonalRunResults() const { return PersonalRunResults; }

	/** Adds profile-persistent gold on the authority and mirrors the cache used by checkpoint saves. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Currency")
	void AddGold(int64 DeltaGold, FName Reason = NAME_None);

	/** Returns true when this player can afford the requested gold cost. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Currency")
	bool CanSpendGold(int64 Cost) const;

	/** Spends gold on the authority if affordable and mirrors the cache used by checkpoint saves. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Currency")
	bool TrySpendGold(int64 Cost, FName Reason = NAME_None);

	UFUNCTION(BlueprintPure, Category="Aeyerji|Abilities")
	int32 GetAbilityRank(FGameplayTag AbilityTag) const;

	UFUNCTION(BlueprintPure, Category="Aeyerji|Abilities")
	bool IsAbilityBaseUnlocked(FGameplayTag AbilityTag) const;

	UFUNCTION(BlueprintPure, Category="Aeyerji|Abilities")
	bool CanUpgradeAbility(FGameplayTag AbilityTag, FText& OutFailureReason) const;

	/** Applies save-loaded progression before abilities are granted from the action bar. */
	void ApplyLoadedAbilityProgression(const TArray<FAeyerjiAbilityProgressEntry>& InEntries, int32 InUnspentAbilityPoints, int32 InTotalAbilityPointSpends);

	/** Applies save-loaded profile currency during authoritative profile hydration. */
	void ApplyLoadedGold(int64 LoadedGold);

	/** Applies migrated save-backed Greater Rift progression during authoritative profile hydration. */
	void ApplyLoadedRiftProgression(int32 LoadedHighestUnlockedTier, int32 LoadedLastSelectedTier);

	/** Server-only progression update used after validated selection or tier advancement. */
	void SetRiftProgressionFromServer(int32 NewHighestUnlockedTier, int32 NewLastSelectedTier);

	/** Server-only owner result snapshot. */
	void SetPersonalRunResultsFromServer(const FAeyerjiRunResults& NewResults);

	/** Server-side leveling rewards call this to add spendable ability points. */
	void GrantAbilityPoints(int32 DeltaPoints);

	/** Server/local load pipeline updates this as resolved profile data moves through hydration. */
	void SetProfileLoadState(EAeyerjiProfileLoadState NewState);

	/** Server-side helper that overwrites the bar and replicates it. */
	UFUNCTION(BlueprintCallable, Server, Reliable)
	void Server_SetActionBar(const TArray<FAeyerjiAbilitySlot>& NewBar);

	/** Server-side helper for a single-slot edit so stale client bars do not wipe other slots. */
	UFUNCTION(Server, Reliable)
	void Server_SetActionBarSlot(int32 SlotIndex, const FAeyerjiAbilitySlot& NewSlot);

	/** Client notify when a swap/removal was blocked by cooldown. */
	UFUNCTION(Client, Reliable)
	void Client_ActionBarSwapBlocked(const FText& Reason, TSubclassOf<UGameplayAbility> AbilityClass);

	/** Client-side commit of a server-approved profile snapshot. */
	UFUNCTION(Client, Reliable)
	void Client_CommitAuthoritativeProfile(const FAeyerjiSaveTransportHeader& Header, const TArray<uint8>& Bytes);

	/** Begins a bounded server-to-owning-client profile commit transfer. */
	UFUNCTION(Client, Reliable)
	void Client_BeginAuthoritativeProfileCommit(const FAeyerjiSaveTransportHeader& Header, int32 TotalBytes, int32 ChunkSize);

	/** Sends one bounded profile commit chunk to the owning client. */
	UFUNCTION(Client, Reliable)
	void Client_SendAuthoritativeProfileCommitChunk(int32 ChunkIndex, const TArray<uint8>& ChunkBytes);

	/** Finalizes the bounded profile commit transfer and writes the reconstructed save locally. */
	UFUNCTION(Client, Reliable)
	void Client_FinalizeAuthoritativeProfileCommit();

	/** Server-side: make sure the owning pawn's ASC actually owns this ability. */
	UFUNCTION(Server, Reliable)
	void Server_GrantAbilityFromSlot(const FAeyerjiAbilitySlot& Slot);

	/** Server-authoritative ability rank-up request. */
	UFUNCTION(Server, Reliable, BlueprintCallable, Category="Aeyerji|Abilities")
	void Server_RequestAbilityRankUp(FGameplayTag AbilityTag);

	/** Pushes an authoritative profile snapshot to the owning client or local save manager. */
	bool CommitProfileSaveToOwningClient(UAeyerjiSaveGame* SaveData, bool bBumpRevision);

	/** Commits already prepared profile data as a named checkpoint. */
	bool CommitPreparedCheckpointProfile(UAeyerjiSaveGame* SaveData, EAeyerjiSaveCheckpointReason Reason, bool bBumpRevision);

	/** Captures and persists the current authoritative runtime profile at an intentional checkpoint. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|SaveGame")
	bool CommitCheckpointProfile(EAeyerjiSaveCheckpointReason Reason);

	/** Captures from a specific pawn source, used when death/shutdown invalidates PlayerState->GetPawn(). */
	bool CommitCheckpointProfileFromPawn(EAeyerjiSaveCheckpointReason Reason, const APawn* SourcePawn, bool bBumpRevision = true);

	/** Applies a save-loaded bar without user-edit cooldown checks or implicit persistence. */
	void ApplyLoadedActionBar(const TArray<FAeyerjiAbilitySlot>& LoadedBar);

	/** Client-callable helper to request a specific save slot name (per player). */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|SaveGame")
	void RequestSetSaveSlotOverride(const FString& NewSlot);

	UFUNCTION(Server, Reliable)
	void Server_SetSaveSlotOverride(const FString& NewSlot);

	UFUNCTION(BlueprintPure, Category = "Aeyerji|SaveGame")
	const FString& GetSaveSlotOverride() const { return SaveSlotOverride; }

	/** Accessor for the loot stats component. */
	UFUNCTION(BlueprintPure, Category = "Aeyerji|Stats")
	UPlayerStatsTrackingComponent* GetPlayerStatsTrackingComponent() const { return PlayerStatsTracking; }

	/* ---------- Passives ---------- */
	/** Passive options the character can choose from (IDs used in save/replication). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Passives")
	TArray<FName> PassiveOptions;

	/** Currently selected passive ID (must be in PassiveOptions). */
	UPROPERTY(ReplicatedUsing=OnRep_SelectedPassive, SaveGame, BlueprintReadOnly, Category="Aeyerji|Passives")
	FName SelectedPassiveId;

	UPROPERTY(ReplicatedUsing=OnRep_AbilityProgression, SaveGame, BlueprintReadOnly, Category="Aeyerji|Abilities")
	TArray<FAeyerjiAbilityProgressEntry> AbilityProgressEntries;

	UPROPERTY(ReplicatedUsing=OnRep_AbilityProgression, SaveGame, BlueprintReadOnly, Category="Aeyerji|Abilities")
	int32 UnspentAbilityPoints = 0;

	UPROPERTY(ReplicatedUsing=OnRep_AbilityProgression, SaveGame, BlueprintReadOnly, Category="Aeyerji|Abilities")
	int32 TotalAbilityPointSpends = 0;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPassiveChanged, FName, PassiveId);

	/** Blueprint-assignable notification when the selected passive changes. */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|Passives")
	FOnPassiveChanged OnPassiveChanged;

	UFUNCTION(Server, Reliable, BlueprintCallable, Category="Aeyerji|Passives")
	void Server_SelectPassive(FName PassiveId);

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Passives")
	void SetPassiveLocal(FName PassiveId);

	UFUNCTION(BlueprintPure, Category="Aeyerji|Passives")
	FName GetSelectedPassiveId() const { return SelectedPassiveId; }

	/* ---------- Run Flow ---------- */
	/** Client-callable: requests the server to transition PreRun -> InRun and start the level run. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Run")
	void RequestStartRun();

	/** Server RPC for RequestStartRun(). */
	UFUNCTION(Server, Reliable)
	void Server_RequestStartRun();

	/** Client-callable: requests the server to transition to RunComplete and snapshot results (useful for a "Quit Run" button). */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Run")
	void RequestEndRun();

	/** Server RPC for RequestEndRun(). */
	UFUNCTION(Server, Reliable)
	void Server_RequestEndRun();

	/** Client-callable: requests the server to transition RunComplete -> ReturnToMenu. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Run")
	void RequestReturnToMenu();

	/** Client-callable: requests the server to restart the same gameplay zone after RunComplete. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Run")
	void RequestRetryRun();

	/** Leader-only: selects the tier earned by this clear and retries after RunComplete. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Rift")
	void RequestRetryEarnedRiftTier();

	/** Requests a pre-run Greater Rift tier change; only the elected run leader is authorized. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Rift")
	void RequestSelectRiftTier(int32 RequestedTier);

	/** Server RPC for RequestReturnToMenu(). */
	UFUNCTION(Server, Reliable)
	void Server_RequestReturnToMenu();

	/** Server RPC for RequestRetryRun(). */
	UFUNCTION(Server, Reliable)
	void Server_RequestRetryRun();

	/** Server validates the leader and common unlock cap before retrying the earned tier. */
	UFUNCTION(Server, Reliable)
	void Server_RequestRetryEarnedRiftTier();

	/** Server validates leader authority, loaded participant profiles, the common unlock cap, and tier data. */
	UFUNCTION(Server, Reliable)
	void Server_RequestSelectRiftTier(int32 RequestedTier);

	/** Returns a stable rejection enum to the owning client for localized Blueprint presentation. */
	UFUNCTION(Client, Reliable)
	void Client_RiftTierSelectionRejected(EAeyerjiRiftTierSelectionFailure Reason, int32 RequestedTier);

	/* ---------- Frontend party staging ---------- */
	/** Serializes one resolved local profile through bounded owned-PlayerState RPC chunks. */
	bool SubmitFrontendProfile(const FAeyerjiSaveTransportHeader& Header, const TArray<uint8>& Bytes);

	/** Requests a manual ready-state change; the server still gates this on a verified profile. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Frontend|Lobby")
	void RequestFrontendReady(bool bReady);

	/** Leader-only shared activity request. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Frontend|Lobby")
	void RequestFrontendActivity(EAeyerjiRiftActivityType ActivityType);

	/** Leader-only shared Excursion tier request. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Frontend|Lobby")
	void RequestFrontendTier(int32 Tier);

	/** Leader-only launch request validated against the complete authoritative party snapshot. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Frontend|Lobby")
	void RequestFrontendLaunch();

	UFUNCTION(BlueprintPure, Category="Aeyerji|Frontend|Lobby")
	bool IsFrontendProfileVerified() const { return FrontendProfileState == EAeyerjiLobbyProfileState::Verified; }

	UFUNCTION(BlueprintPure, Category="Aeyerji|Frontend|Lobby")
	EAeyerjiLobbyProfileState GetFrontendProfileState() const { return FrontendProfileState; }

	UFUNCTION(BlueprintPure, Category="Aeyerji|Frontend|Lobby")
	int32 GetFrontendCharacterLevel() const { return FrontendCharacterLevel; }

	UFUNCTION(BlueprintPure, Category="Aeyerji|Frontend|Lobby")
	int32 GetFrontendHighestTier() const { return FrontendHighestUnlockedTier; }

	UFUNCTION(BlueprintPure, Category="Aeyerji|Frontend|Lobby")
	int64 GetFrontendProfileRevision() const { return FrontendProfileRevision; }

	UFUNCTION(BlueprintPure, Category="Aeyerji|Frontend|Lobby")
	bool IsFrontendReady() const { return bFrontendReady; }

	/** Server-owned reset used when shared lobby inputs or roster membership changes. */
	void SetFrontendReadyFromServer(bool bReady);

	/** Owning-client native failure channel consumed by the frontend read model. */
	FAeyerjiFrontendRequestRejectedNative OnFrontendRequestRejectedNative;

	UFUNCTION(Server, Reliable)
	void Server_BeginFrontendProfileSubmission(const FAeyerjiSaveTransportHeader& Header, int32 TotalBytes, int32 ChunkSize);

	UFUNCTION(Server, Reliable)
	void Server_SendFrontendProfileChunk(int32 ChunkIndex, const TArray<uint8>& ChunkBytes);

	UFUNCTION(Server, Reliable)
	void Server_FinalizeFrontendProfileSubmission();

	UFUNCTION(Server, Reliable)
	void Server_RequestFrontendReady(bool bReady);

	UFUNCTION(Server, Reliable)
	void Server_RequestFrontendActivity(EAeyerjiRiftActivityType ActivityType);

	UFUNCTION(Server, Reliable)
	void Server_RequestFrontendTier(int32 Tier);

	UFUNCTION(Server, Reliable)
	void Server_RequestFrontendLaunch();

	UFUNCTION(Client, Reliable)
	void Client_FrontendRequestRejected(EAeyerjiFrontendFailure Failure);

protected:

	/* ---------- Replication ---------- */
	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Automatic callback generated by the ReplicatedUsing tag. */
	UFUNCTION()
	void OnRep_ActionBar();

	/* ---------- Saved & replicated data ---------- */

	UFUNCTION()
	void OnRep_SaveSlotOverride();

	UPROPERTY(ReplicatedUsing = OnRep_SaveSlotOverride)
	FString SaveSlotOverride;

	UFUNCTION()
	void OnRep_ProfileLoadState();

	UPROPERTY(ReplicatedUsing = OnRep_ProfileLoadState)
	EAeyerjiProfileLoadState ProfileLoadState = EAeyerjiProfileLoadState::Pending;

	UFUNCTION()
	void OnRep_SelectedPassive();

	UFUNCTION()
	void OnRep_AbilityProgression();

	UFUNCTION()
	void OnRep_Gold(int64 OldGold);

	UFUNCTION()
	void OnRep_RiftTierProgression();

	UFUNCTION()
	void OnRep_PersonalRunResults();

	/** Profile-persistent gold replicated for local HUD and shop/repair validation feedback. */
	UPROPERTY(ReplicatedUsing=OnRep_Gold, SaveGame, BlueprintReadOnly, Category="Aeyerji|Currency", meta=(AllowPrivateAccess="true"))
	int64 Gold = 0;

	/** Save-backed unlock replicated to all party members for common-cap UI. */
	UPROPERTY(ReplicatedUsing=OnRep_RiftTierProgression, SaveGame, BlueprintReadOnly, Category="Aeyerji|Rift", meta=(AllowPrivateAccess="true"))
	int32 HighestUnlockedRiftTier = 1;

	/** Save-backed last selection replicated to all party members for shared pre-run UI. */
	UPROPERTY(ReplicatedUsing=OnRep_RiftTierProgression, SaveGame, BlueprintReadOnly, Category="Aeyerji|Rift", meta=(AllowPrivateAccess="true"))
	int32 LastSelectedRiftTier = 1;

	/** Personal result is owner-only replicated so private reward information is not exposed party-wide. */
	UPROPERTY(ReplicatedUsing=OnRep_PersonalRunResults, VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Rift", meta=(AllowPrivateAccess="true"))
	FAeyerjiRunResults PersonalRunResults;

	/** Menu-safe profile verification state derived only from a validated server profile cache entry. */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Lobby", meta=(AllowPrivateAccess="true"))
	EAeyerjiLobbyProfileState FrontendProfileState = EAeyerjiLobbyProfileState::NotSubmitted;

	/** Character level projected from the verified profile, never trusted from a client scalar. */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Lobby", meta=(AllowPrivateAccess="true"))
	int32 FrontendCharacterLevel = 1;

	/** Excursion unlock projected from the verified profile. */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Lobby", meta=(AllowPrivateAccess="true"))
	int32 FrontendHighestUnlockedTier = 1;

	/** Revision of the verified profile currently represented in the lobby. */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Lobby", meta=(AllowPrivateAccess="true"))
	int64 FrontendProfileRevision = 0;

	/** Manual ready flag; roster/shared-selection/profile changes clear it on authority. */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Frontend|Lobby", meta=(AllowPrivateAccess="true"))
	bool bFrontendReady = false;

private:
	/** Applies the authoritative action-bar update and optionally enforces user-edit cooldown rules. */
	void ApplyActionBarUpdate(const TArray<FAeyerjiAbilitySlot>& NewBar, bool bValidateCooldowns);
	void GrantAbilityFromSlotInternal(const FAeyerjiAbilitySlot& AbilitySlot);
	void MirrorProgressionRanksIntoActionBar();
	int32 GetProgressionRankForSlot(const FAeyerjiAbilitySlot& AbilitySlot) const;
	void SyncGrantedAbilityRank(FGameplayTag AbilityTag);
	void SyncProfileAbilityProgressionCache(const TCHAR* Reason) const;
	void SyncProfileGoldCache(const TCHAR* Reason) const;
	int32 GetCurrentPlayerLevel() const;
	FAeyerjiAbilityProgressEntry* FindMutableAbilityProgressEntry(FGameplayTag AbilityTag);
	const FAeyerjiAbilityProgressEntry* FindAbilityProgressEntry(FGameplayTag AbilityTag) const;
	void ApplySaveSlotOverride(const FString& NewSlot);
	void ApplySelectedPassive(FName PassiveId, bool bBroadcast);
	void ApplyGoldValue(int64 NewGold, FName Reason, bool bBroadcast);
	void SendAuthoritativeProfileCommitToOwningClient(const FAeyerjiSaveTransportHeader& Header, const TArray<uint8>& Bytes);
	void ResetPendingAuthoritativeProfileCommit();

	static constexpr int32 ProfileCommitTransportChunkSize = 48 * 1024;
	static constexpr int32 LegacyProfileCommitRpcWarningBytes = 60 * 1024;

	FAeyerjiSaveTransportHeader PendingAuthoritativeProfileCommitHeader;
	TArray<uint8> PendingAuthoritativeProfileCommitBytes;
	TSet<int32> PendingAuthoritativeProfileCommitReceivedChunks;
	int32 PendingAuthoritativeProfileCommitExpectedBytes = 0;
	int32 PendingAuthoritativeProfileCommitExpectedChunks = 0;
	int32 PendingAuthoritativeProfileCommitChunkSize = 0;
	bool bAuthoritativeProfileCommitTransferActive = false;

	/** Server-side bounded preflight transport. It is distinct from the server-to-client commit transport above. */
	FAeyerjiSaveTransportHeader PendingFrontendProfileHeader;
	TArray<uint8> PendingFrontendProfileBytes;
	TSet<int32> PendingFrontendProfileReceivedChunks;
	int32 PendingFrontendProfileExpectedBytes = 0;
	int32 PendingFrontendProfileExpectedChunks = 0;
	int32 PendingFrontendProfileChunkSize = 0;
	bool bFrontendProfileTransferActive = false;

	void ResetPendingFrontendProfileSubmission();
	void RejectFrontendProfileSubmission(const TCHAR* Reason);
	static constexpr int32 FrontendProfileTransportChunkSize = 48 * 1024;
	static constexpr int32 FrontendProfileMaximumBytes = 4 * 1024 * 1024;
};
