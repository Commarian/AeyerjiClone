#include "Systems/AeyerjiSaveManagerSubsystem.h"

#include "Aeyerji/AeyerjiPlayerState.h"
#include "Aeyerji/AeyerjiSaveGame.h"
#include "CharacterStatsLibrary.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Interfaces/OnlineUserCloudInterface.h"
#include "Items/InventoryComponent.h"
#include "Kismet/GameplayStatics.h"
#include "OnlineSubsystem.h"
#include "Systems/AeyerjiDifficultyTuning.h"
#include "Systems/AeyerjiRiftRules.h"
#include "Systems/AeyerjiStreamingSaveGame.h"
#include "Systems/AeyerjiWorldStateSaveGame.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Paths.h"

namespace
{
	constexpr int32 AeyerjiSaveSchemaVersion = 2;
	const TCHAR* LegacyStreamingSlotName = TEXT("AeyerjiStreamingState");
	const TCHAR* SharedWorldStateOwnerKey = TEXT("SharedWorld");
	const TCHAR* SharedWorldStateSlotName = TEXT("AeyerjiWorldState");

	FString GetLocalDevOwnerKey()
	{
		static FString CachedToken;
		if (!CachedToken.IsEmpty())
		{
			return CachedToken;
		}

		const TArray<FString> Candidates = {
			FPlatformMisc::GetLoginId(),
			FPlatformMisc::GetDeviceId(),
			FPlatformProcess::ComputerName()
		};

		for (const FString& Candidate : Candidates)
		{
			const FString Safe = UCharacterStatsLibrary::SanitizeSaveSlotName(Candidate);
			if (!Safe.IsEmpty())
			{
				CachedToken = Safe;
				return CachedToken;
			}
		}

		CachedToken = TEXT("LocalDev");
		return CachedToken;
	}

	bool IsNetIdUsable(const FUniqueNetIdRepl& NetId)
	{
		return NetId.IsValid()
			&& NetId.GetUniqueNetId().IsValid()
			&& !NetId.GetUniqueNetId()->GetType().IsEqual(FName("NULL"), ENameCase::IgnoreCase);
	}

	bool IsFirstSaveNewer(const int64 FirstRevision, const FDateTime& FirstModifiedUtc, const int64 SecondRevision, const FDateTime& SecondModifiedUtc)
	{
		if (FirstRevision != SecondRevision)
		{
			return FirstRevision > SecondRevision;
		}

		if (FirstModifiedUtc != SecondModifiedUtc)
		{
			return FirstModifiedUtc > SecondModifiedUtc;
		}

		return true;
	}

	int32 CountResolvedActionBarClasses(const UAeyerjiSaveGame* SaveData)
	{
		if (!SaveData)
		{
			return 0;
		}

		int32 Count = 0;
		for (const FAeyerjiAbilitySlot& Slot : SaveData->ActionBar)
		{
			FAeyerjiAbilitySlot ResolvedSlot = Slot;
			ResolvedSlot.ResolveSavedReferences();
			if (ResolvedSlot.Class)
			{
				++Count;
			}
		}

		return Count;
	}

	int32 CountInventoryPersistenceEntries(const UAeyerjiSaveGame* SaveData)
	{
		if (!SaveData)
		{
			return 0;
		}

		return SaveData->Inventory.ItemSnapshots.Num()
			+ SaveData->Inventory.EquippedItems.Num()
			+ SaveData->Inventory.GridPlacements.Num();
	}

	int32 SanitizeProfileInventoryAttributes(UAeyerjiSaveGame* SaveData, const TCHAR* Phase)
	{
		if (!SaveData)
		{
			return 0;
		}

		const int32 RemovedCount = UAeyerjiInventoryComponent::SanitizeSaveDataAttributes(SaveData->Inventory);
		if (RemovedCount > 0)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[ProfileCommit] Phase=%s PrunedInvalidItemStatAttributes=%d Owner=%s Revision=%lld"),
				Phase ? Phase : TEXT("Unknown"),
				RemovedCount,
				*SaveData->OwnerKey,
				SaveData->Revision);
		}
		return RemovedCount;
	}

	bool ShouldPreferProfile(const UAeyerjiSaveGame* Candidate, const UAeyerjiSaveGame* Current)
	{
		if (!Candidate)
		{
			return false;
		}

		if (!Current)
		{
			return true;
		}

		const int32 CandidateClasses = CountResolvedActionBarClasses(Candidate);
		const int32 CurrentClasses = CountResolvedActionBarClasses(Current);
		if (CandidateClasses != CurrentClasses)
		{
			return CandidateClasses > CurrentClasses;
		}

		const int32 CandidateInventoryEntries = CountInventoryPersistenceEntries(Candidate);
		const int32 CurrentInventoryEntries = CountInventoryPersistenceEntries(Current);
		if (CandidateInventoryEntries != CurrentInventoryEntries)
		{
			return CandidateInventoryEntries > CurrentInventoryEntries;
		}

		return IsFirstSaveNewer(Candidate->Revision, Candidate->LastModifiedUtc, Current->Revision, Current->LastModifiedUtc);
	}

	FString GetSanitizedExplicitSaveSlotOverride(const FAeyerjiSaveTransportHeader& Header)
	{
		return UCharacterStatsLibrary::SanitizeSaveSlotName(Header.ExplicitSaveSlotOverride);
	}

	FString GetSanitizedPlayerStateSaveSlotOverride(const APlayerState* PlayerState)
	{
		if (const AAeyerjiPlayerState* AeyerjiPS = Cast<AAeyerjiPlayerState>(PlayerState))
		{
			return UCharacterStatsLibrary::SanitizeSaveSlotName(AeyerjiPS->GetSaveSlotOverride());
		}

		return FString();
	}
}

struct UAeyerjiSaveManagerSubsystem::FPendingProfileResolve
{
	FString OwnerKey;
	FString LocalSlotName;
	FString CloudFileName;
	FUniqueNetIdRepl UserId;
	IOnlineUserCloudPtr UserCloud;
	TStrongObjectPtr<UAeyerjiSaveGame> LocalSave;
	TStrongObjectPtr<UAeyerjiSaveGame> CloudSave;
	bool bHadLocalPersistedData = false;
	bool bHadCloudPersistedData = false;
	bool bLocalManagerEra = false;
	bool bCloudManagerEra = false;
	TArray<FAeyerjiOnProfileResolved> Callbacks;
	FDelegateHandle EnumerateHandle;
	FDelegateHandle ReadHandle;
};

