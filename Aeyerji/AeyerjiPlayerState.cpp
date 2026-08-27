#include "AeyerjiPlayerState.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemGlobals.h"
#include "Logging/AeyerjiLog.h"
#include "Net/UnrealNetwork.h"
#include "AeyerjiGameplayTags.h"
#include "GUI/AeyerjiStringLibrary.h"
#include "Abilities/AeyerjiAbilityTuning.h"
#include "Abilities/Potions/GA_HealPotion.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Player/PlayerParentNative.h"
#include "CharacterStatsLibrary.h"
#include "Player/PlayerStatsTrackingComponent.h"
#include "AeyerjiGameState.h"
#include "AeyerjiSaveGame.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "Systems/AeyerjiSaveManagerSubsystem.h"
#include "Systems/AeyerjiDifficultyTuning.h"
#include "Frontend/AeyerjiFrontendRules.h"

namespace
{
	const TCHAR* LexToStringAeyerjiCheckpointReason(const EAeyerjiSaveCheckpointReason Reason)
	{
		switch (Reason)
		{
		case EAeyerjiSaveCheckpointReason::ProfileCreatedOrMigrated: return TEXT("ProfileCreatedOrMigrated");
		case EAeyerjiSaveCheckpointReason::DeathBeforeRespawn: return TEXT("DeathBeforeRespawn");
		case EAeyerjiSaveCheckpointReason::RunCompleted: return TEXT("RunCompleted");
		case EAeyerjiSaveCheckpointReason::ReturnToMenu: return TEXT("ReturnToMenu");
		case EAeyerjiSaveCheckpointReason::RetryRun: return TEXT("RetryRun");
		case EAeyerjiSaveCheckpointReason::PawnEndPlay: return TEXT("PawnEndPlay");
		case EAeyerjiSaveCheckpointReason::LogoutOrShutdown: return TEXT("LogoutOrShutdown");
		case EAeyerjiSaveCheckpointReason::Manual: return TEXT("Manual");
		default: return TEXT("Unknown");
		}
	}

	const TCHAR* LexToStringAeyerjiProfileLoadState(const EAeyerjiProfileLoadState State)
	{
		switch (State)
		{
		case EAeyerjiProfileLoadState::Pending: return TEXT("Pending");
		case EAeyerjiProfileLoadState::Applying: return TEXT("Applying");
		case EAeyerjiProfileLoadState::Applied: return TEXT("Applied");
		case EAeyerjiProfileLoadState::Failed: return TEXT("Failed");
		default: return TEXT("Unknown");
		}
	}

	int32 CountActionBarSlotsWithClass(const TArray<FAeyerjiAbilitySlot>& Bar)
	{
		int32 Count = 0;
		for (const FAeyerjiAbilitySlot& Slot : Bar)
		{
			if (Slot.Class)
			{
				++Count;
			}
		}

		return Count;
	}

	void NormalizeNativePotionAbilitySlot(FAeyerjiAbilitySlot& Slot)
	{
		if (!Slot.Tag.HasTagExact(AeyerjiTags::Ability_Potion_Heal))
		{
			return;
		}

		TSubclassOf<UGameplayAbility> CurrentClass = Slot.Class;
		if (!CurrentClass && !Slot.SavedAbilityClass.IsNull())
		{
			CurrentClass = Slot.SavedAbilityClass.Get();
			if (!CurrentClass)
			{
				CurrentClass = Slot.SavedAbilityClass.LoadSynchronous();
			}
		}

		if (CurrentClass && CurrentClass->IsChildOf(UGA_HealPotion::StaticClass()))
		{
			Slot.Class = CurrentClass;
			Slot.SavedAbilityClass = TSoftClassPtr<UGameplayAbility>(CurrentClass);
			return;
		}

		Slot.Class = UGA_HealPotion::StaticClass();
		Slot.SavedAbilityClass = TSoftClassPtr<UGameplayAbility>(Slot.Class);
	}

	FGameplayTag FindPrimaryAbilityTag(const FAeyerjiAbilitySlot& Slot)
	{
		for (const FGameplayTag& Tag : Slot.Tag)
		{
			if (Tag.IsValid() && Tag.ToString().StartsWith(TEXT("Ability.")))
			{
				return Tag;
			}
		}

		return FGameplayTag();
	}

	bool IsPotionAbilityTag(const FGameplayTag AbilityTag)
	{
		return AbilityTag.IsValid() && AbilityTag.ToString().StartsWith(TEXT("Ability.Potion"));
	}

	void LogCheckpointSummary(
		const UObject* Context,
		const UAeyerjiSaveGame* SaveData,
		const EAeyerjiSaveCheckpointReason Reason,
		const bool bBumpRevision,
		const TCHAR* Phase)
	{
		if (!SaveData)
		{
			return;
		}

		UE_LOG(LogTemp, Display,
			TEXT("[ProfileCheckpoint] Phase=%s Reason=%s Slot=%s Revision=%lld Bump=%d Items=%d Equipped=%d Grid=%d WorldState=%d ActionBar=%d RunResultCount=%d Context=%s"),
			Phase,
			LexToStringAeyerjiCheckpointReason(Reason),
			*SaveData->OwnerKey,
			SaveData->Revision,
			bBumpRevision ? 1 : 0,
			SaveData->Inventory.ItemSnapshots.Num(),
			SaveData->Inventory.EquippedItems.Num(),
			SaveData->Inventory.GridPlacements.Num(),
			SaveData->WorldStateEntries.Num(),
			SaveData->ActionBar.Num(),
			SaveData->RecentRuns.Num(),
			*GetNameSafe(Context));
	}
}

AAeyerjiPlayerState::AAeyerjiPlayerState()
{
	ActionBar.SetNum(7);
	bReplicates = true;

	PlayerStatsTracking = CreateDefaultSubobject<UPlayerStatsTrackingComponent>(TEXT("PlayerStatsTracking"));
}

int32 AAeyerjiPlayerState::GetCurrentPlayerLevel() const
{
	APawn* Pawn = GetPawn();
	if (!Pawn)
	{
		if (const APlayerController* PC = GetPlayerController())
		{
			Pawn = PC->GetPawn();
		}
	}

	if (Pawn)
	{
		if (UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Pawn, true))
		{
			return UAeyerjiDifficultySettings::FloatToGameplayLevel(
				ASC->GetNumericAttribute(UAeyerjiAttributeSet::GetLevelAttribute()));
		}
	}

	return 1;
}

const FAeyerjiAbilityProgressEntry* AAeyerjiPlayerState::FindAbilityProgressEntry(FGameplayTag AbilityTag) const
{
	if (!AbilityTag.IsValid())
	{
		return nullptr;
	}

	for (const FAeyerjiAbilityProgressEntry& Entry : AbilityProgressEntries)
	{
		if (Entry.AbilityTag == AbilityTag)
		{
			return &Entry;
		}
	}

	return nullptr;
}

FAeyerjiAbilityProgressEntry* AAeyerjiPlayerState::FindMutableAbilityProgressEntry(FGameplayTag AbilityTag)
{
	if (!AbilityTag.IsValid())
	{
		return nullptr;
	}

	for (FAeyerjiAbilityProgressEntry& Entry : AbilityProgressEntries)
	{
		if (Entry.AbilityTag == AbilityTag)
		{
			return &Entry;
		}
	}

	return nullptr;
}

bool AAeyerjiPlayerState::IsAbilityBaseUnlocked(FGameplayTag AbilityTag) const
{
	if (!AbilityTag.IsValid())
	{
		return false;
	}

	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UAeyerjiAbilityTuningSubsystem* Tuning = GameInstance ? GameInstance->GetSubsystem<UAeyerjiAbilityTuningSubsystem>() : nullptr;
	const FAeyerjiAbilityTableRow* Row = Tuning ? Tuning->FindAbilityRow(AbilityTag) : nullptr;
	if (!Row)
	{
		return false;
	}

	if (Row->bUnlockedByDefault)
	{
		return true;
	}

	return GetCurrentPlayerLevel() >= FMath::Max(1, Row->RequiredLevel);
}

int32 AAeyerjiPlayerState::GetAbilityRank(FGameplayTag AbilityTag) const
{
	if (!IsAbilityBaseUnlocked(AbilityTag))
	{
		return 0;
	}

	if (const FAeyerjiAbilityProgressEntry* Entry = FindAbilityProgressEntry(AbilityTag))
	{
		return FMath::Max(1, Entry->CurrentRank);
	}

	return 1;
}

int32 AAeyerjiPlayerState::GetProgressionRankForSlot(const FAeyerjiAbilitySlot& AbilitySlot) const
{
	const FGameplayTag AbilityTag = FindPrimaryAbilityTag(AbilitySlot);
	if (!AbilityTag.IsValid())
	{
		return FMath::Max(1, AbilitySlot.Level);
	}

	const int32 ProgressionRank = GetAbilityRank(AbilityTag);
	return ProgressionRank > 0 ? ProgressionRank : FMath::Max(1, AbilitySlot.Level);
}

void AAeyerjiPlayerState::MirrorProgressionRanksIntoActionBar()
{
	for (FAeyerjiAbilitySlot& Slot : ActionBar)
	{
		if (!AeyerjiAbilitySlotUtils::IsAbilitySlotEmpty(Slot))
		{
			Slot.Level = GetProgressionRankForSlot(Slot);
		}
	}
}

void AAeyerjiPlayerState::SyncGrantedAbilityRank(FGameplayTag AbilityTag)
{
	if (!AbilityTag.IsValid())
	{
		return;
	}

	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UAeyerjiAbilityTuningSubsystem* Tuning = GameInstance ? GameInstance->GetSubsystem<UAeyerjiAbilityTuningSubsystem>() : nullptr;
	const FAeyerjiAbilityTableRow* Row = Tuning ? Tuning->FindAbilityRow(AbilityTag) : nullptr;
	if (!Row)
	{
		return;
	}

	FAeyerjiAbilitySlot Slot;
	if (!UAeyerjiAbilityTuningSubsystem::BuildAbilitySlotFromRow(*Row, Slot))
	{
		return;
	}

	Slot.Level = GetAbilityRank(AbilityTag);
	GrantAbilityFromSlotInternal(Slot);
}

