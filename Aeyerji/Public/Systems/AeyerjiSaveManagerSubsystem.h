#pragma once

#include "CoreMinimal.h"
#include "OnlineSubsystemTypes.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Systems/AeyerjiSaveTypes.h"
#include "AeyerjiSaveManagerSubsystem.generated.h"

class APlayerState;
class IOnlineSubsystem;
class IOnlineUserCloud;
class UAeyerjiSaveGame;
class UAeyerjiStreamingSaveGame;
class UAeyerjiWorldStateSaveGame;

DECLARE_DELEGATE_ThreeParams(FAeyerjiOnProfileResolved, bool, bool, UAeyerjiSaveGame*);
DECLARE_MULTICAST_DELEGATE_TwoParams(FAeyerjiProfileChangedNative, const FString&, int64);

/**
 * Central save entrypoint for per-player profile and streaming save data.
 */
UCLASS()
class AEYERJI_API UAeyerjiSaveManagerSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Resolves the subsystem from any world-context object. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Save", meta=(WorldContext="WorldContextObject"))
	static UAeyerjiSaveManagerSubsystem* Get(const UObject* WorldContextObject);

	/** Defined out-of-line so the pending resolve type can stay forward-declared in the header. */
	virtual ~UAeyerjiSaveManagerSubsystem() override;

	/** Initializes in-memory caches for the owning game instance. */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Clears async cloud delegates and local caches during shutdown. */
	virtual void Deinitialize() override;

	/** Resolves the current local profile, reconciling local and Steam UserCloud when available. */
	void ResolveProfileForLocalOwner(const FAeyerjiOnProfileResolved& Callback, const APlayerState* PreferredPlayerState = nullptr);

	/** Native event emitted after local profile resolution or an authoritative client commit. */
	FAeyerjiProfileChangedNative OnProfileChanged;

	/** Returns the cached/local profile immediately without waiting for cloud reconciliation. */
	bool GetCachedOrLocalProfileForOwner(UAeyerjiSaveGame*& OutSaveData, const APlayerState* PreferredPlayerState = nullptr);

	/** Persists an authoritative profile snapshot received from the server on the owning client. */
	bool CommitResolvedProfileForLocalOwner(const FAeyerjiSaveTransportHeader& Header, const TArray<uint8>& Bytes);

	/** Persists an authoritative profile snapshot using the same local slot resolution as the given player state. */
	bool CommitResolvedProfileForLocalOwner(const FAeyerjiSaveTransportHeader& Header, const TArray<uint8>& Bytes, const APlayerState* PreferredPlayerState);

	/** Loads streaming state from the per-owner local slot, migrating the legacy fixed slot on first use. */
	UAeyerjiStreamingSaveGame* ResolveStreamingStateForOwner(const APlayerState* PreferredPlayerState = nullptr);

	/** Persists streaming state to the per-owner local slot and mirrors it to Steam cloud when available. */
	bool CommitStreamingStateForOwner(UAeyerjiStreamingSaveGame* SaveData, const APlayerState* PreferredPlayerState = nullptr);

	/** Loads the shared world-state artifact from its fixed local slot. */
	UAeyerjiWorldStateSaveGame* ResolveWorldState();

	/** Persists the shared world-state artifact to its fixed local slot. */
	bool CommitWorldState(UAeyerjiWorldStateSaveGame* SaveData);

	/** Updates the cached/local profile difficulty selection without depending on PlayerState readiness. */
	bool MutateCachedProfileDifficulty(float DifficultySlider, int32 WorldTier, const APlayerState* PreferredPlayerState = nullptr);

	/** Returns the server-runtime cached profile for the given player when one has already been approved. */
	bool GetServerCachedProfile(const APlayerState* PlayerState, UAeyerjiSaveGame*& OutSaveData) const;

	/** Creates a fresh profile object for the specified owner key and default level. */
	UAeyerjiSaveGame* CreateDefaultProfile(const FString& OwnerKey, int32 InitialLevel) const;

	/** Deserializes a profile transport payload back into a save object. */
	UAeyerjiSaveGame* DeserializeProfileFromTransport(const FAeyerjiSaveTransportHeader& Header, const TArray<uint8>& Bytes) const;

	/** Serializes a profile save object into transport metadata and payload bytes. */
	bool BuildTransportFromProfile(const UAeyerjiSaveGame* SaveData, FAeyerjiSaveTransportHeader& OutHeader, TArray<uint8>& OutBytes) const;

	/** Stores a mutable authoritative profile in the server cache, optionally bumping revision before serialization. */
	bool PrepareProfileForServerCommit(APlayerState* PlayerState, UAeyerjiSaveGame* SaveData, bool bBumpRevision, FAeyerjiSaveTransportHeader& OutHeader, TArray<uint8>& OutBytes);

	/** Returns true when a save object already carries valid manager-era metadata for profile reconciliation. */
	bool IsManagerEraProfile(const UAeyerjiSaveGame* SaveData) const;

	/** Resolves the stable per-owner key from PlayerState, local platform user, or development fallback. */
	FString ResolveOwnerKey(const APlayerState* PreferredPlayerState = nullptr) const;

	/** Returns the profile slot name for the resolved owner, preserving legacy slot naming. */
	FString MakeProfileSlotNameForOwner(const FString& OwnerKey, const APlayerState* PreferredPlayerState = nullptr) const;

	/** Returns the per-owner streaming slot name. */
	FString MakeStreamingSlotNameForOwner(const FString& OwnerKey) const;

	/** Returns the fixed shared-world state slot name. */
	FString MakeWorldStateSlotName() const;