UAeyerjiSaveManagerSubsystem* UAeyerjiSaveManagerSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	if (const UWorld* World = WorldContextObject->GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			return GameInstance->GetSubsystem<UAeyerjiSaveManagerSubsystem>();
		}
	}

	return nullptr;
}

UAeyerjiSaveManagerSubsystem::~UAeyerjiSaveManagerSubsystem()
{
	delete PendingProfileResolve;
	PendingProfileResolve = nullptr;
}

void UAeyerjiSaveManagerSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
}

void UAeyerjiSaveManagerSubsystem::Deinitialize()
{
	ClearPendingResolveDelegates();

	if (CloudWriteDelegateHandle.IsValid())
	{
		FUniqueNetIdRepl UserId;
		IOnlineUserCloudPtr UserCloud;
		if (ResolveSteamCloudContext(UserId, UserCloud) && UserCloud.IsValid())
		{
			UserCloud->ClearOnWriteUserFileCompleteDelegate_Handle(CloudWriteDelegateHandle);
		}

		CloudWriteDelegateHandle.Reset();
	}

	delete PendingProfileResolve;
	PendingProfileResolve = nullptr;
	LocalProfileCache.Reset();
	LocalStreamingCache.Reset();
	WorldStateCache = nullptr;
	ServerProfileCache.Reset();

	Super::Deinitialize();
}

void UAeyerjiSaveManagerSubsystem::ResolveProfileForLocalOwner(const FAeyerjiOnProfileResolved& Callback, const APlayerState* PreferredPlayerState)
{
	const FString OwnerKey = ResolveOwnerKey(PreferredPlayerState);
	if (OwnerKey.IsEmpty())
	{
		Callback.ExecuteIfBound(false, false, nullptr);
		return;
	}

	if (PendingProfileResolve && PendingProfileResolve->OwnerKey == OwnerKey)
	{
		PendingProfileResolve->Callbacks.Add(Callback);
		return;
	}

	if (PendingProfileResolve)
	{
		ClearPendingResolveDelegates();
		delete PendingProfileResolve;
		PendingProfileResolve = nullptr;
	}

	PendingProfileResolve = new FPendingProfileResolve();
	FPendingProfileResolve& Pending = *PendingProfileResolve;
	Pending.OwnerKey = OwnerKey;
	Pending.LocalSlotName = MakeProfileSlotNameForOwner(OwnerKey, PreferredPlayerState);
	Pending.CloudFileName = MakeCloudFilename(EAeyerjiSaveArtifactKind::Profile, OwnerKey);
	Pending.Callbacks.Add(Callback);

	bool bLoadedExisting = false;
	if (UAeyerjiSaveGame* LocalData = LoadProfileFromLocalSlot(Pending.LocalSlotName, bLoadedExisting))
	{
		Pending.LocalSave.Reset(DuplicateObject<UAeyerjiSaveGame>(LocalData, this));
		Pending.bHadLocalPersistedData = bLoadedExisting;
		Pending.bLocalManagerEra = IsManagerEraProfileForOwner(LocalData, OwnerKey);
	}

	const FString DefaultOwnerSlotName = MakeProfileSlotNameForOwner(OwnerKey);
	if (DefaultOwnerSlotName != Pending.LocalSlotName)
	{
		bool bLoadedMismatchedOwnerSlot = false;
		if (UAeyerjiSaveGame* MismatchedOwnerSlotData = LoadProfileFromLocalSlot(DefaultOwnerSlotName, bLoadedMismatchedOwnerSlot))
		{
			if (ShouldPreferProfile(MismatchedOwnerSlotData, Pending.LocalSave.Get()))
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[ProfileLoad] Repair=PreferMismatchedOwnerSlot Owner=%s PreferredSlot=%s MismatchedSlot=%s PreferredClasses=%d MismatchedClasses=%d PreferredInventory=%d MismatchedInventory=%d PreferredRevision=%lld MismatchedRevision=%lld"),
					*OwnerKey,
					*Pending.LocalSlotName,
					*DefaultOwnerSlotName,
					CountResolvedActionBarClasses(Pending.LocalSave.Get()),
					CountResolvedActionBarClasses(MismatchedOwnerSlotData),
					CountInventoryPersistenceEntries(Pending.LocalSave.Get()),
					CountInventoryPersistenceEntries(MismatchedOwnerSlotData),
					Pending.LocalSave.IsValid() ? Pending.LocalSave->Revision : 0,
					MismatchedOwnerSlotData->Revision);
				Pending.LocalSave.Reset(DuplicateObject<UAeyerjiSaveGame>(MismatchedOwnerSlotData, this));
				Pending.bHadLocalPersistedData = bLoadedMismatchedOwnerSlot;
				Pending.bLocalManagerEra = IsManagerEraProfileForOwner(MismatchedOwnerSlotData, OwnerKey);
			}
		}
	}

	FUniqueNetIdRepl UserId;
	IOnlineUserCloudPtr UserCloud;
	if (!ResolveSteamCloudContext(UserId, UserCloud) || !UserCloud.IsValid())
	{
		FinalizePendingProfileResolve(false);
		return;
	}

	Pending.UserId = UserId;
	Pending.UserCloud = UserCloud;
	Pending.EnumerateHandle = UserCloud->AddOnEnumerateUserFilesCompleteDelegate_Handle(
		FOnEnumerateUserFilesCompleteDelegate::CreateUObject(this, &UAeyerjiSaveManagerSubsystem::HandleEnumerateUserFilesComplete));
	UserCloud->EnumerateUserFiles(*UserId.GetUniqueNetId());
}