void AAeyerjiPlayerState::SyncProfileAbilityProgressionCache(const TCHAR* Reason) const
{
	if (!HasAuthority())
	{
		return;
	}

	UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UAeyerjiSaveManagerSubsystem* SaveManager = GameInstance ? GameInstance->GetSubsystem<UAeyerjiSaveManagerSubsystem>() : nullptr;
	if (!SaveManager)
	{
		return;
	}

	UAeyerjiSaveGame* CachedProfile = nullptr;
	if (!SaveManager->GetServerCachedProfile(this, CachedProfile) || !CachedProfile)
	{
		return;
	}

	CachedProfile->AbilityProgressEntries = AbilityProgressEntries;
	CachedProfile->UnspentAbilityPoints = UnspentAbilityPoints;
	CachedProfile->TotalAbilityPointSpends = TotalAbilityPointSpends;
	UE_LOG(LogTemp, Display,
		TEXT("[AbilityProgression] CacheSync Reason=%s PlayerState=%s Entries=%d Unspent=%d Spends=%d Revision=%lld"),
		Reason ? Reason : TEXT("Unknown"),
		*GetNameSafe(this),
		CachedProfile->AbilityProgressEntries.Num(),
		CachedProfile->UnspentAbilityPoints,
		CachedProfile->TotalAbilityPointSpends,
		CachedProfile->Revision);
}

void AAeyerjiPlayerState::SyncProfileGoldCache(const TCHAR* Reason) const
{
	if (!HasAuthority())
	{
		return;
	}

	UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UAeyerjiSaveManagerSubsystem* SaveManager = GameInstance ? GameInstance->GetSubsystem<UAeyerjiSaveManagerSubsystem>() : nullptr;
	if (!SaveManager)
	{
		return;
	}

	UAeyerjiSaveGame* CachedProfile = nullptr;
	if (!SaveManager->GetServerCachedProfile(this, CachedProfile) || !CachedProfile)
	{
		return;
	}

	CachedProfile->Gold = Gold;
	UE_LOG(LogTemp, Display,
		TEXT("[Currency] CacheSync Reason=%s PlayerState=%s Gold=%lld Revision=%lld"),
		Reason ? Reason : TEXT("Unknown"),
		*GetNameSafe(this),
		CachedProfile->Gold,
		CachedProfile->Revision);
}

void AAeyerjiPlayerState::ApplyLoadedAbilityProgression(const TArray<FAeyerjiAbilityProgressEntry>& InEntries, int32 InUnspentAbilityPoints, int32 InTotalAbilityPointSpends)
{
	UnspentAbilityPoints = FMath::Max(0, InUnspentAbilityPoints);
	TotalAbilityPointSpends = FMath::Max(0, InTotalAbilityPointSpends);
	AbilityProgressEntries.Reset();

	const UGameInstance* GameInstance = GetGameInstance();
	const UAeyerjiAbilityTuningSubsystem* Tuning = GameInstance
		? GameInstance->GetSubsystem<UAeyerjiAbilityTuningSubsystem>()
		: nullptr;
	if (Tuning)
	{
		TMap<FGameplayTag, FAeyerjiAbilityProgressEntry> SanitizedEntries;
		for (const FAeyerjiAbilityProgressEntry& LoadedEntry : InEntries)
		{
			if (!LoadedEntry.AbilityTag.IsValid()
				|| IsPotionAbilityTag(LoadedEntry.AbilityTag)
				|| !Tuning->FindAbilityRow(LoadedEntry.AbilityTag))
			{
				continue;
			}

			FAeyerjiAbilityProgressEntry& SanitizedEntry = SanitizedEntries.FindOrAdd(LoadedEntry.AbilityTag);
			SanitizedEntry.AbilityTag = LoadedEntry.AbilityTag;
			SanitizedEntry.CurrentRank = FMath::Max(
				SanitizedEntry.CurrentRank,
				FMath::Clamp(LoadedEntry.CurrentRank, 1, Tuning->GetMaxAbilityRank(LoadedEntry.AbilityTag)));
			SanitizedEntry.LastUpgradePointSpendCount = FMath::Max(
				SanitizedEntry.LastUpgradePointSpendCount,
				FMath::Clamp(LoadedEntry.LastUpgradePointSpendCount, 0, TotalAbilityPointSpends));
		}

		SanitizedEntries.GenerateValueArray(AbilityProgressEntries);
		AbilityProgressEntries.Sort([](const FAeyerjiAbilityProgressEntry& A, const FAeyerjiAbilityProgressEntry& B)
		{
			return A.AbilityTag.ToString() < B.AbilityTag.ToString();
		});
	}

	OnRep_AbilityProgression();
	SyncProfileAbilityProgressionCache(TEXT("Load"));
}

void AAeyerjiPlayerState::ApplyLoadedGold(const int64 LoadedGold)
{
	ApplyGoldValue(LoadedGold, FName(TEXT("Load")), /*bBroadcast=*/true);
	SyncProfileGoldCache(TEXT("Load"));
}

void AAeyerjiPlayerState::ApplyLoadedRiftProgression(const int32 LoadedHighestUnlockedTier, const int32 LoadedLastSelectedTier)
{
	SetRiftProgressionFromServer(LoadedHighestUnlockedTier, LoadedLastSelectedTier);
}

void AAeyerjiPlayerState::SetRiftProgressionFromServer(const int32 NewHighestUnlockedTier, const int32 NewLastSelectedTier)
{
	if (!HasAuthority())
	{
		return;
	}

	HighestUnlockedRiftTier = FMath::Max(NewHighestUnlockedTier, 1);
	LastSelectedRiftTier = FMath::Clamp(NewLastSelectedTier, 1, HighestUnlockedRiftTier);

	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (UAeyerjiSaveManagerSubsystem* SaveManager = GameInstance->GetSubsystem<UAeyerjiSaveManagerSubsystem>())
			{
				UAeyerjiSaveGame* CachedProfile = nullptr;
				if (SaveManager->GetServerCachedProfile(this, CachedProfile) && CachedProfile)
				{
					CachedProfile->HighestUnlockedRiftTier = HighestUnlockedRiftTier;
					CachedProfile->LastSelectedRiftTier = LastSelectedRiftTier;
				}
			}
		}
	}

	OnRep_RiftTierProgression();
	ForceNetUpdate();
}

void AAeyerjiPlayerState::SetPersonalRunResultsFromServer(const FAeyerjiRunResults& NewResults)
{
	if (!HasAuthority())
	{
		return;
	}

	PersonalRunResults = NewResults;
	OnRep_PersonalRunResults();
	ForceNetUpdate();
}

void AAeyerjiPlayerState::AddGold(const int64 DeltaGold, const FName Reason)
{
	if (!HasAuthority() || DeltaGold <= 0)
	{
		return;
	}

	const int64 NewGold = DeltaGold > MAX_int64 - Gold ? MAX_int64 : Gold + DeltaGold;
	ApplyGoldValue(NewGold, Reason.IsNone() ? FName(TEXT("AddGold")) : Reason, /*bBroadcast=*/true);
	SyncProfileGoldCache(TEXT("AddGold"));
}

bool AAeyerjiPlayerState::CanSpendGold(const int64 Cost) const
{
	return Cost <= 0 || Gold >= Cost;
}

bool AAeyerjiPlayerState::TrySpendGold(const int64 Cost, const FName Reason)
{
	if (!HasAuthority() || Cost < 0 || !CanSpendGold(Cost))
	{
		return false;
	}

	if (Cost == 0)
	{
		return true;
	}

	ApplyGoldValue(Gold - Cost, Reason.IsNone() ? FName(TEXT("SpendGold")) : Reason, /*bBroadcast=*/true);
	SyncProfileGoldCache(TEXT("SpendGold"));
	return true;
}

void AAeyerjiPlayerState::GrantAbilityPoints(int32 DeltaPoints)
{
	if (!HasAuthority() || DeltaPoints <= 0)
	{
		return;
	}

	UnspentAbilityPoints = static_cast<int32>(FMath::Min<int64>(
		static_cast<int64>(MAX_int32),
		static_cast<int64>(UnspentAbilityPoints) + DeltaPoints));
	OnRep_AbilityProgression();
	ForceNetUpdate();
	SyncProfileAbilityProgressionCache(TEXT("GrantAbilityPoints"));
}

bool AAeyerjiPlayerState::CanUpgradeAbility(FGameplayTag AbilityTag, FText& OutFailureReason) const
{
	OutFailureReason = FText::GetEmpty();

	if (!AbilityTag.IsValid())
	{
		OutFailureReason = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("AbilityUpgradeMissingTag"));
		return false;
	}

	if (IsPotionAbilityTag(AbilityTag))
	{
		OutFailureReason = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("AbilityUpgradePotionBlocked"));
		return false;
	}

	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UAeyerjiAbilityTuningSubsystem* Tuning = GameInstance ? GameInstance->GetSubsystem<UAeyerjiAbilityTuningSubsystem>() : nullptr;
	if (!Tuning)
	{
		OutFailureReason = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("AbilityUpgradeMissingTuning"));
		return false;
	}

	if (!IsAbilityBaseUnlocked(AbilityTag))
	{
		OutFailureReason = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("AbilityUpgradeBaseLocked"));
		return false;
	}

	const int32 CurrentRank = GetAbilityRank(AbilityTag);
	if (CurrentRank >= MAX_int32)
	{
		OutFailureReason = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("AbilityUpgradeNoNextRank"));
		return false;
	}
	const int32 NextRank = FMath::Max(2, CurrentRank + 1);
	const FAeyerjiAbilityRankTableRow* NextRankRow = Tuning->FindAbilityRankRow(AbilityTag, NextRank);
	if (!NextRankRow)
	{
		OutFailureReason = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("AbilityUpgradeNoNextRank"));
		return false;
	}

	if (GetCurrentPlayerLevel() < FMath::Max(1, NextRankRow->RequiredPlayerLevel))
	{
		OutFailureReason = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("AbilityUpgradeLevelLocked"));
		return false;
	}

	if (UnspentAbilityPoints < FMath::Max(1, NextRankRow->PointCost))
	{
		OutFailureReason = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("AbilityUpgradeInsufficientPoints"));
		return false;
	}

	const int32 LastSpendCount = FindAbilityProgressEntry(AbilityTag)
		? FindAbilityProgressEntry(AbilityTag)->LastUpgradePointSpendCount
		: 0;
	const int32 OtherSpendsSinceLastUpgrade = TotalAbilityPointSpends - LastSpendCount;
	if (OtherSpendsSinceLastUpgrade < FMath::Max(0, NextRankRow->RequiredOtherPointSpendsSinceLastUpgrade))
	{
		OutFailureReason = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("AbilityUpgradeSpreadLocked"));
		return false;
	}

	return true;
}

