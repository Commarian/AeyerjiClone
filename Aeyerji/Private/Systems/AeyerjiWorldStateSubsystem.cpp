#include "Systems/AeyerjiWorldStateSubsystem.h"

#include "Aeyerji/AeyerjiGameState.h"
#include "Systems/AeyerjiSaveManagerSubsystem.h"
#include "Systems/AeyerjiWorldStateSaveGame.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/Guid.h"

namespace
{
	constexpr int32 MaxWorldStateEntries = 8192;
	constexpr int32 MaxWorldStateDebugEntries = 8192;
	constexpr int32 MaxWorldStateStringLength = 4096;
	constexpr int32 MaxWorldStateSoftPathLength = 1024;

	bool IsRuntimeGameWorld(const UWorld* World)
	{
		return World && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE);
	}

	bool IsValidPersistence(const EAeyerjiWorldStatePersistence Value)
	{
		return Value == EAeyerjiWorldStatePersistence::RuntimeOnly
			|| Value == EAeyerjiWorldStatePersistence::Persistent;
	}

	bool IsValidReplication(const EAeyerjiWorldStateReplication Value)
	{
		return Value == EAeyerjiWorldStateReplication::ServerOnly
			|| Value == EAeyerjiWorldStateReplication::PublicReplicated;
	}

	bool IsValidScope(const EAeyerjiWorldStateScope Value)
	{
		return Value == EAeyerjiWorldStateScope::Global
			|| Value == EAeyerjiWorldStateScope::Run
			|| Value == EAeyerjiWorldStateScope::Character
			|| Value == EAeyerjiWorldStateScope::Session;
	}

	bool SanitizeWorldStateValue(
		const FAeyerjiWorldStateValue& InValue,
		const UWorld* ExpectedWorld,
		FAeyerjiWorldStateValue& OutValue)
	{
		switch (InValue.Type)
		{
		case EAeyerjiWorldStateValueType::None:
			OutValue = FAeyerjiWorldStateValue();
			return true;
		case EAeyerjiWorldStateValueType::Bool:
			OutValue = FAeyerjiWorldStateValue::FromBool(InValue.BoolValue);
			return true;
		case EAeyerjiWorldStateValueType::Int:
			OutValue = FAeyerjiWorldStateValue::FromInt(InValue.IntValue);
			return true;
		case EAeyerjiWorldStateValueType::Float:
			if (!FMath::IsFinite(InValue.FloatValue))
			{
				return false;
			}
			OutValue = FAeyerjiWorldStateValue::FromFloat(FMath::Clamp(InValue.FloatValue, -1.0e30f, 1.0e30f));
			return true;
		case EAeyerjiWorldStateValueType::Name:
			OutValue = FAeyerjiWorldStateValue::FromName(InValue.NameValue);
			return true;
		case EAeyerjiWorldStateValueType::String:
			OutValue = FAeyerjiWorldStateValue::FromString(InValue.StringValue.Left(MaxWorldStateStringLength));
			return true;
		case EAeyerjiWorldStateValueType::GameplayTag:
			OutValue = FAeyerjiWorldStateValue::FromGameplayTag(InValue.TagValue);
			return true;
		case EAeyerjiWorldStateValueType::SoftObjectPath:
			if (!InValue.SoftObjectPathValue.IsValid()
				|| InValue.SoftObjectPathValue.ToString().Len() > MaxWorldStateSoftPathLength)
			{
				return false;
			}
			OutValue = FAeyerjiWorldStateValue::FromSoftObjectPath(InValue.SoftObjectPathValue);
			return true;
		case EAeyerjiWorldStateValueType::Object:
		{
			UObject* LiveObject = InValue.ObjectValue.Get();
			if (LiveObject && (!IsValid(LiveObject)
				|| (LiveObject->GetWorld() && ExpectedWorld && LiveObject->GetWorld() != ExpectedWorld)))
			{
				return false;
			}
			const FSoftObjectPath SoftPath = LiveObject ? FSoftObjectPath(LiveObject) : InValue.SoftObjectPathValue;
			if (!LiveObject && !SoftPath.IsValid())
			{
				return false;
			}
			if (SoftPath.IsValid() && SoftPath.ToString().Len() > MaxWorldStateSoftPathLength)
			{
				return false;
			}
			OutValue = FAeyerjiWorldStateValue::FromObject(LiveObject);
			OutValue.Type = EAeyerjiWorldStateValueType::Object;
			OutValue.SoftObjectPathValue = SoftPath;
			return true;
		}
		default:
			return false;
		}
	}

	int32 NextWorldStateVersion(const int32 CurrentVersion)
	{
		return CurrentVersion >= MAX_int32 ? MAX_int32 : FMath::Max(CurrentVersion + 1, 1);
	}

	bool SanitizeWorldStateEntry(
		const FAeyerjiWorldStateEntry& InEntry,
		const UWorld* ExpectedWorld,
		FAeyerjiWorldStateEntry& OutEntry)
	{
		if (!InEntry.Key.IsValid() || !IsValidPersistence(InEntry.Persistence)
			|| !IsValidReplication(InEntry.Replication) || !IsValidScope(InEntry.Scope))
		{
			return false;
		}

		FAeyerjiWorldStateValue SanitizedValue;
		if (!SanitizeWorldStateValue(InEntry.Value, ExpectedWorld, SanitizedValue))
		{
			return false;
		}

		OutEntry = InEntry.MakeDataOnlyCopy();
		OutEntry.Value = MoveTemp(SanitizedValue);
		OutEntry.Version = FMath::Clamp(InEntry.Version, 0, MAX_int32);
		return true;
	}
}

void UAeyerjiWorldStateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency(UAeyerjiSaveManagerSubsystem::StaticClass());
	Super::Initialize(Collection);

	if (bAutoLoadPersistentState && HasWriteAuthority())
	{
		LoadPersistentState();
	}
}

void UAeyerjiWorldStateSubsystem::Deinitialize()
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(AutoSaveTimerHandle);
	}

	if (bPersistentStateDirty && HasWriteAuthority())
	{
		SavePersistentState();
	}

	Entries.Reset();
	Super::Deinitialize();
}

UAeyerjiWorldStateSubsystem* UAeyerjiWorldStateSubsystem::Get(const UObject* WorldContextObject)
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
		return GameInstance->GetSubsystem<UAeyerjiWorldStateSubsystem>();
	}

	return nullptr;
}