bool UAeyerjiSaveManagerSubsystem::GetCachedOrLocalProfileForOwner(UAeyerjiSaveGame*& OutSaveData, const APlayerState* PreferredPlayerState)
{
	OutSaveData = nullptr;

	const FString OwnerKey = ResolveOwnerKey(PreferredPlayerState);
	if (OwnerKey.IsEmpty())
	{
		return false;
	}

	if (TObjectPtr<UAeyerjiSaveGame>* Cached = LocalProfileCache.Find(OwnerKey))
	{
		OutSaveData = *Cached;
		return OutSaveData != nullptr;
	}

	const FString SlotName = MakeProfileSlotNameForOwner(OwnerKey, PreferredPlayerState);
	bool bLoadedExisting = false;
	UAeyerjiSaveGame* SaveData = LoadProfileFromLocalSlot(SlotName, bLoadedExisting);
	if (!SaveData)
	{
		SaveData = CreateDefaultProfile(OwnerKey, 1);
	}
	else if (!IsManagerEraProfileForOwner(SaveData, OwnerKey))
	{
		// Keep legacy payloads readable in-memory until the first authoritative commit upgrades them.
		SaveData->SchemaVersion = 0;
		SaveData->OwnerKey = OwnerKey;
		SaveData->ArtifactKind = EAeyerjiSaveArtifactKind::Profile;
	}

	OutSaveData = DuplicateObject<UAeyerjiSaveGame>(SaveData, this);
	LocalProfileCache.Add(OwnerKey, OutSaveData);
	return OutSaveData != nullptr;
}

bool UAeyerjiSaveManagerSubsystem::CommitResolvedProfileForLocalOwner(const FAeyerjiSaveTransportHeader& Header, const TArray<uint8>& Bytes)
{
	return CommitResolvedProfileForLocalOwner(Header, Bytes, nullptr);
}

bool UAeyerjiSaveManagerSubsystem::CommitResolvedProfileForLocalOwner(const FAeyerjiSaveTransportHeader& Header, const TArray<uint8>& Bytes, const APlayerState* PreferredPlayerState)
{
	if (Header.ArtifactKind != EAeyerjiSaveArtifactKind::Profile)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ProfileCommit] Phase=LocalResolve Result=0 Reason=WrongArtifact Artifact=%d Owner=%s"),
			static_cast<int32>(Header.ArtifactKind),
			*Header.OwnerKey);
		return false;
	}

	UAeyerjiSaveGame* SaveData = DeserializeProfileFromTransport(Header, Bytes);
	if (!SaveData)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ProfileCommit] Phase=LocalResolve Result=0 Reason=DeserializeFailed Owner=%s PayloadBytes=%d"),
			*Header.OwnerKey,
			Bytes.Num());
		return false;
	}

	const FString ExplicitSlotOverride = GetSanitizedExplicitSaveSlotOverride(Header);
	const FString OwnerKey = !ExplicitSlotOverride.IsEmpty()
		? ExplicitSlotOverride
		: (Header.OwnerKey.IsEmpty() ? ResolveOwnerKey(PreferredPlayerState) : Header.OwnerKey);
	const FString SlotName = !ExplicitSlotOverride.IsEmpty()
		? ExplicitSlotOverride
		: MakeProfileSlotNameForOwner(OwnerKey, PreferredPlayerState);

	SaveData->SchemaVersion = FMath::Max(Header.SchemaVersion, AeyerjiSaveSchemaVersion);
	SaveData->Revision = Header.Revision;
	SaveData->LastModifiedUtc = Header.LastModifiedUtc;
	SaveData->OwnerKey = OwnerKey;
	SaveData->ArtifactKind = EAeyerjiSaveArtifactKind::Profile;

	if (!SaveProfileToLocalSlot(SaveData, SlotName))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ProfileCommit] Phase=LocalWrite Result=0 Owner=%s Slot=%s Revision=%lld PayloadBytes=%d ActionBar=%d ResolvedClasses=%d"),
			*OwnerKey,
			*SlotName,
			SaveData->Revision,
			Bytes.Num(),
			SaveData->ActionBar.Num(),
			CountResolvedActionBarClasses(SaveData));
		return false;
	}

	LocalProfileCache.Add(OwnerKey, DuplicateObject<UAeyerjiSaveGame>(SaveData, this));
	UE_LOG(LogTemp, Display,
		TEXT("[ProfileCommit] Phase=LocalWrite Result=1 Owner=%s Slot=%s ExplicitSlot=%s Revision=%lld PayloadBytes=%d ActionBar=%d ResolvedClasses=%d"),
		*OwnerKey,
		*SlotName,
		*ExplicitSlotOverride,
		SaveData->Revision,
		Bytes.Num(),
		SaveData->ActionBar.Num(),
		CountResolvedActionBarClasses(SaveData));
	MirrorProfileToCloud(SaveData, OwnerKey);
	OnProfileChanged.Broadcast(OwnerKey, SaveData->Revision);
	return true;
}

UAeyerjiStreamingSaveGame* UAeyerjiSaveManagerSubsystem::ResolveStreamingStateForOwner(const APlayerState* PreferredPlayerState)
{
	const FString OwnerKey = ResolveOwnerKey(PreferredPlayerState);
	if (OwnerKey.IsEmpty())
	{
		return nullptr;
	}

	if (TObjectPtr<UAeyerjiStreamingSaveGame>* Cached = LocalStreamingCache.Find(OwnerKey))
	{
		return *Cached;
	}

	const FString SlotName = MakeStreamingSlotNameForOwner(OwnerKey);
	bool bLoadedExisting = false;
	UAeyerjiStreamingSaveGame* SaveData = LoadStreamingFromLocalSlot(SlotName, bLoadedExisting);
	bool bNeedsImmediateUpgrade = false;

	if (!SaveData)
	{
		bool bLoadedLegacy = false;
		SaveData = LoadStreamingFromLocalSlot(LegacyStreamingSlotName, bLoadedLegacy);
		bNeedsImmediateUpgrade = bLoadedLegacy && SaveData != nullptr;
	}

	if (!SaveData)
	{
		SaveData = Cast<UAeyerjiStreamingSaveGame>(UGameplayStatics::CreateSaveGameObject(UAeyerjiStreamingSaveGame::StaticClass()));
	}

	if (!SaveData)
	{
		return nullptr;
	}

	if (bNeedsImmediateUpgrade || !IsManagerEraStreamingForOwner(SaveData, OwnerKey))
	{
		StampStreamingMetadata(SaveData, OwnerKey, true);
		SaveStreamingToLocalSlot(SaveData, SlotName);
		MirrorStreamingToCloud(SaveData, OwnerKey);
	}

	UAeyerjiStreamingSaveGame* CachedCopy = DuplicateObject<UAeyerjiStreamingSaveGame>(SaveData, this);
	LocalStreamingCache.Add(OwnerKey, CachedCopy);
	return CachedCopy;
}