void AAeyerjiPlayerState::Server_SetActionBar_Implementation(
		const TArray<FAeyerjiAbilitySlot>& NewBar)
{
	TArray<FAeyerjiAbilitySlot> AuthorizedBar;
	AuthorizedBar.SetNum(ActionBar.Num());
	for (int32 SlotIndex = 0; SlotIndex < AuthorizedBar.Num(); ++SlotIndex)
	{
		const FAeyerjiAbilitySlot& RequestedSlot = NewBar.IsValidIndex(SlotIndex)
			? NewBar[SlotIndex]
			: FAeyerjiAbilitySlot();
		if (!ResolveAuthorizedAbilitySlot(RequestedSlot, AuthorizedBar[SlotIndex]))
		{
			AuthorizedBar[SlotIndex] = ActionBar[SlotIndex];
			UE_LOG(LogTemp, Warning,
				TEXT("[AbilityBar] FullUpdateRejected PlayerState=%s Slot=%d Tags=%s Class=%s"),
				*GetNameSafe(this),
				SlotIndex,
				*RequestedSlot.Tag.ToString(),
				*GetNameSafe(RequestedSlot.Class));
		}
	}

	ApplyActionBarUpdate(AuthorizedBar, /*bValidateCooldowns=*/true);
}

void AAeyerjiPlayerState::Server_SetActionBarSlot_Implementation(
	const int32 SlotIndex,
	const FAeyerjiAbilitySlot& NewSlot)
{
	TArray<FAeyerjiAbilitySlot> UpdatedBar = ActionBar;
	if (!UpdatedBar.IsValidIndex(SlotIndex))
	{
		AJ_LOG(this, TEXT("Server_SetActionBarSlot ignored invalid index %d (BarSize=%d)"), SlotIndex, UpdatedBar.Num());
		return;
	}

	FAeyerjiAbilitySlot NormalizedSlot;
	if (!ResolveAuthorizedAbilitySlot(NewSlot, NormalizedSlot))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[AbilityBar] SlotUpdateRejected PlayerState=%s Slot=%d Tags=%s Class=%s"),
			*GetNameSafe(this),
			SlotIndex,
			*NewSlot.Tag.ToString(),
			*GetNameSafe(NewSlot.Class));
		return;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[AbilityBar] SlotUpdate PlayerState=%s Slot=%d Description=%s RuntimeClass=%s SavedClass=%s"),
		*GetNameSafe(this),
		SlotIndex,
		*NormalizedSlot.Description.ToString(),
		*GetNameSafe(NormalizedSlot.Class),
		*NormalizedSlot.SavedAbilityClass.ToSoftObjectPath().ToString());

	UpdatedBar[SlotIndex] = NormalizedSlot;
	ApplyActionBarUpdate(UpdatedBar, /*bValidateCooldowns=*/true);
}

bool AAeyerjiPlayerState::ResolveAuthorizedAbilitySlot(
	const FAeyerjiAbilitySlot& RequestedSlot,
	FAeyerjiAbilitySlot& OutSlot) const
{
	OutSlot = FAeyerjiAbilitySlot();
	if (AeyerjiAbilitySlotUtils::IsAbilitySlotEmpty(RequestedSlot))
	{
		return true;
	}

	const FGameplayTag AbilityTag = FindPrimaryAbilityTag(RequestedSlot);
	if (!AbilityTag.IsValid())
	{
		return false;
	}

	// Keep the tuning row authoritative for potion UI metadata as well as its runtime defaults.
	// The native class is only a compatibility fallback for a migrated profile whose row is unavailable.
	if (AbilityTag == AeyerjiTags::Ability_Potion_Heal)
	{
		const UWorld* World = GetWorld();
		const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		const UAeyerjiAbilityTuningSubsystem* Tuning = GameInstance
			? GameInstance->GetSubsystem<UAeyerjiAbilityTuningSubsystem>()
			: nullptr;
		if (Tuning && Tuning->BuildAbilitySlot(AbilityTag, OutSlot) && OutSlot.Class)
		{
			OutSlot.Level = 1;
			OutSlot.CaptureStableReferences();
			return true;
		}

		OutSlot = RequestedSlot;
		OutSlot.Tag.Reset();
		OutSlot.Tag.AddTag(AeyerjiTags::Ability_Potion_Heal);
		OutSlot.Class = UGA_HealPotion::StaticClass();
		OutSlot.SavedAbilityClass = TSoftClassPtr<UGameplayAbility>(OutSlot.Class);
		OutSlot.Level = 1;
		OutSlot.TargetMode = EAeyerjiTargetMode::Instant;
		OutSlot.CaptureStableReferences();
		return true;
	}

	if (!IsAbilityBaseUnlocked(AbilityTag))
	{
		return false;
	}

	const UWorld* World = GetWorld();
	const UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	const UAeyerjiAbilityTuningSubsystem* Tuning = GameInstance
		? GameInstance->GetSubsystem<UAeyerjiAbilityTuningSubsystem>()
		: nullptr;
	if (!Tuning || !Tuning->BuildAbilitySlot(AbilityTag, OutSlot) || !OutSlot.Class)
	{
		OutSlot = FAeyerjiAbilitySlot();
		return false;
	}

	OutSlot.Level = FMath::Max(1, GetAbilityRank(AbilityTag));
	OutSlot.CaptureStableReferences();
	return true;
}

void AAeyerjiPlayerState::ApplyLoadedActionBar(const TArray<FAeyerjiAbilitySlot>& LoadedBar)
{
	TArray<FAeyerjiAbilitySlot> AuthorizedBar;
	AuthorizedBar.SetNum(ActionBar.Num());
	for (int32 SlotIndex = 0; SlotIndex < AuthorizedBar.Num() && SlotIndex < LoadedBar.Num(); ++SlotIndex)
	{
		if (!ResolveAuthorizedAbilitySlot(LoadedBar[SlotIndex], AuthorizedBar[SlotIndex]))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[AbilityLoad] SavedSlotRejected PlayerState=%s Slot=%d Tags=%s Class=%s"),
				*GetNameSafe(this),
				SlotIndex,
				*LoadedBar[SlotIndex].Tag.ToString(),
				*GetNameSafe(LoadedBar[SlotIndex].Class));
			AuthorizedBar[SlotIndex] = FAeyerjiAbilitySlot();
		}
	}

	ApplyActionBarUpdate(AuthorizedBar, /*bValidateCooldowns=*/false);

	for (const FAeyerjiAbilitySlot& Slot : ActionBar)
	{
		GrantAbilityFromSlotInternal(Slot);
	}

	UE_LOG(LogTemp, Display,
		TEXT("[AbilityLoad] PlayerState=%s Slots=%d ResolvedClasses=%d"),
		*GetNameSafe(this),
		ActionBar.Num(),
		CountActionBarSlotsWithClass(ActionBar));
}