FAeyerjiWorldStateKey UAeyerjiWorldStateSubsystem::MakeWorldStateKey(const FGameplayTag StateTag, const FName InstanceId, const FName OwnerId)
{
	return FAeyerjiWorldStateKey(StateTag, InstanceId, OwnerId);
}

bool UAeyerjiWorldStateSubsystem::SetValue(const FAeyerjiWorldStateKey& Key, const FAeyerjiWorldStateValue& Value, const EAeyerjiWorldStatePersistence Persistence, const EAeyerjiWorldStateReplication Replication, const EAeyerjiWorldStateScope Scope)
{
	if (!Key.IsValid() || !HasWriteAuthority() || !IsValidPersistence(Persistence)
		|| !IsValidReplication(Replication) || !IsValidScope(Scope))
	{
		return false;
	}

	EnsurePersistentStateLoaded();
	FAeyerjiWorldStateValue SanitizedValue;
	if (!SanitizeWorldStateValue(Value, GetWorld(), SanitizedValue))
	{
		return false;
	}

	FAeyerjiWorldStateEntry* ExistingEntry = Entries.Find(Key);
	if (!ExistingEntry && Entries.Num() >= MaxWorldStateEntries)
	{
		return false;
	}
	const bool bExistingReplicated = ExistingEntry && ExistingEntry->Replication == EAeyerjiWorldStateReplication::PublicReplicated;
	const bool bChanged = !ExistingEntry
		|| !ExistingEntry->Value.Equals(SanitizedValue)
		|| ExistingEntry->Persistence != Persistence
		|| ExistingEntry->Replication != Replication
		|| ExistingEntry->Scope != Scope;

	if (!bChanged)
	{
		return true;
	}

	FAeyerjiWorldStateEntry Entry;
	if (ExistingEntry)
	{
		Entry = *ExistingEntry;
	}

	Entry.Key = Key;
	Entry.Value = MoveTemp(SanitizedValue);
	Entry.Persistence = Persistence;
	Entry.Replication = Replication;
	Entry.Scope = Scope;
	Entry.Version = NextWorldStateVersion(Entry.Version);
	Entry.LastUpdatedUtc = FDateTime::UtcNow();

	Entries.Add(Key, Entry);
	BroadcastEntryChanged(Entry);

	if (ShouldPersistToSharedWorldSave(Entry))
	{
		bPersistentStateDirty = true;
		ScheduleAutoSave();
	}

	if (Entry.Replication == EAeyerjiWorldStateReplication::PublicReplicated)
	{
		PublishEntryForReplication(Entry);
	}
	else if (bExistingReplicated)
	{
		RemoveEntryFromReplication(Key);
	}

	return true;
}

bool UAeyerjiWorldStateSubsystem::GetEntry(const FAeyerjiWorldStateKey& Key, FAeyerjiWorldStateEntry& OutEntry) const
{
	if (const FAeyerjiWorldStateEntry* Entry = Entries.Find(Key))
	{
		OutEntry = *Entry;
		return true;
	}

	return false;
}

bool UAeyerjiWorldStateSubsystem::GetValue(const FAeyerjiWorldStateKey& Key, FAeyerjiWorldStateValue& OutValue) const
{
	if (const FAeyerjiWorldStateEntry* Entry = Entries.Find(Key))
	{
		OutValue = Entry->Value;
		return true;
	}

	return false;
}

bool UAeyerjiWorldStateSubsystem::ClearValue(const FAeyerjiWorldStateKey& Key)
{
	if (!Key.IsValid() || !HasWriteAuthority())
	{
		return false;
	}

	EnsurePersistentStateLoaded();

	FAeyerjiWorldStateEntry RemovedEntry;
	if (!Entries.RemoveAndCopyValue(Key, RemovedEntry))
	{
		return false;
	}

	if (ShouldPersistToSharedWorldSave(RemovedEntry))
	{
		bPersistentStateDirty = true;
		ScheduleAutoSave();
	}

	RemoveEntryFromReplication(Key);
	BroadcastEntryRemoved(Key);
	return true;
}

bool UAeyerjiWorldStateSubsystem::ClearEntriesByScope(const EAeyerjiWorldStateScope Scope)
{
	if (!HasWriteAuthority() || !IsValidScope(Scope))
	{
		return false;
	}

	EnsurePersistentStateLoaded();

	TArray<FAeyerjiWorldStateKey> KeysToRemove;
	for (const TPair<FAeyerjiWorldStateKey, FAeyerjiWorldStateEntry>& Pair : Entries)
	{
		if (Pair.Value.Scope == Scope)
		{
			KeysToRemove.Add(Pair.Key);
		}
	}

	bool bClearedAnySharedPersistentEntry = false;
	for (const FAeyerjiWorldStateKey& Key : KeysToRemove)
	{
		FAeyerjiWorldStateEntry RemovedEntry;
		if (Entries.RemoveAndCopyValue(Key, RemovedEntry))
		{
			bClearedAnySharedPersistentEntry |= ShouldPersistToSharedWorldSave(RemovedEntry);
			RemoveEntryFromReplication(Key);
			BroadcastEntryRemoved(Key);
		}
	}

	if (bClearedAnySharedPersistentEntry)
	{
		bPersistentStateDirty = true;
		ScheduleAutoSave();
	}

	return KeysToRemove.Num() > 0;
}

bool UAeyerjiWorldStateSubsystem::ClearEntriesForOwner(const FName OwnerId)
{
	if (OwnerId.IsNone() || !HasWriteAuthority())
	{
		return false;
	}

	EnsurePersistentStateLoaded();

	TArray<FAeyerjiWorldStateKey> KeysToRemove;
	for (const TPair<FAeyerjiWorldStateKey, FAeyerjiWorldStateEntry>& Pair : Entries)
	{
		if (Pair.Key.OwnerId == OwnerId)
		{
			KeysToRemove.Add(Pair.Key);
		}
	}

	for (const FAeyerjiWorldStateKey& Key : KeysToRemove)
	{
		Entries.Remove(Key);
		RemoveEntryFromReplication(Key);
		BroadcastEntryRemoved(Key);
	}

	return KeysToRemove.Num() > 0;
}