bool UAeyerjiSaveManagerSubsystem::CommitStreamingStateForOwner(UAeyerjiStreamingSaveGame* SaveData, const APlayerState* PreferredPlayerState)
{
	if (!SaveData)
	{
		return false;
	}

	const FString OwnerKey = ResolveOwnerKey(PreferredPlayerState);
	if (OwnerKey.IsEmpty())
	{
		return false;
	}

	StampStreamingMetadata(SaveData, OwnerKey, true);

	if (!SaveStreamingToLocalSlot(SaveData, MakeStreamingSlotNameForOwner(OwnerKey)))
	{
		return false;
	}

	MirrorStreamingToCloud(SaveData, OwnerKey);
	return true;
}

UAeyerjiWorldStateSaveGame* UAeyerjiSaveManagerSubsystem::ResolveWorldState()
{
	if (WorldStateCache)
	{
		return WorldStateCache;
	}

	bool bLoadedExisting = false;
	UAeyerjiWorldStateSaveGame* SaveData = LoadWorldStateFromLocalSlot(MakeWorldStateSlotName(), bLoadedExisting);
	if (!SaveData)
	{
		SaveData = Cast<UAeyerjiWorldStateSaveGame>(UGameplayStatics::CreateSaveGameObject(UAeyerjiWorldStateSaveGame::StaticClass()));
	}

	if (!SaveData)
	{
		return nullptr;
	}

	if (!IsManagerEraWorldState(SaveData))
	{
		StampWorldStateMetadata(SaveData, bLoadedExisting);
	}

	WorldStateCache = DuplicateObject<UAeyerjiWorldStateSaveGame>(SaveData, this);
	return WorldStateCache;
}

bool UAeyerjiSaveManagerSubsystem::CommitWorldState(UAeyerjiWorldStateSaveGame* SaveData)
{
	if (!SaveData)
	{
		return false;
	}

	StampWorldStateMetadata(SaveData, true);
	return SaveWorldStateToLocalSlot(SaveData, MakeWorldStateSlotName());
}

bool UAeyerjiSaveManagerSubsystem::MutateCachedProfileDifficulty(const float DifficultySlider, const int32 WorldTier, const APlayerState* PreferredPlayerState)
{
	UAeyerjiSaveGame* SaveData = nullptr;
	if (!GetCachedOrLocalProfileForOwner(SaveData, PreferredPlayerState) || !SaveData)
	{
		return false;
	}

	const FString OwnerKey = ResolveOwnerKey(PreferredPlayerState);
	SaveData->DifficultySlider = DifficultySlider;
	SaveData->bHasDifficultySelection = true;
	SaveData->WorldTier = WorldTier;
	SaveData->bHasWorldTierSelection = true;
	StampProfileMetadata(SaveData, OwnerKey, true);

	if (!SaveProfileToLocalSlot(SaveData, MakeProfileSlotNameForOwner(OwnerKey, PreferredPlayerState)))
	{
		return false;
	}

	MirrorProfileToCloud(SaveData, OwnerKey);
	OnProfileChanged.Broadcast(OwnerKey, SaveData->Revision);
	return true;
}

bool UAeyerjiSaveManagerSubsystem::GetServerCachedProfile(const APlayerState* PlayerState, UAeyerjiSaveGame*& OutSaveData) const
{
	OutSaveData = nullptr;

	if (!PlayerState)
	{
		return false;
	}

	const FString OwnerKey = ResolveOwnerKey(PlayerState);
	if (const TObjectPtr<UAeyerjiSaveGame>* Cached = ServerProfileCache.Find(OwnerKey))
	{
		OutSaveData = *Cached;
	}

	return OutSaveData != nullptr;
}

UAeyerjiSaveGame* UAeyerjiSaveManagerSubsystem::CreateDefaultProfile(const FString& OwnerKey, const int32 InitialLevel) const
{
	UAeyerjiSaveGame* SaveData = Cast<UAeyerjiSaveGame>(UGameplayStatics::CreateSaveGameObject(UAeyerjiSaveGame::StaticClass()));
	if (!SaveData)
	{
		return nullptr;
	}

	SaveData->ActionBar.Reset();
	SaveData->Attributes = FAttrSnapshot();
	SaveData->Attributes.Level = UAeyerjiDifficultySettings::ClampGameplayLevel(InitialLevel);
	SaveData->Inventory = FAeyerjiInventorySaveData();
	SaveData->SelectedPassiveId = NAME_None;
	SaveData->AbilityProgressEntries.Reset();
	SaveData->UnspentAbilityPoints = 0;
	SaveData->TotalAbilityPointSpends = 0;
	SaveData->Gold = 0;
	SaveData->DifficultySlider = 0.f;
	SaveData->bHasDifficultySelection = false;
	SaveData->WorldTier = 0;
	SaveData->bHasWorldTierSelection = false;
	SaveData->HighestUnlockedRiftTier = 1;
	SaveData->LastSelectedRiftTier = 1;
	SaveData->BestRunTimeSecondsByDifficulty.Reset();
	SaveData->RecentRuns.Reset();
	SaveData->SchemaVersion = AeyerjiSaveSchemaVersion;
	SaveData->Revision = 0;
	SaveData->LastModifiedUtc = FDateTime::MinValue();
	SaveData->OwnerKey = OwnerKey;
	SaveData->ArtifactKind = EAeyerjiSaveArtifactKind::Profile;
	return SaveData;
}

UAeyerjiSaveGame* UAeyerjiSaveManagerSubsystem::DeserializeProfileFromTransport(const FAeyerjiSaveTransportHeader& Header, const TArray<uint8>& Bytes) const
{
	if (Header.ArtifactKind != EAeyerjiSaveArtifactKind::Profile || Bytes.Num() <= 0)
	{
		return nullptr;
	}

	USaveGame* RawSave = UGameplayStatics::LoadGameFromMemory(Bytes);
	UAeyerjiSaveGame* SaveData = Cast<UAeyerjiSaveGame>(RawSave);
	if (!SaveData)
	{
		return nullptr;
	}

	SaveData->SchemaVersion = FMath::Max(Header.SchemaVersion, SaveData->SchemaVersion);
	SaveData->Revision = Header.Revision;
	SaveData->LastModifiedUtc = Header.LastModifiedUtc;
	const FString ExplicitSlotOverride = GetSanitizedExplicitSaveSlotOverride(Header);
	SaveData->OwnerKey = !ExplicitSlotOverride.IsEmpty() ? ExplicitSlotOverride : Header.OwnerKey;
	SaveData->ArtifactKind = EAeyerjiSaveArtifactKind::Profile;
	AeyerjiRiftRules::NormalizeProfileTiers(
		SaveData->HighestUnlockedRiftTier, SaveData->LastSelectedRiftTier);
	SanitizeProfileInventoryAttributes(SaveData, TEXT("Deserialize"));
	return SaveData;
}

