#include "Systems/AeyerjiStreamingSubsystem.h"

#include "AeyerjiGameplayTags.h"
#include "Systems/AeyerjiSaveManagerSubsystem.h"
#include "Systems/AeyerjiStreamingSaveGame.h"
#include "Systems/AeyerjiWorldStateSubsystem.h"
#include "Frontend/AeyerjiFrontendRules.h"
#include "Engine/GameInstance.h"
#include "Engine/LevelStreaming.h"
#include "Engine/World.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"

namespace
{
	const TCHAR* DefaultMainMenuPath = TEXT("/Game/Levels/L_MainMenu");
	const FName FallbackMenuZoneId(TEXT("Zone.Menu"));
	const FName FallbackNeonZoneId(TEXT("Zone.Neon"));
	const FName LegacyMenuZoneId(TEXT("L_MainMenu"));
	const FName LegacyNeonZoneId(TEXT("NeonMap"));
	const TCHAR* FallbackGameplayMapPath = TEXT("/Game/Levels/L_PersistentRoot.L_PersistentRoot");
	const FName FallbackNeonPlayerStartTag(TEXT("Zone.Neon.Entry"));
	constexpr float StreamingTickIntervalSeconds = 0.05f;

	TArray<FName> BuildSortedArray(const TSet<FName>& InSet)
	{
		TArray<FName> OutArray;
		OutArray.Reserve(InSet.Num());
		for (const FName Name : InSet)
		{
			OutArray.Add(Name);
		}

		OutArray.Sort([](const FName A, const FName B)
		{
			return A.LexicalLess(B);
		});

		return OutArray;
	}
}

UAeyerjiStreamingSubsystem* UAeyerjiStreamingSubsystem::GetStreamingSubsystem(const UObject* WorldContextObject)
{
	return Get(WorldContextObject);
}

void UAeyerjiStreamingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	EnsureManifestLoaded();

	if (bAutoLoadPersistentState)
	{
		LoadPersistentState();
	}

	RefreshLoadedLevels();
}

void UAeyerjiStreamingSubsystem::Deinitialize()
{
	StopStreamingTick();

	PendingLoads.Reset();
	PendingUnloads.Reset();
	DesiredLevels.Reset();
	LoadedLevels.Reset();

	Super::Deinitialize();
}

void UAeyerjiStreamingSubsystem::SetManifest(UAeyerjiStreamingManifest* InManifest)
{
	Manifest = InManifest;
}

bool UAeyerjiStreamingSubsystem::EnterZone(const FName ZoneId)
{
	EnsureManifestLoaded();

	FZoneDef ZoneDef;
	if (!GetZoneDefinition(ZoneId, ZoneDef))
	{
		UE_LOG(LogTemp, Warning, TEXT("UAeyerjiStreamingSubsystem::EnterZone failed - unknown ZoneId=%s"), *ZoneId.ToString());
		return false;
	}

	CurrentZoneId = ZoneDef.ZoneId;
	bCurrentZoneRequiresVisibility = ZoneDef.bMakeVisibleAfterLoad;
	bCurrentZoneReadyPending = true;

	TArray<FName> Desired;
	Desired.Reserve(ZoneDef.LevelsToKeep.Num() + ZoneDef.LevelsToLoad.Num());

	for (const FName LevelName : ZoneDef.LevelsToKeep)
	{
		Desired.Add(LevelName);
	}

	for (const FName LevelName : ZoneDef.LevelsToLoad)
	{
		Desired.Add(LevelName);
	}

	SetDesiredLevels(Desired);
	MarkStateDirtyAndMaybeSave();
	return true;
}

bool UAeyerjiStreamingSubsystem::GetZoneDefinition(const FName ZoneId, FZoneDef& OutZoneDefinition) const
{
	if (ZoneId.IsNone())
	{
		return false;
	}

	const_cast<UAeyerjiStreamingSubsystem*>(this)->EnsureManifestLoaded();

	if (Manifest && Manifest->GetZoneDefinition(ZoneId, OutZoneDefinition))
	{
		return true;
	}

	if (ZoneId == FallbackMenuZoneId || ZoneId == LegacyMenuZoneId)
	{
		OutZoneDefinition = FZoneDef();
		OutZoneDefinition.ZoneId = FallbackMenuZoneId;
		OutZoneDefinition.LevelsToLoad = { LegacyMenuZoneId };
		OutZoneDefinition.LevelsToKeep.Reset();
		OutZoneDefinition.EntryPlayerStartTag = NAME_None;
		OutZoneDefinition.bSpawnPlayerAfterReady = false;
		OutZoneDefinition.bAutoStartRun = false;
		OutZoneDefinition.bMakeVisibleAfterLoad = true;
		OutZoneDefinition.bBlockOnLoad = false;
		return true;
	}

	if (ZoneId == FallbackNeonZoneId || ZoneId == LegacyNeonZoneId)
	{
		OutZoneDefinition = FZoneDef();
		OutZoneDefinition.ZoneId = FallbackNeonZoneId;
		OutZoneDefinition.LevelsToLoad = { LegacyNeonZoneId };
		OutZoneDefinition.LevelsToKeep.Reset();
		OutZoneDefinition.EntryPlayerStartTag = FallbackNeonPlayerStartTag;
		OutZoneDefinition.bSpawnPlayerAfterReady = true;
		OutZoneDefinition.bAutoStartRun = true;
		OutZoneDefinition.bMakeVisibleAfterLoad = true;
		OutZoneDefinition.bBlockOnLoad = false;
		return true;
	}

	return false;
}