bool UAeyerjiWorldStateSubsystem::MarkEventHappened(const FGameplayTag& EventTag, const FName InstanceId, const EAeyerjiWorldStatePersistence Persistence, const EAeyerjiWorldStateReplication Replication, const EAeyerjiWorldStateScope Scope, const FName OwnerId)
{
	return SetValue(FAeyerjiWorldStateKey(EventTag, InstanceId, OwnerId), FAeyerjiWorldStateValue::FromBool(true), Persistence, Replication, Scope);
}

bool UAeyerjiWorldStateSubsystem::HasEventHappened(const FGameplayTag& EventTag, const FName InstanceId, const FName OwnerId) const
{
	FAeyerjiWorldStateValue Value;
	if (!GetValue(FAeyerjiWorldStateKey(EventTag, InstanceId, OwnerId), Value))
	{
		return false;
	}

	if (Value.Type == EAeyerjiWorldStateValueType::Bool)
	{
		return Value.BoolValue;
	}

	return Value.Type != EAeyerjiWorldStateValueType::None;
}

bool UAeyerjiWorldStateSubsystem::IncrementInt(const FAeyerjiWorldStateKey& Key, const int32 Delta, int32& OutNewValue, const EAeyerjiWorldStatePersistence Persistence, const EAeyerjiWorldStateReplication Replication, const EAeyerjiWorldStateScope Scope)
{
	OutNewValue = 0;
	if (!Key.IsValid() || !HasWriteAuthority())
	{
		return false;
	}

	FAeyerjiWorldStateValue ExistingValue;
	if (GetValue(Key, ExistingValue))
	{
		switch (ExistingValue.Type)
		{
		case EAeyerjiWorldStateValueType::Bool:
			OutNewValue = static_cast<int32>(FMath::Clamp(
				static_cast<int64>(ExistingValue.BoolValue ? 1 : 0) + Delta,
				static_cast<int64>(MIN_int32),
				static_cast<int64>(MAX_int32)));
			break;
		case EAeyerjiWorldStateValueType::Int:
			OutNewValue = static_cast<int32>(FMath::Clamp(
				static_cast<int64>(ExistingValue.IntValue) + Delta,
				static_cast<int64>(MIN_int32),
				static_cast<int64>(MAX_int32)));
			break;
		case EAeyerjiWorldStateValueType::Float:
			if (!FMath::IsFinite(ExistingValue.FloatValue))
			{
				return false;
			}
			OutNewValue = static_cast<int32>(FMath::Clamp(
				FMath::RoundToInt64(FMath::Clamp(
					static_cast<double>(ExistingValue.FloatValue),
					static_cast<double>(MIN_int32),
					static_cast<double>(MAX_int32))) + Delta,
				static_cast<int64>(MIN_int32),
				static_cast<int64>(MAX_int32)));
			break;
		default:
			return false;
		}
	}
	else
	{
		OutNewValue = Delta;
	}

	return SetValue(Key, FAeyerjiWorldStateValue::FromInt(OutNewValue), Persistence, Replication, Scope);
}

bool UAeyerjiWorldStateSubsystem::RegisterLiveObject(const FAeyerjiWorldStateKey& Key, UObject* Object, const EAeyerjiWorldStatePersistence Persistence, const EAeyerjiWorldStateReplication Replication, const EAeyerjiWorldStateScope Scope)
{
	if (!IsValid(Object) || !HasWriteAuthority()
		|| (Object->GetWorld() && Object->GetWorld() != GetWorld()))
	{
		return false;
	}

	return SetValue(Key, FAeyerjiWorldStateValue::FromObject(Object), Persistence, Replication, Scope);
}

bool UAeyerjiWorldStateSubsystem::UnregisterLiveObject(const FAeyerjiWorldStateKey& Key, const UObject* ExpectedObject)
{
	if (!Key.IsValid() || !HasWriteAuthority())
	{
		return false;
	}

	EnsurePersistentStateLoaded();

	FAeyerjiWorldStateEntry* Entry = Entries.Find(Key);
	if (!Entry || Entry->Value.Type != EAeyerjiWorldStateValueType::Object)
	{
		return false;
	}

	if (ExpectedObject && Entry->Value.ObjectValue.IsValid() && Entry->Value.ObjectValue.Get() != ExpectedObject)
	{
		return false;
	}

	if (Entry->Persistence == EAeyerjiWorldStatePersistence::RuntimeOnly)
	{
		return ClearValue(Key);
	}

	Entry->Value.ObjectValue.Reset();
	Entry->Version = NextWorldStateVersion(Entry->Version);
	Entry->LastUpdatedUtc = FDateTime::UtcNow();
	if (ShouldPersistToSharedWorldSave(*Entry))
	{
		bPersistentStateDirty = true;
		ScheduleAutoSave();
	}
	BroadcastEntryChanged(*Entry);

	if (Entry->Replication == EAeyerjiWorldStateReplication::PublicReplicated)
	{
		PublishEntryForReplication(*Entry);
	}

	return true;
}

UObject* UAeyerjiWorldStateSubsystem::GetRegisteredObject(const FAeyerjiWorldStateKey& Key) const
{
	const FAeyerjiWorldStateEntry* Entry = Entries.Find(Key);
	if (!Entry || Entry->Value.Type != EAeyerjiWorldStateValueType::Object)
	{
		return nullptr;
	}

	if (Entry->Value.ObjectValue.IsValid())
	{
		return Entry->Value.ObjectValue.Get();
	}

	return Entry->Value.SoftObjectPathValue.IsValid()
		? Entry->Value.SoftObjectPathValue.TryLoad()
		: nullptr;
}

