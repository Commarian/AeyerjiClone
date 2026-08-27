#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Systems/AeyerjiWorldStateTypes.h"
#include "AeyerjiWorldStateSubsystem.generated.h"

class AAeyerjiGameState;

DECLARE_MULTICAST_DELEGATE_OneParam(FAeyerjiWorldStateChangedNativeSignature, const FAeyerjiWorldStateEntry&);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAeyerjiWorldStateChangedSignature, const FAeyerjiWorldStateEntry&, Entry);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAeyerjiWorldStateRemovedSignature, const FAeyerjiWorldStateKey&, Key);

/**
 * Server-authoritative registry for shared world facts, event outcomes, and registered runtime objects.
 */
UCLASS(BlueprintType, Config=Game, DefaultConfig)
class AEYERJI_API UAeyerjiWorldStateSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Initializes the shared world-state cache and loads persistent state on authority. */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Flushes pending persistent state and clears runtime-only object references. */
	virtual void Deinitialize() override;

	/** Resolves the subsystem from any world-context object. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|World State", meta=(WorldContext="WorldContextObject"))
	static UAeyerjiWorldStateSubsystem* Get(const UObject* WorldContextObject);

	/** Builds a world-state key from a tag and optional instance id. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|World State")
	static FAeyerjiWorldStateKey MakeWorldStateKey(FGameplayTag StateTag, FName InstanceId = NAME_None, FName OwnerId = NAME_None);

	/** Sets or replaces an entry. Writes are accepted only on authority. */
	bool SetValue(const FAeyerjiWorldStateKey& Key, const FAeyerjiWorldStateValue& Value, EAeyerjiWorldStatePersistence Persistence, EAeyerjiWorldStateReplication Replication, EAeyerjiWorldStateScope Scope = EAeyerjiWorldStateScope::Global);

	/** Finds an entry by key. */
	bool GetEntry(const FAeyerjiWorldStateKey& Key, FAeyerjiWorldStateEntry& OutEntry) const;

	/** Finds only the value for a key. */
	bool GetValue(const FAeyerjiWorldStateKey& Key, FAeyerjiWorldStateValue& OutValue) const;

	/** Clears an entry by key. Writes are accepted only on authority. */
	bool ClearValue(const FAeyerjiWorldStateKey& Key);

	/** Clears all entries in one scope. Useful for ending or restarting a run. */
	bool ClearEntriesByScope(EAeyerjiWorldStateScope Scope);

	/** Clears all character-scoped entries for one owner id. */
	bool ClearEntriesForOwner(FName OwnerId);

	/** Records an event as having happened. */
	bool MarkEventHappened(const FGameplayTag& EventTag, FName InstanceId, EAeyerjiWorldStatePersistence Persistence, EAeyerjiWorldStateReplication Replication, EAeyerjiWorldStateScope Scope = EAeyerjiWorldStateScope::Global, FName OwnerId = NAME_None);

	/** Returns true when an event key exists and is set to true. */
	bool HasEventHappened(const FGameplayTag& EventTag, FName InstanceId = NAME_None, FName OwnerId = NAME_None) const;

	/** Increments an integer-like entry and creates it when missing. */
	bool IncrementInt(const FAeyerjiWorldStateKey& Key, int32 Delta, int32& OutNewValue, EAeyerjiWorldStatePersistence Persistence, EAeyerjiWorldStateReplication Replication, EAeyerjiWorldStateScope Scope = EAeyerjiWorldStateScope::Global);

	/** Registers a live object against a key. Persistent copies store only the object's soft path. */
	bool RegisterLiveObject(const FAeyerjiWorldStateKey& Key, UObject* Object, EAeyerjiWorldStatePersistence Persistence, EAeyerjiWorldStateReplication Replication, EAeyerjiWorldStateScope Scope = EAeyerjiWorldStateScope::Global);

	/** Removes a transient live object pointer without deleting persistent data. */
	bool UnregisterLiveObject(const FAeyerjiWorldStateKey& Key, const UObject* ExpectedObject = nullptr);

	/** Resolves a live object pointer for a key when available. */
	UObject* GetRegisteredObject(const FAeyerjiWorldStateKey& Key) const;

	/** Loads shared persistent entries from the save manager. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|World State")
	bool LoadPersistentState();

	/** Writes persistent entries to the shared world-state save artifact. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|World State")
	bool SavePersistentState();

	/** Starts a new run and clears any stale run-scoped facts from the previous run. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|World State|Run")
	bool BeginRun(FName RunId = NAME_None);

	/** Ends the active run and clears run-scoped facts. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|World State|Run")
	bool EndRun();

	/** Clears all run-scoped facts without changing the active run id. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|World State|Run")
	bool ClearRunState();

	/** Returns the current server-authored run id for facts that need a run label in debug UI. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|World State|Run")
	FName GetActiveRunId() const { return ActiveRunId; }

	/** Returns a compact text summary of current run/global/character facts for debug UI and console logging. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|World State|Debug")
	FString GetWorldStateDebugSummary() const;

	/** Returns compact debug strings for all current run-scoped facts. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|World State|Debug")
	void GetRunFactDebugStrings(TArray<FString>& OutFacts) const;

	/** Returns compact debug strings for all currently loaded persistent facts. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|World State|Debug")
	void GetPersistentFactDebugStrings(TArray<FString>& OutFacts) const;

	/** Copies one run-scoped fact into persistent character scope for the supplied owner id. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|World State|Persistence")
	bool PromoteRunFactToPersistentCharacterFact(FGameplayTag StateTag, FName TargetOwnerId, FName InstanceId = NAME_None, FName SourceOwnerId = NAME_None);

	/** Copies one run-scoped fact into persistent global scope. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|World State|Persistence")
	bool PromoteRunFactToPersistentGlobalFact(FGameplayTag StateTag, FName InstanceId = NAME_None, FName SourceOwnerId = NAME_None);

	/** Copies persistent character-scoped entries for an owner id into a profile save payload. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|World State|Character")
	bool ExportPersistentCharacterState(FName OwnerId, TArray<FAeyerjiWorldStateEntry>& OutEntries) const;

	/** Imports persistent character-scoped entries from a profile save payload for one owner id. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|World State|Character")
	bool ImportPersistentCharacterState(FName OwnerId, const TArray<FAeyerjiWorldStateEntry>& InEntries, bool bReplaceExisting = true);

	/** Pushes all public entries into a GameState replication bridge. */
	void PublishReplicatedEntriesToGameState(AAeyerjiGameState* GameState) const;

	/** Applies a replicated public entry on clients. */
	void ApplyReplicatedEntry(const FAeyerjiWorldStateEntry& Entry);

	/** Removes a replicated public entry on clients. */
	void RemoveReplicatedEntry(const FAeyerjiWorldStateKey& Key);

	/** Blueprint helper for setting boolean values. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|World State", meta=(WorldContext="WorldContextObject"))
	static bool SetWorldStateBool(UObject* WorldContextObject, FGameplayTag StateTag, bool bValue, FName InstanceId = NAME_None, FName OwnerId = NAME_None, EAeyerjiWorldStatePersistence Persistence = EAeyerjiWorldStatePersistence::Persistent, EAeyerjiWorldStateReplication Replication = EAeyerjiWorldStateReplication::ServerOnly, EAeyerjiWorldStateScope Scope = EAeyerjiWorldStateScope::Global);

	/** Blueprint helper for reading boolean values. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|World State", meta=(WorldContext="WorldContextObject"))
	static bool GetWorldStateBool(const UObject* WorldContextObject, FGameplayTag StateTag, bool& bOutValue, FName InstanceId = NAME_None, FName OwnerId = NAME_None);

	/** Blueprint helper for setting integer values. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|World State", meta=(WorldContext="WorldContextObject"))
	static bool SetWorldStateInt(UObject* WorldContextObject, FGameplayTag StateTag, int32 Value, FName InstanceId = NAME_None, FName OwnerId = NAME_None, EAeyerjiWorldStatePersistence Persistence = EAeyerjiWorldStatePersistence::Persistent, EAeyerjiWorldStateReplication Replication = EAeyerjiWorldStateReplication::ServerOnly, EAeyerjiWorldStateScope Scope = EAeyerjiWorldStateScope::Global);

	/** Blueprint helper for reading integer values. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|World State", meta=(WorldContext="WorldContextObject"))
	static bool GetWorldStateInt(const UObject* WorldContextObject, FGameplayTag StateTag, int32& OutValue, FName InstanceId = NAME_None, FName OwnerId = NAME_None);

	/** Blueprint helper for setting float values. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|World State", meta=(WorldContext="WorldContextObject"))
	static bool SetWorldStateFloat(UObject* WorldContextObject, FGameplayTag StateTag, float Value, FName InstanceId = NAME_None, FName OwnerId = NAME_None, EAeyerjiWorldStatePersistence Persistence = EAeyerjiWorldStatePersistence::Persistent, EAeyerjiWorldStateReplication Replication = EAeyerjiWorldStateReplication::ServerOnly, EAeyerjiWorldStateScope Scope = EAeyerjiWorldStateScope::Global);

	/** Blueprint helper for reading float values. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|World State", meta=(WorldContext="WorldContextObject"))
	static bool GetWorldStateFloat(const UObject* WorldContextObject, FGameplayTag StateTag, float& OutValue, FName InstanceId = NAME_None, FName OwnerId = NAME_None);

	/** Blueprint helper for setting name values. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|World State", meta=(WorldContext="WorldContextObject"))
	static bool SetWorldStateName(UObject* WorldContextObject, FGameplayTag StateTag, FName Value, FName InstanceId = NAME_None, FName OwnerId = NAME_None, EAeyerjiWorldStatePersistence Persistence = EAeyerjiWorldStatePersistence::Persistent, EAeyerjiWorldStateReplication Replication = EAeyerjiWorldStateReplication::ServerOnly, EAeyerjiWorldStateScope Scope = EAeyerjiWorldStateScope::Global);

	/** Blueprint helper for reading name values. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|World State", meta=(WorldContext="WorldContextObject"))
	static bool GetWorldStateName(const UObject* WorldContextObject, FGameplayTag StateTag, FName& OutValue, FName InstanceId = NAME_None, FName OwnerId = NAME_None);

	/** Blueprint helper for setting string values. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|World State", meta=(WorldContext="WorldContextObject"))
	static bool SetWorldStateString(UObject* WorldContextObject, FGameplayTag StateTag, const FString& Value, FName InstanceId = NAME_None, FName OwnerId = NAME_None, EAeyerjiWorldStatePersistence Persistence = EAeyerjiWorldStatePersistence::Persistent, EAeyerjiWorldStateReplication Replication = EAeyerjiWorldStateReplication::ServerOnly, EAeyerjiWorldStateScope Scope = EAeyerjiWorldStateScope::Global);

	/** Blueprint helper for reading string values. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|World State", meta=(WorldContext="WorldContextObject"))
	static bool GetWorldStateString(const UObject* WorldContextObject, FGameplayTag StateTag, FString& OutValue, FName InstanceId = NAME_None, FName OwnerId = NAME_None);

	/** Blueprint helper for setting gameplay tag values. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|World State", meta=(WorldContext="WorldContextObject"))
	static bool SetWorldStateTag(UObject* WorldContextObject, FGameplayTag StateTag, FGameplayTag Value, FName InstanceId = NAME_None, FName OwnerId = NAME_None, EAeyerjiWorldStatePersistence Persistence = EAeyerjiWorldStatePersistence::Persistent, EAeyerjiWorldStateReplication Replication = EAeyerjiWorldStateReplication::ServerOnly, EAeyerjiWorldStateScope Scope = EAeyerjiWorldStateScope::Global);

	/** Blueprint helper for reading gameplay tag values. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|World State", meta=(WorldContext="WorldContextObject"))
	static bool GetWorldStateTag(const UObject* WorldContextObject, FGameplayTag StateTag, FGameplayTag& OutValue, FName InstanceId = NAME_None, FName OwnerId = NAME_None);

	/** Blueprint helper for setting soft object path values. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|World State", meta=(WorldContext="WorldContextObject"))
	static bool SetWorldStateSoftObjectPath(UObject* WorldContextObject, FGameplayTag StateTag, FSoftObjectPath Value, FName InstanceId = NAME_None, FName OwnerId = NAME_None, EAeyerjiWorldStatePersistence Persistence = EAeyerjiWorldStatePersistence::Persistent, EAeyerjiWorldStateReplication Replication = EAeyerjiWorldStateReplication::ServerOnly, EAeyerjiWorldStateScope Scope = EAeyerjiWorldStateScope::Global);

	/** Blueprint helper for reading soft object path values. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|World State", meta=(WorldContext="WorldContextObject"))
	static bool GetWorldStateSoftObjectPath(const UObject* WorldContextObject, FGameplayTag StateTag, FSoftObjectPath& OutValue, FName InstanceId = NAME_None, FName OwnerId = NAME_None);

	/** Blueprint helper for marking an event as happened. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|World State", meta=(WorldContext="WorldContextObject"))
	static bool MarkWorldEventHappened(UObject* WorldContextObject, FGameplayTag EventTag, FName InstanceId = NAME_None, FName OwnerId = NAME_None, EAeyerjiWorldStatePersistence Persistence = EAeyerjiWorldStatePersistence::Persistent, EAeyerjiWorldStateReplication Replication = EAeyerjiWorldStateReplication::ServerOnly, EAeyerjiWorldStateScope Scope = EAeyerjiWorldStateScope::Global);

	/** Blueprint helper for testing whether an event happened. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|World State", meta=(WorldContext="WorldContextObject"))
	static bool HasWorldEventHappened(const UObject* WorldContextObject, FGameplayTag EventTag, FName InstanceId = NAME_None, FName OwnerId = NAME_None);

	/** Blueprint helper for incrementing integer entries. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|World State", meta=(WorldContext="WorldContextObject"))
	static bool IncrementWorldStateInt(UObject* WorldContextObject, FGameplayTag StateTag, int32 Delta, int32& OutNewValue, FName InstanceId = NAME_None, FName OwnerId = NAME_None, EAeyerjiWorldStatePersistence Persistence = EAeyerjiWorldStatePersistence::Persistent, EAeyerjiWorldStateReplication Replication = EAeyerjiWorldStateReplication::ServerOnly, EAeyerjiWorldStateScope Scope = EAeyerjiWorldStateScope::Global);

	/** Blueprint helper for clearing entries. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|World State", meta=(WorldContext="WorldContextObject"))
	static bool ClearWorldState(UObject* WorldContextObject, FGameplayTag StateTag, FName InstanceId = NAME_None, FName OwnerId = NAME_None);

	/** Blueprint helper for registering a live object. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|World State", meta=(WorldContext="WorldContextObject"))
	static bool RegisterWorldStateObject(UObject* WorldContextObject, FGameplayTag StateTag, UObject* Object, FName InstanceId = NAME_None, FName OwnerId = NAME_None, EAeyerjiWorldStatePersistence Persistence = EAeyerjiWorldStatePersistence::RuntimeOnly, EAeyerjiWorldStateReplication Replication = EAeyerjiWorldStateReplication::ServerOnly, EAeyerjiWorldStateScope Scope = EAeyerjiWorldStateScope::Global);

	/** Blueprint helper for reading a live object. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|World State", meta=(WorldContext="WorldContextObject"))
	static UObject* GetWorldStateObject(const UObject* WorldContextObject, FGameplayTag StateTag, FName InstanceId = NAME_None, FName OwnerId = NAME_None);

public:
	/** Blueprint change signal fired whenever an entry changes on this instance. */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|World State")
	FAeyerjiWorldStateChangedSignature OnWorldStateChanged;

	/** Blueprint removal signal fired whenever an entry is removed on this instance. */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|World State")
	FAeyerjiWorldStateRemovedSignature OnWorldStateRemoved;

	/** Native change signal for C++ systems. */
	FAeyerjiWorldStateChangedNativeSignature OnWorldStateChangedNative;