bool UAeyerjiStreamingSubsystem::EnterStartupZone(const FName FallbackZoneId, const bool bPreferSavedZone)
{
	FName StartupZone = FallbackZoneId;
	if (TakePendingStartupZoneOverride(StartupZone))
	{
		return EnterZone(StartupZone);
	}

	if (bPreferSavedZone && !CurrentZoneId.IsNone())
	{
		StartupZone = CurrentZoneId;
	}

	if (StartupZone.IsNone() && EnsureManifestLoaded() && Manifest)
	{
		StartupZone = Manifest->DefaultZoneId;
	}

	if (StartupZone.IsNone())
	{
		FZoneDef MenuZoneDefinition;
		if (GetZoneDefinition(FallbackMenuZoneId, MenuZoneDefinition))
		{
			StartupZone = MenuZoneDefinition.ZoneId;
		}
	}

	if (StartupZone.IsNone())
	{
		return false;
	}

	return EnterZone(StartupZone);
}

bool UAeyerjiStreamingSubsystem::TakePendingStartupZoneOverride(FName& OutZoneId)
{
	if (PendingStartupZoneOverride.IsNone())
	{
		return false;
	}

	OutZoneId = PendingStartupZoneOverride;
	UE_LOG(LogTemp, Display, TEXT("UAeyerjiStreamingSubsystem: Consuming pending startup zone override %s"), *OutZoneId.ToString());
	PendingStartupZoneOverride = NAME_None;
	return true;
}

void UAeyerjiStreamingSubsystem::SetDesiredLevels(const TArray<FName>& Desired)
{
	DesiredLevels.Reset();
	for (const FName RawLevelName : Desired)
	{
		const FName Normalized = NormalizePackageName(RawLevelName);
		if (!Normalized.IsNone())
		{
			DesiredLevels.Add(Normalized);
		}
	}

	RefreshLoadedLevels();

	PendingLoads.Reset();
	PendingUnloads.Reset();

	auto DesiredContainsLevel = [this](const FName LoadedLevel) -> bool
	{
		for (const FName DesiredLevel : DesiredLevels)
		{
			if (PackageNamesMatch(DesiredLevel, LoadedLevel))
			{
				return true;
			}
		}

		return false;
	};

	for (const FName DesiredLevel : DesiredLevels)
	{
		if (!IsLevelLoaded(DesiredLevel))
		{
			PendingLoads.Add(DesiredLevel);
		}
	}

	for (const FName LoadedLevel : LoadedLevels)
	{
		if (!DesiredContainsLevel(LoadedLevel))
		{
			PendingUnloads.Add(LoadedLevel);
		}
	}

	const TArray<FName> LevelsToLoad = BuildSortedArray(PendingLoads);
	const TArray<FName> LevelsToUnload = BuildSortedArray(PendingUnloads);
	OnStreamingRequestStarted.Broadcast(CurrentZoneId, LevelsToLoad, LevelsToUnload);

	if (ShouldDriveStreamingInCurrentWorld() && (LevelsToLoad.Num() > 0 || LevelsToUnload.Num() > 0))
	{
		bool bBlockOnLoad = false;
		FZoneDef Zone;
		if (GetZoneDefinition(CurrentZoneId, Zone))
		{
			bBlockOnLoad = Zone.bBlockOnLoad;
		}

		ApplyStreamingDelta(LevelsToLoad, LevelsToUnload, bCurrentZoneRequiresVisibility, bBlockOnLoad);
		StartStreamingTick();
	}

	EvaluateStreamingState();
}

void UAeyerjiStreamingSubsystem::GetLoadedLevels(TArray<FName>& OutLoadedLevels) const
{
	OutLoadedLevels = BuildSortedArray(LoadedLevels);
}

bool UAeyerjiStreamingSubsystem::IsLevelLoaded(const FName LevelName) const
{
	const FName NormalizedQuery = NormalizePackageName(LevelName);
	if (NormalizedQuery.IsNone())
	{
		return false;
	}

	for (const FName LoadedLevel : LoadedLevels)
	{
		if (PackageNamesMatch(LoadedLevel, NormalizedQuery))
		{
			return true;
		}
	}

	return IsLevelReady(NormalizedQuery, false);
}

bool UAeyerjiStreamingSubsystem::StartGameplaySession(const bool bCampaignMode)
{
	// Legacy direct-menu entry point. New frontend assembly must use the validated lobby request path.
	PendingFrontendRunLaunch = FAeyerjiPendingRunLaunchRequest();
	FAeyerjiGameplayMapDef MapDef;
	FName MapPackageName = NAME_None;
	if (!SelectGameplayMap(bCampaignMode, true, MapDef, MapPackageName))
	{
		return false;
	}
	CurrentZoneId = MapDef.EntryZoneId;

	// The persistent root's WorldDirector consumes this one-shot value. Its authored setting
	// intentionally does not prefer saved state, so CurrentZoneId alone cannot select gameplay.
	PendingStartupZoneOverride = CurrentZoneId;
	bCurrentZoneReadyPending = false;

	OnGameplayMapSelected.Broadcast(CurrentGameplayMapId, MapPackageName);
	MarkStateDirtyAndMaybeSave();
	return TravelToMapPackage(MapPackageName);
}