bool UAeyerjiWorldStateSubsystem::LoadPersistentState()
{
	if (!HasWriteAuthority())
	{
		return false;
	}

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

	UAeyerjiWorldStateSaveGame* SaveData = SaveManager->ResolveWorldState();
	if (!SaveData)
	{
		return false;
	}

	// Reload only the shared-persistent lane so an explicit runtime reload cannot erase active run,
	// session, or character-profile facts.
	TArray<FAeyerjiWorldStateKey> PreviousSharedKeys;
	for (const TPair<FAeyerjiWorldStateKey, FAeyerjiWorldStateEntry>& Pair : Entries)
	{
		if (ShouldPersistToSharedWorldSave(Pair.Value))
		{
			PreviousSharedKeys.Add(Pair.Key);
		}
	}
	for (const FAeyerjiWorldStateKey& PreviousKey : PreviousSharedKeys)
	{
		Entries.Remove(PreviousKey);
		RemoveEntryFromReplication(PreviousKey);
		BroadcastEntryRemoved(PreviousKey);
	}

	const int32 SavedEntryCount = FMath::Min(SaveData->Entries.Num(), MaxWorldStateEntries);
	for (int32 SavedEntryIndex = 0; SavedEntryIndex < SavedEntryCount; ++SavedEntryIndex)
	{
		FAeyerjiWorldStateEntry Entry;
		if (!SanitizeWorldStateEntry(SaveData->Entries[SavedEntryIndex], GetWorld(), Entry))
		{
			continue;
		}

		Entry.Persistence = EAeyerjiWorldStatePersistence::Persistent;
		Entry.Scope = EAeyerjiWorldStateScope::Global;
		if (!Entries.Contains(Entry.Key) && Entries.Num() >= MaxWorldStateEntries)
		{
			break;
		}
		Entries.Add(Entry.Key, Entry);
		BroadcastEntryChanged(Entry);
		if (Entry.Replication == EAeyerjiWorldStateReplication::PublicReplicated)
		{
			PublishEntryForReplication(Entry);
		}
	}

	bPersistentStateLoaded = true;
	bPersistentStateDirty = false;
	return true;
}

bool UAeyerjiWorldStateSubsystem::SavePersistentState()
{
	if (!HasWriteAuthority())
	{
		return false;
	}

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

	UAeyerjiWorldStateSaveGame* SaveData = Cast<UAeyerjiWorldStateSaveGame>(
		UGameplayStatics::CreateSaveGameObject(UAeyerjiWorldStateSaveGame::StaticClass()));
	if (!SaveData)
	{
		return false;
	}

	for (const TPair<FAeyerjiWorldStateKey, FAeyerjiWorldStateEntry>& Pair : Entries)
	{
		if (SaveData->Entries.Num() >= MaxWorldStateEntries)
		{
			break;
		}
		FAeyerjiWorldStateEntry SanitizedEntry;
		if (ShouldPersistToSharedWorldSave(Pair.Value)
			&& SanitizeWorldStateEntry(Pair.Value, GetWorld(), SanitizedEntry))
		{
			SaveData->Entries.Add(MoveTemp(SanitizedEntry));
		}
	}

	SaveData->Entries.Sort([](const FAeyerjiWorldStateEntry& A, const FAeyerjiWorldStateEntry& B)
	{
		return A.Key.ToString() < B.Key.ToString();
	});

	if (!SaveManager->CommitWorldState(SaveData))
	{
		return false;
	}

	bPersistentStateLoaded = true;
	bPersistentStateDirty = false;
	return true;
}

bool UAeyerjiWorldStateSubsystem::BeginRun(const FName RunId)
{
	if (!HasWriteAuthority())
	{
		return false;
	}

	ClearRunState();
	ActiveRunId = RunId.IsNone() ? FName(*FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower)) : RunId;
	return true;
}

bool UAeyerjiWorldStateSubsystem::EndRun()
{
	if (!HasWriteAuthority())
	{
		return false;
	}

	ClearRunState();
	ActiveRunId = NAME_None;
	return true;
}

bool UAeyerjiWorldStateSubsystem::ClearRunState()
{
	return ClearEntriesByScope(EAeyerjiWorldStateScope::Run);
}

FString UAeyerjiWorldStateSubsystem::GetWorldStateDebugSummary() const
{
	int32 GlobalPersistentCount = 0;
	int32 GlobalRuntimeCount = 0;
	int32 CharacterPersistentCount = 0;
	int32 CharacterRuntimeCount = 0;
	int32 RunCount = 0;
	int32 SessionCount = 0;

	for (const TPair<FAeyerjiWorldStateKey, FAeyerjiWorldStateEntry>& Pair : Entries)
	{
		const FAeyerjiWorldStateEntry& Entry = Pair.Value;
		switch (Entry.Scope)
		{
		case EAeyerjiWorldStateScope::Global:
			if (Entry.Persistence == EAeyerjiWorldStatePersistence::Persistent)
			{
				++GlobalPersistentCount;
			}
			else
			{
				++GlobalRuntimeCount;
			}
			break;
		case EAeyerjiWorldStateScope::Character:
			if (Entry.Persistence == EAeyerjiWorldStatePersistence::Persistent)
			{
				++CharacterPersistentCount;
			}
			else
			{
				++CharacterRuntimeCount;
			}
			break;
		case EAeyerjiWorldStateScope::Run:
			++RunCount;
			break;
		case EAeyerjiWorldStateScope::Session:
			++SessionCount;
			break;
		default:
			break;
		}
	}

	return FString::Printf(
		TEXT("ActiveRunId=%s Total=%d GlobalPersistent=%d GlobalRuntime=%d CharacterPersistent=%d CharacterRuntime=%d Run=%d Session=%d Dirty=%d"),
		*ActiveRunId.ToString(),
		Entries.Num(),
		GlobalPersistentCount,
		GlobalRuntimeCount,
		CharacterPersistentCount,
		CharacterRuntimeCount,
		RunCount,
		SessionCount,
		bPersistentStateDirty ? 1 : 0);
}

void UAeyerjiWorldStateSubsystem::GetRunFactDebugStrings(TArray<FString>& OutFacts) const
{
	OutFacts.Reset();
	for (const TPair<FAeyerjiWorldStateKey, FAeyerjiWorldStateEntry>& Pair : Entries)
	{
		if (OutFacts.Num() >= MaxWorldStateDebugEntries)
		{
			break;
		}
		const FAeyerjiWorldStateEntry& Entry = Pair.Value;
		if (Entry.Scope == EAeyerjiWorldStateScope::Run)
		{
			OutFacts.Add(FString::Printf(TEXT("%s=%s Version=%d"), *Entry.Key.ToString(), *Entry.Value.ToString(), Entry.Version));
		}
	}

	OutFacts.Sort();
}