void AAeyerjiPlayerState::ApplyActionBarUpdate(
	const TArray<FAeyerjiAbilitySlot>& NewBar,
	const bool bValidateCooldowns)
{
	AJ_LOG(this, TEXT("AAeyerjiPlayerState::ApplyActionBarUpdate"));

	TArray<FAeyerjiAbilitySlot> Sanitized = NewBar;
	for (FAeyerjiAbilitySlot& Slot : Sanitized)
	{
		Slot.CaptureStableReferences();
		Slot.ResolveSavedReferences();
		NormalizeNativePotionAbilitySlot(Slot);
	}

	// Ensure same length as current bar to avoid mismatched sizes
	if (Sanitized.Num() != ActionBar.Num())
	{
		Sanitized.SetNum(ActionBar.Num());
	}

	// Track previous positions of each ability class (from existing ActionBar)
	TMap<TSubclassOf<UGameplayAbility>, int32> PreviousIndex;
	for (int32 Idx = 0; Idx < ActionBar.Num(); ++Idx)
	{
		if (ActionBar[Idx].Class && !PreviousIndex.Contains(ActionBar[Idx].Class))
		{
			PreviousIndex.Add(ActionBar[Idx].Class, Idx);
		}
	}

	// Helper: check if an ability class is on cooldown on the owning ASC
	auto IsAbilityOnCooldown = [](UAbilitySystemComponent* ASC, TSubclassOf<UGameplayAbility> AbilityClass) -> bool
	{
		if (!ASC || !AbilityClass)
		{
			return false;
		}
		if (FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromClass(AbilityClass))
		{
			if (const UGameplayAbility* AbilityCDO = Spec->Ability)
			{
				if (const FGameplayAbilityActorInfo* ActorInfo = ASC->AbilityActorInfo.Get())
				{
					float Remaining = 0.f;
					float Duration = 0.f;
					AbilityCDO->GetCooldownTimeRemainingAndDuration(Spec->Handle, ActorInfo, Remaining, Duration);
					return Remaining > KINDA_SMALL_NUMBER;
				}
			}
		}
		return false;
	};

	// Access ASC if possible
	UAbilitySystemComponent* ASC = nullptr;
	if (APawn* Pawn = GetPawn())
	{
		if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Pawn))
		{
			ASC = ASI->GetAbilitySystemComponent();
		}
	}

	// 1) Enforce no duplicates: latest slot wins, previous occurrence is cleared.
	TMap<TSubclassOf<UGameplayAbility>, int32> ChosenIndex;
	for (int32 Idx = 0; Idx < Sanitized.Num(); ++Idx)
	{
		if (const TSubclassOf<UGameplayAbility> Class = Sanitized[Idx].Class)
		{
			if (int32* ExistingIdx = ChosenIndex.Find(Class))
			{
				const int32 OldIdx = PreviousIndex.Contains(Class) ? PreviousIndex[Class] : INDEX_NONE;

				// If the currently chosen index is the previous location and the new occurrence is different,
				// prefer the new occurrence (move ability).
				if (*ExistingIdx == OldIdx && Idx != OldIdx)
				{
					Sanitized[*ExistingIdx] = FAeyerjiAbilitySlot();
					*ExistingIdx = Idx;
				}
				else
				{
					// Otherwise keep the existing choice and clear this duplicate
					Sanitized[Idx] = FAeyerjiAbilitySlot();
				}
			}
			else
			{
				ChosenIndex.Add(Class, Idx);
			}
		}
	}

	if (bValidateCooldowns)
	{
		// 2) Block changes for abilities currently on cooldown (cannot remove/swap while cooling).
		for (int32 Idx = 0; Idx < Sanitized.Num(); ++Idx)
		{
			const FAeyerjiAbilitySlot& OldSlot = ActionBar.IsValidIndex(Idx) ? ActionBar[Idx] : FAeyerjiAbilitySlot();
			FAeyerjiAbilitySlot& NewSlot = Sanitized[Idx];

			const bool bClassChanged = (OldSlot.Class != NewSlot.Class) || (OldSlot.Tag != NewSlot.Tag);
			if (bClassChanged && OldSlot.Class && IsAbilityOnCooldown(ASC, OldSlot.Class))
			{
				// Keep the old slot; reject the change while cooldown is active.
				NewSlot = OldSlot;
				AJ_LOG(this, TEXT("Swap blocked for %s (cooldown)"), *GetNameSafe(OldSlot.Class));
				// From GlobalStringTable.csv
				const FText SwapBlockedReason = AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("ActionBarSwapBlockedCooldown"));
				OnActionBarSwapBlocked.Broadcast(SwapBlockedReason, OldSlot.Class);
				Client_ActionBarSwapBlocked(SwapBlockedReason, OldSlot.Class);
			}
		}
	}

	// 2.5) Enforce the fixed final-slot potion rule in both directions.
	for (int32 Idx = 0; Idx < Sanitized.Num(); ++Idx)
	{
		FAeyerjiAbilitySlot& NewSlot = Sanitized[Idx];
		if (AeyerjiAbilitySlotUtils::IsAbilitySlotEmpty(NewSlot))
		{
			continue;
		}

		const bool bIsPotionSlot = AeyerjiAbilitySlotUtils::IsPotionSlotIndex(Idx, Sanitized.Num());
		const bool bIsPotionAbility = AeyerjiAbilitySlotUtils::IsPotionAbilityTagContainer(NewSlot.Tag);
		if (bIsPotionSlot == bIsPotionAbility)
		{
			continue;
		}

		const FAeyerjiAbilitySlot& OldSlot = ActionBar.IsValidIndex(Idx) ? ActionBar[Idx] : FAeyerjiAbilitySlot();
		const TSubclassOf<UGameplayAbility> BlockedClass = NewSlot.Class ? NewSlot.Class : OldSlot.Class;
		NewSlot = AeyerjiAbilitySlotUtils::IsSlotAllowedAtIndex(OldSlot, Idx, Sanitized.Num())
			? OldSlot
			: FAeyerjiAbilitySlot();

		const FText SwapBlockedReason = bIsPotionSlot
			? AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("ActionBarSwapBlockedPotionSlot"))
			: AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("ActionBarSwapBlockedPotionNormalSlot"));

		AJ_LOG(this, TEXT("Action bar slot %d rejected by potion-slot rule. IsPotionSlot=%d IsPotionAbility=%d"),
			Idx,
			bIsPotionSlot ? 1 : 0,
			bIsPotionAbility ? 1 : 0);
		OnActionBarSwapBlocked.Broadcast(SwapBlockedReason, BlockedClass);
		Client_ActionBarSwapBlocked(SwapBlockedReason, BlockedClass);
	}

	// 3) Final de-duplication pass: ensure only one instance of each ability remains.
	TSet<TSubclassOf<UGameplayAbility>> Seen;
	for (int32 Idx = 0; Idx < Sanitized.Num(); ++Idx)
	{
		const TSubclassOf<UGameplayAbility> Class = Sanitized[Idx].Class;
		if (!Class)
		{
			continue;
		}

		if (Seen.Contains(Class))
		{
			Sanitized[Idx] = FAeyerjiAbilitySlot();
		}
		else
		{
			Seen.Add(Class);
		}
	}

	for (FAeyerjiAbilitySlot& Slot : Sanitized)
	{
		if (!AeyerjiAbilitySlotUtils::IsAbilitySlotEmpty(Slot))
		{
			Slot.Level = GetProgressionRankForSlot(Slot);
		}
	}

	ActionBar = Sanitized;     // replicated property
	for (const FAeyerjiAbilitySlot& Slot : ActionBar)
	{
		GrantAbilityFromSlotInternal(Slot);
	}

	UE_LOG(LogTemp, Display,
		TEXT("[AbilityBar] PlayerState=%s Slots=%d ResolvedClasses=%d ValidateCooldowns=%d"),
		*GetNameSafe(this),
		ActionBar.Num(),
		CountActionBarSlotsWithClass(ActionBar),
		bValidateCooldowns ? 1 : 0);

	OnRep_ActionBar();         // run locally on the server for symmetry
	ForceNetUpdate();
}

void AAeyerjiPlayerState::Client_ActionBarSwapBlocked_Implementation(
		const FText& Reason, TSubclassOf<UGameplayAbility> AbilityClass)
{
	OnActionBarSwapBlocked.Broadcast(Reason, AbilityClass);
}

void AAeyerjiPlayerState::Client_BeginAuthoritativeProfileCommit_Implementation(
	const FAeyerjiSaveTransportHeader& Header,
	const int32 TotalBytes,
	const int32 ChunkSize)
{
	ResetPendingAuthoritativeProfileCommit();

	const int32 SafeChunkSize = FMath::Clamp(ChunkSize, 1, ProfileCommitTransportChunkSize);
	if (TotalBytes <= 0 || TotalBytes > UAeyerjiSaveManagerSubsystem::MaximumProfileTransportBytes)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ProfileCommit] State=Failed Phase=Begin Owner=%s Reason=InvalidSize Size=%d Maximum=%d"),
			*Header.OwnerKey,
			TotalBytes,
			UAeyerjiSaveManagerSubsystem::MaximumProfileTransportBytes);
		return;
	}

	PendingAuthoritativeProfileCommitHeader = Header;
	PendingAuthoritativeProfileCommitExpectedBytes = TotalBytes;
	PendingAuthoritativeProfileCommitChunkSize = SafeChunkSize;
	PendingAuthoritativeProfileCommitExpectedChunks = TotalBytes > 0 ? FMath::DivideAndRoundUp(TotalBytes, SafeChunkSize) : 0;
	PendingAuthoritativeProfileCommitBytes.SetNumZeroed(TotalBytes);
	PendingAuthoritativeProfileCommitReceivedChunks.Reset();
	bAuthoritativeProfileCommitTransferActive = true;

	UE_LOG(LogTemp, Display,
		TEXT("[ProfileCommit] State=Pending Phase=Begin Owner=%s PayloadBytes=%d Chunks=%d Revision=%lld"),
		*Header.OwnerKey,
		TotalBytes,
		PendingAuthoritativeProfileCommitExpectedChunks,
		Header.Revision);
}

void AAeyerjiPlayerState::Client_SendAuthoritativeProfileCommitChunk_Implementation(const int32 ChunkIndex, const TArray<uint8>& ChunkBytes)
{
	if (!bAuthoritativeProfileCommitTransferActive)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ProfileCommit] State=Failed Phase=Chunk Reason=NoActiveTransfer Chunk=%d Bytes=%d"),
			ChunkIndex,
			ChunkBytes.Num());
		return;
	}

	if (ChunkIndex < 0 || ChunkIndex >= PendingAuthoritativeProfileCommitExpectedChunks)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ProfileCommit] State=Failed Phase=Chunk Reason=BadIndex Chunk=%d ExpectedChunks=%d"),
			ChunkIndex,
			PendingAuthoritativeProfileCommitExpectedChunks);
		ResetPendingAuthoritativeProfileCommit();
		return;
	}

	if (PendingAuthoritativeProfileCommitReceivedChunks.Contains(ChunkIndex))
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[ProfileCommit] Duplicate profile commit chunk ignored Chunk=%d"),
			ChunkIndex);
		return;
	}

	const int32 Offset = ChunkIndex * PendingAuthoritativeProfileCommitChunkSize;
	const int32 ExpectedBytes = FMath::Min(PendingAuthoritativeProfileCommitChunkSize, PendingAuthoritativeProfileCommitExpectedBytes - Offset);
	if (ChunkBytes.Num() != ExpectedBytes)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ProfileCommit] State=Failed Phase=Chunk Reason=BadChunkSize Chunk=%d Bytes=%d Expected=%d"),
			ChunkIndex,
			ChunkBytes.Num(),
			ExpectedBytes);
		ResetPendingAuthoritativeProfileCommit();
		return;
	}

	if (ExpectedBytes > 0)
	{
		FMemory::Memcpy(PendingAuthoritativeProfileCommitBytes.GetData() + Offset, ChunkBytes.GetData(), ExpectedBytes);
	}
	PendingAuthoritativeProfileCommitReceivedChunks.Add(ChunkIndex);
}