bool UAeyerjiStreamingSubsystem::PrepareFrontendRunLaunch(const EAeyerjiRiftActivityType ActivityType,
	const int32 ExcursionTier, FAeyerjiPendingRunLaunchRequest& OutRequest)
{
	OutRequest = FAeyerjiPendingRunLaunchRequest();
	UWorld* World = GetRuntimeWorld();
	if (!World || World->GetNetMode() == NM_Client
		|| (ActivityType == EAeyerjiRiftActivityType::Excursion && ExcursionTier <= 0))
	{
		return false;
	}

	const bool bCampaignMode = ActivityType == EAeyerjiRiftActivityType::StandardRift;
	FAeyerjiGameplayMapDef MapDef;
	FName MapPackageName = NAME_None;
	if (!SelectGameplayMap(bCampaignMode, true, MapDef, MapPackageName))
	{
		return false;
	}
	CurrentZoneId = MapDef.EntryZoneId;

	PendingFrontendRunLaunch.RequestId = FMath::Max(NextFrontendRunLaunchRequestId++, 1);
	PendingFrontendRunLaunch.ActivityType = ActivityType;
	PendingFrontendRunLaunch.ExcursionTier = ActivityType == EAeyerjiRiftActivityType::Excursion ? ExcursionTier : 0;
	PendingFrontendRunLaunch.MapId = CurrentGameplayMapId;
	PendingFrontendRunLaunch.MapPackageName = MapPackageName;
	if (!PendingFrontendRunLaunch.IsValid())
	{
		PendingFrontendRunLaunch = FAeyerjiPendingRunLaunchRequest();
		return false;
	}
	OutRequest = PendingFrontendRunLaunch;
	UE_LOG(LogTemp, Display, TEXT("[LobbyLaunch] Prepared RequestId=%d Activity=%d Tier=%d MapId=%s Package=%s"),
		OutRequest.RequestId, static_cast<int32>(OutRequest.ActivityType), OutRequest.ExcursionTier,
		*OutRequest.MapId.ToString(), *OutRequest.MapPackageName.ToString());
	return true;
}

bool UAeyerjiStreamingSubsystem::ExecutePendingFrontendRunLaunch()
{
	if (!PendingFrontendRunLaunch.IsValid())
	{
		return false;
	}
	// Preserve the exact entry zone selected with the launch request across seamless travel.
	// BP_AeyerjiWorldDirector consumes and clears this after the persistent root has loaded.
	PendingStartupZoneOverride = CurrentZoneId;
	bCurrentZoneReadyPending = false;
	OnGameplayMapSelected.Broadcast(PendingFrontendRunLaunch.MapId, PendingFrontendRunLaunch.MapPackageName);
	MarkStateDirtyAndMaybeSave();
	UE_LOG(LogTemp, Display, TEXT("[LobbyLaunch] Travel RequestId=%d Package=%s EntryZone=%s"),
		PendingFrontendRunLaunch.RequestId, *PendingFrontendRunLaunch.MapPackageName.ToString(),
		*PendingStartupZoneOverride.ToString());
	return TravelToMapPackage(PendingFrontendRunLaunch.MapPackageName);
}

bool UAeyerjiStreamingSubsystem::GetPendingFrontendRunLaunch(FAeyerjiPendingRunLaunchRequest& OutRequest) const
{
	OutRequest = PendingFrontendRunLaunch;
	return OutRequest.IsValid();
}

bool UAeyerjiStreamingSubsystem::ConsumePendingFrontendRunLaunch(const int32 RequestId)
{
	if (!PendingFrontendRunLaunch.IsValid() || PendingFrontendRunLaunch.RequestId != RequestId)
	{
		return false;
	}
	UE_LOG(LogTemp, Display, TEXT("[LobbyLaunch] Consumed RequestId=%d Activity=%d Tier=%d"),
		PendingFrontendRunLaunch.RequestId, static_cast<int32>(PendingFrontendRunLaunch.ActivityType),
		PendingFrontendRunLaunch.ExcursionTier);
	return AeyerjiFrontendRules::ConsumeLaunchRequest(PendingFrontendRunLaunch, RequestId);
}