void UAeyerjiWorldStateSubsystem::GetPersistentFactDebugStrings(TArray<FString>& OutFacts) const
{
	OutFacts.Reset();
	for (const TPair<FAeyerjiWorldStateKey, FAeyerjiWorldStateEntry>& Pair : Entries)
	{
		if (OutFacts.Num() >= MaxWorldStateDebugEntries)
		{
			break;
		}
		const FAeyerjiWorldStateEntry& Entry = Pair.Value;
		if (Entry.Persistence == EAeyerjiWorldStatePersistence::Persistent)
		{
			OutFacts.Add(FString::Printf(
				TEXT("%s=%s Scope=%d Replication=%d Version=%d"),
				*Entry.Key.ToString(),
				*Entry.Value.ToString(),
				static_cast<int32>(Entry.Scope),
				static_cast<int32>(Entry.Replication),
				Entry.Version));
		}
	}

	OutFacts.Sort();
}

bool UAeyerjiWorldStateSubsystem::PromoteRunFactToPersistentCharacterFact(
	const FGameplayTag StateTag,
	const FName TargetOwnerId,
	const FName InstanceId,
	const FName SourceOwnerId)
{
	if (TargetOwnerId.IsNone() || !HasWriteAuthority())
	{
		return false;
	}

	EnsurePersistentStateLoaded();

	FAeyerjiWorldStateEntry SourceEntry;
	if (!GetEntry(FAeyerjiWorldStateKey(StateTag, InstanceId, SourceOwnerId), SourceEntry)
		|| SourceEntry.Scope != EAeyerjiWorldStateScope::Run)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[WorldState] PromoteRunFactToPersistentCharacterFact failed Tag=%s Instance=%s SourceOwner=%s TargetOwner=%s"),
			*StateTag.ToString(),
			*InstanceId.ToString(),
			*SourceOwnerId.ToString(),
			*TargetOwnerId.ToString());
		return false;
	}

	const FAeyerjiWorldStateKey TargetKey(StateTag, InstanceId, TargetOwnerId);
	const bool bPromoted = SetValue(
		TargetKey,
		SourceEntry.Value.MakeDataOnlyCopy(),
		EAeyerjiWorldStatePersistence::Persistent,
		EAeyerjiWorldStateReplication::ServerOnly,
		EAeyerjiWorldStateScope::Character);

	UE_LOG(LogTemp, Display,
		TEXT("[WorldState] PromoteRunFactToPersistentCharacterFact Result=%d Source=%s Target=%s Value=%s"),
		bPromoted ? 1 : 0,
		*SourceEntry.Key.ToString(),
		*TargetKey.ToString(),
		*SourceEntry.Value.ToString());
	return bPromoted;
}

bool UAeyerjiWorldStateSubsystem::PromoteRunFactToPersistentGlobalFact(
	const FGameplayTag StateTag,
	const FName InstanceId,
	const FName SourceOwnerId)
{
	if (!HasWriteAuthority())
	{
		return false;
	}

	EnsurePersistentStateLoaded();

	FAeyerjiWorldStateEntry SourceEntry;
	if (!GetEntry(FAeyerjiWorldStateKey(StateTag, InstanceId, SourceOwnerId), SourceEntry)
		|| SourceEntry.Scope != EAeyerjiWorldStateScope::Run)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[WorldState] PromoteRunFactToPersistentGlobalFact failed Tag=%s Instance=%s SourceOwner=%s"),
			*StateTag.ToString(),
			*InstanceId.ToString(),
			*SourceOwnerId.ToString());
		return false;
	}

	const FAeyerjiWorldStateKey TargetKey(StateTag, InstanceId, NAME_None);
	const bool bPromoted = SetValue(
		TargetKey,
		SourceEntry.Value.MakeDataOnlyCopy(),
		EAeyerjiWorldStatePersistence::Persistent,
		EAeyerjiWorldStateReplication::ServerOnly,
		EAeyerjiWorldStateScope::Global);

	UE_LOG(LogTemp, Display,
		TEXT("[WorldState] PromoteRunFactToPersistentGlobalFact Result=%d Source=%s Target=%s Value=%s"),
		bPromoted ? 1 : 0,
		*SourceEntry.Key.ToString(),
		*TargetKey.ToString(),
		*SourceEntry.Value.ToString());
	return bPromoted;
}

bool UAeyerjiWorldStateSubsystem::ExportPersistentCharacterState(const FName OwnerId, TArray<FAeyerjiWorldStateEntry>& OutEntries) const
{
	OutEntries.Reset();
	if (OwnerId.IsNone())
	{
		return false;
	}

	for (const TPair<FAeyerjiWorldStateKey, FAeyerjiWorldStateEntry>& Pair : Entries)
	{
		if (OutEntries.Num() >= MaxWorldStateEntries)
		{
			break;
		}
		const FAeyerjiWorldStateEntry& Entry = Pair.Value;
		if (Entry.Scope == EAeyerjiWorldStateScope::Character
			&& Entry.Persistence == EAeyerjiWorldStatePersistence::Persistent
			&& Entry.Key.OwnerId == OwnerId)
		{
			OutEntries.Add(Entry.MakeDataOnlyCopy());
		}
	}

	OutEntries.Sort([](const FAeyerjiWorldStateEntry& A, const FAeyerjiWorldStateEntry& B)
	{
		return A.Key.ToString() < B.Key.ToString();
	});

	return true;
}

bool UAeyerjiWorldStateSubsystem::ImportPersistentCharacterState(const FName OwnerId, const TArray<FAeyerjiWorldStateEntry>& InEntries, const bool bReplaceExisting)
{
	if (OwnerId.IsNone() || !HasWriteAuthority())
	{
		return false;
	}

	EnsurePersistentStateLoaded();

	if (bReplaceExisting)
	{
		TArray<FAeyerjiWorldStateKey> KeysToRemove;
		for (const TPair<FAeyerjiWorldStateKey, FAeyerjiWorldStateEntry>& Pair : Entries)
		{
			if (Pair.Value.Scope == EAeyerjiWorldStateScope::Character && Pair.Key.OwnerId == OwnerId)
			{
				KeysToRemove.Add(Pair.Key);
			}
		}

		for (const FAeyerjiWorldStateKey& Key : KeysToRemove)
		{
			Entries.Remove(Key);
			RemoveEntryFromReplication(Key);
			BroadcastEntryRemoved(Key);
		}
	}

	const int32 ImportEntryCount = FMath::Min(InEntries.Num(), MaxWorldStateEntries);
	for (int32 ImportEntryIndex = 0; ImportEntryIndex < ImportEntryCount; ++ImportEntryIndex)
	{
		FAeyerjiWorldStateEntry Entry;
		if (!SanitizeWorldStateEntry(InEntries[ImportEntryIndex], GetWorld(), Entry))
		{
			continue;
		}
		Entry.Key.OwnerId = OwnerId;
		Entry.Scope = EAeyerjiWorldStateScope::Character;
		Entry.Persistence = EAeyerjiWorldStatePersistence::Persistent;
		if (!Entries.Contains(Entry.Key) && Entries.Num() >= MaxWorldStateEntries)
		{
			break;
		}
		Entries.Add(Entry.Key, Entry);
		BroadcastEntryChanged(Entry);

		if (Entry.Replication == EAeyerjiWorldStateReplication::PublicReplicated)
		{
			PublishEntryForReplication(Entry);
		}
	}

	return true;
}