bool UAeyerjiSaveManagerSubsystem::BuildTransportFromProfile(const UAeyerjiSaveGame* SaveData, FAeyerjiSaveTransportHeader& OutHeader, TArray<uint8>& OutBytes) const
{
	OutBytes.Reset();
	OutHeader = FAeyerjiSaveTransportHeader();

	if (!SaveData)
	{
		return false;
	}

	OutHeader.ArtifactKind = EAeyerjiSaveArtifactKind::Profile;
	OutHeader.OwnerKey = SaveData->OwnerKey;
	OutHeader.SchemaVersion = SaveData->SchemaVersion;
	OutHeader.Revision = SaveData->Revision;
	OutHeader.LastModifiedUtc = SaveData->LastModifiedUtc;
	OutHeader.bHadPersistedData = true;

	UAeyerjiSaveGame* MutableSaveData = const_cast<UAeyerjiSaveGame*>(SaveData);
	SanitizeProfileInventoryAttributes(MutableSaveData, TEXT("BuildTransport"));
	return UGameplayStatics::SaveGameToMemory(MutableSaveData, OutBytes);
}

bool UAeyerjiSaveManagerSubsystem::PrepareProfileForServerCommit(APlayerState* PlayerState, UAeyerjiSaveGame* SaveData, const bool bBumpRevision, FAeyerjiSaveTransportHeader& OutHeader, TArray<uint8>& OutBytes)
{
	if (!PlayerState || !SaveData)
	{
		return false;
	}

	const FString OwnerKey = ResolveOwnerKey(PlayerState);
	StampProfileMetadata(SaveData, OwnerKey, bBumpRevision);
	SanitizeProfileInventoryAttributes(SaveData, TEXT("PrepareServerCommit"));

	ServerProfileCache.Add(OwnerKey, DuplicateObject<UAeyerjiSaveGame>(SaveData, this));
	const bool bBuilt = BuildTransportFromProfile(SaveData, OutHeader, OutBytes);
	OutHeader.ExplicitSaveSlotOverride = GetSanitizedPlayerStateSaveSlotOverride(PlayerState);
	return bBuilt;
}

bool UAeyerjiSaveManagerSubsystem::IsManagerEraProfile(const UAeyerjiSaveGame* SaveData) const
{
	return SaveData
		&& SaveData->SchemaVersion >= AeyerjiSaveSchemaVersion
		&& SaveData->ArtifactKind == EAeyerjiSaveArtifactKind::Profile
		&& !SaveData->OwnerKey.IsEmpty();
}

FString UAeyerjiSaveManagerSubsystem::ResolveOwnerKey(const APlayerState* PreferredPlayerState) const
{
	if (const AAeyerjiPlayerState* AeyerjiPS = Cast<AAeyerjiPlayerState>(PreferredPlayerState))
	{
		const FString& OverrideSlot = AeyerjiPS->GetSaveSlotOverride();
		if (!OverrideSlot.IsEmpty())
		{
			return OverrideSlot;
		}
	}

	if (PreferredPlayerState)
	{
		const FUniqueNetIdRepl& NetId = PreferredPlayerState->GetUniqueId();
		if (IsNetIdUsable(NetId))
		{
			const FString SafeNetId = UCharacterStatsLibrary::SanitizeSaveSlotName(NetId.GetUniqueNetId()->ToString());
			if (!SafeNetId.IsEmpty())
			{
				return SafeNetId;
			}
		}

		const FString FallbackName = UCharacterStatsLibrary::SanitizeSaveSlotName(PreferredPlayerState->GetPlayerName());
		if (!FallbackName.IsEmpty())
		{
			return FallbackName;
		}

		return FString::Printf(TEXT("Player%d"), FMath::Max(0, PreferredPlayerState->GetPlayerId()));
	}

	if (const UGameInstance* GameInstance = GetGameInstance())
	{
		if (const ULocalPlayer* LocalPlayer = GameInstance->GetFirstGamePlayer())
		{
			const FUniqueNetIdRepl NetId = LocalPlayer->GetPreferredUniqueNetId();
			if (IsNetIdUsable(NetId))
			{
				const FString SafeNetId = UCharacterStatsLibrary::SanitizeSaveSlotName(NetId.GetUniqueNetId()->ToString());
				if (!SafeNetId.IsEmpty())
				{
					return SafeNetId;
				}
			}
		}
	}

	return GetLocalDevOwnerKey();
}

FString UAeyerjiSaveManagerSubsystem::MakeProfileSlotNameForOwner(const FString& OwnerKey, const APlayerState* PreferredPlayerState) const
{
	if (const AAeyerjiPlayerState* AeyerjiPS = Cast<AAeyerjiPlayerState>(PreferredPlayerState))
	{
		const FString& OverrideSlot = AeyerjiPS->GetSaveSlotOverride();
		if (!OverrideSlot.IsEmpty())
		{
			return OverrideSlot;
		}
	}

	return OwnerKey + TEXT("_Char");
}

FString UAeyerjiSaveManagerSubsystem::MakeStreamingSlotNameForOwner(const FString& OwnerKey) const
{
	return OwnerKey + TEXT("_Streaming");
}

FString UAeyerjiSaveManagerSubsystem::MakeWorldStateSlotName() const
{
	return SharedWorldStateSlotName;
}

UAeyerjiSaveGame* UAeyerjiSaveManagerSubsystem::LoadProfileFromLocalSlot(const FString& SlotName, bool& bOutLoadedExisting) const
{
	bOutLoadedExisting = false;

	if (SlotName.IsEmpty() || !UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		return nullptr;
	}

	USaveGame* RawSave = UGameplayStatics::LoadGameFromSlot(SlotName, 0);
	UAeyerjiSaveGame* SaveData = Cast<UAeyerjiSaveGame>(RawSave);
	if (SaveData)
	{
		bOutLoadedExisting = true;
		SanitizeProfileInventoryAttributes(SaveData, TEXT("LoadSlot"));
	}
	return SaveData;
}