bool UAeyerjiStreamingSubsystem::RestartCurrentGameplaySession(const FName ZoneIdOverride, const bool bPreferSeamlessTravel)
{
	EnsureManifestLoaded();

	FAeyerjiGameplayMapDef MapDef;
	FName MapPackageName = NAME_None;
	bool bResolvedFromManifest = false;

	if (Manifest && !CurrentGameplayMapId.IsNone() && Manifest->GetGameplayMapDefinition(CurrentGameplayMapId, MapDef))
	{
		bResolvedFromManifest = ResolveMapPackageName(MapDef, MapPackageName);
		if (!bResolvedFromManifest)
		{
			MapPackageName = NAME_None;
		}
	}

	UWorld* World = GetRuntimeWorld();
	if (!World)
	{
		return false;
	}

	if (!bResolvedFromManifest)
	{
		const FName CurrentMapPackage = NormalizePackageName(FName(*World->GetPackage()->GetName()));
		if (CurrentMapPackage.IsNone())
		{
			return false;
		}

		if (Manifest)
		{
			for (const FAeyerjiGameplayMapDef& CandidateMapDef : Manifest->GameplayMaps)
			{
				FName CandidateMapPackage = NAME_None;
				if (!ResolveMapPackageName(CandidateMapDef, CandidateMapPackage))
				{
					continue;
				}

				if (!PackageNamesMatch(CandidateMapPackage, CurrentMapPackage))
				{
					continue;
				}

				MapDef = CandidateMapDef;
				MapPackageName = CandidateMapPackage;
				bResolvedFromManifest = true;

				if (CurrentGameplayMapId.IsNone())
				{
					CurrentGameplayMapId = CandidateMapDef.MapId.IsNone()
						? FName(*FPackageName::GetShortName(CandidateMapPackage.ToString()))
						: CandidateMapDef.MapId;
				}
				break;
			}
		}

		if (!bResolvedFromManifest)
		{
			MapPackageName = CurrentMapPackage;
		}
	}

	if (!ZoneIdOverride.IsNone())
	{
		CurrentZoneId = ZoneIdOverride;
	}
	else if (bResolvedFromManifest && !MapDef.EntryZoneId.IsNone())
	{
		CurrentZoneId = MapDef.EntryZoneId;
	}

	bCurrentZoneReadyPending = false;

	if (CurrentGameplayMapId.IsNone())
	{
		CurrentGameplayMapId = FName(*FPackageName::GetShortName(MapPackageName.ToString()));
	}

	PendingStartupZoneOverride = CurrentZoneId;
	UE_LOG(LogTemp, Display,
		TEXT("UAeyerjiStreamingSubsystem: RestartCurrentGameplaySession queued startup override Zone=%s Map=%s"),
		*CurrentZoneId.ToString(),
		*MapPackageName.ToString());
	OnGameplayMapSelected.Broadcast(CurrentGameplayMapId, MapPackageName);
	MarkStateDirtyAndMaybeSave();
	return TravelToMapPackage(MapPackageName, bPreferSeamlessTravel);
}

bool UAeyerjiStreamingSubsystem::PreviewNextGameplayMap(const bool bCampaignMode, FName& OutMapId, FName& OutMapPackageName)
{
	FAeyerjiGameplayMapDef MapDef;
	if (!SelectGameplayMap(bCampaignMode, false, MapDef, OutMapPackageName))
	{
		return false;
	}

	OutMapId = MapDef.MapId;
	return true;
}

bool UAeyerjiStreamingSubsystem::TravelToMainMenu()
{
	FName MainMenuPackage = NAME_None;

	if (EnsureManifestLoaded() && Manifest && Manifest->MainMenuMap.ToSoftObjectPath().IsValid())
	{
		FAeyerjiGameplayMapDef MainMenuDef;
		MainMenuDef.MapAsset = Manifest->MainMenuMap;
		if (!ResolveMapPackageName(MainMenuDef, MainMenuPackage))
		{
			MainMenuPackage = NAME_None;
		}
	}

	if (MainMenuPackage.IsNone())
	{
		MainMenuPackage = FName(DefaultMainMenuPath);
	}

	PendingStartupZoneOverride = NAME_None;
	return TravelToMapPackage(MainMenuPackage);
}

bool UAeyerjiStreamingSubsystem::SavePersistentState()
{
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return false;
	}

	UAeyerjiSaveManagerSubsystem* SaveManager = GameInstance->GetSubsystem<UAeyerjiSaveManagerSubsystem>();
	if (!SaveManager)
	{
		return false;
	}

	UAeyerjiStreamingSaveGame* SaveData = Cast<UAeyerjiStreamingSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UAeyerjiStreamingSaveGame::StaticClass()));
	if (!SaveData)
	{
		return false;
	}

	SaveData->LastZoneId = CurrentZoneId;
	SaveData->LastGameplayMapId = CurrentGameplayMapId;
	SaveData->bCampaignMode = bCampaignModeEnabled;
	SaveData->CampaignMapCursor = CampaignMapCursor;
	SaveData->QuestFlags = QuestFlags;

	SaveData->UnlockedTeleporterIds.Reset();
	for (const FName TeleporterId : UnlockedTeleporterIds)
	{
		SaveData->UnlockedTeleporterIds.Add(TeleporterId);
	}
	SaveData->UnlockedTeleporterIds.Sort([](const FName A, const FName B)
	{
		return A.LexicalLess(B);
	});

	return SaveManager->CommitStreamingStateForOwner(SaveData);
}