void AAeyerjiPlayerState::Client_FinalizeAuthoritativeProfileCommit_Implementation()
{
	if (!bAuthoritativeProfileCommitTransferActive)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ProfileCommit] State=Failed Phase=Finalize Reason=NoActiveTransfer"));
		return;
	}

	if (PendingAuthoritativeProfileCommitReceivedChunks.Num() != PendingAuthoritativeProfileCommitExpectedChunks)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ProfileCommit] State=Failed Phase=Finalize Reason=MissingChunks Received=%d Expected=%d"),
			PendingAuthoritativeProfileCommitReceivedChunks.Num(),
			PendingAuthoritativeProfileCommitExpectedChunks);
		ResetPendingAuthoritativeProfileCommit();
		return;
	}

	const FAeyerjiSaveTransportHeader Header = PendingAuthoritativeProfileCommitHeader;
	const TArray<uint8> Bytes = PendingAuthoritativeProfileCommitBytes;
	ResetPendingAuthoritativeProfileCommit();

	bool bCommitted = false;
	if (UWorld* World = GetWorld())
	{
		if (UGameInstance* GameInstance = World->GetGameInstance())
		{
			if (UAeyerjiSaveManagerSubsystem* SaveManager = GameInstance->GetSubsystem<UAeyerjiSaveManagerSubsystem>())
			{
				bCommitted = SaveManager->CommitResolvedProfileForLocalOwner(Header, Bytes, this);
			}
		}
	}

	UE_LOG(LogTemp, Display,
		TEXT("[ProfileCommit] State=%s Phase=Finalize Owner=%s PayloadBytes=%d Revision=%lld"),
		bCommitted ? TEXT("Applied") : TEXT("Failed"),
		*Header.OwnerKey,
		Bytes.Num(),
		Header.Revision);
}

void AAeyerjiPlayerState::Server_GrantAbilityFromSlot_Implementation(
		const FAeyerjiAbilitySlot& AbilitySlot)
{
	FAeyerjiAbilitySlot AuthorizedSlot;
	if (!ResolveAuthorizedAbilitySlot(AbilitySlot, AuthorizedSlot)
		|| AeyerjiAbilitySlotUtils::IsAbilitySlotEmpty(AuthorizedSlot))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[AbilityLoad] GrantRequestRejected PlayerState=%s Tags=%s Class=%s"),
			*GetNameSafe(this),
			*AbilitySlot.Tag.ToString(),
			*GetNameSafe(AbilitySlot.Class));
		return;
	}

	GrantAbilityFromSlotInternal(AuthorizedSlot);
}

void AAeyerjiPlayerState::Server_RequestAbilityRankUp_Implementation(FGameplayTag AbilityTag)
{
	if (!HasAuthority())
	{
		return;
	}

	FText FailureReason;
	if (!CanUpgradeAbility(AbilityTag, FailureReason))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[AbilityProgression] UpgradeRejected PlayerState=%s Ability=%s Reason=%s"),
			*GetNameSafe(this),
			*AbilityTag.ToString(),
			*FailureReason.ToString());
		return;
	}

	UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UAeyerjiAbilityTuningSubsystem* Tuning = GameInstance ? GameInstance->GetSubsystem<UAeyerjiAbilityTuningSubsystem>() : nullptr;
	if (!Tuning)
	{
		return;
	}

	const int32 CurrentRank = GetAbilityRank(AbilityTag);
	if (CurrentRank >= MAX_int32)
	{
		return;
	}
	const int32 NextRank = CurrentRank + 1;
	const FAeyerjiAbilityRankTableRow* NextRankRow = Tuning->FindAbilityRankRow(AbilityTag, NextRank);
	if (!NextRankRow)
	{
		return;
	}

	FAeyerjiAbilityProgressEntry* Entry = FindMutableAbilityProgressEntry(AbilityTag);
	if (!Entry)
	{
		FAeyerjiAbilityProgressEntry NewEntry;
		NewEntry.AbilityTag = AbilityTag;
		NewEntry.CurrentRank = 1;
		AbilityProgressEntries.Add(NewEntry);
		Entry = &AbilityProgressEntries.Last();
	}

	UnspentAbilityPoints = FMath::Max(0, UnspentAbilityPoints - FMath::Max(1, NextRankRow->PointCost));
	TotalAbilityPointSpends = static_cast<int32>(FMath::Min<int64>(
		static_cast<int64>(MAX_int32),
		static_cast<int64>(TotalAbilityPointSpends) + FMath::Max(1, NextRankRow->PointCost)));
	Entry->CurrentRank = NextRank;
	Entry->LastUpgradePointSpendCount = TotalAbilityPointSpends;

	MirrorProgressionRanksIntoActionBar();
	SyncGrantedAbilityRank(AbilityTag);
	OnRep_AbilityProgression();
	OnRep_ActionBar();
	ForceNetUpdate();
	SyncProfileAbilityProgressionCache(TEXT("RankUp"));

	UE_LOG(LogTemp, Display,
		TEXT("[AbilityProgression] UpgradeApplied PlayerState=%s Ability=%s Rank=%d Unspent=%d Spends=%d"),
		*GetNameSafe(this),
		*AbilityTag.ToString(),
		Entry->CurrentRank,
		UnspentAbilityPoints,
		TotalAbilityPointSpends);
}

void AAeyerjiPlayerState::GrantAbilityFromSlotInternal(const FAeyerjiAbilitySlot& AbilitySlot)
{
	FAeyerjiAbilitySlot ResolvedSlot = AbilitySlot;
	ResolvedSlot.ResolveSavedReferences();
	NormalizeNativePotionAbilitySlot(ResolvedSlot);

	APawn* Pawn = GetPawn();
	if (!Pawn)
	{
		if (APlayerController* PC = GetPlayerController())
		{
			Pawn = PC->GetPawn();
		}
	}

	if (!Pawn)
	{
		AJ_LOG(this, TEXT("[AbilityLoad] GrantSkipped Reason=MissingPawn Slot=%s SavedClass=%s"),
			*ResolvedSlot.Description.ToString(),
			*ResolvedSlot.SavedAbilityClass.ToSoftObjectPath().ToString());
		UE_LOG(LogTemp, Warning, TEXT("[AbilityLoad] GrantSkipped Reason=MissingPawn PlayerState=%s Slot=%s"),
			*GetNameSafe(this),
			*ResolvedSlot.Description.ToString());
		return;
	}

	IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Pawn);
	if (!ASI)
	{
		AJ_LOG(this, TEXT("[AbilityLoad] GrantSkipped Reason=PawnMissingASI Pawn=%s Slot=%s"),
			*GetNameSafe(Pawn),
			*ResolvedSlot.Description.ToString());
		UE_LOG(LogTemp, Warning, TEXT("[AbilityLoad] GrantSkipped Reason=PawnMissingASI Pawn=%s Slot=%s"),
			*GetNameSafe(Pawn),
			*ResolvedSlot.Description.ToString());
		return;
	}

	UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent();
	if (!ASC)
	{
		AJ_LOG(this, TEXT("[AbilityLoad] GrantSkipped Reason=MissingASC Pawn=%s Slot=%s"),
			*GetNameSafe(Pawn),
			*ResolvedSlot.Description.ToString());
		UE_LOG(LogTemp, Warning, TEXT("[AbilityLoad] GrantSkipped Reason=MissingASC Pawn=%s Slot=%s"),
			*GetNameSafe(Pawn),
			*ResolvedSlot.Description.ToString());
		return;
	}

	if (!ResolvedSlot.Class)
	{
		if (ResolvedSlot.HasPersistentIdentity())
		{
			AJ_LOG(this, TEXT("[AbilityLoad] GrantSkipped Reason=UnresolvedClass Slot=%s SavedClass=%s Tags=%s"),
				*ResolvedSlot.Description.ToString(),
				*ResolvedSlot.SavedAbilityClass.ToSoftObjectPath().ToString(),
				*ResolvedSlot.Tag.ToString());
			UE_LOG(LogTemp, Warning,
				TEXT("[AbilityLoad] GrantSkipped Reason=UnresolvedClass Slot=%s SavedClass=%s Tags=%s"),
				*ResolvedSlot.Description.ToString(),
				*ResolvedSlot.SavedAbilityClass.ToSoftObjectPath().ToString(),
				*ResolvedSlot.Tag.ToString());
		}
		return;
	}

	ResolvedSlot.Level = GetProgressionRankForSlot(ResolvedSlot);

	// Avoid duplicates – compare by class
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.Ability->GetClass() == ResolvedSlot.Class)
		{
			FGameplayAbilitySpec* MutableSpec = ASC->FindAbilitySpecFromHandle(Spec.Handle);
			if (MutableSpec)
			{
				const int32 DesiredRank = FMath::Max(1, ResolvedSlot.Level);
				if (MutableSpec->Level != DesiredRank)
				{
					MutableSpec->Level = DesiredRank;
					ASC->MarkAbilitySpecDirty(*MutableSpec);
				}
			}

			AJ_LOG(this, TEXT("[AbilityLoad] GrantSynced Reason=AlreadyOwned Class=%s Rank=%d Pawn=%s"),
				*GetNameSafe(ResolvedSlot.Class),
				FMath::Max(1, ResolvedSlot.Level),
				*GetNameSafe(Pawn));
			return;
		}
	}

	const int32 AbilityLevel = FMath::Max(1, ResolvedSlot.Level);
	FGameplayAbilitySpec NewSpec(ResolvedSlot.Class, AbilityLevel, INDEX_NONE, this);
	const FGameplayAbilitySpecHandle Handle = ASC->GiveAbility(NewSpec);
	if (FGameplayAbilitySpec* GrantedSpec = ASC->FindAbilitySpecFromHandle(Handle))
	{
		ASC->MarkAbilitySpecDirty(*GrantedSpec);
	}

	UE_LOG(LogTemp, Display,
		TEXT("[AbilityLoad] Granted Class=%s Level=%d Pawn=%s PlayerState=%s"),
		*GetNameSafe(ResolvedSlot.Class),
		AbilityLevel,
		*GetNameSafe(Pawn),
		*GetNameSafe(this));
}