UAeyerjiStreamingSaveGame* UAeyerjiSaveManagerSubsystem::LoadStreamingFromLocalSlot(const FString& SlotName, bool& bOutLoadedExisting) const
{
	bOutLoadedExisting = false;

	if (SlotName.IsEmpty() || !UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		return nullptr;
	}

	USaveGame* RawSave = UGameplayStatics::LoadGameFromSlot(SlotName, 0);
	UAeyerjiStreamingSaveGame* SaveData = Cast<UAeyerjiStreamingSaveGame>(RawSave);
	if (SaveData)
	{
		bOutLoadedExisting = true;
	}
	return SaveData;
}

UAeyerjiWorldStateSaveGame* UAeyerjiSaveManagerSubsystem::LoadWorldStateFromLocalSlot(const FString& SlotName, bool& bOutLoadedExisting) const
{
	bOutLoadedExisting = false;

	if (SlotName.IsEmpty() || !UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		return nullptr;
	}

	USaveGame* RawSave = UGameplayStatics::LoadGameFromSlot(SlotName, 0);
	UAeyerjiWorldStateSaveGame* SaveData = Cast<UAeyerjiWorldStateSaveGame>(RawSave);
	if (SaveData)
	{
		bOutLoadedExisting = true;
	}
	return SaveData;
}

bool UAeyerjiSaveManagerSubsystem::SaveProfileToLocalSlot(UAeyerjiSaveGame* SaveData, const FString& SlotName)
{
	if (!SaveData || SlotName.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ProfileCommit] Phase=SaveSlot Result=0 Reason=InvalidInput Slot=%s SaveData=%s"),
			*SlotName,
			*GetNameSafe(SaveData));
		return false;
	}

	SanitizeProfileInventoryAttributes(SaveData, TEXT("SaveSlot"));

	if (!UGameplayStatics::SaveGameToSlot(SaveData, SlotName, 0))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ProfileCommit] Phase=SaveSlot Result=0 Reason=SaveGameToSlotFailed Slot=%s Owner=%s Revision=%lld ActionBar=%d ResolvedClasses=%d"),
			*SlotName,
			*SaveData->OwnerKey,
			SaveData->Revision,
			SaveData->ActionBar.Num(),
			CountResolvedActionBarClasses(SaveData));
		return false;
	}

	LocalProfileCache.Add(SaveData->OwnerKey, DuplicateObject<UAeyerjiSaveGame>(SaveData, this));
	UE_LOG(LogTemp, Display,
		TEXT("[ProfileCommit] Phase=SaveSlot Result=1 Slot=%s Owner=%s Revision=%lld ActionBar=%d ResolvedClasses=%d"),
		*SlotName,
		*SaveData->OwnerKey,
		SaveData->Revision,
		SaveData->ActionBar.Num(),
		CountResolvedActionBarClasses(SaveData));
	return true;
}

bool UAeyerjiSaveManagerSubsystem::SaveStreamingToLocalSlot(UAeyerjiStreamingSaveGame* SaveData, const FString& SlotName)
{
	if (!SaveData || SlotName.IsEmpty())
	{
		return false;
	}

	if (!UGameplayStatics::SaveGameToSlot(SaveData, SlotName, 0))
	{
		return false;
	}

	LocalStreamingCache.Add(SaveData->OwnerKey, DuplicateObject<UAeyerjiStreamingSaveGame>(SaveData, this));
	return true;
}

bool UAeyerjiSaveManagerSubsystem::SaveWorldStateToLocalSlot(UAeyerjiWorldStateSaveGame* SaveData, const FString& SlotName)
{
	if (!SaveData || SlotName.IsEmpty())
	{
		return false;
	}

	if (!UGameplayStatics::SaveGameToSlot(SaveData, SlotName, 0))
	{
		return false;
	}

	WorldStateCache = DuplicateObject<UAeyerjiWorldStateSaveGame>(SaveData, this);
	return true;
}

bool UAeyerjiSaveManagerSubsystem::ResolveSteamCloudContext(FUniqueNetIdRepl& OutUserId, TSharedPtr<IOnlineUserCloud, ESPMode::ThreadSafe>& OutUserCloud) const
{
	OutUserId = FUniqueNetIdRepl();
	OutUserCloud.Reset();

	const UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return false;
	}

	const ULocalPlayer* LocalPlayer = GameInstance->GetFirstGamePlayer();
	if (!LocalPlayer)
	{
		return false;
	}

	const FUniqueNetIdRepl PreferredNetId = LocalPlayer->GetPreferredUniqueNetId();
	if (!IsNetIdUsable(PreferredNetId))
	{
		return false;
	}

	IOnlineSubsystem* OnlineSubsystem = IOnlineSubsystem::Get(TEXT("STEAM"));
	if (!OnlineSubsystem)
	{
		OnlineSubsystem = IOnlineSubsystem::Get();
	}

	if (!OnlineSubsystem)
	{
		return false;
	}

	IOnlineUserCloudPtr UserCloud = OnlineSubsystem->GetUserCloudInterface();
	if (!UserCloud.IsValid())
	{
		return false;
	}

	OutUserId = PreferredNetId;
	OutUserCloud = UserCloud;
	return true;
}

FString UAeyerjiSaveManagerSubsystem::MakeCloudFilename(const EAeyerjiSaveArtifactKind ArtifactKind, const FString& OwnerKey)
{
	switch (ArtifactKind)
	{
	case EAeyerjiSaveArtifactKind::Profile:
		return FString::Printf(TEXT("Profile_%s.sav"), *OwnerKey);

	case EAeyerjiSaveArtifactKind::Streaming:
		return FString::Printf(TEXT("Streaming_%s.sav"), *OwnerKey);

	case EAeyerjiSaveArtifactKind::WorldState:
		return FString::Printf(TEXT("WorldState_%s.sav"), *OwnerKey);

	default:
		return FString::Printf(TEXT("Unknown_%s.sav"), *OwnerKey);
	}
}

bool UAeyerjiSaveManagerSubsystem::IsManagerEraProfileForOwner(const UAeyerjiSaveGame* SaveData, const FString& OwnerKey) const
{
	return IsManagerEraProfile(SaveData)
		&& SaveData->OwnerKey == OwnerKey;
}

bool UAeyerjiSaveManagerSubsystem::IsManagerEraStreamingForOwner(const UAeyerjiStreamingSaveGame* SaveData, const FString& OwnerKey) const
{
	return SaveData
		&& SaveData->SchemaVersion >= AeyerjiSaveSchemaVersion
		&& SaveData->ArtifactKind == EAeyerjiSaveArtifactKind::Streaming
		&& SaveData->OwnerKey == OwnerKey;
}