bool UAeyerjiStreamingSubsystem::LoadPersistentState()
{
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return false;
	}

	UAeyerjiSaveManagerSubsystem* SaveManager = GameInstance->GetSubsystem<UAeyerjiSaveManagerSubsystem>();
	if (!SaveManager)
	{
		return false;
	}

	UAeyerjiStreamingSaveGame* SaveData = SaveManager->ResolveStreamingStateForOwner();
	if (!SaveData)
	{
		return false;
	}

	CurrentZoneId = SaveData->LastZoneId;
	CurrentGameplayMapId = SaveData->LastGameplayMapId;
	bCampaignModeEnabled = SaveData->bCampaignMode;
	CampaignMapCursor = FMath::Max(0, SaveData->CampaignMapCursor);
	QuestFlags = SaveData->QuestFlags;

	UnlockedTeleporterIds.Reset();
	for (const FName TeleporterId : SaveData->UnlockedTeleporterIds)
	{
		if (!TeleporterId.IsNone())
		{
			UnlockedTeleporterIds.Add(TeleporterId);
		}
	}

	if (UAeyerjiWorldStateSubsystem* WorldStateSubsystem = UAeyerjiWorldStateSubsystem::Get(this))
	{
		for (const TPair<FName, bool>& Pair : QuestFlags)
		{
			if (!Pair.Key.IsNone())
			{
				WorldStateSubsystem->SetValue(
					FAeyerjiWorldStateKey(AeyerjiTags::World_Quest_Flag, Pair.Key),
					FAeyerjiWorldStateValue::FromBool(Pair.Value),
					EAeyerjiWorldStatePersistence::Persistent,
					EAeyerjiWorldStateReplication::ServerOnly);
			}
		}

		for (const FName TeleporterId : UnlockedTeleporterIds)
		{
			WorldStateSubsystem->SetValue(
				FAeyerjiWorldStateKey(AeyerjiTags::World_Teleporter_Unlocked, TeleporterId),
				FAeyerjiWorldStateValue::FromBool(true),
				EAeyerjiWorldStatePersistence::Persistent,
				EAeyerjiWorldStateReplication::PublicReplicated);
		}
	}

	return true;
}

void UAeyerjiStreamingSubsystem::UnlockTeleporter(const FName TeleporterId)
{
	if (TeleporterId.IsNone())
	{
		return;
	}

	UnlockedTeleporterIds.Add(TeleporterId);
	if (UAeyerjiWorldStateSubsystem* WorldStateSubsystem = UAeyerjiWorldStateSubsystem::Get(this))
	{
		WorldStateSubsystem->SetValue(
			FAeyerjiWorldStateKey(AeyerjiTags::World_Teleporter_Unlocked, TeleporterId),
			FAeyerjiWorldStateValue::FromBool(true),
			EAeyerjiWorldStatePersistence::Persistent,
			EAeyerjiWorldStateReplication::PublicReplicated);
	}
	MarkStateDirtyAndMaybeSave();
}

bool UAeyerjiStreamingSubsystem::IsTeleporterUnlocked(const FName TeleporterId) const
{
	if (TeleporterId.IsNone())
	{
		return false;
	}

	if (const UAeyerjiWorldStateSubsystem* WorldStateSubsystem = UAeyerjiWorldStateSubsystem::Get(this))
	{
		FAeyerjiWorldStateValue Value;
		if (WorldStateSubsystem->GetValue(FAeyerjiWorldStateKey(AeyerjiTags::World_Teleporter_Unlocked, TeleporterId), Value)
			&& Value.Type == EAeyerjiWorldStateValueType::Bool)
		{
			return Value.BoolValue;
		}
	}

	return UnlockedTeleporterIds.Contains(TeleporterId);
}

void UAeyerjiStreamingSubsystem::SetQuestFlag(const FName QuestFlagId, const bool bIsSet)
{
	if (QuestFlagId.IsNone())
	{
		return;
	}

	QuestFlags.Add(QuestFlagId, bIsSet);
	if (UAeyerjiWorldStateSubsystem* WorldStateSubsystem = UAeyerjiWorldStateSubsystem::Get(this))
	{
		WorldStateSubsystem->SetValue(
			FAeyerjiWorldStateKey(AeyerjiTags::World_Quest_Flag, QuestFlagId),
			FAeyerjiWorldStateValue::FromBool(bIsSet),
			EAeyerjiWorldStatePersistence::Persistent,
			EAeyerjiWorldStateReplication::ServerOnly);
	}
	MarkStateDirtyAndMaybeSave();
}

bool UAeyerjiStreamingSubsystem::GetQuestFlag(const FName QuestFlagId) const
{
	if (QuestFlagId.IsNone())
	{
		return false;
	}

	if (const UAeyerjiWorldStateSubsystem* WorldStateSubsystem = UAeyerjiWorldStateSubsystem::Get(this))
	{
		FAeyerjiWorldStateValue Value;
		if (WorldStateSubsystem->GetValue(FAeyerjiWorldStateKey(AeyerjiTags::World_Quest_Flag, QuestFlagId), Value)
			&& Value.Type == EAeyerjiWorldStateValueType::Bool)
		{
			return Value.BoolValue;
		}
	}

	if (const bool* bValue = QuestFlags.Find(QuestFlagId))
	{
		return *bValue;
	}

	return false;
}

UAeyerjiStreamingSubsystem* UAeyerjiStreamingSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	const UWorld* World = WorldContextObject->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	if (UGameInstance* GameInstance = World->GetGameInstance())
	{
		return GameInstance->GetSubsystem<UAeyerjiStreamingSubsystem>();
	}

	return nullptr;
}

bool UAeyerjiStreamingSubsystem::EnsureManifestLoaded()
{
	if (Manifest)
	{
		return true;
	}

	if (StreamingManifestAsset.IsNull())
	{
		return false;
	}

	Manifest = StreamingManifestAsset.LoadSynchronous();
	return Manifest != nullptr;
}

UWorld* UAeyerjiStreamingSubsystem::GetRuntimeWorld() const
{
	const UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return nullptr;
	}

	UWorld* World = GameInstance->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	if (World->WorldType != EWorldType::Game && World->WorldType != EWorldType::PIE)
	{
		return nullptr;
	}

	return World;
}