void UAeyerjiWorldStateSubsystem::PublishReplicatedEntriesToGameState(AAeyerjiGameState* GameState) const
{
	if (!IsValid(GameState) || GameState->GetWorld() != GetWorld() || !HasWriteAuthority())
	{
		return;
	}

	TArray<FAeyerjiWorldStateEntry> PublicEntries;
	for (const TPair<FAeyerjiWorldStateKey, FAeyerjiWorldStateEntry>& Pair : Entries)
	{
		if (PublicEntries.Num() >= MaxWorldStateEntries)
		{
			break;
		}
		FAeyerjiWorldStateEntry SanitizedEntry;
		if (Pair.Value.Replication == EAeyerjiWorldStateReplication::PublicReplicated
			&& SanitizeWorldStateEntry(Pair.Value, GetWorld(), SanitizedEntry))
		{
			PublicEntries.Add(MoveTemp(SanitizedEntry));
		}
	}

	GameState->RepublishWorldStateFromServer(PublicEntries);
}

void UAeyerjiWorldStateSubsystem::ApplyReplicatedEntry(const FAeyerjiWorldStateEntry& Entry)
{
	FAeyerjiWorldStateEntry SanitizedEntry;
	if (HasWriteAuthority() || Entry.Replication != EAeyerjiWorldStateReplication::PublicReplicated
		|| !SanitizeWorldStateEntry(Entry, GetWorld(), SanitizedEntry)
		|| (!Entries.Contains(Entry.Key) && Entries.Num() >= MaxWorldStateEntries))
	{
		return;
	}

	Entries.Add(SanitizedEntry.Key, SanitizedEntry);
	BroadcastEntryChanged(SanitizedEntry);
}

void UAeyerjiWorldStateSubsystem::RemoveReplicatedEntry(const FAeyerjiWorldStateKey& Key)
{
	if (Entries.Remove(Key) > 0)
	{
		BroadcastEntryRemoved(Key);
	}
}

bool UAeyerjiWorldStateSubsystem::SetWorldStateBool(UObject* WorldContextObject, const FGameplayTag StateTag, const bool bValue, const FName InstanceId, const FName OwnerId, const EAeyerjiWorldStatePersistence Persistence, const EAeyerjiWorldStateReplication Replication, const EAeyerjiWorldStateScope Scope)
{
	if (UAeyerjiWorldStateSubsystem* Subsystem = Get(WorldContextObject))
	{
		return Subsystem->SetValue(FAeyerjiWorldStateKey(StateTag, InstanceId, OwnerId), FAeyerjiWorldStateValue::FromBool(bValue), Persistence, Replication, Scope);
	}

	return false;
}

bool UAeyerjiWorldStateSubsystem::GetWorldStateBool(const UObject* WorldContextObject, const FGameplayTag StateTag, bool& bOutValue, const FName InstanceId, const FName OwnerId)
{
	bOutValue = false;
	if (const UAeyerjiWorldStateSubsystem* Subsystem = Get(WorldContextObject))
	{
		FAeyerjiWorldStateValue Value;
		if (Subsystem->GetValue(FAeyerjiWorldStateKey(StateTag, InstanceId, OwnerId), Value) && Value.Type == EAeyerjiWorldStateValueType::Bool)
		{
			bOutValue = Value.BoolValue;
			return true;
		}
	}

	return false;
}

bool UAeyerjiWorldStateSubsystem::SetWorldStateInt(UObject* WorldContextObject, const FGameplayTag StateTag, const int32 Value, const FName InstanceId, const FName OwnerId, const EAeyerjiWorldStatePersistence Persistence, const EAeyerjiWorldStateReplication Replication, const EAeyerjiWorldStateScope Scope)
{
	if (UAeyerjiWorldStateSubsystem* Subsystem = Get(WorldContextObject))
	{
		return Subsystem->SetValue(FAeyerjiWorldStateKey(StateTag, InstanceId, OwnerId), FAeyerjiWorldStateValue::FromInt(Value), Persistence, Replication, Scope);
	}

	return false;
}

bool UAeyerjiWorldStateSubsystem::GetWorldStateInt(const UObject* WorldContextObject, const FGameplayTag StateTag, int32& OutValue, const FName InstanceId, const FName OwnerId)
{
	OutValue = 0;
	if (const UAeyerjiWorldStateSubsystem* Subsystem = Get(WorldContextObject))
	{
		FAeyerjiWorldStateValue Value;
		if (Subsystem->GetValue(FAeyerjiWorldStateKey(StateTag, InstanceId, OwnerId), Value) && Value.Type == EAeyerjiWorldStateValueType::Int)
		{
			OutValue = Value.IntValue;
			return true;
		}
	}

	return false;
}

bool UAeyerjiWorldStateSubsystem::SetWorldStateFloat(UObject* WorldContextObject, const FGameplayTag StateTag, const float Value, const FName InstanceId, const FName OwnerId, const EAeyerjiWorldStatePersistence Persistence, const EAeyerjiWorldStateReplication Replication, const EAeyerjiWorldStateScope Scope)
{
	if (UAeyerjiWorldStateSubsystem* Subsystem = Get(WorldContextObject))
	{
		return Subsystem->SetValue(FAeyerjiWorldStateKey(StateTag, InstanceId, OwnerId), FAeyerjiWorldStateValue::FromFloat(Value), Persistence, Replication, Scope);
	}

	return false;
}