bool UAeyerjiSaveManagerSubsystem::IsManagerEraWorldState(const UAeyerjiWorldStateSaveGame* SaveData) const
{
	return SaveData
		&& SaveData->SchemaVersion >= AeyerjiSaveSchemaVersion
		&& SaveData->ArtifactKind == EAeyerjiSaveArtifactKind::WorldState
		&& SaveData->OwnerKey == SharedWorldStateOwnerKey;
}

void UAeyerjiSaveManagerSubsystem::StampProfileMetadata(UAeyerjiSaveGame* SaveData, const FString& OwnerKey, const bool bBumpRevision) const
{
	if (!SaveData)
	{
		return;
	}

	SaveData->SchemaVersion = AeyerjiSaveSchemaVersion;
	SaveData->OwnerKey = OwnerKey;
	SaveData->ArtifactKind = EAeyerjiSaveArtifactKind::Profile;

	if (bBumpRevision)
	{
		SaveData->Revision = FMath::Max<int64>(SaveData->Revision + 1, 1);
		SaveData->LastModifiedUtc = FDateTime::UtcNow();
	}
}

void UAeyerjiSaveManagerSubsystem::StampStreamingMetadata(UAeyerjiStreamingSaveGame* SaveData, const FString& OwnerKey, const bool bBumpRevision) const
{
	if (!SaveData)
	{
		return;
	}

	SaveData->SchemaVersion = AeyerjiSaveSchemaVersion;
	SaveData->OwnerKey = OwnerKey;
	SaveData->ArtifactKind = EAeyerjiSaveArtifactKind::Streaming;

	if (bBumpRevision)
	{
		SaveData->Revision = FMath::Max<int64>(SaveData->Revision + 1, 1);
		SaveData->LastModifiedUtc = FDateTime::UtcNow();
	}
}

void UAeyerjiSaveManagerSubsystem::StampWorldStateMetadata(UAeyerjiWorldStateSaveGame* SaveData, const bool bBumpRevision) const
{
	if (!SaveData)
	{
		return;
	}

	SaveData->SchemaVersion = AeyerjiSaveSchemaVersion;
	SaveData->OwnerKey = SharedWorldStateOwnerKey;
	SaveData->ArtifactKind = EAeyerjiSaveArtifactKind::WorldState;

	if (bBumpRevision)
	{
		SaveData->Revision = FMath::Max<int64>(SaveData->Revision + 1, 1);
		SaveData->LastModifiedUtc = FDateTime::UtcNow();
	}
}

void UAeyerjiSaveManagerSubsystem::FinalizePendingProfileResolve(const bool bCloudReadCompleted)
{
	if (!PendingProfileResolve)
	{
		return;
	}

	ClearPendingResolveDelegates();
	FPendingProfileResolve Pending = MoveTemp(*PendingProfileResolve);
	delete PendingProfileResolve;
	PendingProfileResolve = nullptr;

	UAeyerjiSaveGame* Winner = nullptr;
	bool bHadPersistedData = false;
	bool bRepairLocal = false;
	bool bRepairCloud = false;

	if (Pending.bLocalManagerEra && Pending.bCloudManagerEra)
	{
		Winner = IsFirstSaveNewer(Pending.LocalSave->Revision, Pending.LocalSave->LastModifiedUtc, Pending.CloudSave->Revision, Pending.CloudSave->LastModifiedUtc)
			? Pending.LocalSave.Get()
			: Pending.CloudSave.Get();
		bHadPersistedData = true;
		bRepairLocal = Winner == Pending.CloudSave.Get();
		bRepairCloud = Winner == Pending.LocalSave.Get() && bCloudReadCompleted;
	}
	else if (Pending.bLocalManagerEra)
	{
		Winner = Pending.LocalSave.Get();
		bHadPersistedData = true;
		bRepairCloud = bCloudReadCompleted;
	}
	else if (Pending.bCloudManagerEra)
	{
		Winner = Pending.CloudSave.Get();
		bHadPersistedData = true;
		bRepairLocal = true;
	}
	else if (Pending.bHadLocalPersistedData && Pending.LocalSave.IsValid())
	{
		Winner = Pending.LocalSave.Get();
		bHadPersistedData = true;
		StampProfileMetadata(Winner, Pending.OwnerKey, true);
		bRepairLocal = true;
		bRepairCloud = bCloudReadCompleted;
	}
	else if (Pending.bHadCloudPersistedData && Pending.CloudSave.IsValid())
	{
		Winner = Pending.CloudSave.Get();
		bHadPersistedData = true;
		StampProfileMetadata(Winner, Pending.OwnerKey, true);
		bRepairLocal = true;
		bRepairCloud = bCloudReadCompleted;
	}

	if (!Winner)
	{
		Winner = CreateDefaultProfile(Pending.OwnerKey, 1);
		bHadPersistedData = false;
	}
	SanitizeProfileInventoryAttributes(Winner, TEXT("FinalizeResolve"));

	UAeyerjiSaveGame* ResolvedSave = DuplicateObject<UAeyerjiSaveGame>(Winner, this);
	LocalProfileCache.Add(Pending.OwnerKey, ResolvedSave);

	if (bRepairLocal)
	{
		SaveProfileToLocalSlot(ResolvedSave, Pending.LocalSlotName);
	}

	if (bRepairCloud)
	{
		MirrorProfileToCloud(ResolvedSave, Pending.OwnerKey);
	}

	if (ResolvedSave)
	{
		OnProfileChanged.Broadcast(Pending.OwnerKey, ResolvedSave->Revision);
	}

	for (const FAeyerjiOnProfileResolved& Callback : Pending.Callbacks)
	{
		Callback.ExecuteIfBound(ResolvedSave != nullptr, bHadPersistedData, ResolvedSave);
	}
}