FName UAeyerjiStreamingSubsystem::NormalizePackageName(const FName RawName)
{
	FString NameString = RawName.ToString();
	if (NameString.IsEmpty())
	{
		return NAME_None;
	}

	const FString ObjectPathPackageName = FPackageName::ObjectPathToPackageName(NameString);
	if (!ObjectPathPackageName.IsEmpty())
	{
		NameString = ObjectPathPackageName;
	}

	// PIE world packages are transient duplicates like /Game/Levels/UEDPIE_0_L_PersistentRoot.
	// Travel/load paths must point at the source package on disk instead.
	if (FPackageName::IsValidLongPackageName(NameString, /*bIncludeReadOnlyRoots=*/true))
	{
		const FString PackagePath = FPackageName::GetLongPackagePath(NameString);
		const FString PackageLeaf = FPackageName::GetShortName(NameString);
		const FString StrippedLeaf = StripPIEPrefixFromLeaf(PackageLeaf);
		if (StrippedLeaf != PackageLeaf)
		{
			NameString = PackagePath.IsEmpty()
				? StrippedLeaf
				: FString::Printf(TEXT("%s/%s"), *PackagePath, *StrippedLeaf);
		}
	}
	else
	{
		NameString = StripPIEPrefixFromLeaf(NameString);
	}

	return NameString.IsEmpty() ? NAME_None : FName(*NameString);
}

FString UAeyerjiStreamingSubsystem::StripPIEPrefixFromLeaf(const FString& LeafName)
{
	static const FString PIEPrefix = TEXT("UEDPIE_");
	if (!LeafName.StartsWith(PIEPrefix))
	{
		return LeafName;
	}

	const int32 SecondUnderscore = LeafName.Find(TEXT("_"), ESearchCase::CaseSensitive, ESearchDir::FromStart, PIEPrefix.Len());
	if (SecondUnderscore == INDEX_NONE || (SecondUnderscore + 1) >= LeafName.Len())
	{
		return LeafName;
	}

	return LeafName.Mid(SecondUnderscore + 1);
}

bool UAeyerjiStreamingSubsystem::PackageNamesMatch(const FName A, const FName B)
{
	const FName NormalizedA = NormalizePackageName(A);
	const FName NormalizedB = NormalizePackageName(B);
	if (NormalizedA.IsNone() || NormalizedB.IsNone())
	{
		return false;
	}

	if (NormalizedA == NormalizedB)
	{
		return true;
	}

	const FString ShortA = StripPIEPrefixFromLeaf(FPackageName::GetShortName(NormalizedA.ToString()));
	const FString ShortB = StripPIEPrefixFromLeaf(FPackageName::GetShortName(NormalizedB.ToString()));
	return ShortA.Equals(ShortB, ESearchCase::CaseSensitive);
}

const FZoneDef* UAeyerjiStreamingSubsystem::FindZoneDef(const FName ZoneId) const
{
	if (!Manifest || ZoneId.IsNone())
	{
		return nullptr;
	}

	for (const FZoneDef& Zone : Manifest->Zones)
	{
		if (Zone.ZoneId == ZoneId)
		{
			return &Zone;
		}
	}

	return nullptr;
}

bool UAeyerjiStreamingSubsystem::SelectGameplayMap(const bool bCampaignMode, const bool bAdvanceCursor, FAeyerjiGameplayMapDef& OutMapDef, FName& OutMapPackageName)
{
	const bool bHasAuthoredGameplayMaps = EnsureManifestLoaded() && Manifest && !Manifest->GameplayMaps.IsEmpty();
	if (!bHasAuthoredGameplayMaps)
	{
		// NeonMap is a streamed gameplay zone, not the persistent travel destination. Route an
		// empty authored rotation through the persistent root so its WorldDirector can enter Zone.Neon.
		OutMapDef = FAeyerjiGameplayMapDef();
		OutMapDef.MapId = LegacyNeonZoneId;
		OutMapDef.MapAsset = TSoftObjectPtr<UWorld>(FSoftObjectPath(FallbackGameplayMapPath));
		OutMapDef.EntryZoneId = FallbackNeonZoneId;
		if (!ResolveMapPackageName(OutMapDef, OutMapPackageName))
		{
			UE_LOG(LogTemp, Error, TEXT("UAeyerjiStreamingSubsystem: fallback gameplay map is invalid Path=%s"),
				FallbackGameplayMapPath);
			return false;
		}

		if (bAdvanceCursor)
		{
			bCampaignModeEnabled = bCampaignMode;
			CurrentGameplayMapId = OutMapDef.MapId;
			CampaignMapCursor = 0;
		}
		UE_LOG(LogTemp, Warning,
			TEXT("UAeyerjiStreamingSubsystem: gameplay rotation is empty; using fallback MapId=%s Package=%s"),
			*OutMapDef.MapId.ToString(), *OutMapPackageName.ToString());
		return true;
	}

	const int32 NumMaps = Manifest->GameplayMaps.Num();
	int32 SelectedIndex = 0;

	if (bCampaignMode)
	{
		SelectedIndex = FMath::Clamp(CampaignMapCursor, 0, NumMaps - 1);
	}
	else
	{
		SelectedIndex = (NumMaps == 1) ? 0 : FMath::RandRange(0, NumMaps - 1);
	}

	OutMapDef = Manifest->GameplayMaps[SelectedIndex];
	if (!ResolveMapPackageName(OutMapDef, OutMapPackageName))
	{
		return false;
	}

	if (OutMapDef.MapId.IsNone())
	{
		OutMapDef.MapId = FName(*FPackageName::GetShortName(OutMapPackageName.ToString()));
	}

	if (bAdvanceCursor)
	{
		bCampaignModeEnabled = bCampaignMode;
		CurrentGameplayMapId = OutMapDef.MapId;

		if (bCampaignMode)
		{
			CampaignMapCursor = (SelectedIndex + 1) % NumMaps;
		}
	}

	return true;
}