protected:
	/** Loads the shared global-persistent lane during authoritative subsystem initialization. */
	UPROPERTY(EditDefaultsOnly, Config, Category="Aeyerji|World State|Persistence")
	bool bAutoLoadPersistentState = true;

	/** Debounces shared global-persistent writes through the save manager after mutations. */
	UPROPERTY(EditDefaultsOnly, Config, Category="Aeyerji|World State|Persistence")
	bool bAutoSavePersistentState = true;

	/** Quiet period before a dirty shared-world snapshot is committed; zero saves immediately. */
	UPROPERTY(EditDefaultsOnly, Config, Category="Aeyerji|World State|Persistence", meta=(ClampMin="0.0"))
	float AutoSaveDelaySeconds = 2.0f;

private:
	/** Returns true when this subsystem can mutate authoritative state. */
	bool HasWriteAuthority() const;

	/** Loads persistent state once before authority-side reads or writes. */
	void EnsurePersistentStateLoaded();

	/** Emits delegates for a changed entry. */
	void BroadcastEntryChanged(const FAeyerjiWorldStateEntry& Entry);

	/** Emits delegates for a removed entry. */
	void BroadcastEntryRemoved(const FAeyerjiWorldStateKey& Key);

	/** Schedules a debounced save after a persistent entry changes. */
	void ScheduleAutoSave();

	/** Timer callback that commits debounced persistent state. */
	void HandleAutoSaveTimer();

	/** Pushes one changed entry into GameState replication when needed. */
	void PublishEntryForReplication(const FAeyerjiWorldStateEntry& Entry);

	/** Removes one key from GameState replication when needed. */
	void RemoveEntryFromReplication(const FAeyerjiWorldStateKey& Key);

	/** Returns true when an entry belongs in the shared world-state save artifact. */
	bool ShouldPersistToSharedWorldSave(const FAeyerjiWorldStateEntry& Entry) const;

private:
	TMap<FAeyerjiWorldStateKey, FAeyerjiWorldStateEntry> Entries;

	FTimerHandle AutoSaveTimerHandle;

	FName ActiveRunId = NAME_None;

	bool bPersistentStateLoaded = false;
	bool bPersistentStateDirty = false;
};