void UAeyerjiSaveManagerSubsystem::HandleEnumerateUserFilesComplete(const bool bWasSuccessful, const FUniqueNetId& UserId)
{
	if (!PendingProfileResolve || !PendingProfileResolve->UserCloud.IsValid() || !PendingProfileResolve->UserId.IsValid())
	{
		return;
	}

	if (*PendingProfileResolve->UserId.GetUniqueNetId() != UserId)
	{
		return;
	}

	PendingProfileResolve->UserCloud->ClearOnEnumerateUserFilesCompleteDelegate_Handle(PendingProfileResolve->EnumerateHandle);
	PendingProfileResolve->EnumerateHandle.Reset();

	if (!bWasSuccessful)
	{
		FinalizePendingProfileResolve(false);
		return;
	}

	TArray<FCloudFileHeader> UserFiles;
	PendingProfileResolve->UserCloud->GetUserFileList(UserId, UserFiles);

	bool bFoundCloudFile = false;
	for (const FCloudFileHeader& FileHeader : UserFiles)
	{
		if (FileHeader.FileName == PendingProfileResolve->CloudFileName)
		{
			bFoundCloudFile = true;
			break;
		}
	}

	if (!bFoundCloudFile)
	{
		FinalizePendingProfileResolve(true);
		return;
	}

	PendingProfileResolve->ReadHandle = PendingProfileResolve->UserCloud->AddOnReadUserFileCompleteDelegate_Handle(
		FOnReadUserFileCompleteDelegate::CreateUObject(this, &UAeyerjiSaveManagerSubsystem::HandleReadUserFileComplete));
	PendingProfileResolve->UserCloud->ReadUserFile(UserId, PendingProfileResolve->CloudFileName);
}

void UAeyerjiSaveManagerSubsystem::HandleReadUserFileComplete(const bool bWasSuccessful, const FUniqueNetId& UserId, const FString& FileName)
{
	if (!PendingProfileResolve || !PendingProfileResolve->UserCloud.IsValid() || !PendingProfileResolve->UserId.IsValid())
	{
		return;
	}

	if (*PendingProfileResolve->UserId.GetUniqueNetId() != UserId || FileName != PendingProfileResolve->CloudFileName)
	{
		return;
	}

	PendingProfileResolve->UserCloud->ClearOnReadUserFileCompleteDelegate_Handle(PendingProfileResolve->ReadHandle);
	PendingProfileResolve->ReadHandle.Reset();

	if (bWasSuccessful)
	{
		TArray<uint8> CloudBytes;
		if (PendingProfileResolve->UserCloud->GetFileContents(UserId, FileName, CloudBytes))
		{
			if (USaveGame* RawSave = UGameplayStatics::LoadGameFromMemory(CloudBytes))
			{
				if (UAeyerjiSaveGame* CloudSave = Cast<UAeyerjiSaveGame>(RawSave))
				{
					SanitizeProfileInventoryAttributes(CloudSave, TEXT("ReadCloud"));
					PendingProfileResolve->CloudSave.Reset(DuplicateObject<UAeyerjiSaveGame>(CloudSave, this));
					PendingProfileResolve->bHadCloudPersistedData = true;
					PendingProfileResolve->bCloudManagerEra = IsManagerEraProfileForOwner(CloudSave, PendingProfileResolve->OwnerKey);
				}
			}
		}
	}

	FinalizePendingProfileResolve(true);
}

void UAeyerjiSaveManagerSubsystem::HandleWriteUserFileComplete(const bool bWasSuccessful, const FUniqueNetId& UserId, const FString& FileName)
{
	UE_LOG(LogTemp, Display,
		TEXT("AeyerjiSaveManagerSubsystem: Cloud write complete Success=%d User=%s File=%s"),
		bWasSuccessful ? 1 : 0,
		*UserId.ToString(),
		*FileName);
}

void UAeyerjiSaveManagerSubsystem::MirrorProfileToCloud(UAeyerjiSaveGame* SaveData, const FString& OwnerKey)
{
	if (!SaveData)
	{
		return;
	}

	FUniqueNetIdRepl UserId;
	IOnlineUserCloudPtr UserCloud;
	if (!ResolveSteamCloudContext(UserId, UserCloud) || !UserCloud.IsValid())
	{
		return;
	}

	if (!CloudWriteDelegateHandle.IsValid())
	{
		CloudWriteDelegateHandle = UserCloud->AddOnWriteUserFileCompleteDelegate_Handle(
			FOnWriteUserFileCompleteDelegate::CreateUObject(this, &UAeyerjiSaveManagerSubsystem::HandleWriteUserFileComplete));
	}

	TArray<uint8> Bytes;
	FAeyerjiSaveTransportHeader Header;
	if (!BuildTransportFromProfile(SaveData, Header, Bytes))
	{
		return;
	}

	UserCloud->WriteUserFile(*UserId.GetUniqueNetId(), MakeCloudFilename(EAeyerjiSaveArtifactKind::Profile, OwnerKey), Bytes);
}

void UAeyerjiSaveManagerSubsystem::MirrorStreamingToCloud(UAeyerjiStreamingSaveGame* SaveData, const FString& OwnerKey)
{
	if (!SaveData)
	{
		return;
	}

	FUniqueNetIdRepl UserId;
	IOnlineUserCloudPtr UserCloud;
	if (!ResolveSteamCloudContext(UserId, UserCloud) || !UserCloud.IsValid())
	{
		return;
	}

	if (!CloudWriteDelegateHandle.IsValid())
	{
		CloudWriteDelegateHandle = UserCloud->AddOnWriteUserFileCompleteDelegate_Handle(
			FOnWriteUserFileCompleteDelegate::CreateUObject(this, &UAeyerjiSaveManagerSubsystem::HandleWriteUserFileComplete));
	}

	TArray<uint8> Bytes;
	if (!UGameplayStatics::SaveGameToMemory(SaveData, Bytes))
	{
		return;
	}

	UserCloud->WriteUserFile(*UserId.GetUniqueNetId(), MakeCloudFilename(EAeyerjiSaveArtifactKind::Streaming, OwnerKey), Bytes);
}

void UAeyerjiSaveManagerSubsystem::ClearPendingResolveDelegates()
{
	if (!PendingProfileResolve || !PendingProfileResolve->UserCloud.IsValid())
	{
		return;
	}

	if (PendingProfileResolve->EnumerateHandle.IsValid())
	{
		PendingProfileResolve->UserCloud->ClearOnEnumerateUserFilesCompleteDelegate_Handle(PendingProfileResolve->EnumerateHandle);
		PendingProfileResolve->EnumerateHandle.Reset();
	}

	if (PendingProfileResolve->ReadHandle.IsValid())
	{
		PendingProfileResolve->UserCloud->ClearOnReadUserFileCompleteDelegate_Handle(PendingProfileResolve->ReadHandle);
		PendingProfileResolve->ReadHandle.Reset();
	}
}