bool UAeyerjiStreamingSubsystem::ResolveMapPackageName(const FAeyerjiGameplayMapDef& MapDef, FName& OutMapPackageName)
{
	const FSoftObjectPath MapPath = MapDef.MapAsset.ToSoftObjectPath();
	if (!MapPath.IsValid())
	{
		return false;
	}

	FString PackageName = MapPath.GetLongPackageName();
	if (PackageName.IsEmpty())
	{
		PackageName = FPackageName::ObjectPathToPackageName(MapPath.ToString());
	}

	if (PackageName.IsEmpty())
	{
		return false;
	}

	OutMapPackageName = FName(*PackageName);
	return true;
}

bool UAeyerjiStreamingSubsystem::TravelToMapPackage(const FName MapPackageName, const bool bPreferSeamlessTravel)
{
	const FName NormalizedMapPackageName = NormalizePackageName(MapPackageName);
	if (NormalizedMapPackageName.IsNone())
	{
		return false;
	}

	UWorld* World = GetRuntimeWorld();
	if (!World)
	{
		return false;
	}

	const FString TravelURL = NormalizedMapPackageName.ToString();
	switch (World->GetNetMode())
	{
	case NM_DedicatedServer:
	case NM_ListenServer:
	{
		const bool bUseSeamlessTravel = bPreferSeamlessTravel;
		if (AGameModeBase* GameMode = World->GetAuthGameMode())
		{
			GameMode->bUseSeamlessTravel = bUseSeamlessTravel;
		}

		UE_LOG(LogTemp, Display,
			TEXT("UAeyerjiStreamingSubsystem: ServerTravel Map=%s NetMode=%d Seamless=%d PreferSeamless=%d WorldType=%d"),
			*TravelURL,
			static_cast<int32>(World->GetNetMode()),
			World->GetAuthGameMode() && World->GetAuthGameMode()->bUseSeamlessTravel ? 1 : 0,
			bPreferSeamlessTravel ? 1 : 0,
			static_cast<int32>(World->WorldType));
		World->ServerTravel(TravelURL, true);
		return true;
	}

	case NM_Standalone:
		UGameplayStatics::OpenLevel(this, NormalizedMapPackageName);
		return true;

	case NM_Client:
		if (APlayerController* LocalPC = UGameplayStatics::GetPlayerController(World, 0))
		{
			LocalPC->ClientTravel(TravelURL, TRAVEL_Absolute);
			return true;
		}

		UGameplayStatics::OpenLevel(this, NormalizedMapPackageName);
		return true;

	default:
		break;
	}

	return false;
}

void UAeyerjiStreamingSubsystem::ApplyStreamingDelta(const TArray<FName>& LevelsToLoad, const TArray<FName>& LevelsToUnload, const bool bMakeVisibleAfterLoad, const bool bBlockOnLoad)
{
	UWorld* World = GetRuntimeWorld();
	if (!World)
	{
		return;
	}

	for (const FName RawLevelName : LevelsToLoad)
	{
		const FName LevelName = NormalizePackageName(RawLevelName);
		if (LevelName.IsNone())
		{
			continue;
		}

		if (ULevelStreaming* StreamingLevel = UGameplayStatics::GetStreamingLevel(World, LevelName))
		{
			StreamingLevel->SetShouldBeLoaded(true);
			StreamingLevel->SetShouldBeVisible(bMakeVisibleAfterLoad);
		}
		else
		{
			UGameplayStatics::LoadStreamLevel(this, LevelName, bMakeVisibleAfterLoad, bBlockOnLoad, FLatentActionInfo());
		}
	}

	for (const FName RawLevelName : LevelsToUnload)
	{
		const FName LevelName = NormalizePackageName(RawLevelName);
		if (LevelName.IsNone())
		{
			continue;
		}

		if (ULevelStreaming* StreamingLevel = UGameplayStatics::GetStreamingLevel(World, LevelName))
		{
			StreamingLevel->SetShouldBeVisible(false);
			StreamingLevel->SetShouldBeLoaded(false);
		}
		else
		{
			UGameplayStatics::UnloadStreamLevel(this, LevelName, FLatentActionInfo(), bBlockOnLoad);
		}
	}
}

