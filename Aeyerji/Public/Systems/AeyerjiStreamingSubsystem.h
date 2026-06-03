#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Systems/AeyerjiStreamingManifest.h"
#include "AeyerjiStreamingSubsystem.generated.h"

class UAeyerjiStreamingSaveGame;
class UWorld;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FAeyerjiStreamingRequestStartedSignature, FName, ZoneId, const TArray<FName>&, LevelsToLoad, const TArray<FName>&, LevelsToUnload);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FAeyerjiStreamingStateChangedSignature, const TArray<FName>&, LoadedNow, const TArray<FName>&, UnloadedNow);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAeyerjiZoneReadySignature, FName, ZoneId);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FAeyerjiGameplayMapSelectedSignature, FName, MapId, FName, MapPackageName);

/**
 * Persistent runtime owner for map flow + sublevel streaming state across the entire play session.
 */
UCLASS(BlueprintType, Config=Game, DefaultConfig)
class AEYERJI_API UAeyerjiStreamingSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Loads manifest/save state and initializes runtime caches for this game instance. */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/** Tears down ticker/state when the owning game instance is shutting down. */
	virtual void Deinitialize() override;

	/** Helper to resolve this subsystem from any world context object. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Streaming", meta=(WorldContext="WorldContextObject"))
	static UAeyerjiStreamingSubsystem* GetStreamingSubsystem(const UObject* WorldContextObject);

	/** Returns the active manifest pointer (can be null when no manifest is configured). */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Streaming")
	UAeyerjiStreamingManifest* GetManifest() const { return Manifest; }

	/** Replaces the active manifest at runtime (useful for BP setup during boot). */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Streaming")
	void SetManifest(UAeyerjiStreamingManifest* InManifest);

	/** Enters the target zone and applies load/unload delta against currently loaded sublevels. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Streaming")
	bool EnterZone(FName ZoneId);

	/** Resolves a zone definition from manifest data, with built-in fallback zones when no manifest exists. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Streaming")
	bool GetZoneDefinition(FName ZoneId, FZoneDef& OutZoneDefinition) const;

	/** Enters startup zone using save-first fallback behavior for world bootstrap actors. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Streaming")
	bool EnterStartupZone(FName FallbackZoneId, bool bPreferSavedZone);

	/** Advanced runtime API to push an explicit desired sublevel set. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Streaming")
	void SetDesiredLevels(const TArray<FName>& Desired);

	/** Outputs the currently loaded sublevel package names tracked by this subsystem. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Streaming")
	void GetLoadedLevels(TArray<FName>& OutLoadedLevels) const;

	/** Returns true when the queried sublevel package name is currently loaded. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Streaming")
	bool IsLevelLoaded(FName LevelName) const;

	/** Returns the active zone id for this runtime session. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Streaming")
	FName GetCurrentZoneId() const { return CurrentZoneId; }

	/** Chooses the next gameplay map (random or campaign sequential) and travels to it. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Flow")
	bool StartGameplaySession(bool bCampaignMode);

	/** Restarts the current gameplay map with an optional zone override without advancing map rotation state. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Flow")
	bool RestartCurrentGameplaySession(FName ZoneIdOverride = NAME_None, bool bPreferSeamlessTravel = true);

	/** Returns true when a one-shot startup zone override is queued for the next persistent-world bootstrap. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Flow")
	bool HasPendingStartupZoneOverride() const { return !PendingStartupZoneOverride.IsNone(); }

	/** Returns the queued one-shot startup zone override without consuming it. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Flow")
	FName GetPendingStartupZoneOverride() const { return PendingStartupZoneOverride; }

	/** Consumes the one-shot startup zone override used by retry-style reload flows. */
	bool TakePendingStartupZoneOverride(FName& OutZoneId);

	/** Returns the next gameplay map selection without advancing cursor/traveling. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Flow")
	bool PreviewNextGameplayMap(bool bCampaignMode, FName& OutMapId, FName& OutMapPackageName);

	/** Travels to the manifest-configured main menu map (or default fallback path). */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Flow")
	bool TravelToMainMenu();

	/** Current selected gameplay map id persisted for runtime/save state. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Flow")
	FName GetCurrentGameplayMapId() const { return CurrentGameplayMapId; }

	/** True when campaign sequential map selection mode is currently enabled. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Flow")
	bool IsCampaignModeEnabled() const { return bCampaignModeEnabled; }

	/** Immediately writes current persistent runtime state to save slot. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Persistence")
	bool SavePersistentState();

	/** Loads persistent runtime state from save slot and applies to this subsystem. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Persistence")
	bool LoadPersistentState();

	/** Marks a teleporter id as unlocked and persists when auto-save is enabled. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Persistence")
	void UnlockTeleporter(FName TeleporterId);

	/** Returns true when a teleporter id has been unlocked in persistent state. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Persistence")
	bool IsTeleporterUnlocked(FName TeleporterId) const;

	/** Sets a quest flag value and persists when auto-save is enabled. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Persistence")
	void SetQuestFlag(FName QuestFlagId, bool bIsSet);

	/** Reads a quest flag value from persistent state (false when missing). */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Persistence")
	bool GetQuestFlag(FName QuestFlagId) const;

public:
	/** Broadcasts when EnterZone() computes a new load/unload request delta. */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|Streaming|Events")
	FAeyerjiStreamingRequestStartedSignature OnStreamingRequestStarted;

	/** Broadcasts on loaded/unloaded level changes while a request is in flight. */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|Streaming|Events")
	FAeyerjiStreamingStateChangedSignature OnStreamingStateChanged;

	/** Broadcasts once all required levels for the active zone are loaded/visible. */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|Streaming|Events")
	FAeyerjiZoneReadySignature OnZoneReady;

	/** Broadcasts when gameplay map selection resolves a map for session travel. */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|Flow|Events")
	FAeyerjiGameplayMapSelectedSignature OnGameplayMapSelected;