bool UAeyerjiWorldStateSubsystem::GetWorldStateFloat(const UObject* WorldContextObject, const FGameplayTag StateTag, float& OutValue, const FName InstanceId, const FName OwnerId)
{
	OutValue = 0.f;
	if (const UAeyerjiWorldStateSubsystem* Subsystem = Get(WorldContextObject))
	{
		FAeyerjiWorldStateValue Value;
		if (Subsystem->GetValue(FAeyerjiWorldStateKey(StateTag, InstanceId, OwnerId), Value) && Value.Type == EAeyerjiWorldStateValueType::Float)
		{
			OutValue = Value.FloatValue;
			return true;
		}
	}

	return false;
}

bool UAeyerjiWorldStateSubsystem::SetWorldStateName(UObject* WorldContextObject, const FGameplayTag StateTag, const FName Value, const FName InstanceId, const FName OwnerId, const EAeyerjiWorldStatePersistence Persistence, const EAeyerjiWorldStateReplication Replication, const EAeyerjiWorldStateScope Scope)
{
	if (UAeyerjiWorldStateSubsystem* Subsystem = Get(WorldContextObject))
	{
		return Subsystem->SetValue(FAeyerjiWorldStateKey(StateTag, InstanceId, OwnerId), FAeyerjiWorldStateValue::FromName(Value), Persistence, Replication, Scope);
	}

	return false;
}

bool UAeyerjiWorldStateSubsystem::GetWorldStateName(const UObject* WorldContextObject, const FGameplayTag StateTag, FName& OutValue, const FName InstanceId, const FName OwnerId)
{
	OutValue = NAME_None;
	if (const UAeyerjiWorldStateSubsystem* Subsystem = Get(WorldContextObject))
	{
		FAeyerjiWorldStateValue Value;
		if (Subsystem->GetValue(FAeyerjiWorldStateKey(StateTag, InstanceId, OwnerId), Value) && Value.Type == EAeyerjiWorldStateValueType::Name)
		{
			OutValue = Value.NameValue;
			return true;
		}
	}

	return false;
}

bool UAeyerjiWorldStateSubsystem::SetWorldStateString(UObject* WorldContextObject, const FGameplayTag StateTag, const FString& Value, const FName InstanceId, const FName OwnerId, const EAeyerjiWorldStatePersistence Persistence, const EAeyerjiWorldStateReplication Replication, const EAeyerjiWorldStateScope Scope)
{
	if (UAeyerjiWorldStateSubsystem* Subsystem = Get(WorldContextObject))
	{
		return Subsystem->SetValue(FAeyerjiWorldStateKey(StateTag, InstanceId, OwnerId), FAeyerjiWorldStateValue::FromString(Value), Persistence, Replication, Scope);
	}

	return false;
}

bool UAeyerjiWorldStateSubsystem::GetWorldStateString(const UObject* WorldContextObject, const FGameplayTag StateTag, FString& OutValue, const FName InstanceId, const FName OwnerId)
{
	OutValue.Reset();
	if (const UAeyerjiWorldStateSubsystem* Subsystem = Get(WorldContextObject))
	{
		FAeyerjiWorldStateValue Value;
		if (Subsystem->GetValue(FAeyerjiWorldStateKey(StateTag, InstanceId, OwnerId), Value) && Value.Type == EAeyerjiWorldStateValueType::String)
		{
			OutValue = Value.StringValue;
			return true;
		}
	}

	return false;
}

bool UAeyerjiWorldStateSubsystem::SetWorldStateTag(UObject* WorldContextObject, const FGameplayTag StateTag, const FGameplayTag Value, const FName InstanceId, const FName OwnerId, const EAeyerjiWorldStatePersistence Persistence, const EAeyerjiWorldStateReplication Replication, const EAeyerjiWorldStateScope Scope)
{
	if (UAeyerjiWorldStateSubsystem* Subsystem = Get(WorldContextObject))
	{
		return Subsystem->SetValue(FAeyerjiWorldStateKey(StateTag, InstanceId, OwnerId), FAeyerjiWorldStateValue::FromGameplayTag(Value), Persistence, Replication, Scope);
	}

	return false;
}

bool UAeyerjiWorldStateSubsystem::GetWorldStateTag(const UObject* WorldContextObject, const FGameplayTag StateTag, FGameplayTag& OutValue, const FName InstanceId, const FName OwnerId)
{
	OutValue = FGameplayTag();
	if (const UAeyerjiWorldStateSubsystem* Subsystem = Get(WorldContextObject))
	{
		FAeyerjiWorldStateValue Value;
		if (Subsystem->GetValue(FAeyerjiWorldStateKey(StateTag, InstanceId, OwnerId), Value) && Value.Type == EAeyerjiWorldStateValueType::GameplayTag)
		{
			OutValue = Value.TagValue;
			return true;
		}
	}

	return false;
}

bool UAeyerjiWorldStateSubsystem::SetWorldStateSoftObjectPath(UObject* WorldContextObject, const FGameplayTag StateTag, const FSoftObjectPath Value, const FName InstanceId, const FName OwnerId, const EAeyerjiWorldStatePersistence Persistence, const EAeyerjiWorldStateReplication Replication, const EAeyerjiWorldStateScope Scope)
{
	if (UAeyerjiWorldStateSubsystem* Subsystem = Get(WorldContextObject))
	{
		return Subsystem->SetValue(FAeyerjiWorldStateKey(StateTag, InstanceId, OwnerId), FAeyerjiWorldStateValue::FromSoftObjectPath(Value), Persistence, Replication, Scope);
	}

	return false;
}

bool UAeyerjiWorldStateSubsystem::GetWorldStateSoftObjectPath(const UObject* WorldContextObject, const FGameplayTag StateTag, FSoftObjectPath& OutValue, const FName InstanceId, const FName OwnerId)
{
	OutValue = FSoftObjectPath();
	if (const UAeyerjiWorldStateSubsystem* Subsystem = Get(WorldContextObject))
	{
		FAeyerjiWorldStateValue Value;
		if (Subsystem->GetValue(FAeyerjiWorldStateKey(StateTag, InstanceId, OwnerId), Value) && Value.Type == EAeyerjiWorldStateValueType::SoftObjectPath)
		{
			OutValue = Value.SoftObjectPathValue;
			return true;
		}
	}

	return false;
}