bool AAeyerjiPlayerState::CommitProfileSaveToOwningClient(UAeyerjiSaveGame* SaveData, const bool bBumpRevision)
{
	if (!HasAuthority() || !SaveData)
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	if (!GameInstance)
	{
		return false;
	}

	UAeyerjiSaveManagerSubsystem* SaveManager = GameInstance->GetSubsystem<UAeyerjiSaveManagerSubsystem>();
	if (!SaveManager)
	{
		return false;
	}

	FAeyerjiSaveTransportHeader Header;
	TArray<uint8> Bytes;
	if (!SaveManager->PrepareProfileForServerCommit(this, SaveData, bBumpRevision, Header, Bytes))
	{
		return false;
	}

	const ENetMode NetMode = World->GetNetMode();
	APlayerController* PlayerController = GetPlayerController();
	const bool bShouldCommitLocally =
		NetMode == NM_Standalone
		|| (NetMode != NM_DedicatedServer && PlayerController && PlayerController->IsLocalController());

	if (bShouldCommitLocally)
	{
		const bool bCommitted = SaveManager->CommitResolvedProfileForLocalOwner(Header, Bytes, this);
		UE_LOG(LogTemp, Display,
			TEXT("[ProfileCommit] Phase=LocalSync Result=%d NetMode=%d PlayerController=%s Owner=%s Revision=%lld PayloadBytes=%d ActionBar=%d ResolvedClasses=%d"),
			bCommitted ? 1 : 0,
			static_cast<int32>(NetMode),
			*GetNameSafe(PlayerController),
			*Header.OwnerKey,
			Header.Revision,
			Bytes.Num(),
			SaveData->ActionBar.Num(),
			CountActionBarSlotsWithClass(SaveData->ActionBar));
		return bCommitted;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[ProfileCommit] Phase=ClientTransfer NetMode=%d PlayerController=%s Owner=%s Revision=%lld PayloadBytes=%d ActionBar=%d ResolvedClasses=%d"),
		static_cast<int32>(NetMode),
		*GetNameSafe(PlayerController),
		*Header.OwnerKey,
		Header.Revision,
		Bytes.Num(),
		SaveData->ActionBar.Num(),
		CountActionBarSlotsWithClass(SaveData->ActionBar));
	SendAuthoritativeProfileCommitToOwningClient(Header, Bytes);
	return true;
}

void AAeyerjiPlayerState::SendAuthoritativeProfileCommitToOwningClient(const FAeyerjiSaveTransportHeader& Header, const TArray<uint8>& Bytes)
{
	const int32 TotalBytes = Bytes.Num();
	if (TotalBytes > LegacyProfileCommitRpcWarningBytes)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ProfileCommit] Payload=%d Owner=%s exceeds legacy RPC array risk threshold; using chunked client commit."),
			TotalBytes,
			*Header.OwnerKey);
	}

	Client_BeginAuthoritativeProfileCommit(Header, TotalBytes, ProfileCommitTransportChunkSize);
	for (int32 Offset = 0, ChunkIndex = 0; Offset < TotalBytes; Offset += ProfileCommitTransportChunkSize, ++ChunkIndex)
	{
		const int32 ChunkBytes = FMath::Min(ProfileCommitTransportChunkSize, TotalBytes - Offset);
		TArray<uint8> Chunk;
		Chunk.Append(Bytes.GetData() + Offset, ChunkBytes);
		Client_SendAuthoritativeProfileCommitChunk(ChunkIndex, Chunk);
	}
	Client_FinalizeAuthoritativeProfileCommit();
}

void AAeyerjiPlayerState::ResetPendingAuthoritativeProfileCommit()
{
	PendingAuthoritativeProfileCommitHeader = FAeyerjiSaveTransportHeader();
	PendingAuthoritativeProfileCommitBytes.Reset();
	PendingAuthoritativeProfileCommitReceivedChunks.Reset();
	PendingAuthoritativeProfileCommitExpectedBytes = 0;
	PendingAuthoritativeProfileCommitExpectedChunks = 0;
	PendingAuthoritativeProfileCommitChunkSize = 0;
	bAuthoritativeProfileCommitTransferActive = false;
}

bool AAeyerjiPlayerState::CommitPreparedCheckpointProfile(
	UAeyerjiSaveGame* SaveData,
	const EAeyerjiSaveCheckpointReason Reason,
	const bool bBumpRevision)
{
	if (!SaveData)
	{
		return false;
	}

	LogCheckpointSummary(this, SaveData, Reason, bBumpRevision, TEXT("Prepared"));

	return CommitProfileSaveToOwningClient(SaveData, bBumpRevision);
}

bool AAeyerjiPlayerState::CommitCheckpointProfile(EAeyerjiSaveCheckpointReason Reason)
{
	return CommitCheckpointProfileFromPawn(Reason, GetPawn(), /*bBumpRevision=*/true);
}

bool AAeyerjiPlayerState::CommitCheckpointProfileFromPawn(
	const EAeyerjiSaveCheckpointReason Reason,
	const APawn* SourcePawn,
	const bool bBumpRevision)
{
	if (!HasAuthority())
	{
		return false;
	}

	UWorld* World = GetWorld();
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	UAeyerjiSaveManagerSubsystem* SaveManager =
		GameInstance ? GameInstance->GetSubsystem<UAeyerjiSaveManagerSubsystem>() : nullptr;
	if (!SaveManager)
	{
		return false;
	}

	UAeyerjiSaveGame* WorkingSave = nullptr;
	if (!SaveManager->GetServerCachedProfile(this, WorkingSave) || !WorkingSave)
	{
		WorkingSave = SaveManager->CreateDefaultProfile(SaveManager->ResolveOwnerKey(this), 1);
	}

	if (!WorkingSave)
	{
		return false;
	}

	const FString Slot = UCharacterStatsLibrary::MakeStableCharSlotName(this);
	if (!UCharacterStatsLibrary::BuildAeyerjiSaveDataFromRuntime(WorkingSave, this, Slot, SourcePawn ? SourcePawn : GetPawn()))
	{
		return false;
	}

	LogCheckpointSummary(this, WorkingSave, Reason, bBumpRevision, TEXT("Captured"));

	return CommitPreparedCheckpointProfile(WorkingSave, Reason, bBumpRevision);
}

void AAeyerjiPlayerState::RequestSetSaveSlotOverride(const FString& NewSlot)
{
	if (HasAuthority())
	{
		ApplySaveSlotOverride(NewSlot);
	}
	else
	{
		// The owning client resolves local/cloud profile data before sending it to the server.
		// Apply the sanitized slot locally first so client-side resolution uses the same slot
		// that the server will authorize once the RPC arrives.
		ApplySaveSlotOverride(NewSlot);
		Server_SetSaveSlotOverride(NewSlot);
	}
}

void AAeyerjiPlayerState::RequestStartRun()
{
	if (HasAuthority())
	{
		Server_RequestStartRun_Implementation();
		return;
	}

	Server_RequestStartRun();
}

void AAeyerjiPlayerState::Server_RequestStartRun_Implementation()
{
	if (UWorld* World = GetWorld())
	{
		if (AAeyerjiGameState* GS = World->GetGameState<AAeyerjiGameState>())
		{
			if (GS->IsRunControlRequesterAuthorized(this))
			{
				GS->Server_StartRun();
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[RiftRun][Start] Client request rejected: %s is not the run leader"), *GetNameSafe(this));
			}
		}
	}
}

void AAeyerjiPlayerState::RequestEndRun()
{
	if (HasAuthority())
	{
		Server_RequestEndRun_Implementation();
		return;
	}

	Server_RequestEndRun();
}

void AAeyerjiPlayerState::Server_RequestEndRun_Implementation()
{
	if (UWorld* World = GetWorld())
	{
		if (AAeyerjiGameState* GS = World->GetGameState<AAeyerjiGameState>())
		{
			if (GS->IsRunControlRequesterAuthorized(this))
			{
				GS->Server_MarkRunComplete();
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[RiftRun][End] Client request rejected: %s is not the run leader"), *GetNameSafe(this));
			}
		}
	}
}

void AAeyerjiPlayerState::RequestReturnToMenu()
{
	UE_LOG(LogTemp, Display, TEXT("AeyerjiPlayerState: RequestReturnToMenu PlayerState=%s HasAuthority=%d"), *GetNameSafe(this), HasAuthority() ? 1 : 0);

	if (HasAuthority())
	{
		Server_RequestReturnToMenu_Implementation();
		return;
	}

	Server_RequestReturnToMenu();
}

void AAeyerjiPlayerState::RequestRetryRun()
{
	UE_LOG(LogTemp, Display, TEXT("AeyerjiPlayerState: RequestRetryRun PlayerState=%s HasAuthority=%d"), *GetNameSafe(this), HasAuthority() ? 1 : 0);

	if (HasAuthority())
	{
		Server_RequestRetryRun_Implementation();
		return;
	}

	Server_RequestRetryRun();
}

void AAeyerjiPlayerState::RequestRetryEarnedRiftTier()
{
	if (HasAuthority())
	{
		Server_RequestRetryEarnedRiftTier_Implementation();
		return;
	}

	Server_RequestRetryEarnedRiftTier();
}

void AAeyerjiPlayerState::RequestSelectRiftTier(const int32 RequestedTier)
{
	if (HasAuthority())
	{
		Server_RequestSelectRiftTier_Implementation(RequestedTier);
		return;
	}

	Server_RequestSelectRiftTier(RequestedTier);
}

void AAeyerjiPlayerState::Server_RequestReturnToMenu_Implementation()
{
	UE_LOG(LogTemp, Display, TEXT("AeyerjiPlayerState: Server_RequestReturnToMenu PlayerState=%s"), *GetNameSafe(this));

	CommitCheckpointProfile(EAeyerjiSaveCheckpointReason::ReturnToMenu);

	if (UWorld* World = GetWorld())
	{
		if (AAeyerjiGameState* GS = World->GetGameState<AAeyerjiGameState>())
		{
			if (GS->IsRunControlRequesterAuthorized(this))
			{
				GS->Server_ReturnToMenu();
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[RiftRun][Return] Client request rejected: %s is not the run leader"), *GetNameSafe(this));
			}
		}
	}
}

void AAeyerjiPlayerState::Server_RequestRetryRun_Implementation()
{
	UE_LOG(LogTemp, Display, TEXT("AeyerjiPlayerState: Server_RequestRetryRun PlayerState=%s"), *GetNameSafe(this));

	if (UWorld* World = GetWorld())
	{
		if (AAeyerjiGameState* GS = World->GetGameState<AAeyerjiGameState>())
		{
			EAeyerjiRiftTierSelectionFailure Failure = EAeyerjiRiftTierSelectionFailure::None;
			if (!GS->Server_RetryRiftRunForRequester(this, false, Failure))
			{
				Client_RiftTierSelectionRejected(Failure, GS->GetRunResults().SelectedRiftTier);
			}
		}
	}
}