protected:
	/** Optional manifest configured via .ini (can also be injected at runtime via SetManifest). */
	UPROPERTY(EditDefaultsOnly, Config, Category="Aeyerji|Streaming")
	TSoftObjectPtr<UAeyerjiStreamingManifest> StreamingManifestAsset;

	/** Save slot name used for persistent streaming/session state. */
	UPROPERTY(EditDefaultsOnly, Config, Category="Aeyerji|Persistence")
	FString StreamingStateSlotName = TEXT("AeyerjiStreamingState");

	/** If true, persistent state is loaded automatically during subsystem initialize. */
	UPROPERTY(EditDefaultsOnly, Config, Category="Aeyerji|Persistence")
	bool bAutoLoadPersistentState = true;

	/** If true, state-changing operations auto-save to slot. */
	UPROPERTY(EditDefaultsOnly, Config, Category="Aeyerji|Persistence")
	bool bAutoSavePersistentState = true;

	/** If false, clients skip driving sublevel streaming and only server/listen/standalone applies it. */
	UPROPERTY(EditDefaultsOnly, Config, Category="Aeyerji|Streaming|Networking")
	bool bAllowClientSideStreaming = true;

	/** Active loaded manifest instance. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Streaming")
	TObjectPtr<UAeyerjiStreamingManifest> Manifest = nullptr;

	/** Current runtime zone id. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Streaming")
	FName CurrentZoneId = NAME_None;

	/** Current campaign mode selection. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Flow")
	bool bCampaignModeEnabled = false;

	/** Sequential map cursor used when campaign mode is active. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Flow")
	int32 CampaignMapCursor = 0;

	/** Current gameplay map id selected for this runtime session. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Flow")
	FName CurrentGameplayMapId = NAME_None;

	/** One-shot startup zone override carried across hard map travel for retry/restart flows. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Flow")
	FName PendingStartupZoneOverride = NAME_None;

private:
	/** Resolves this subsystem from world context internally. */
	static UAeyerjiStreamingSubsystem* Get(const UObject* WorldContextObject);

	/** Loads manifest from config soft reference when needed. */
	bool EnsureManifestLoaded();

	/** Returns current runtime world eligible for gameplay streaming/travel. */
	UWorld* GetRuntimeWorld() const;

	/** Applies normalization to level package names for reliable comparisons. */
	static FName NormalizePackageName(FName RawName);

	/** Removes PIE prefixes from short package names for matching (UEDPIE_X_). */
	static FString StripPIEPrefixFromLeaf(const FString& LeafName);

	/** Compares package names using full-path and short-name fallback matching. */
	static bool PackageNamesMatch(FName A, FName B);

	/** Finds zone definition by id on the currently loaded manifest. */
	const FZoneDef* FindZoneDef(FName ZoneId) const;

	/** Resolves next gameplay map based on random/campaign mode. */
	bool SelectGameplayMap(bool bCampaignMode, bool bAdvanceCursor, FAeyerjiGameplayMapDef& OutMapDef, FName& OutMapPackageName);

	/** Converts map asset reference into travel package name. */
	static bool ResolveMapPackageName(const FAeyerjiGameplayMapDef& MapDef, FName& OutMapPackageName);

	/** Performs runtime travel to the provided map package. */
	bool TravelToMapPackage(FName MapPackageName, bool bPreferSeamlessTravel = true);

	/** Applies pending load/unload level streaming requests to the active world. */
	void ApplyStreamingDelta(const TArray<FName>& LevelsToLoad, const TArray<FName>& LevelsToUnload, bool bMakeVisibleAfterLoad, bool bBlockOnLoad);

	/** Rebuilds the loaded sublevel cache from current world streaming state. */
	void RefreshLoadedLevels();

	/** Returns true when a level is loaded (and visible when requested). */
	bool IsLevelReady(FName LevelName, bool bRequireVisible) const;

	/** Returns true when all desired levels are in the ready state. */
	bool AreDesiredLevelsReady() const;

	/** Updates pending sets, emits state events, and resolves zone-ready transitions. */
	void EvaluateStreamingState();

	/** FTicker callback used while waiting for streaming requests to complete. */
	bool HandleStreamingTick(float DeltaTime);

	/** Starts runtime streaming ticker when work is pending. */
	void StartStreamingTick();

	/** Stops runtime streaming ticker when no work remains. */
	void StopStreamingTick();

	/** Returns true if this runtime world should drive local streaming requests. */
	bool ShouldDriveStreamingInCurrentWorld() const;

	/** Persists state automatically when auto-save is enabled. */
	void MarkStateDirtyAndMaybeSave();

private:
	/** Runtime loaded level cache. */
	TSet<FName> LoadedLevels;

	/** Requested target level set for current zone. */
	TSet<FName> DesiredLevels;

	/** Levels currently requested for loading. */
	TSet<FName> PendingLoads;

	/** Levels currently requested for unloading. */
	TSet<FName> PendingUnloads;

	/** Whether current zone requires visibility in addition to load completion. */
	bool bCurrentZoneRequiresVisibility = true;

	/** Guard for single OnZoneReady broadcast per EnterZone request. */
	bool bCurrentZoneReadyPending = false;

	/** Persistent unlocked teleporter ids. */
	TSet<FName> UnlockedTeleporterIds;

	/** Persistent quest flags. */
	TMap<FName, bool> QuestFlags;

	/** Runtime ticker handle for async streaming completion checks. */
	FTSTicker::FDelegateHandle StreamingTickHandle;
};