void UAeyerjiStreamingSubsystem::RefreshLoadedLevels()
{
	LoadedLevels.Reset();

	UWorld* World = GetRuntimeWorld();
	if (!World)
	{
		return;
	}

	for (const ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
	{
		if (!StreamingLevel || !StreamingLevel->IsLevelLoaded())
		{
			continue;
		}

		const FName PackageName = NormalizePackageName(FName(*StreamingLevel->GetWorldAssetPackageName()));
		if (!PackageName.IsNone())
		{
			LoadedLevels.Add(PackageName);
		}
	}
}

bool UAeyerjiStreamingSubsystem::IsLevelReady(const FName LevelName, const bool bRequireVisible) const
{
	const FName QueryPackageName = NormalizePackageName(LevelName);
	if (QueryPackageName.IsNone())
	{
		return false;
	}

	const UWorld* World = GetRuntimeWorld();
	if (!World)
	{
		return false;
	}

	const FName PersistentPackageName = NormalizePackageName(FName(*World->GetOutermost()->GetName()));
	if (PackageNamesMatch(QueryPackageName, PersistentPackageName))
	{
		return true;
	}

	for (const ULevelStreaming* StreamingLevel : World->GetStreamingLevels())
	{
		if (!StreamingLevel)
		{
			continue;
		}

		const FName StreamingPackageName = NormalizePackageName(FName(*StreamingLevel->GetWorldAssetPackageName()));
		if (!PackageNamesMatch(QueryPackageName, StreamingPackageName))
		{
			continue;
		}

		if (!StreamingLevel->IsLevelLoaded())
		{
			return false;
		}

		if (bRequireVisible && !StreamingLevel->IsLevelVisible())
		{
			return false;
		}

		return true;
	}

	return false;
}

bool UAeyerjiStreamingSubsystem::AreDesiredLevelsReady() const
{
	for (const FName DesiredLevel : DesiredLevels)
	{
		if (!IsLevelReady(DesiredLevel, bCurrentZoneRequiresVisibility))
		{
			return false;
		}
	}

	return true;
}

void UAeyerjiStreamingSubsystem::EvaluateStreamingState()
{
	const TSet<FName> PreviousLoaded = LoadedLevels;
	RefreshLoadedLevels();

	TSet<FName> LoadedNowSet;
	TSet<FName> UnloadedNowSet;

	for (const FName LoadedLevel : LoadedLevels)
	{
		if (!PreviousLoaded.Contains(LoadedLevel))
		{
			LoadedNowSet.Add(LoadedLevel);
		}
	}

	for (const FName PreviouslyLoadedLevel : PreviousLoaded)
	{
		if (!LoadedLevels.Contains(PreviouslyLoadedLevel))
		{
			UnloadedNowSet.Add(PreviouslyLoadedLevel);
		}
	}

	if (LoadedNowSet.Num() > 0 || UnloadedNowSet.Num() > 0)
	{
		const TArray<FName> LoadedNow = BuildSortedArray(LoadedNowSet);
		const TArray<FName> UnloadedNow = BuildSortedArray(UnloadedNowSet);
		OnStreamingStateChanged.Broadcast(LoadedNow, UnloadedNow);
	}

	for (const FName PendingLoad : BuildSortedArray(PendingLoads))
	{
		if (IsLevelReady(PendingLoad, bCurrentZoneRequiresVisibility))
		{
			PendingLoads.Remove(PendingLoad);
		}
	}

	for (const FName PendingUnload : BuildSortedArray(PendingUnloads))
	{
		if (!IsLevelLoaded(PendingUnload))
		{
			PendingUnloads.Remove(PendingUnload);
		}
	}

	if (bCurrentZoneReadyPending && PendingLoads.Num() == 0 && PendingUnloads.Num() == 0 && AreDesiredLevelsReady())
	{
		bCurrentZoneReadyPending = false;
		StopStreamingTick();
		OnZoneReady.Broadcast(CurrentZoneId);
	}
	else if (PendingLoads.Num() == 0 && PendingUnloads.Num() == 0 && !bCurrentZoneReadyPending)
	{
		StopStreamingTick();
	}
}

bool UAeyerjiStreamingSubsystem::HandleStreamingTick(float DeltaTime)
{
	(void)DeltaTime;
	EvaluateStreamingState();

	const bool bKeepTicking = (PendingLoads.Num() > 0) || (PendingUnloads.Num() > 0) || bCurrentZoneReadyPending;
	if (!bKeepTicking)
	{
		StreamingTickHandle.Reset();
	}

	return bKeepTicking;
}

void UAeyerjiStreamingSubsystem::StartStreamingTick()
{
	if (StreamingTickHandle.IsValid())
	{
		return;
	}

	StreamingTickHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateUObject(this, &UAeyerjiStreamingSubsystem::HandleStreamingTick),
		StreamingTickIntervalSeconds);
}

void UAeyerjiStreamingSubsystem::StopStreamingTick()
{
	if (!StreamingTickHandle.IsValid())
	{
		return;
	}

	FTSTicker::GetCoreTicker().RemoveTicker(StreamingTickHandle);
	StreamingTickHandle.Reset();
}

bool UAeyerjiStreamingSubsystem::ShouldDriveStreamingInCurrentWorld() const
{
	const UWorld* World = GetRuntimeWorld();
	if (!World)
	{
		return false;
	}

	if (!bAllowClientSideStreaming && World->GetNetMode() == NM_Client)
	{
		return false;
	}

	return true;
}

void UAeyerjiStreamingSubsystem::MarkStateDirtyAndMaybeSave()
{
	if (bAutoSavePersistentState)
	{
		SavePersistentState();
	}
}