void AAeyerjiPlayerState::Server_RequestRetryEarnedRiftTier_Implementation()
{
	EAeyerjiRiftTierSelectionFailure Failure = EAeyerjiRiftTierSelectionFailure::None;
	int32 RequestedTier = 0;
	if (UWorld* World = GetWorld())
	{
		if (AAeyerjiGameState* GameState = World->GetGameState<AAeyerjiGameState>())
		{
			RequestedTier = GameState->GetRiftRunState().EarnedNextRiftTier;
			if (GameState->Server_RetryRiftRunForRequester(this, true, Failure))
			{
				return;
			}
		}
		else
		{
			Failure = EAeyerjiRiftTierSelectionFailure::RunNotReady;
		}
	}
	Client_RiftTierSelectionRejected(Failure, RequestedTier);
}

void AAeyerjiPlayerState::Server_RequestSelectRiftTier_Implementation(const int32 RequestedTier)
{
	EAeyerjiRiftTierSelectionFailure Failure = EAeyerjiRiftTierSelectionFailure::None;
	bool bAccepted = false;
	if (UWorld* World = GetWorld())
	{
		if (AAeyerjiGameState* GameState = World->GetGameState<AAeyerjiGameState>())
		{
			bAccepted = GameState->Server_TrySelectRiftTier(this, RequestedTier, Failure);
		}
		else
		{
			Failure = EAeyerjiRiftTierSelectionFailure::RunNotReady;
		}
	}

	if (!bAccepted)
	{
		Client_RiftTierSelectionRejected(Failure, RequestedTier);
	}
}

bool AAeyerjiPlayerState::SubmitFrontendProfile(const FAeyerjiSaveTransportHeader& Header, const TArray<uint8>& Bytes)
{
	if (Bytes.Num() <= 0 || Bytes.Num() > UAeyerjiSaveManagerSubsystem::MaximumProfileTransportBytes)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Lobby] ProfileSubmissionRejected Player=%s Reason=ClientSize Bytes=%d"), *GetPlayerName(), Bytes.Num());
		return false;
	}

	Server_BeginFrontendProfileSubmission(Header, Bytes.Num(), FrontendProfileTransportChunkSize);
	for (int32 Offset = 0, ChunkIndex = 0; Offset < Bytes.Num(); Offset += FrontendProfileTransportChunkSize, ++ChunkIndex)
	{
		const int32 Count = FMath::Min(FrontendProfileTransportChunkSize, Bytes.Num() - Offset);
		TArray<uint8> Chunk;
		Chunk.Append(Bytes.GetData() + Offset, Count);
		Server_SendFrontendProfileChunk(ChunkIndex, Chunk);
	}
	Server_FinalizeFrontendProfileSubmission();
	return true;
}

void AAeyerjiPlayerState::Server_BeginFrontendProfileSubmission_Implementation(
	const FAeyerjiSaveTransportHeader& Header, const int32 TotalBytes, const int32 ChunkSize)
{
	ResetPendingFrontendProfileSubmission();
	UAeyerjiSaveManagerSubsystem* SaveManager = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAeyerjiSaveManagerSubsystem>() : nullptr;
	const FString ExpectedOwner = SaveManager ? SaveManager->ResolveOwnerKey(this) : FString();
	const FUniqueNetIdRepl& PlayerUniqueId = GetUniqueId();
	const TSharedPtr<const FUniqueNetId> NativeUniqueId = PlayerUniqueId.GetUniqueNetId();
	const bool bNullIdentity = PlayerUniqueId.IsValid() && NativeUniqueId.IsValid()
		&& NativeUniqueId->GetType().IsEqual(FName(TEXT("NULL")), ENameCase::IgnoreCase);
	const bool bOwnerAccepted = AeyerjiFrontendRules::IsProfileOwnerKeyAccepted(
		Header.OwnerKey, ExpectedOwner, FrontendProfileOwnerKey, bNullIdentity);
	const bool bValid = SaveManager
		&& Header.ArtifactKind == EAeyerjiSaveArtifactKind::Profile
		&& Header.SchemaVersion > 0
		&& bOwnerAccepted
		&& AeyerjiFrontendRules::IsProfileTransferLayoutValid(
			TotalBytes, ChunkSize, UAeyerjiSaveManagerSubsystem::MaximumProfileTransportBytes, FrontendProfileTransportChunkSize);
	if (!bValid)
	{
		const FString IdentityType = NativeUniqueId.IsValid()
			? NativeUniqueId->GetType().ToString() : FString(TEXT("INVALID"));
		UE_LOG(LogTemp, Warning,
			TEXT("[Lobby] ProfileSubmissionValidation Player=%s SubmittedOwner=%s ExpectedOwner=%s BoundOwner=%s IdentityType=%s Artifact=%d Schema=%d Bytes=%d ChunkSize=%d"),
			*GetPlayerName(), *Header.OwnerKey, *ExpectedOwner, *FrontendProfileOwnerKey,
			*IdentityType, static_cast<int32>(Header.ArtifactKind), Header.SchemaVersion,
			TotalBytes, ChunkSize);
		RejectFrontendProfileSubmission(TEXT("InvalidHeaderIdentityOrSize"));
		return;
	}

	PendingFrontendProfileHeader = Header;
	PendingFrontendProfileExpectedBytes = TotalBytes;
	PendingFrontendProfileChunkSize = ChunkSize;
	PendingFrontendProfileExpectedChunks = FMath::DivideAndRoundUp(TotalBytes, ChunkSize);
	PendingFrontendProfileBytes.SetNumZeroed(TotalBytes);
	bFrontendProfileTransferActive = true;
	FrontendProfileState = EAeyerjiLobbyProfileState::Receiving;
	bFrontendReady = false;
	ForceNetUpdate();
	UE_LOG(LogTemp, Display, TEXT("[Lobby] ProfileTransferBegin Player=%s Owner=%s Revision=%lld Bytes=%d Chunks=%d"),
		*GetPlayerName(), *Header.OwnerKey, Header.Revision, TotalBytes, PendingFrontendProfileExpectedChunks);
}

void AAeyerjiPlayerState::Server_SendFrontendProfileChunk_Implementation(const int32 ChunkIndex, const TArray<uint8>& ChunkBytes)
{
	if (!bFrontendProfileTransferActive
		|| ChunkIndex < 0 || ChunkIndex >= PendingFrontendProfileExpectedChunks
		|| PendingFrontendProfileReceivedChunks.Contains(ChunkIndex))
	{
		RejectFrontendProfileSubmission(TEXT("InvalidOrDuplicateChunk"));
		return;
	}
	const int32 Offset = ChunkIndex * PendingFrontendProfileChunkSize;
	const int32 ExpectedCount = FMath::Min(PendingFrontendProfileChunkSize, PendingFrontendProfileExpectedBytes - Offset);
	if (ChunkBytes.Num() != ExpectedCount)
	{
		RejectFrontendProfileSubmission(TEXT("ChunkSizeMismatch"));
		return;
	}
	FMemory::Memcpy(PendingFrontendProfileBytes.GetData() + Offset, ChunkBytes.GetData(), ExpectedCount);
	PendingFrontendProfileReceivedChunks.Add(ChunkIndex);
}

void AAeyerjiPlayerState::Server_FinalizeFrontendProfileSubmission_Implementation()
{
	if (!bFrontendProfileTransferActive
		|| PendingFrontendProfileReceivedChunks.Num() != PendingFrontendProfileExpectedChunks)
	{
		RejectFrontendProfileSubmission(TEXT("MissingChunks"));
		return;
	}

	UAeyerjiSaveManagerSubsystem* SaveManager = GetGameInstance()
		? GetGameInstance()->GetSubsystem<UAeyerjiSaveManagerSubsystem>() : nullptr;
	UAeyerjiSaveGame* SaveData = SaveManager
		? SaveManager->DeserializeProfileFromTransport(PendingFrontendProfileHeader, PendingFrontendProfileBytes) : nullptr;
	if (!SaveManager || !SaveData || !SaveManager->IsManagerEraProfile(SaveData))
	{
		RejectFrontendProfileSubmission(TEXT("DeserializeOrSchemaFailed"));
		return;
	}

	const int64 PreviousRevision = FrontendProfileRevision;
	const FString PreviousBoundOwner = FrontendProfileOwnerKey;
	FrontendProfileOwnerKey = PendingFrontendProfileHeader.OwnerKey;
	FAeyerjiSaveTransportHeader PreparedHeader;
	TArray<uint8> PreparedBytes;
	if (!SaveManager->PrepareProfileForServerCommit(this, SaveData, false, PreparedHeader, PreparedBytes))
	{
		FrontendProfileOwnerKey = PreviousBoundOwner;
		RejectFrontendProfileSubmission(TEXT("ServerCacheFailed"));
		return;
	}

	FrontendCharacterLevel = FMath::Max(1, SaveData->Attributes.Level);
	FrontendHighestUnlockedTier = FMath::Max(1, SaveData->HighestUnlockedRiftTier);
	FrontendProfileRevision = SaveData->Revision;
	FrontendProfileState = EAeyerjiLobbyProfileState::Verified;
	if (PreviousRevision != 0 && PreviousRevision != FrontendProfileRevision)
	{
		bFrontendReady = false;
	}
	ResetPendingFrontendProfileSubmission();
	ForceNetUpdate();
	UE_LOG(LogTemp, Display, TEXT("[Lobby] ProfileVerified Player=%s Revision=%lld Level=%d HighestTier=%d"),
		*GetPlayerName(), FrontendProfileRevision, FrontendCharacterLevel, FrontendHighestUnlockedTier);
	if (AAeyerjiGameState* GS = GetWorld() ? GetWorld()->GetGameState<AAeyerjiGameState>() : nullptr)
	{
		GS->Server_NotifyFrontendProfileChanged(this, PreviousRevision != FrontendProfileRevision);
	}
}

void AAeyerjiPlayerState::ResetPendingFrontendProfileSubmission()
{
	PendingFrontendProfileHeader = FAeyerjiSaveTransportHeader();
	PendingFrontendProfileBytes.Reset();
	PendingFrontendProfileReceivedChunks.Reset();
	PendingFrontendProfileExpectedBytes = 0;
	PendingFrontendProfileExpectedChunks = 0;
	PendingFrontendProfileChunkSize = 0;
	bFrontendProfileTransferActive = false;
}