private:
	struct FPendingProfileResolve;

	/** Loads a profile object from the given local slot. */
	UAeyerjiSaveGame* LoadProfileFromLocalSlot(const FString& SlotName, bool& bOutLoadedExisting) const;

	/** Loads a streaming object from the given local slot. */
	UAeyerjiStreamingSaveGame* LoadStreamingFromLocalSlot(const FString& SlotName, bool& bOutLoadedExisting) const;

	/** Loads a shared world-state object from the given local slot. */
	UAeyerjiWorldStateSaveGame* LoadWorldStateFromLocalSlot(const FString& SlotName, bool& bOutLoadedExisting) const;

	/** Writes a profile save object to the specified local slot. */
	bool SaveProfileToLocalSlot(UAeyerjiSaveGame* SaveData, const FString& SlotName);

	/** Writes a streaming save object to the specified local slot. */
	bool SaveStreamingToLocalSlot(UAeyerjiStreamingSaveGame* SaveData, const FString& SlotName);

	/** Writes a shared world-state save object to the specified local slot. */
	bool SaveWorldStateToLocalSlot(UAeyerjiWorldStateSaveGame* SaveData, const FString& SlotName);

	/** Returns true when Steam UserCloud is currently available for the first local player. */
	bool ResolveSteamCloudContext(FUniqueNetIdRepl& OutUserId, TSharedPtr<IOnlineUserCloud, ESPMode::ThreadSafe>& OutUserCloud) const;

	/** Returns the UserCloud filename for a save artifact. */
	static FString MakeCloudFilename(EAeyerjiSaveArtifactKind ArtifactKind, const FString& OwnerKey);

	/** Returns true when the object metadata matches a manager-era profile payload. */
	bool IsManagerEraProfileForOwner(const UAeyerjiSaveGame* SaveData, const FString& OwnerKey) const;

	/** Returns true when the object metadata matches a manager-era streaming payload. */
	bool IsManagerEraStreamingForOwner(const UAeyerjiStreamingSaveGame* SaveData, const FString& OwnerKey) const;

	/** Returns true when the object metadata matches a manager-era shared world-state payload. */
	bool IsManagerEraWorldState(const UAeyerjiWorldStateSaveGame* SaveData) const;

	/** Stamps metadata onto a profile save before it is committed. */
	void StampProfileMetadata(UAeyerjiSaveGame* SaveData, const FString& OwnerKey, bool bBumpRevision) const;

	/** Stamps metadata onto a streaming save before it is committed. */
	void StampStreamingMetadata(UAeyerjiStreamingSaveGame* SaveData, const FString& OwnerKey, bool bBumpRevision) const;

	/** Stamps metadata onto a shared world-state save before it is committed. */
	void StampWorldStateMetadata(UAeyerjiWorldStateSaveGame* SaveData, bool bBumpRevision) const;

	/** Finalizes a pending profile resolve using the best local/cloud candidate. */
	void FinalizePendingProfileResolve(bool bCloudReadCompleted);

	/** Handles completion of the Steam cloud enumeration step for the active resolve. */
	void HandleEnumerateUserFilesComplete(bool bWasSuccessful, const FUniqueNetId& UserId);

	/** Handles completion of the Steam cloud read step for the active resolve. */
	void HandleReadUserFileComplete(bool bWasSuccessful, const FUniqueNetId& UserId, const FString& FileName);

	/** Handles completion of best-effort Steam cloud writes. */
	void HandleWriteUserFileComplete(bool bWasSuccessful, const FUniqueNetId& UserId, const FString& FileName);

	/** Starts a best-effort profile mirror write to Steam cloud. */
	void MirrorProfileToCloud(UAeyerjiSaveGame* SaveData, const FString& OwnerKey);

	/** Starts a best-effort streaming mirror write to Steam cloud. */
	void MirrorStreamingToCloud(UAeyerjiStreamingSaveGame* SaveData, const FString& OwnerKey);

	/** Clears any pending cloud delegates associated with the active resolve. */
	void ClearPendingResolveDelegates();

private:
	/** Local-owner profile cache keyed by resolved owner key. */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UAeyerjiSaveGame>> LocalProfileCache;

	/** Local-owner streaming cache keyed by resolved owner key. */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UAeyerjiStreamingSaveGame>> LocalStreamingCache;

	/** Shared world-state cache. */
	UPROPERTY(Transient)
	TObjectPtr<UAeyerjiWorldStateSaveGame> WorldStateCache;

	/** Authoritative server-runtime profile cache keyed by resolved owner key. */
	UPROPERTY(Transient)
	TMap<FString, TObjectPtr<UAeyerjiSaveGame>> ServerProfileCache;

	/** Single in-flight profile resolve operation for the local owner. */
	FPendingProfileResolve* PendingProfileResolve = nullptr;

	/** Handle for the generic cloud write-complete delegate. */
	FDelegateHandle CloudWriteDelegateHandle;
};