bool UAeyerjiWorldStateSubsystem::MarkWorldEventHappened(UObject* WorldContextObject, const FGameplayTag EventTag, const FName InstanceId, const FName OwnerId, const EAeyerjiWorldStatePersistence Persistence, const EAeyerjiWorldStateReplication Replication, const EAeyerjiWorldStateScope Scope)
{
	if (UAeyerjiWorldStateSubsystem* Subsystem = Get(WorldContextObject))
	{
		return Subsystem->MarkEventHappened(EventTag, InstanceId, Persistence, Replication, Scope, OwnerId);
	}

	return false;
}

bool UAeyerjiWorldStateSubsystem::HasWorldEventHappened(const UObject* WorldContextObject, const FGameplayTag EventTag, const FName InstanceId, const FName OwnerId)
{
	if (const UAeyerjiWorldStateSubsystem* Subsystem = Get(WorldContextObject))
	{
		return Subsystem->HasEventHappened(EventTag, InstanceId, OwnerId);
	}

	return false;
}

bool UAeyerjiWorldStateSubsystem::IncrementWorldStateInt(UObject* WorldContextObject, const FGameplayTag StateTag, const int32 Delta, int32& OutNewValue, const FName InstanceId, const FName OwnerId, const EAeyerjiWorldStatePersistence Persistence, const EAeyerjiWorldStateReplication Replication, const EAeyerjiWorldStateScope Scope)
{
	OutNewValue = 0;
	if (UAeyerjiWorldStateSubsystem* Subsystem = Get(WorldContextObject))
	{
		return Subsystem->IncrementInt(FAeyerjiWorldStateKey(StateTag, InstanceId, OwnerId), Delta, OutNewValue, Persistence, Replication, Scope);
	}

	return false;
}

bool UAeyerjiWorldStateSubsystem::ClearWorldState(UObject* WorldContextObject, const FGameplayTag StateTag, const FName InstanceId, const FName OwnerId)
{
	if (UAeyerjiWorldStateSubsystem* Subsystem = Get(WorldContextObject))
	{
		return Subsystem->ClearValue(FAeyerjiWorldStateKey(StateTag, InstanceId, OwnerId));
	}

	return false;
}

bool UAeyerjiWorldStateSubsystem::RegisterWorldStateObject(UObject* WorldContextObject, const FGameplayTag StateTag, UObject* Object, const FName InstanceId, const FName OwnerId, const EAeyerjiWorldStatePersistence Persistence, const EAeyerjiWorldStateReplication Replication, const EAeyerjiWorldStateScope Scope)
{
	if (UAeyerjiWorldStateSubsystem* Subsystem = Get(WorldContextObject))
	{
		return Subsystem->RegisterLiveObject(FAeyerjiWorldStateKey(StateTag, InstanceId, OwnerId), Object, Persistence, Replication, Scope);
	}

	return false;
}

UObject* UAeyerjiWorldStateSubsystem::GetWorldStateObject(const UObject* WorldContextObject, const FGameplayTag StateTag, const FName InstanceId, const FName OwnerId)
{
	if (const UAeyerjiWorldStateSubsystem* Subsystem = Get(WorldContextObject))
	{
		return Subsystem->GetRegisteredObject(FAeyerjiWorldStateKey(StateTag, InstanceId, OwnerId));
	}

	return nullptr;
}

bool UAeyerjiWorldStateSubsystem::HasWriteAuthority() const
{
	const UWorld* World = GetWorld();
	return IsRuntimeGameWorld(World) && World->GetNetMode() != NM_Client;
}

void UAeyerjiWorldStateSubsystem::EnsurePersistentStateLoaded()
{
	if (!bPersistentStateLoaded && HasWriteAuthority())
	{
		LoadPersistentState();
	}
}

void UAeyerjiWorldStateSubsystem::BroadcastEntryChanged(const FAeyerjiWorldStateEntry& Entry)
{
	OnWorldStateChangedNative.Broadcast(Entry);
	OnWorldStateChanged.Broadcast(Entry);
}

void UAeyerjiWorldStateSubsystem::BroadcastEntryRemoved(const FAeyerjiWorldStateKey& Key)
{
	OnWorldStateRemoved.Broadcast(Key);
}

void UAeyerjiWorldStateSubsystem::ScheduleAutoSave()
{
	if (!bAutoSavePersistentState || !HasWriteAuthority())
	{
		return;
	}

	UWorld* World = GetWorld();
	const float SafeDelay = FMath::Clamp(
		FMath::IsFinite(AutoSaveDelaySeconds) ? AutoSaveDelaySeconds : 0.f,
		0.f,
		300.f);
	if (!World || SafeDelay <= 0.f)
	{
		SavePersistentState();
		return;
	}

	World->GetTimerManager().SetTimer(
		AutoSaveTimerHandle,
		this,
		&UAeyerjiWorldStateSubsystem::HandleAutoSaveTimer,
		SafeDelay,
		false);
}

void UAeyerjiWorldStateSubsystem::HandleAutoSaveTimer()
{
	if (bPersistentStateDirty)
	{
		SavePersistentState();
	}
}

void UAeyerjiWorldStateSubsystem::PublishEntryForReplication(const FAeyerjiWorldStateEntry& Entry)
{
	UWorld* World = GetWorld();
	AAeyerjiGameState* GameState = World ? World->GetGameState<AAeyerjiGameState>() : nullptr;
	if (GameState)
	{
		GameState->PublishWorldStateEntryFromServer(Entry);
	}
}

void UAeyerjiWorldStateSubsystem::RemoveEntryFromReplication(const FAeyerjiWorldStateKey& Key)
{
	UWorld* World = GetWorld();
	AAeyerjiGameState* GameState = World ? World->GetGameState<AAeyerjiGameState>() : nullptr;
	if (GameState)
	{
		GameState->RemoveWorldStateEntryFromServer(Key);
	}
}

bool UAeyerjiWorldStateSubsystem::ShouldPersistToSharedWorldSave(const FAeyerjiWorldStateEntry& Entry) const
{
	return Entry.Persistence == EAeyerjiWorldStatePersistence::Persistent
		&& Entry.Scope == EAeyerjiWorldStateScope::Global;
}