void AAeyerjiPlayerState::RejectFrontendProfileSubmission(const TCHAR* Reason)
{
	UE_LOG(LogTemp, Warning, TEXT("[Lobby] ProfileSubmissionRejected Player=%s Reason=%s"), *GetPlayerName(), Reason ? Reason : TEXT("Unknown"));
	ResetPendingFrontendProfileSubmission();
	FrontendProfileState = EAeyerjiLobbyProfileState::Failed;
	bFrontendReady = false;
	ForceNetUpdate();
	if (AAeyerjiGameState* GS = GetWorld() ? GetWorld()->GetGameState<AAeyerjiGameState>() : nullptr)
	{
		GS->Server_NotifyFrontendProfileChanged(this, true);
	}
}

void AAeyerjiPlayerState::RequestFrontendReady(const bool bReady)
{
	Server_RequestFrontendReady(bReady);
}

void AAeyerjiPlayerState::RequestFrontendActivity(const EAeyerjiRiftActivityType ActivityType)
{
	Server_RequestFrontendActivity(ActivityType);
}

void AAeyerjiPlayerState::RequestFrontendTier(const int32 Tier)
{
	Server_RequestFrontendTier(Tier);
}

void AAeyerjiPlayerState::RequestFrontendLaunch()
{
	Server_RequestFrontendLaunch();
}

void AAeyerjiPlayerState::Server_RequestFrontendReady_Implementation(const bool bReady)
{
	if (AAeyerjiGameState* GS = GetWorld() ? GetWorld()->GetGameState<AAeyerjiGameState>() : nullptr)
	{
		if (!GS->Server_SetFrontendReady(this, bReady))
		{
			Client_FrontendRequestRejected(EAeyerjiFrontendFailure::PartyNotReady);
		}
	}
}

void AAeyerjiPlayerState::Server_RequestFrontendActivity_Implementation(const EAeyerjiRiftActivityType ActivityType)
{
	if (AAeyerjiGameState* GS = GetWorld() ? GetWorld()->GetGameState<AAeyerjiGameState>() : nullptr)
	{
		if (!GS->Server_SetFrontendActivity(this, ActivityType))
		{
			Client_FrontendRequestRejected(EAeyerjiFrontendFailure::NotLeader);
		}
	}
}

void AAeyerjiPlayerState::Server_RequestFrontendTier_Implementation(const int32 Tier)
{
	if (AAeyerjiGameState* GS = GetWorld() ? GetWorld()->GetGameState<AAeyerjiGameState>() : nullptr)
	{
		if (!GS->Server_SetFrontendTier(this, Tier))
		{
			Client_FrontendRequestRejected(EAeyerjiFrontendFailure::TierLockedForParty);
		}
	}
}

void AAeyerjiPlayerState::Server_RequestFrontendLaunch_Implementation()
{
	if (AAeyerjiGameState* GS = GetWorld() ? GetWorld()->GetGameState<AAeyerjiGameState>() : nullptr)
	{
		EAeyerjiFrontendFailure Failure = EAeyerjiFrontendFailure::LaunchFailed;
		if (!GS->Server_RequestFrontendLaunch(this, Failure))
		{
			Client_FrontendRequestRejected(Failure);
		}
	}
}

void AAeyerjiPlayerState::Client_FrontendRequestRejected_Implementation(const EAeyerjiFrontendFailure Failure)
{
	OnFrontendRequestRejectedNative.Broadcast(Failure);
}

void AAeyerjiPlayerState::SetFrontendReadyFromServer(const bool bReady)
{
	if (!HasAuthority())
	{
		return;
	}
	bFrontendReady = bReady && FrontendProfileState == EAeyerjiLobbyProfileState::Verified;
	ForceNetUpdate();
}

void AAeyerjiPlayerState::Client_RiftTierSelectionRejected_Implementation(
	const EAeyerjiRiftTierSelectionFailure Reason,
	const int32 RequestedTier)
{
	OnRiftTierSelectionRejected.Broadcast(Reason, RequestedTier);
}

void AAeyerjiPlayerState::Server_SetSaveSlotOverride_Implementation(const FString& NewSlot)
{
	ApplySaveSlotOverride(NewSlot);
}

void AAeyerjiPlayerState::Server_SelectPassive_Implementation(FName PassiveId)
{
	SetPassiveLocal(PassiveId);
}

void AAeyerjiPlayerState::SetPassiveLocal(FName PassiveId)
{
	if (!HasAuthority() || PassiveId.IsNone())
	{
		return;
	}

	if (!PassiveOptions.Contains(PassiveId))
	{
		AJ_LOG(this, TEXT("SetPassiveLocal rejected passive %s (not in options)"), *PassiveId.ToString());
		return;
	}

	if (SelectedPassiveId == PassiveId)
	{
		return;
	}

	SelectedPassiveId = PassiveId;
	OnRep_SelectedPassive();
}

void AAeyerjiPlayerState::ApplySaveSlotOverride(const FString& NewSlot)
{
	const FString Sanitized = UCharacterStatsLibrary::SanitizeSaveSlotName(NewSlot);
	if (Sanitized == SaveSlotOverride)
	{
		return;
	}

	SaveSlotOverride = Sanitized;
	OnRep_SaveSlotOverride();
}

void AAeyerjiPlayerState::GetLifetimeReplicatedProps(
		TArray< FLifetimeProperty >& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	DOREPLIFETIME(AAeyerjiPlayerState, ActionBar);
	DOREPLIFETIME(AAeyerjiPlayerState, SaveSlotOverride);
	DOREPLIFETIME(AAeyerjiPlayerState, ProfileLoadState);
	DOREPLIFETIME(AAeyerjiPlayerState, SelectedPassiveId);
	DOREPLIFETIME(AAeyerjiPlayerState, AbilityProgressEntries);
	DOREPLIFETIME(AAeyerjiPlayerState, UnspentAbilityPoints);
	DOREPLIFETIME(AAeyerjiPlayerState, TotalAbilityPointSpends);
	DOREPLIFETIME(AAeyerjiPlayerState, Gold);
	DOREPLIFETIME(AAeyerjiPlayerState, HighestUnlockedRiftTier);
	DOREPLIFETIME(AAeyerjiPlayerState, LastSelectedRiftTier);
	DOREPLIFETIME_CONDITION_NOTIFY(AAeyerjiPlayerState, PersonalRunResults, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME(AAeyerjiPlayerState, FrontendProfileState);
	DOREPLIFETIME(AAeyerjiPlayerState, FrontendCharacterLevel);
	DOREPLIFETIME(AAeyerjiPlayerState, FrontendHighestUnlockedTier);
	DOREPLIFETIME(AAeyerjiPlayerState, FrontendProfileRevision);
	DOREPLIFETIME(AAeyerjiPlayerState, bFrontendReady);
}

void AAeyerjiPlayerState::OnRep_ActionBar()
{
	// 1) Notify any Blueprint listeners (widgets, pawns, controllers…)
	OnActionBarChanged.Broadcast(ActionBar);
	
	
	// 2) Optional: let Blueprints override in a child BP if you like
	//     (Uncomment if you want a BPImplementableEvent instead of the broadcast)
	// BP_ActionBarChanged(ActionBar);
}

void AAeyerjiPlayerState::OnRep_SaveSlotOverride()
{
	OnSaveSlotOverrideChanged.Broadcast(SaveSlotOverride);
}

void AAeyerjiPlayerState::OnRep_ProfileLoadState()
{
	UE_LOG(LogTemp, Verbose,
		TEXT("[ProfileLoad] PlayerState=%s ReplicatedState=%s"),
		*GetNameSafe(this),
		LexToStringAeyerjiProfileLoadState(ProfileLoadState));
}

void AAeyerjiPlayerState::SetProfileLoadState(const EAeyerjiProfileLoadState NewState)
{
	if (ProfileLoadState == NewState)
	{
		return;
	}

	ProfileLoadState = NewState;
	OnRep_ProfileLoadState();
	ForceNetUpdate();
}

void AAeyerjiPlayerState::OnRep_SelectedPassive()
{
	ApplySelectedPassive(SelectedPassiveId, /*bBroadcast*/true);
}

void AAeyerjiPlayerState::OnRep_AbilityProgression()
{
	MirrorProgressionRanksIntoActionBar();
	OnAbilityProgressionChanged.Broadcast(AbilityProgressEntries, UnspentAbilityPoints, TotalAbilityPointSpends);
	OnRep_ActionBar();
}

void AAeyerjiPlayerState::OnRep_Gold(const int64 OldGold)
{
	OnGoldChanged.Broadcast(Gold, Gold - OldGold);
}

void AAeyerjiPlayerState::OnRep_RiftTierProgression()
{
	OnRiftTierProgressionChanged.Broadcast(HighestUnlockedRiftTier, LastSelectedRiftTier);
}

void AAeyerjiPlayerState::OnRep_PersonalRunResults()
{
	if (PersonalRunResults.ResultsVersion > 0)
	{
		OnPersonalRunResultsChanged.Broadcast(PersonalRunResults);
	}
}

void AAeyerjiPlayerState::ApplySelectedPassive(FName PassiveId, bool bBroadcast)
{
	if (!PassiveId.IsNone())
	{
		SelectedPassiveId = PassiveId;
	}

	if (bBroadcast)
	{
		OnPassiveChanged.Broadcast(SelectedPassiveId);
	}
}

void AAeyerjiPlayerState::ApplyGoldValue(const int64 NewGold, const FName Reason, const bool bBroadcast)
{
	const int64 ClampedGold = FMath::Max<int64>(0, NewGold);
	if (Gold == ClampedGold)
	{
		return;
	}

	const int64 OldGold = Gold;
	Gold = ClampedGold;

	UE_LOG(LogTemp, Display,
		TEXT("[Currency] GoldChanged PlayerState=%s Reason=%s Old=%lld New=%lld Delta=%lld"),
		*GetNameSafe(this),
		*Reason.ToString(),
		OldGold,
		Gold,
		Gold - OldGold);

	if (bBroadcast)
	{
		OnGoldChanged.Broadcast(Gold, Gold - OldGold);
	}

	if (HasAuthority())
	{
		ForceNetUpdate();
	}
}
