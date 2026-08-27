#include "CharacterStatsLibrary.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Aeyerji/AeyerjiGameMode.h"
#include "Aeyerji/AeyerjiPlayerState.h"
#include "Aeyerji/AeyerjiSaveGame.h"
#include "../AeyerjiGameInstance.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/Controller.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "GameFramework/Character.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "GameFramework/PlayerController.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "AIController.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "Blueprint/AIBlueprintHelperLibrary.h"
#include "Navigation/PathFollowingComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Progression/AeyerjiLevelingComponent.h"
#include "Logging/AeyerjiLog.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformProcess.h"
#include "Templates/SubclassOf.h"
#include "Microsoft/AllowMicrosoftPlatformTypes.h"
#include "GameplayTagContainer.h"
#include "Kismet/KismetMathLibrary.h"
#include "Items/InventoryComponent.h"
#include "Items/ItemDefinition.h"
#include "Player/PlayerStatsTrackingComponent.h"
#include "Systems/AeyerjiSaveManagerSubsystem.h"
#include "Systems/AeyerjiDifficultyTuning.h"
#include "Systems/LootService.h"
#include "Systems/AeyerjiWorldStateSubsystem.h"
#include "Engine/AssetManager.h"
#include "Engine/World.h"

//This works together with @EAeyerjiStat in CharacterStatsLibrary.h
namespace
{
	float GetDefaultDifficultySlider()
	{
		return UAeyerjiDifficultySettings::WorldTierToDifficultySlider(UAeyerjiDifficultySettings::GetNormalWorldTier());
	}

	int32 GetDefaultWorldTier()
	{
		return UAeyerjiDifficultySettings::GetNormalWorldTier();
	}

	bool IsAbilitySlotPersisted(const FAeyerjiAbilitySlot& Slot)
	{
		return Slot.HasPersistentIdentity();
	}

	int32 CountPersistedActionBarSlots(const TArray<FAeyerjiAbilitySlot>& ActionBar)
	{
		int32 Count = 0;
		for (const FAeyerjiAbilitySlot& Slot : ActionBar)
		{
			if (IsAbilitySlotPersisted(Slot))
			{
				++Count;
			}
		}

		return Count;
	}

	int32 CountResolvedAbilityClasses(const TArray<FAeyerjiAbilitySlot>& ActionBar)
	{
		int32 Count = 0;
		for (const FAeyerjiAbilitySlot& Slot : ActionBar)
		{
			if (Slot.Class)
			{
				++Count;
			}
		}

		return Count;
	}

	void NormalizeActionBarForPersistence(TArray<FAeyerjiAbilitySlot>& ActionBar)
	{
		for (FAeyerjiAbilitySlot& Slot : ActionBar)
		{
			Slot.CaptureStableReferences();
			Slot.ResolveSavedReferences();
		}
	}

	void LogActionBarPersistenceSnapshot(const TCHAR* Phase, const FString& SlotName, const TArray<FAeyerjiAbilitySlot>& ActionBar)
	{
		for (int32 Index = 0; Index < ActionBar.Num(); ++Index)
		{
			const FAeyerjiAbilitySlot& Slot = ActionBar[Index];
			UE_LOG(LogTemp, Display,
				TEXT("[AbilitySave] Phase=%s Slot=%s Index=%d Description=%s RuntimeClass=%s SavedClass=%s Tags=%s"),
				Phase,
				*SlotName,
				Index,
				*Slot.Description.ToString(),
				*GetNameSafe(Slot.Class),
				*Slot.SavedAbilityClass.ToSoftObjectPath().ToString(),
				*Slot.Tag.ToString());
		}
	}

	bool PreserveExistingActionBarIfCaptureIsEmpty(
		UAeyerjiSaveGame* CapturedSave,
		const TArray<FAeyerjiAbilitySlot>& ExistingActionBar,
		const FName ExistingPassiveId,
		const FString& SlotName)
	{
		if (!CapturedSave)
		{
			return false;
		}

		const int32 CapturedSlotCount = CountPersistedActionBarSlots(CapturedSave->ActionBar);
		const int32 ExistingSlotCount = CountPersistedActionBarSlots(ExistingActionBar);
		if (CapturedSlotCount > 0 || ExistingSlotCount <= 0)
		{
			return false;
		}

		UE_LOG(LogTemp, Warning,
			TEXT("[AbilitySave] Preserving existing action bar for slot %s because the current runtime capture is empty during save."),
			*SlotName);

		CapturedSave->ActionBar = ExistingActionBar;
		NormalizeActionBarForPersistence(CapturedSave->ActionBar);
		if (CapturedSave->SelectedPassiveId.IsNone() && !ExistingPassiveId.IsNone())
		{
			CapturedSave->SelectedPassiveId = ExistingPassiveId;
		}
		return true;
	}

	bool CommitProfileThroughSaveManager(UAeyerjiSaveGame* Data, const AAeyerjiPlayerState* PS)
	{
		if (!Data || !PS)
		{
			return false;
		}

		UWorld* World = PS->GetWorld();
		UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
		UAeyerjiSaveManagerSubsystem* SaveManager =
			GameInstance ? GameInstance->GetSubsystem<UAeyerjiSaveManagerSubsystem>() : nullptr;
		if (!SaveManager)
		{
			return false;
		}

		if (PS->HasAuthority())
		{
			return const_cast<AAeyerjiPlayerState*>(PS)->CommitPreparedCheckpointProfile(Data, EAeyerjiSaveCheckpointReason::Manual, /*bBumpRevision=*/true);
		}

		FAeyerjiSaveTransportHeader Header;
		TArray<uint8> Bytes;
		if (!SaveManager->PrepareProfileForServerCommit(const_cast<AAeyerjiPlayerState*>(PS), Data, /*bBumpRevision=*/true, Header, Bytes))
		{
			return false;
		}

		return SaveManager->CommitResolvedProfileForLocalOwner(Header, Bytes);
	}

	int32 DifficultySliderToUiWorldTier(const float Slider)
	{
		return UAeyerjiDifficultySettings::DifficultySliderToWorldTier(Slider);
	}

	void ApplySavedDifficultyToGameInstance(const UAeyerjiSaveGame* Data, UAeyerjiGameInstance* GI)
	{
		if (!GI)
		{
			UE_LOG(LogTemp, Warning, TEXT("ApplySavedDifficultyToGameInstance: GI is null, cannot apply saved difficulty."));
			return;
		}

		if (Data)
		{
			if (Data->bHasDifficultySelection && Data->bHasWorldTierSelection)
			{
				UE_LOG(LogTemp, Display,
					TEXT("ApplySavedDifficultyToGameInstance: Save has both values. Preferring authoritative WorldTier=%d over legacy DifficultySlider=%.2f."),
					Data->WorldTier,
					Data->DifficultySlider);
				GI->ApplySavedWorldTier(Data->WorldTier);
				return;
			}

			if (Data->bHasWorldTierSelection)
			{
				UE_LOG(LogTemp, Display,
					TEXT("ApplySavedDifficultyToGameInstance: Applying saved WorldTier=%d (bHasDifficulty=%d Difficulty=%.2f)."),
					Data->WorldTier,
					Data->bHasDifficultySelection ? 1 : 0,
					Data->DifficultySlider);
				GI->ApplySavedWorldTier(Data->WorldTier);
				return;
			}

			if (Data->bHasDifficultySelection)
			{
				const int32 DerivedWorldTier = DifficultySliderToUiWorldTier(Data->DifficultySlider);
				UE_LOG(LogTemp, Display,
					TEXT("ApplySavedDifficultyToGameInstance: Legacy save missing WorldTier. Derived WorldTier=%d from DifficultySlider=%.2f."),
					DerivedWorldTier,
					Data->DifficultySlider);
				GI->ApplySavedWorldTier(DerivedWorldTier);
				return;
			}

			UE_LOG(LogTemp, Warning, TEXT("ApplySavedDifficultyToGameInstance: Save exists but has no difficulty/world-tier flags. Using default WorldTier=%d."), GetDefaultWorldTier());
		}

		UE_LOG(LogTemp, Display, TEXT("ApplySavedDifficultyToGameInstance: Applying default WorldTier=%d."), GetDefaultWorldTier());
		GI->ApplySavedWorldTier(GetDefaultWorldTier());
	}

	FGameplayAttribute GetAttributeForAeyerjiStat(EAeyerjiStat Stat)
	{
		switch (Stat)
		{
		case EAeyerjiStat::None:
			return FGameplayAttribute();

		case EAeyerjiStat::Armor:
			return UAeyerjiAttributeSet::GetArmorAttribute();

		case EAeyerjiStat::AttackAngle:
			return UAeyerjiAttributeSet::GetAttackAngleAttribute();

		case EAeyerjiStat::AttackCooldown:
			return UAeyerjiAttributeSet::GetAttackCooldownAttribute();

		case EAeyerjiStat::AttackDamage:
			return UAeyerjiAttributeSet::GetAttackDamageAttribute();

		case EAeyerjiStat::AttackDamageVariance:
			return UAeyerjiAttributeSet::GetAttackDamageVarianceAttribute();

		case EAeyerjiStat::AttackRange:
			return UAeyerjiAttributeSet::GetAttackRangeAttribute();

		case EAeyerjiStat::AttackSpeed:
			return UAeyerjiAttributeSet::GetAttackSpeedAttribute();

		case EAeyerjiStat::HP:
			return UAeyerjiAttributeSet::GetHPAttribute();

		case EAeyerjiStat::HPMax:
			return UAeyerjiAttributeSet::GetHPMaxAttribute();

		case EAeyerjiStat::Mana:
			return UAeyerjiAttributeSet::GetManaAttribute();

		case EAeyerjiStat::ManaMax:
			return UAeyerjiAttributeSet::GetManaMaxAttribute();

		case EAeyerjiStat::PatrolRadius:
			return UAeyerjiAttributeSet::GetPatrolRadiusAttribute();

		case EAeyerjiStat::ProjectilePredictionAmount:
			return UAeyerjiAttributeSet::GetProjectilePredictionAmountAttribute();

		case EAeyerjiStat::ProjectileSpeedRanged:
			return UAeyerjiAttributeSet::GetProjectileSpeedRangedAttribute();

		case EAeyerjiStat::RunSpeed:
			return UAeyerjiAttributeSet::GetRunSpeedAttribute();

		case EAeyerjiStat::WalkSpeed:
			return UAeyerjiAttributeSet::GetWalkSpeedAttribute();

		case EAeyerjiStat::Strength:
			return UAeyerjiAttributeSet::GetStrengthAttribute();

		case EAeyerjiStat::Agility:
			return UAeyerjiAttributeSet::GetAgilityAttribute();

		case EAeyerjiStat::Intellect:
			return UAeyerjiAttributeSet::GetIntellectAttribute();
		case EAeyerjiStat::PoisonAmount:
			return UAeyerjiAttributeSet::GetPoisonAmountAttribute();

		case EAeyerjiStat::PoisonDuration:
			return UAeyerjiAttributeSet::GetPoisonDurationAttribute();

		case EAeyerjiStat::TraumaAmount:
			return UAeyerjiAttributeSet::GetTraumaAmountAttribute();

		case EAeyerjiStat::TraumaDuration:
			return UAeyerjiAttributeSet::GetTraumaDurationAttribute();

		case EAeyerjiStat::CorruptionAmount:
			return UAeyerjiAttributeSet::GetCorruptionAmountAttribute();

		case EAeyerjiStat::CorruptionDuration:
			return UAeyerjiAttributeSet::GetCorruptionDurationAttribute();

		case EAeyerjiStat::CritChance:
			return UAeyerjiAttributeSet::GetCritChanceAttribute();

		case EAeyerjiStat::CriticalDamageMultiplier:
			return UAeyerjiAttributeSet::GetCriticalDamageMultiplierAttribute();

		case EAeyerjiStat::PhysicalDamageBonus:
			return UAeyerjiAttributeSet::GetPhysicalDamageBonusAttribute();

		case EAeyerjiStat::ArmorPenetration:
			return UAeyerjiAttributeSet::GetArmorPenetrationAttribute();

		case EAeyerjiStat::LifeSteal:
			return UAeyerjiAttributeSet::GetLifeStealAttribute();

		case EAeyerjiStat::StaggerPower:
			return UAeyerjiAttributeSet::GetStaggerPowerAttribute();

		case EAeyerjiStat::StaggerResistance:
			return UAeyerjiAttributeSet::GetStaggerResistanceAttribute();

		case EAeyerjiStat::Poise:
			return UAeyerjiAttributeSet::GetPoiseAttribute();

		case EAeyerjiStat::PoiseMax:
			return UAeyerjiAttributeSet::GetPoiseMaxAttribute();

		case EAeyerjiStat::DodgeChance:
			return UAeyerjiAttributeSet::GetDodgeChanceAttribute();

		case EAeyerjiStat::SpellPower:
			return UAeyerjiAttributeSet::GetSpellPowerAttribute();

		case EAeyerjiStat::MagicAmp:
			return UAeyerjiAttributeSet::GetMagicAmpAttribute();

		case EAeyerjiStat::ManaRegen:
			return UAeyerjiAttributeSet::GetManaRegenAttribute();

		case EAeyerjiStat::HPRegen:
			return UAeyerjiAttributeSet::GetHPRegenAttribute();

		case EAeyerjiStat::CooldownReduction:
			return UAeyerjiAttributeSet::GetCooldownReductionAttribute();

		case EAeyerjiStat::XP:
			return UAeyerjiAttributeSet::GetXPAttribute();

		case EAeyerjiStat::XPMax:
			return UAeyerjiAttributeSet::GetXPMaxAttribute();

		case EAeyerjiStat::Level:
			return UAeyerjiAttributeSet::GetLevelAttribute();
			
		case EAeyerjiStat::HearingRange:
			return UAeyerjiAttributeSet::GetHearingRangeAttribute();

		case EAeyerjiStat::VisionRange:
			return UAeyerjiAttributeSet::GetVisionRangeAttribute();

		default:
			return FGameplayAttribute();
		}
	}

	UAbilitySystemComponent *ResolveASCForActor(const AActor *Actor)
	{
		if (!Actor)

		{

			return nullptr;
		}

		if (UAbilitySystemComponent *ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor, /*LookForComponent*/ true))

		{

			return ASC;
		}

		if (const APawn *Pawn = Cast<APawn>(Actor))

		{

			if (const AController *Controller = Pawn->GetController())

			{

				if (UAbilitySystemComponent *ControllerASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Controller, /*LookForComponent*/ true))

				{

					return ControllerASC;
				}
			}
		}

		return Actor->FindComponentByClass<UAbilitySystemComponent>();
	}

	FString GetLocalDevSlotToken()

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

		for (const FString &Candidate : Candidates)

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

}

bool UCharacterStatsLibrary::GetAeyerjiStatFromActor(const AActor *Actor, EAeyerjiStat Stat, float &OutValue)
{
	OutValue = 0.f;
	if (!Actor || Stat == EAeyerjiStat::None)
	{
		return false;
	}

	const FGameplayAttribute Attribute = GetAttributeForAeyerjiStat(Stat);
	if (!Attribute.IsValid())
	{
		return false;
	}

	UAbilitySystemComponent *ASC = ResolveASCForActor(Actor);
	if (!ASC)
	{
		return false;
	}

	if (!ASC->HasAttributeSetForAttribute(Attribute))
	{
		return false;
	}

	OutValue = ASC->GetNumericAttribute(Attribute);
	return true;
}

/*Logging helper*/

static UAbilitySystemComponent *FindASCChecked(const AAeyerjiPlayerState *PS)

{

	if (!PS)

	{

		UE_LOG(LogTemp, Error,

			   TEXT("LoadAeyerjiChar NULL PlayerState passed in"));

		return nullptr;
	}

	/* First prefer the pawn already owned by the PlayerState. */

	APawn *Pawn = PS->GetPawn();

	if (!Pawn)

	{

		UE_LOG(LogTemp, Error,

			   TEXT("LoadAeyerjiChar  PS %s has no Pawn yet (OnRep_PlayerState fired before possession)"),

			   *GetNameSafe(PS));

		return nullptr;
	}

	/* Confirm that the resolved pawn exposes an ability-system component. */

	const bool bImplementsASI = Pawn->GetClass()->ImplementsInterface(UAbilitySystemInterface::StaticClass());

	if (!bImplementsASI)

	{

		UE_LOG(LogTemp, Error,

			   TEXT("LoadAeyerjiChar  Pawn %s does NOT implement IAbilitySystemInterface"),

			   *GetNameSafe(Pawn));

		return nullptr;
	}

	/* Reject stale component pointers before returning the ASC. */

	if (const IAbilitySystemInterface *ASI = Cast<IAbilitySystemInterface>(Pawn))

	{

		UAbilitySystemComponent *ASC = ASI->GetAbilitySystemComponent();

		if (!ASC)

		{

			UE_LOG(LogTemp, Error,

				   TEXT("LoadAeyerjiChar  Pawn %s implements ASI but GetAbilitySystemComponent() returned NULL"),

				   *GetNameSafe(Pawn));
		}

		return ASC;
	}

	return nullptr; // should never hit
}

static int32 MakeDifficultyKey(const float DifficultySlider)
{
	const float SafeSlider = FMath::IsFinite(DifficultySlider) ? DifficultySlider : GetDefaultDifficultySlider();
	return FMath::RoundToInt(FMath::Clamp(SafeSlider, 0.f, UAeyerjiDifficultySettings::DifficultySliderMax));
}

static float ApplyRunBestTimeToSaveData(UAeyerjiSaveGame* Data, const FAeyerjiRunResults& Results)
{
	if (!Data)
	{
		return 0.f;
	}

	const int32 DifficultyKey = MakeDifficultyKey(Results.DifficultySlider);
	const bool bHasExisting = Data->BestRunTimeSecondsByDifficulty.Contains(DifficultyKey);
	const float ExistingBest = Data->BestRunTimeSecondsByDifficulty.FindRef(DifficultyKey);
	float BestTime = (bHasExisting && ExistingBest > 0.f) ? ExistingBest : 0.f;

	if (Results.Resolution == EAeyerjiRunResolution::Victory
		&& FMath::IsFinite(Results.RunTimeSeconds) && Results.RunTimeSeconds > 0.f)
	{
		if (BestTime <= 0.f || Results.RunTimeSeconds < BestTime)
		{
			BestTime = Results.RunTimeSeconds;
			Data->BestRunTimeSecondsByDifficulty.Add(DifficultyKey, BestTime);
		}
	}

	return BestTime;
}

static void AppendRecentRunRecord(UAeyerjiSaveGame* Data, const FAeyerjiRunResults& Results)
{
	if (!Data)
	{
		return;
	}

	FAeyerjiCompletedRunRecord NewRecord;
	NewRecord.CompletedAtUtc = FDateTime::UtcNow();
	NewRecord.Resolution = Results.Resolution;
	NewRecord.RunTimeSeconds = FMath::IsFinite(Results.RunTimeSeconds) ? FMath::Max(0.f, Results.RunTimeSeconds) : 0.f;
	NewRecord.UnitsKilled = Results.UnitsKilled;
	NewRecord.UnitsKillTarget = Results.UnitsKillTarget;
	NewRecord.DifficultySlider = static_cast<float>(MakeDifficultyKey(Results.DifficultySlider));
	NewRecord.SpeedBonusPercent = FMath::IsFinite(Results.SpeedBonusPercent)
		? FMath::Clamp(Results.SpeedBonusPercent, 0.f, 1000000.f)
		: 0.f;
	NewRecord.CompletedZoneId = Results.CompletedZoneId;
	NewRecord.SelectedRiftTier = Results.SelectedRiftTier;
	NewRecord.ProgressPoints = Results.ProgressPoints;
	NewRecord.ProgressPointTarget = Results.ProgressPointTarget;
	NewRecord.bCompletedInTime = Results.bCompletedInTime;
	NewRecord.bBossPhaseDeathOccurred = Results.bBossPhaseDeathOccurred;

	// ResultsVersion is local to a GameState instance, so use the stable run serial carried by
	// the results payload to prevent repeated checkpoint calls from appending the same run.
	const bool bAlreadyRecorded = Results.RunSerial > 0 && Data->RecentRuns.ContainsByPredicate(
		[&Results](const FAeyerjiCompletedRunRecord& Existing)
		{
			return Existing.RunSerial == Results.RunSerial;
		});
	if (!bAlreadyRecorded)
	{
		NewRecord.RunSerial = Results.RunSerial;
		Data->RecentRuns.Insert(NewRecord, 0);
	}

	constexpr int32 MaxRecentRuns = 20;
	while (Data->RecentRuns.Num() > MaxRecentRuns)
	{
		Data->RecentRuns.RemoveAt(Data->RecentRuns.Num() - 1);
	}
}

static bool GatherAeyerjiCharSaveData(
	UAeyerjiSaveGame* Data,
	const AAeyerjiPlayerState* PS,
	const FString& Slot,
	const APawn* SourcePawn = nullptr)
{
	if (!Data || !PS)
	{
		UE_LOG(LogTemp, Error, TEXT("SaveAeyerjiChar: Called with null Data or PlayerState. Aborting save."));
		return false;
	}

	if (const UWorld* World = PS->GetWorld(); World && World->GetNetMode() == NM_Client)
	{
		UE_LOG(LogTemp, Warning, TEXT("SaveAeyerjiChar: Refusing client-side save for slot %s."), *Slot);
		return false;
	}

	// Capture the current difficulty/world-tier selection for persistence.
	Data->bHasDifficultySelection = false;
	Data->bHasWorldTierSelection = false;
	if (PS->GetWorld())
	{
		if (UAeyerjiGameInstance* GI = Cast<UAeyerjiGameInstance>(PS->GetWorld()->GetGameInstance()))
		{
			UE_LOG(LogTemp, Display,
				TEXT("SaveAeyerjiChar(Difficulty): GI pre-capture HasDiff=%d HasTier=%d Diff=%.2f Tier=%d"),
				GI->HasDifficultySelection() ? 1 : 0,
				GI->HasWorldTierSelection() ? 1 : 0,
				GI->GetDifficultySlider(),
				GI->GetWorldTier());

			if (!GI->HasDifficultySelection() && !GI->HasWorldTierSelection())
			{
				UE_LOG(LogTemp, Warning, TEXT("SaveAeyerjiChar(Difficulty): GI has no selection flags; forcing default WorldTier=%d."), GetDefaultWorldTier());
				GI->ApplySavedWorldTier(GetDefaultWorldTier());
			}

			if (GI->HasDifficultySelection() || GI->HasWorldTierSelection())
			{
				Data->DifficultySlider = GI->GetDifficultySlider();
				Data->WorldTier = GI->GetWorldTier();
				Data->bHasDifficultySelection = true;
				Data->bHasWorldTierSelection = true;
			}
		}
	}

	// Keep save data deterministic even if no world context exists.
	if (!Data->bHasDifficultySelection)
	{
		UE_LOG(LogTemp, Warning, TEXT("SaveAeyerjiChar(Difficulty): Missing captured difficulty; defaulting to the WorldTier=%d slider alias."), GetDefaultWorldTier());
		Data->DifficultySlider = GetDefaultDifficultySlider();
		Data->bHasDifficultySelection = true;
	}

	if (!Data->bHasWorldTierSelection)
	{
		Data->WorldTier = DifficultySliderToUiWorldTier(Data->DifficultySlider);
		Data->bHasWorldTierSelection = true;
		UE_LOG(LogTemp, Warning,
			TEXT("SaveAeyerjiChar(Difficulty): Missing captured world tier; derived Tier=%d from Difficulty=%.2f."),
			Data->WorldTier,
			Data->DifficultySlider);
	}

	UE_LOG(LogTemp, Display,
		TEXT("SaveAeyerjiChar(Difficulty): Slot=%s SaveFlags(Diff=%d Tier=%d) SaveValues(Diff=%.2f Tier=%d)"),
		*Slot,
		Data->bHasDifficultySelection ? 1 : 0,
		Data->bHasWorldTierSelection ? 1 : 0,
		Data->DifficultySlider,
		Data->WorldTier);

	const APawn* Pawn = SourcePawn ? SourcePawn : PS->GetPawn();
	if (Pawn)
	{
		if (UAeyerjiInventoryComponent* Inventory = Pawn->FindComponentByClass<UAeyerjiInventoryComponent>())
		{
			Data->Inventory = Inventory->BuildSaveData();
			UE_LOG(LogTemp, Display, TEXT("SaveAeyerjiChar: Captured inventory with %d items, %d placements"),
				Data->Inventory.ItemSnapshots.Num(),
				Data->Inventory.GridPlacements.Num());
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("SaveAeyerjiChar: Pawn %s missing inventory component; skipping inventory save"), *GetNameSafe(Pawn));
		}

		if (const IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Pawn))
		{
			if (const UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
			{
				for (UAttributeSet* Set : ASC->GetSpawnedAttributes())
				{
					if (UAeyerjiAttributeSet* AeyerjiSet = Cast<UAeyerjiAttributeSet>(Set))
					{
						UE_LOG(LogTemp, Log, TEXT("SaveAeyerjiChar: Found Attribute XP found '%f'"), AeyerjiSet->GetXP());
						Data->Attributes.XP = FMath::IsFinite(AeyerjiSet->GetXP()) ? FMath::Max(0.f, AeyerjiSet->GetXP()) : 0.f;
						Data->Attributes.Level = UAeyerjiDifficultySettings::FloatToGameplayLevel(AeyerjiSet->GetLevel());
					}
				}
			}
		}
	}

	if (const UPlayerStatsTrackingComponent* StatsComp = PS->FindComponentByClass<UPlayerStatsTrackingComponent>())
	{
		StatsComp->ExtractLootStats(Data->LootStats);
	}
	else
	{
		UE_LOG(LogTemp, Verbose, TEXT("SaveAeyerjiChar: PlayerState %s missing PlayerStatsTrackingComponent; skipping loot stats save"), *GetNameSafe(PS));
	}

	UE_LOG(LogTemp, Display,
		TEXT("SaveAeyerjiChar(LootPity): Slot=%s DropsSinceLastLegendary=%d TotalLegendariesDropped=%d TotalLegendariesPickedUp=%d WindowCount=%d LegendariesInWindow=%d"),
		*Slot,
		Data->LootStats.DropsSinceLastLegendary,
		Data->LootStats.TotalLegendariesDropped,
		Data->LootStats.TotalLegendariesPickedUp,
		Data->LootStats.WindowCount,
		Data->LootStats.LegendariesInWindow);

	Data->WorldStateEntries.Reset();
	if (const UWorld* World = PS->GetWorld())
	{
		if (const UAeyerjiWorldStateSubsystem* WorldStateSubsystem = UAeyerjiWorldStateSubsystem::Get(World))
		{
			WorldStateSubsystem->ExportPersistentCharacterState(FName(*Slot), Data->WorldStateEntries);
		}
	}

	const TArray<FAeyerjiAbilitySlot> ExistingActionBar = Data->ActionBar;
	const FName ExistingPassiveId = Data->SelectedPassiveId;
	const TArray<FAeyerjiAbilityProgressEntry> ExistingAbilityProgressEntries = Data->AbilityProgressEntries;
	const int32 ExistingUnspentAbilityPoints = Data->UnspentAbilityPoints;
	const int32 ExistingTotalAbilityPointSpends = Data->TotalAbilityPointSpends;
	Data->ActionBar = PS->ActionBar;
	NormalizeActionBarForPersistence(Data->ActionBar);
	Data->SelectedPassiveId = PS->GetSelectedPassiveId();
	Data->AbilityProgressEntries = PS->GetAbilityProgressEntries();
	Data->UnspentAbilityPoints = PS->GetUnspentAbilityPoints();
	Data->TotalAbilityPointSpends = PS->GetTotalAbilityPointSpends();
	Data->Gold = PS->GetGold();
	Data->HighestUnlockedRiftTier = PS->GetHighestUnlockedRiftTier();
	Data->LastSelectedRiftTier = PS->GetLastSelectedRiftTier();
	const bool bPreservedExistingActionBar = PreserveExistingActionBarIfCaptureIsEmpty(Data, ExistingActionBar, ExistingPassiveId, Slot);
	if (Data->AbilityProgressEntries.Num() == 0
		&& Data->UnspentAbilityPoints == 0
		&& Data->TotalAbilityPointSpends == 0
		&& (ExistingAbilityProgressEntries.Num() > 0 || ExistingUnspentAbilityPoints > 0 || ExistingTotalAbilityPointSpends > 0))
	{
		Data->AbilityProgressEntries = ExistingAbilityProgressEntries;
		Data->UnspentAbilityPoints = ExistingUnspentAbilityPoints;
		Data->TotalAbilityPointSpends = ExistingTotalAbilityPointSpends;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[AbilitySave] Slot=%s CapturedSlots=%d ResolvedClasses=%d Passive=%s ProgressEntries=%d Unspent=%d Spends=%d Gold=%lld PreservedExisting=%d"),
		*Slot,
		CountPersistedActionBarSlots(Data->ActionBar),
		CountResolvedAbilityClasses(Data->ActionBar),
		*Data->SelectedPassiveId.ToString(),
		Data->AbilityProgressEntries.Num(),
		Data->UnspentAbilityPoints,
		Data->TotalAbilityPointSpends,
		Data->Gold,
		bPreservedExistingActionBar ? 1 : 0);
	LogActionBarPersistenceSnapshot(TEXT("Captured"), Slot, Data->ActionBar);

	UE_LOG(LogTemp, Display, TEXT("SaveAeyerjiChar(BeforeWrite): Slot=%s PS=%s Pawn=%s DataXP=%f DataLevel=%d"),
		*Slot,
		*GetNameSafe(PS),
		*GetNameSafe(Pawn),
		Data->Attributes.XP,
		Data->Attributes.Level);

	UE_LOGFMT(LogTemp, Log,
		"Saving to slot '{Slot}'. XP={XP}, Level={Level}. SaveGameName='{SaveGameName}', First item='{First}', Second item='{Second}'",
		("Slot", Slot),
		("XP", Data->Attributes.XP),
		("Level", Data->Attributes.Level),
		("SaveGameName", Data->GetName()),
		("First", Data->ActionBar.Num() ? Data->ActionBar[0].Description.ToString() : TEXT("Empty")),
		("Second", Data->ActionBar.Num() > 1 ? Data->ActionBar[1].Description.ToString() : TEXT("Empty")));

	UE_LOG(LogTemp, Display, TEXT("SaveAeyerjiChar(AfterGather): Slot=%s XP=%f Level=%d (FirstSlot=%s SecondSlot=%s)"),
		*Slot,
		Data->Attributes.XP,
		Data->Attributes.Level,
		Data->ActionBar.Num() ? *Data->ActionBar[0].Description.ToString() : TEXT("Empty"),
		Data->ActionBar.Num() > 1 ? *Data->ActionBar[1].Description.ToString() : TEXT("Empty"));

	return true;
}

bool UCharacterStatsLibrary::BuildAeyerjiSaveDataFromRuntime(
	UAeyerjiSaveGame* Data,
	const AAeyerjiPlayerState* PS,
	const FString& Slot,
	const APawn* SourcePawn)
{
	return GatherAeyerjiCharSaveData(Data, PS, Slot, SourcePawn);
}

FString UCharacterStatsLibrary::MakeStableOwnerKey(const APlayerState* PS)
{
	if (!PS)
	{
		return TEXT("UNKNOWN");
	}

	// Authenticated platform identity is the account boundary. A Blueprint character-slot
	// override must not make two Steam users share profile or character-scoped world state.
	const FUniqueNetIdRepl& NetId = PS->GetUniqueId();
	if (NetId.IsValid() && NetId.GetUniqueNetId().IsValid())
	{
		const FUniqueNetId& Raw = *NetId.GetUniqueNetId();
		if (!Raw.GetType().IsEqual(FName("NULL"), ENameCase::IgnoreCase))
		{
			const FString SafeNetId = FPaths::MakeValidFileName(Raw.ToString());
			if (!SafeNetId.IsEmpty())
			{
				return SafeNetId;
			}
		}
	}

	if (const AAeyerjiPlayerState* AeyerjiPS = Cast<AAeyerjiPlayerState>(PS))
	{
		const FString SafeFrontendOwner = SanitizeSaveSlotName(AeyerjiPS->GetFrontendProfileOwnerKey());
		if (!SafeFrontendOwner.IsEmpty())
		{
			return SafeFrontendOwner;
		}

		const FString& OverrideSlot = AeyerjiPS->GetSaveSlotOverride();
		if (UAeyerjiSaveManagerSubsystem::IsExplicitSaveSlotOverrideAllowed(PS) && !OverrideSlot.IsEmpty())
		{
			return OverrideSlot;
		}
	}

	const FString DevToken = GetLocalDevSlotToken();
	if (!DevToken.IsEmpty())
	{
		return DevToken;
	}

	FString FallbackName = FPaths::MakeValidFileName(PS->GetPlayerName());
	if (!FallbackName.IsEmpty())
	{
		return FallbackName;
	}

	const int32 StableIndex = FMath::Max(0, PS->GetPlayerId());
	return FString::Printf(TEXT("Player%d"), StableIndex);
}

FString UCharacterStatsLibrary::MakeStableCharSlotName(const APlayerState *PS)

{
	if (!PS)
	{
		return TEXT("UNKNOWN_Char");
	}

	if (const AAeyerjiPlayerState* AeyerjiPS = Cast<AAeyerjiPlayerState>(PS))
	{
		const FString& OverrideSlot = AeyerjiPS->GetSaveSlotOverride();
		if (UAeyerjiSaveManagerSubsystem::IsExplicitSaveSlotOverrideAllowed(PS) && !OverrideSlot.IsEmpty())
		{
			return OverrideSlot;
		}
	}

	return MakeStableOwnerKey(PS) + TEXT("_Char");
}

UAeyerjiSaveGame *UCharacterStatsLibrary::LoadOrCreateAeyerjiSave(const FString &Slot, bool &bOutLoadedFromDisk)

{

	bOutLoadedFromDisk = false;

	UAeyerjiSaveGame *Data = nullptr;

	if (!Slot.IsEmpty() && UGameplayStatics::DoesSaveGameExist(Slot, 0))

	{

		if (USaveGame *RawSave = UGameplayStatics::LoadGameFromSlot(Slot, 0))

		{

			Data = Cast<UAeyerjiSaveGame>(RawSave);

			if (Data)

			{

				bOutLoadedFromDisk = true;
				const int32 RemovedInvalidAttributes = UAeyerjiInventoryComponent::SanitizeSaveDataAttributes(Data->Inventory);
				if (RemovedInvalidAttributes > 0)
				{
					UE_LOG(LogTemp, Warning,
						TEXT("LoadOrCreateAeyerjiSave: Slot=%s pruned %d invalid item stat attribute references."),
						*Slot,
						RemovedInvalidAttributes);
				}
			}

			else

			{

				UE_LOG(LogTemp, Error, TEXT("Save slot %s contains unexpected save type. A new save will be created."), *Slot);
			}
		}

		else

		{

			UE_LOG(LogTemp, Error, TEXT("Failed to load save slot %s. A new save will be created."), *Slot);
		}
	}

	if (!Data)

	{

		Data = Cast<UAeyerjiSaveGame>(UGameplayStatics::CreateSaveGameObject(UAeyerjiSaveGame::StaticClass()));

		if (!Data)

		{

			UE_LOG(LogTemp, Error, TEXT("LoadOrCreateAeyerjiSave: Failed to create save object for slot %s."), *Slot);

			return nullptr;
		}

		Data->ActionBar.Reset();
		Data->Attributes = FAttrSnapshot();
		Data->Inventory = FAeyerjiInventorySaveData();
		Data->Gold = 0;

		const FString AbsoluteFilename = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), Slot + TEXT(".sav"));

		UE_LOG(LogTemp, Log, TEXT("LoadOrCreateAeyerjiSave: Created new save object for slot %s (%s)."), *Slot, *AbsoluteFilename);
		UE_LOG(LogTemp, Display, TEXT("LoadOrCreateAeyerjiSave: Initialized new slot %s with XP=%f Level=%d"), *Slot, Data->Attributes.XP, Data->Attributes.Level);
	}

	else

	{

		const FString AbsoluteFilename = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("SaveGames"), Slot + TEXT(".sav"));

		UE_LOG(LogTemp, Verbose, TEXT("LoadOrCreateAeyerjiSave: Loaded existing save from slot %s (%s)."), *Slot, *AbsoluteFilename);
		UE_LOG(LogTemp, Display, TEXT("LoadOrCreateAeyerjiSave: Existing slot %s currently has XP=%f Level=%d"), *Slot, Data->Attributes.XP, Data->Attributes.Level);
	}

	return Data;
}

FString UCharacterStatsLibrary::SanitizeSaveSlotName(const FString &RawSlotName)
{
	FString Safe = RawSlotName;
	Safe.TrimStartAndEndInline();

	if (Safe.IsEmpty())
	{
		return FString();
	}

	Safe = FPaths::MakeValidFileName(Safe);
	return Safe;
}

/* ------------------ Load helper ------------------ */

void UCharacterStatsLibrary::LoadAeyerjiChar(

	UAeyerjiSaveGame *Data,

	AAeyerjiPlayerState *PS,

	UAbilitySystemComponent *ASC)

{

	if (!Data)

	{

		UE_LOG(LogTemp, Warning, TEXT("[ProfileLoad] Hydration=Failed Reason=NullSaveGame"));

		return;
	}

	if (!PS)

	{

		UE_LOG(LogTemp, Warning, TEXT("[ProfileLoad] Hydration=Failed Reason=NullPlayerState"));

		return;
	}

	if (!ASC)

	{

		UE_LOG(LogTemp, Warning, TEXT("[ProfileLoad] Hydration=Failed Reason=MissingASC PlayerState=%s"), *GetNameSafe(PS));

		return;
	}

	const FString SlotName = UCharacterStatsLibrary::MakeStableCharSlotName(PS);
	UE_LOG(LogTemp, Display,
		TEXT("[ProfileLoad] Hydration=Begin Slot=%s Revision=%lld Level=%d XP=%.2f ActionBar=%d Passive=%s Items=%d Equipped=%d Grid=%d WorldFacts=%d"),
		*SlotName,
		Data->Revision,
		Data->Attributes.Level,
		Data->Attributes.XP,
		Data->ActionBar.Num(),
		*Data->SelectedPassiveId.ToString(),
		Data->Inventory.ItemSnapshots.Num(),
		Data->Inventory.EquippedItems.Num(),
		Data->Inventory.GridPlacements.Num(),
		Data->WorldStateEntries.Num());

	UE_LOG(LogTemp, Display, TEXT("[ProfileLoad] HydrationPhase=1_DifficultyLootWorldMemory Slot=%s"), *SlotName);
	// Restore persisted difficulty selection so UI sliders can reflect the saved choice.
	if (PS->GetWorld())
	{
		if (UAeyerjiGameInstance* GI = Cast<UAeyerjiGameInstance>(PS->GetWorld()->GetGameInstance()))
		{
			UE_LOG(LogTemp, Display,
				TEXT("LoadAeyerjiChar(Difficulty): Slot=%s SaveFlags(Diff=%d Tier=%d) SaveValues(Diff=%.2f Tier=%d)"),
				*UCharacterStatsLibrary::MakeStableCharSlotName(PS),
				Data->bHasDifficultySelection ? 1 : 0,
				Data->bHasWorldTierSelection ? 1 : 0,
				Data->DifficultySlider,
				Data->WorldTier);
			ApplySavedDifficultyToGameInstance(Data, GI);
			UE_LOG(LogTemp, Display,
				TEXT("LoadAeyerjiChar(Difficulty): Applied GI values Difficulty=%.2f WorldTier=%d"),
				GI->GetDifficultySlider(),
				GI->GetWorldTier());
		}
	}

	// Restore lifetime loot stats into the player state component if available.
	bool bAppliedLootStatsToComponent = false;
	if (UPlayerStatsTrackingComponent* StatsComp = PS->FindComponentByClass<UPlayerStatsTrackingComponent>())
	{
		StatsComp->LoadLootStats(Data->LootStats);
		bAppliedLootStatsToComponent = true;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[ProfileLoad] MissingSubsystem=PlayerStatsTrackingComponent PlayerState=%s Detail=LootStatsNotApplied"), *GetNameSafe(PS));
	}

	UE_LOG(LogTemp, Display,
		TEXT("LoadAeyerjiChar(LootPity): Slot=%s Applied=%d DropsSinceLastLegendary=%d TotalLegendariesDropped=%d TotalLegendariesPickedUp=%d WindowCount=%d LegendariesInWindow=%d"),
		*UCharacterStatsLibrary::MakeStableCharSlotName(PS),
		bAppliedLootStatsToComponent ? 1 : 0,
		Data->LootStats.DropsSinceLastLegendary,
		Data->LootStats.TotalLegendariesDropped,
		Data->LootStats.TotalLegendariesPickedUp,
		Data->LootStats.WindowCount,
		Data->LootStats.LegendariesInWindow);

	if (ASC->GetOwnerRole() == ROLE_Authority)
	{
		UE_LOG(LogTemp, Display, TEXT("[ProfileLoad] HydrationPhase=2_CharacterWorldFacts Slot=%s Facts=%d"), *SlotName, Data->WorldStateEntries.Num());
		if (UAeyerjiWorldStateSubsystem* WorldStateSubsystem = UAeyerjiWorldStateSubsystem::Get(PS))
		{
			WorldStateSubsystem->ImportPersistentCharacterState(FName(*UCharacterStatsLibrary::MakeStableCharSlotName(PS)), Data->WorldStateEntries);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("LoadAeyerjiChar: World state subsystem missing; character-scoped persistent facts were not imported."));
		}
	}

	// Some older/new slots may not have been initialised with the fixed number of action bar entries.
	// Pad the save data to match the current expected slot count so widgets and selections work.
	const int32 ExpectedSlots = PS->ActionBar.Num();
	NormalizeActionBarForPersistence(Data->ActionBar);
	LogActionBarPersistenceSnapshot(TEXT("LoadedBeforeApply"), SlotName, Data->ActionBar);
	if (ExpectedSlots > 0 && Data->ActionBar.Num() < ExpectedSlots)
	{
		const int32 OldNum = Data->ActionBar.Num();
		Data->ActionBar.SetNum(ExpectedSlots);
		UE_LOG(LogTemp, Warning, TEXT("LoadAeyerjiChar: Save action bar had %d entries, expected %d. Padding with empty slots."),
			   OldNum, ExpectedSlots);
	}

	const UAeyerjiAttributeSet *RuntimeSet = const_cast<UAeyerjiAttributeSet *>(ASC->GetSet<UAeyerjiAttributeSet>());

	if (!RuntimeSet)

	{

		UE_LOG(LogTemp, Error, TEXT("UCharacterStatsLibrary::LoadAeyerjiChar: Runtime AttrSet missing"));

		return;
	}

	const float SavedXP = Data->Attributes.XP;

	const int32 SavedLevel = UAeyerjiDifficultySettings::ClampGameplayLevel(Data->Attributes.Level);

	UE_LOG(LogTemp, Display, TEXT("[ProfileLoad] HydrationPhase=3_LevelXP Slot=%s XP=%.2f Level=%d"), *SlotName, SavedXP, SavedLevel);

	if (ASC->GetOwnerRole() == ROLE_Authority)

	{

		APawn *Pawn = PS->GetPawn();

		UAeyerjiLevelingComponent *Leveling = Pawn ? Pawn->FindComponentByClass<UAeyerjiLevelingComponent>() : nullptr;

		// Restore Level first so XPMax is correct

		if (Leveling)

		{
			UE_LOG(LogTemp, Display, TEXT("LoadAeyerjiChar: Setting level via UAeyerjiLevelingComponent to %d"), SavedLevel);
			Leveling->SetLevel(SavedLevel);
		}

		else

		{
			UE_LOG(LogTemp, Error, TEXT("LoadAeyerjiChar: %s has NO UAeyerjiLevelingComponent; setting Level directly (SavedLevel=%d). This should never happen."), *GetNameSafe(Pawn), SavedLevel);

			ensureAlwaysMsgf(false, TEXT("Missing UAeyerjiLevelingComponent on %s during load; fix the pawn/BP to include it."), *GetNameSafe(Pawn));

			ASC->SetNumericAttributeBase(RuntimeSet->GetLevelAttribute(), static_cast<float>(SavedLevel));
		}

		// Clamp XP to current XPMax and apply

		const float ClampedXP = FMath::Clamp(SavedXP, 0.f, RuntimeSet->GetXPMax());

		ASC->SetNumericAttributeBase(RuntimeSet->GetXPAttribute(), ClampedXP);

		if (UWorld* World = PS->GetWorld())
		{
			if (UGameInstance* GameInstance = World->GetGameInstance())
			{
				if (UAeyerjiSaveManagerSubsystem* SaveManager = GameInstance->GetSubsystem<UAeyerjiSaveManagerSubsystem>())
				{
					UAeyerjiSaveGame* CachedProfile = nullptr;
					if (SaveManager->GetServerCachedProfile(PS, CachedProfile) && CachedProfile)
					{
						CachedProfile->Attributes.Level = SavedLevel;
						CachedProfile->Attributes.XP = ClampedXP;
						UE_LOG(LogTemp, Display,
							TEXT("[ProfileXP] CacheSync Reason=Load PlayerState=%s Level=%d XP=%.2f XPMax=%.2f Revision=%lld"),
							*GetNameSafe(PS),
							CachedProfile->Attributes.Level,
							CachedProfile->Attributes.XP,
							RuntimeSet->GetXPMax(),
							CachedProfile->Revision);
					}
				}
			}
		}

		// Ensure any level-scaled infinite effects are up-to-date after load

		if (Leveling)

		{

			Leveling->ForceRefreshForCurrentLevel();
		}

	}

	/* ---------- Restore any other replicated data ---------- */

	if (ASC->GetOwnerRole() == ROLE_Authority)
	{
		PS->ApplyLoadedGold(Data->Gold);
		PS->ApplyLoadedRiftProgression(Data->HighestUnlockedRiftTier, Data->LastSelectedRiftTier);
		PS->ApplyLoadedAbilityProgression(Data->AbilityProgressEntries, Data->UnspentAbilityPoints, Data->TotalAbilityPointSpends);
		PS->ApplyLoadedActionBar(Data->ActionBar);
		UE_LOG(LogTemp, Display,
			TEXT("[ProfileLoad] HydrationPhase=4_ActionBarPassive Slot=%s ActionBar=%d Passive=%s ProgressEntries=%d Unspent=%d Spends=%d Gold=%lld"),
			*SlotName,
			Data->ActionBar.Num(),
			*Data->SelectedPassiveId.ToString(),
			Data->AbilityProgressEntries.Num(),
			Data->UnspentAbilityPoints,
			Data->TotalAbilityPointSpends,
			Data->Gold);

		if (!Data->SelectedPassiveId.IsNone())
		{
			PS->SetPassiveLocal(Data->SelectedPassiveId);
		}

		APawn *Pawn = PS->GetPawn();
		if (UAeyerjiInventoryComponent *Inventory = Pawn ? Pawn->FindComponentByClass<UAeyerjiInventoryComponent>() : nullptr)
		{
			UE_LOG(LogTemp, Display,
				TEXT("[ProfileLoad] HydrationPhase=5_InventoryEquipment Slot=%s Inventory=%s Items=%d Equipped=%d Grid=%d"),
				*SlotName,
				*GetNameSafe(Inventory),
				Data->Inventory.ItemSnapshots.Num(),
				Data->Inventory.EquippedItems.Num(),
				Data->Inventory.GridPlacements.Num());
			Inventory->ApplySaveData(Data->Inventory);
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[ProfileLoad] MissingSubsystem=InventoryComponent Slot=%s Pawn=%s Items=%d Equipped=%d Grid=%d"),
				*SlotName,
				*GetNameSafe(Pawn),
				Data->Inventory.ItemSnapshots.Num(),
				Data->Inventory.EquippedItems.Num(),
				Data->Inventory.GridPlacements.Num());
		}

		// Start loaded characters from a deterministic full-resource state after
		// level, passive, and equipped item effects have rebuilt max values.
		UE_LOG(LogTemp, Display, TEXT("[ProfileLoad] HydrationPhase=6_RefillRefresh Slot=%s"), *SlotName);
		const float LoadedHPMax = ASC->GetNumericAttribute(RuntimeSet->GetHPMaxAttribute());
		const float LoadedManaMax = ASC->GetNumericAttribute(RuntimeSet->GetManaMaxAttribute());
		ASC->SetNumericAttributeBase(RuntimeSet->GetHPAttribute(), FMath::Max(0.f, LoadedHPMax));
		ASC->SetNumericAttributeBase(RuntimeSet->GetManaAttribute(), FMath::Max(0.f, LoadedManaMax));

		ASC->ForceReplication();
		PS->ForceNetUpdate();
		UE_LOG(LogTemp, Display, TEXT("[ProfileLoad] Hydration=Complete Slot=%s HPMax=%.2f ManaMax=%.2f"), *SlotName, LoadedHPMax, LoadedManaMax);
	}
}

/* ---------- save ---------- */

void UCharacterStatsLibrary::SaveAeyerjiChar(

	UAeyerjiSaveGame *Data,

	const AAeyerjiPlayerState *PS,

	const FString Slot)

{
	const TArray<FAeyerjiAbilitySlot> ExistingActionBar = Data ? Data->ActionBar : TArray<FAeyerjiAbilitySlot>();
	const FName ExistingPassiveId = Data ? Data->SelectedPassiveId : NAME_None;

	if (!BuildAeyerjiSaveDataFromRuntime(Data, PS, Slot))
	{
		return;
	}

	PreserveExistingActionBarIfCaptureIsEmpty(Data, ExistingActionBar, ExistingPassiveId, Slot);

	if (CommitProfileThroughSaveManager(Data, PS))
	{
		UE_LOG(LogTemp, Display, TEXT("SaveAeyerjiChar(Success): Slot=%s committed via save manager XP=%f Level=%d"),
			*Slot,
			Data->Attributes.XP,
			Data->Attributes.Level);
		return;
	}

	const int32 RemovedInvalidAttributes = UAeyerjiInventoryComponent::SanitizeSaveDataAttributes(Data->Inventory);
	if (RemovedInvalidAttributes > 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SaveAeyerjiChar: Slot=%s pruned %d invalid item stat attribute references before fallback save."),
			*Slot,
			RemovedInvalidAttributes);
	}

	if (!UGameplayStatics::SaveGameToSlot(Data, Slot, 0))
	{
		UE_LOG(LogTemp, Error, TEXT("Save failed for slot %s"), *Slot);
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("SaveAeyerjiChar(Success): Slot=%s saved XP=%f Level=%d"),
			*Slot,
			Data->Attributes.XP,
			Data->Attributes.Level);
	}
}

void UCharacterStatsLibrary::SaveAeyerjiCharFromPawn(
	UAeyerjiSaveGame* Data,
	const AAeyerjiPlayerState* PS,
	const FString& Slot,
	const APawn* SourcePawn)
{
	const TArray<FAeyerjiAbilitySlot> ExistingActionBar = Data ? Data->ActionBar : TArray<FAeyerjiAbilitySlot>();
	const FName ExistingPassiveId = Data ? Data->SelectedPassiveId : NAME_None;

	if (!BuildAeyerjiSaveDataFromRuntime(Data, PS, Slot, SourcePawn))
	{
		return;
	}

	PreserveExistingActionBarIfCaptureIsEmpty(Data, ExistingActionBar, ExistingPassiveId, Slot);

	if (CommitProfileThroughSaveManager(Data, PS))
	{
		UE_LOG(LogTemp, Display, TEXT("SaveAeyerjiCharFromPawn(Success): Slot=%s committed via save manager XP=%f Level=%d"),
			*Slot,
			Data->Attributes.XP,
			Data->Attributes.Level);
		return;
	}

	const int32 RemovedInvalidAttributes = UAeyerjiInventoryComponent::SanitizeSaveDataAttributes(Data->Inventory);
	if (RemovedInvalidAttributes > 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("SaveAeyerjiCharFromPawn: Slot=%s pruned %d invalid item stat attribute references before fallback save."),
			*Slot,
			RemovedInvalidAttributes);
	}

	if (!UGameplayStatics::SaveGameToSlot(Data, Slot, 0))
	{
		UE_LOG(LogTemp, Error, TEXT("Save failed for slot %s"), *Slot);
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("SaveAeyerjiCharFromPawn(Success): Slot=%s saved XP=%f Level=%d"),
			*Slot,
			Data->Attributes.XP,
			Data->Attributes.Level);
	}
}

int32 UCharacterStatsLibrary::TagDepth(const FGameplayTag &Tag)

{

	int32 Depth = 0;

	const FString S = Tag.ToString();

	for (const TCHAR Ch : S)

	{

		if (Ch == TEXT('.'))
		{
			++Depth;
		}
	}

	return Depth;
}

FGameplayTag UCharacterStatsLibrary::GetLeafTagFromBranchTag(const UAbilitySystemComponent *ASC, FGameplayTag BranchTag)

{

	if (!ASC || !BranchTag.IsValid())

	{

		return BranchTag; // fallback
	}

	FGameplayTag BestTag; // invalid means "not found yet"

	int32 BestDepth = -1;

	bool bBestWasDynamic = false;

	auto Consider = [&](const FGameplayTag &Candidate, bool bFromDynamic)

	{
		if (!Candidate.IsValid())
			return;

		// Only consider tags that are at/under the branch (hierarchical match).

		if (!Candidate.MatchesTag(BranchTag))
			return;

		const int32 Depth = TagDepth(Candidate);

		const bool bPrefer = (Depth > BestDepth) || (Depth == BestDepth && bFromDynamic && !bBestWasDynamic);

		if (bPrefer)

		{

			BestTag = Candidate;

			BestDepth = Depth;

			bBestWasDynamic = bFromDynamic;
		}
	};

	// Scan all activatable abilities

	const TArray<FGameplayAbilitySpec> &Specs = ASC->GetActivatableAbilities();

	for (const FGameplayAbilitySpec &Spec : Specs)

	{

		// 1) Dynamic spec source tags (replacement for deprecated DynamicAbilityTags)

		{

			TArray<FGameplayTag> Dyn;

			Spec.GetDynamicSpecSourceTags().GetGameplayTagArray(Dyn);

			for (const FGameplayTag &T : Dyn)

			{

				Consider(T, /*bFromDynamic=*/true);
			}
		}

		// 2) Ability asset tags (the GA's AbilityTags, i.e. your "AssetTags (Default AbilityTags)")

		if (Spec.Ability)

		{

			const FGameplayTagContainer &AbilityTags = Spec.Ability->GetAssetTags();

			TArray<FGameplayTag> Arr;

			AbilityTags.GetGameplayTagArray(Arr);

			for (const FGameplayTag &T : Arr)

			{

				Consider(T, /*bFromDynamic=*/false);
			}
		}
	}

	// If nothing deeper was found, return the branch itself (safe fallback).

	return BestTag.IsValid() ? BestTag : BranchTag;
}

FGameplayTag UCharacterStatsLibrary::GetLeafTagFromBranchTag_Container(const UAbilitySystemComponent *ASC, const FGameplayTagContainer &BranchTags)

{

	// Use the first tag in the container as the branch.

	FGameplayTag Branch;

	{

		TArray<FGameplayTag> Arr;

		BranchTags.GetGameplayTagArray(Arr);

		if (Arr.Num() > 0)
		{
			Branch = Arr[0];
		}
	}

	return GetLeafTagFromBranchTag(ASC, Branch);
}

FGameplayTagContainer UCharacterStatsLibrary::MakeContainerFromLeaf(const UAbilitySystemComponent *ASC, FGameplayTag BranchTag)

{

	FGameplayTagContainer Out;

	Out.AddTag(GetLeafTagFromBranchTag(ASC, BranchTag));

	return Out;
}

namespace

{

	// Internal: pick the best spec and its deepest matching tag under BranchTag.

	const FGameplayAbilitySpec *FindBestSpecForBranchTag(

		const UAbilitySystemComponent *ASC,

		const FGameplayTag &BranchTag,

		FGameplayTag &OutLeafTag)

	{

		if (!ASC || !BranchTag.IsValid())

		{

			OutLeafTag = BranchTag;

			return nullptr;
		}

		const FGameplayAbilitySpec *BestSpec = nullptr;

		FGameplayTag BestTag;

		int32 BestDepth = -1;

		bool bBestWasDynamic = false;

		auto Consider = [&](const FGameplayTag &Candidate, bool bFromDynamic, const FGameplayAbilitySpec &Spec)

		{
			if (!Candidate.IsValid() || !Candidate.MatchesTag(BranchTag))

				return;

			const int32 Depth = UCharacterStatsLibrary::TagDepth(Candidate);

			const bool bPrefer = (Depth > BestDepth) || (Depth == BestDepth && bFromDynamic && !bBestWasDynamic);

			if (bPrefer)

			{

				BestSpec = &Spec;

				BestTag = Candidate;

				BestDepth = Depth;

				bBestWasDynamic = bFromDynamic;
			}
		};

		const TArray<FGameplayAbilitySpec> &Specs = ASC->GetActivatableAbilities();

		for (const FGameplayAbilitySpec &Spec : Specs)

		{

			// 1) Dynamic spec source tags (UE 5.6 replacement for DynamicAbilityTags)

			TArray<FGameplayTag> Dyn;

			Spec.GetDynamicSpecSourceTags().GetGameplayTagArray(Dyn);

			for (const FGameplayTag &T : Dyn)

			{

				Consider(T, /*bFromDynamic=*/true, Spec);
			}

			// 2) Ability asset tags (GA AbilityTags)

			if (Spec.Ability)

			{

				const FGameplayTagContainer &AbilityTags = Spec.Ability->GetAssetTags();

				TArray<FGameplayTag> Tags;

				AbilityTags.GetGameplayTagArray(Tags);

				for (const FGameplayTag &T : Tags)

				{

					Consider(T, /*bFromDynamic=*/false, Spec);
				}
			}
		}

		OutLeafTag = BestTag.IsValid() ? BestTag : BranchTag;

		return BestSpec;
	}

}


TSubclassOf<UGameplayAbility> UCharacterStatsLibrary::GetAbilityClassForBranchTag(

	const UAbilitySystemComponent *ASC, FGameplayTag BranchTag)

{

	FGameplayTag Leaf;

	if (const FGameplayAbilitySpec *Spec = FindBestSpecForBranchTag(ASC, BranchTag, Leaf))

	{

		return Spec->Ability ? Spec->Ability->GetClass() : nullptr;
	}

	return nullptr;
}

UGameplayAbility *UCharacterStatsLibrary::GetAbilityCDOForBranchTag(

	const UAbilitySystemComponent *ASC, FGameplayTag BranchTag)

{

	FGameplayTag Leaf;

	if (const FGameplayAbilitySpec *Spec = FindBestSpecForBranchTag(ASC, BranchTag, Leaf))

	{

		// Spec.Ability points at the CDO of the ability class.

		return Spec->Ability;
	}

	return nullptr;
}

UPlayerStatsTrackingComponent* UCharacterStatsLibrary::GetPlayerStatsTracking(const AActor* Actor)
{
	if (!Actor)
	{
		return nullptr;
	}

	if (UPlayerStatsTrackingComponent* Direct = Actor->FindComponentByClass<UPlayerStatsTrackingComponent>())
	{
		return Direct;
	}

	if (const APawn* Pawn = Cast<APawn>(Actor))
	{
		if (APlayerState* PS = Pawn->GetPlayerState())
		{
			if (UPlayerStatsTrackingComponent* FromPS = PS->FindComponentByClass<UPlayerStatsTrackingComponent>())
			{
				return FromPS;
			}
		}
	}

	return nullptr;
}

bool UCharacterStatsLibrary::HasPlayerPickedUpItemId(const AActor* Actor, FName ItemId)
{
	if (ItemId.IsNone())
	{
		return false;
	}

	if (UPlayerStatsTrackingComponent* Stats = GetPlayerStatsTracking(Actor))
	{
		if (Stats->HasPickedUpItemId(ItemId))
		{
			return true;
		}

		// Compatibility: normalize incoming keys through definition resolution.
		if (const UItemDefinition* Definition = ResolveItemDefinitionByKey(nullptr, ItemId))
		{
			const FName CanonicalKey = Definition->GetDefinitionKey();
			if (!CanonicalKey.IsNone() && CanonicalKey != ItemId)
			{
				return Stats->HasPickedUpItemId(CanonicalKey);
			}
		}
	}

	return false;
}

ULootService* UCharacterStatsLibrary::GetLootService(UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	if (UGameInstance* GI = WorldContextObject->GetWorld() ? WorldContextObject->GetWorld()->GetGameInstance() : nullptr)
	{
		return GI->GetSubsystem<ULootService>();
	}

	return nullptr;
}

UItemDefinition* UCharacterStatsLibrary::GetDefinitionFromLootResult(const FLootDropResult& Result)
{
	if (Result.ItemDefinition)
	{
		return Result.ItemDefinition;
	}

	if (Result.ItemDefinitionKey != NAME_None)
	{
		return ResolveItemDefinitionByKey(nullptr, Result.ItemDefinitionKey);
	}

	return nullptr;
}

UItemDefinition* UCharacterStatsLibrary::ResolveItemDefinitionByKey(UObject* WorldContextObject, FName ItemDefinitionKey)
{
	(void)WorldContextObject;

	if (!ItemDefinitionKey.IsValid() || ItemDefinitionKey.IsNone())
	{
		return nullptr;
	}

	UAssetManager& Manager = UAssetManager::Get();
	const FPrimaryAssetType AssetType(UItemDefinition::StaticClass()->GetFName());
	const FString KeyString = ItemDefinitionKey.ToString();

	// Preferred path: key is the full soft object path of the item definition asset.
	const FSoftObjectPath DirectPath(KeyString);
	if (DirectPath.IsValid())
	{
		if (UItemDefinition* Existing = Cast<UItemDefinition>(DirectPath.ResolveObject()))
		{
			return Existing;
		}

		if (UItemDefinition* Loaded = Cast<UItemDefinition>(Manager.GetStreamableManager().LoadSynchronous(DirectPath, false)))
		{
			return Loaded;
		}
	}

	TArray<FPrimaryAssetId> AssetIds;
	Manager.GetPrimaryAssetIdList(AssetType, AssetIds);

	// Compatibility fallback: support keys that were authored as primary asset names.
	for (const FPrimaryAssetId& Id : AssetIds)
	{
		if (Id.PrimaryAssetName != ItemDefinitionKey)
		{
			continue;
		}

		if (UItemDefinition* Def = Cast<UItemDefinition>(Manager.GetPrimaryAssetObject(Id)))
		{
			return Def;
		}

		const FSoftObjectPath Path = Manager.GetPrimaryAssetPath(Id);
		if (Path.IsValid())
		{
			if (UItemDefinition* Loaded = Cast<UItemDefinition>(Manager.GetStreamableManager().LoadSynchronous(Path, false)))
			{
				return Loaded;
			}
		}
	}

	// Final fallback: scan primary assets and compare their derived definition keys.
	for (const FPrimaryAssetId& Id : AssetIds)
	{
		if (UItemDefinition* Def = Cast<UItemDefinition>(Manager.GetPrimaryAssetObject(Id)))
		{
			if (Def->GetDefinitionKey() == ItemDefinitionKey)
			{
				return Def;
			}
		}
		else
		{
			const FSoftObjectPath Path = Manager.GetPrimaryAssetPath(Id);
			if (Path.IsValid())
			{
				if (UItemDefinition::MakeDefinitionKeyFromSoftPath(Path) != ItemDefinitionKey)
				{
					continue;
				}

				if (UItemDefinition* Loaded = Cast<UItemDefinition>(Manager.GetStreamableManager().LoadSynchronous(Path, false)))
				{
					return Loaded;
				}
			}
		}
	}

	return nullptr;
}

UItemDefinition* UCharacterStatsLibrary::ResolveItemDefinitionById(UObject* WorldContextObject, FName ItemId)
{
	return ResolveItemDefinitionByKey(WorldContextObject, ItemId);
}

static UAbilitySystemComponent *GetAscFromActor(const AActor *Actor)

{

	if (!Actor)
		return nullptr;

	// 1) If the actor itself implements ASI

	if (const IAbilitySystemInterface *ASI = Cast<IAbilitySystemInterface>(Actor))

	{

		return ASI->GetAbilitySystemComponent();
	}

	// 2) If it's a pawn/character, try its components

	if (const APawn *Pawn = Cast<APawn>(Actor))

	{

		// Common case: ASC is on the pawn

		if (const IAbilitySystemInterface *PawnASI = Cast<IAbilitySystemInterface>(Pawn))

		{

			return PawnASI->GetAbilitySystemComponent();
		}

		// Or on the PlayerState (GAS common pattern)

		if (const APlayerState *PS = Pawn->GetPlayerState())

		{

			if (const IAbilitySystemInterface *PSASI = Cast<IAbilitySystemInterface>(PS))

			{

				return PSASI->GetAbilitySystemComponent();
			}
		}
	}

	return nullptr;
}

float UCharacterStatsLibrary::GetAttackRangeFromActorASC(const AActor *Actor, float FallbackRange)

{

	const UAbilitySystemComponent *ASC = GetAscFromActor(Actor);

	if (!ASC)
		return (FallbackRange > 0.f) ? FallbackRange : 0.f;

	const UAeyerjiAttributeSet *Set = ASC->GetSet<UAeyerjiAttributeSet>();

	if (!Set)
		return (FallbackRange > 0.f) ? FallbackRange : 0.f;

	auto Attr = UAeyerjiAttributeSet::GetAttackRangeAttribute();
	float Current = ASC->GetNumericAttribute(Attr);
	if (Current > 0.f) 
	{
		return Current;
	}

	if (FallbackRange > 0.f) return FallbackRange;
	return 0.f;
}

bool UCharacterStatsLibrary::ComputeAttackRangeDestination(const FVector &SelfLocation2D,

														   const FVector &TargetLocation2D,

														   const float AttackRange,

														   float StopAtPercentOfRange,

														   FVector &OutDestination)

{

	// Default: stay where we are

	OutDestination = SelfLocation2D;

	if (AttackRange <= 0.f)

	{

		return false;
	}

	// Normalize percentage: accept 0..1 or 0..100

	float P = StopAtPercentOfRange;

	if (P <= 0.f)

	{

		P = 0.8f; // sensible default: stop at 80% of range
	}

	else if (P > 1.f)

	{

		P = (P <= 100.f) ? (P * 0.01f) : 1.f;
	}

	P = FMath::Clamp(P, 0.0f, 1.0f);

	const float DesiredDistance = AttackRange * P;

	// Work strictly in 2D (ignore Z)

	FVector ToSelf = SelfLocation2D - TargetLocation2D;

	ToSelf.Z = 0.f;

	const float CurrentDistance = ToSelf.Size2D();

	if (CurrentDistance <= DesiredDistance + KINDA_SMALL_NUMBER)

	{

		// Already inside the desired ring; no move needed

		return false;
	}

	const FVector DirFromTargetToSelf = ToSelf.GetSafeNormal();

	if (DirFromTargetToSelf.IsNearlyZero())

	{

		return false; // overlapping positions; no stable direction
	}

	// Point on the ray from Target toward Self, at DesiredDistance from Target

	FVector NewLoc = TargetLocation2D + DirFromTargetToSelf * DesiredDistance;

	// Keep current height

	NewLoc.Z = SelfLocation2D.Z;

	OutDestination = NewLoc;

	return true;
}

bool UCharacterStatsLibrary::IsWithinAttackRange(const AActor *SelfActor,

												 const AActor *TargetActor,
												// Will get divided by 100
												 float StopAtPercentOfRange,

												 float FallbackRange)

{

	if (!SelfActor || !TargetActor)
		return false;
	StopAtPercentOfRange *= 0.01f;

	volatile float Range = GetAttackRangeFromActorASC(SelfActor, FallbackRange) * FMath::Max(StopAtPercentOfRange, 0.f);

	if (Range <= 0.f)
		return false;

	const FVector A = SelfActor->GetActorLocation();

	const FVector B = TargetActor->GetActorLocation();

	return FVector::DistSquared(A, B) <= FMath::Square(Range);
}

bool UCharacterStatsLibrary::GetSavedDifficulty(const UObject* WorldContextObject, float& OutSlider, float& OutScale)
{
	OutSlider = GetDefaultDifficultySlider();
	OutScale = FMath::Clamp(OutSlider / UAeyerjiDifficultySettings::DifficultySliderMax, 0.f, 1.f);

	if (!WorldContextObject)
	{
		UE_LOG(LogTemp, Warning, TEXT("GetSavedDifficulty: WorldContextObject is null. Returning default %.2f."), OutSlider);
		return false;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Warning, TEXT("GetSavedDifficulty: World is null. Returning default %.2f."), OutSlider);
		return false;
	}

	UAeyerjiGameInstance* GI = Cast<UAeyerjiGameInstance>(World->GetGameInstance());
	if (!GI)
	{
		UE_LOG(LogTemp, Warning, TEXT("GetSavedDifficulty: GameInstance is not UAeyerjiGameInstance. Returning default %.2f."), OutSlider);
		return false;
	}

	UE_LOG(LogTemp, Display,
		TEXT("GetSavedDifficulty: GI state before resolve HasDiff=%d HasTier=%d Diff=%.2f Tier=%d"),
		GI->HasDifficultySelection() ? 1 : 0,
		GI->HasWorldTierSelection() ? 1 : 0,
		GI->GetDifficultySlider(),
		GI->GetWorldTier());

	if (!GI->HasDifficultySelection() && !GI->HasWorldTierSelection())
	{
		AAeyerjiPlayerState* LocalPS = nullptr;
		if (APlayerController* LocalPC = UGameplayStatics::GetPlayerController(WorldContextObject, 0))
		{
			LocalPS = LocalPC->GetPlayerState<AAeyerjiPlayerState>();
		}

		if (UAeyerjiSaveManagerSubsystem* SaveManager = World->GetGameInstance()->GetSubsystem<UAeyerjiSaveManagerSubsystem>())
		{
			UAeyerjiSaveGame* Data = nullptr;
			if (SaveManager->GetCachedOrLocalProfileForOwner(Data, LocalPS) && Data)
			{
				UE_LOG(LogTemp, Display,
					TEXT("GetSavedDifficulty: Loaded cached/local profile SaveFlags(Diff=%d Tier=%d) SaveValues(Diff=%.2f Tier=%d)"),
					Data->bHasDifficultySelection ? 1 : 0,
					Data->bHasWorldTierSelection ? 1 : 0,
					Data->DifficultySlider,
					Data->WorldTier);
				ApplySavedDifficultyToGameInstance(Data, GI);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("GetSavedDifficulty: Failed to load cached/local profile. Applying default WorldTier=%d."), GetDefaultWorldTier());
				GI->ApplySavedWorldTier(GetDefaultWorldTier());
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("GetSavedDifficulty: Save manager unavailable; applying default WorldTier=%d."), GetDefaultWorldTier());
			GI->ApplySavedWorldTier(GetDefaultWorldTier());
		}
	}

	OutSlider = GI->GetDifficultySlider();
	OutScale = GI->GetDifficultyScale();
	UE_LOG(LogTemp, Display, TEXT("GetSavedDifficulty: Returning Difficulty=%.2f Scale=%.3f WorldTier=%d"), OutSlider, OutScale, GI->GetWorldTier());
	return true;
}

bool UCharacterStatsLibrary::GetSavedWorldTier(const UObject* WorldContextObject, int32& OutWorldTier)
{
	OutWorldTier = GetDefaultWorldTier();

	float SavedSlider = GetDefaultDifficultySlider();
	float SavedScale = 0.f;
	if (!GetSavedDifficulty(WorldContextObject, SavedSlider, SavedScale))
	{
		OutWorldTier = DifficultySliderToUiWorldTier(SavedSlider);
		UE_LOG(LogTemp, Warning, TEXT("GetSavedWorldTier: Difficulty lookup failed. Derived WorldTier=%d from Difficulty=%.2f."), OutWorldTier, SavedSlider);
		return false;
	}

	if (!WorldContextObject)
	{
		OutWorldTier = DifficultySliderToUiWorldTier(SavedSlider);
		UE_LOG(LogTemp, Warning, TEXT("GetSavedWorldTier: WorldContextObject is null after difficulty lookup. Derived WorldTier=%d."), OutWorldTier);
		return true;
	}

	if (UWorld* World = WorldContextObject->GetWorld())
	{
		if (const UAeyerjiGameInstance* GI = Cast<UAeyerjiGameInstance>(World->GetGameInstance()))
		{
			OutWorldTier = GI->GetWorldTier();
			UE_LOG(LogTemp, Display, TEXT("GetSavedWorldTier: Returning authoritative WorldTier=%d (Difficulty=%.2f)."), OutWorldTier, GI->GetDifficultySlider());
			return true;
		}
	}

	OutWorldTier = DifficultySliderToUiWorldTier(SavedSlider);
	UE_LOG(LogTemp, Warning, TEXT("GetSavedWorldTier: GI unavailable. Derived WorldTier=%d from Difficulty=%.2f."), OutWorldTier, SavedSlider);
	return true;
}

bool UCharacterStatsLibrary::RecordBestRunTimeSecondsForDifficulty(const AAeyerjiPlayerState* PS, float RunTimeSeconds, float DifficultySlider)
{
	UWorld* World = PS ? PS->GetWorld() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	if (!PS || !World || !GameInstance || World->GetNetMode() == NM_Client)
	{
		return false;
	}

	if (!FMath::IsFinite(RunTimeSeconds) || RunTimeSeconds <= 0.f)
	{
		return false;
	}

	UAeyerjiSaveManagerSubsystem* SaveManager = GameInstance->GetSubsystem<UAeyerjiSaveManagerSubsystem>();
	if (!SaveManager)
	{
		return false;
	}

	UAeyerjiSaveGame* Data = nullptr;
	if (!SaveManager->GetServerCachedProfile(PS, Data) || !Data)
	{
		Data = SaveManager->CreateDefaultProfile(SaveManager->ResolveOwnerKey(PS), 1);
		if (!Data)
		{
			return false;
		}
	}

	const int32 DifficultyKey = MakeDifficultyKey(DifficultySlider);
	const float ExistingBest = Data->BestRunTimeSecondsByDifficulty.FindRef(DifficultyKey);

	const bool bHasExisting = Data->BestRunTimeSecondsByDifficulty.Contains(DifficultyKey) && ExistingBest > 0.f;
	const bool bIsNewBest = !bHasExisting || RunTimeSeconds < ExistingBest;
	if (!bIsNewBest)
	{
		return true;
	}

	Data->BestRunTimeSecondsByDifficulty.Add(DifficultyKey, RunTimeSeconds);
	return const_cast<AAeyerjiPlayerState*>(PS)->CommitPreparedCheckpointProfile(Data, EAeyerjiSaveCheckpointReason::RunCompleted, /*bBumpRevision=*/true);
}

bool UCharacterStatsLibrary::RecordCompletedRunAndSaveCharacter(const AAeyerjiPlayerState* PS, const FAeyerjiRunResults& Results)
{
	UWorld* World = PS ? PS->GetWorld() : nullptr;
	UGameInstance* GameInstance = World ? World->GetGameInstance() : nullptr;
	if (!PS || !World || !GameInstance || World->GetNetMode() == NM_Client)
	{
		return false;
	}

	UAeyerjiSaveManagerSubsystem* SaveManager = GameInstance->GetSubsystem<UAeyerjiSaveManagerSubsystem>();
	if (!SaveManager)
	{
		return false;
	}

	UAeyerjiSaveGame* Data = nullptr;
	if (!SaveManager->GetServerCachedProfile(PS, Data) || !Data)
	{
		Data = SaveManager->CreateDefaultProfile(SaveManager->ResolveOwnerKey(PS), 1);
		if (!Data)
		{
			return false;
		}
	}

	const FString Slot = MakeStableCharSlotName(PS);
	if (!BuildAeyerjiSaveDataFromRuntime(Data, PS, Slot))
	{
		return false;
	}

	AppendRecentRunRecord(Data, Results);
	const float BestTime = ApplyRunBestTimeToSaveData(Data, Results);

	const bool bSaved = const_cast<AAeyerjiPlayerState*>(PS)->CommitPreparedCheckpointProfile(Data, EAeyerjiSaveCheckpointReason::RunCompleted, /*bBumpRevision=*/true);
	UE_LOG(LogTemp, Display,
		TEXT("RecordCompletedRunAndSaveCharacter: Slot=%s SaveResult=%d Result=%d Time=%.2f Best=%.2f RecentRuns=%d"),
		*Slot,
		bSaved ? 1 : 0,
		static_cast<int32>(Results.Resolution),
		Results.RunTimeSeconds,
		BestTime,
		Data->RecentRuns.Num());
	return bSaved;
}

bool UCharacterStatsLibrary::GetBestRunTimeSecondsForDifficulty(const AAeyerjiPlayerState* PS, float DifficultySlider, float& OutBestRunTimeSeconds)
{
	OutBestRunTimeSeconds = 0.f;

	if (!PS)
	{
		return false;
	}

	UWorld* World = PS->GetWorld();
	if (!World || !World->GetGameInstance())
	{
		return false;
	}

	UAeyerjiSaveManagerSubsystem* SaveManager = World->GetGameInstance()->GetSubsystem<UAeyerjiSaveManagerSubsystem>();
	if (!SaveManager)
	{
		return false;
	}

	UAeyerjiSaveGame* MutableData = nullptr;
	if (World->GetNetMode() != NM_Client)
	{
		SaveManager->GetServerCachedProfile(PS, MutableData);
	}

	if (!MutableData)
	{
		SaveManager->GetCachedOrLocalProfileForOwner(MutableData, PS);
	}

	const UAeyerjiSaveGame* Data = MutableData;
	if (!Data)
	{
		return false;
	}

	const int32 DifficultyKey = MakeDifficultyKey(DifficultySlider);
	const float* Found = Data->BestRunTimeSecondsByDifficulty.Find(DifficultyKey);
	if (!Found || *Found <= 0.f)
	{
		return false;
	}

	OutBestRunTimeSeconds = *Found;
	return true;
}

void UCharacterStatsLibrary::SmoothFaceActorTowardTarget(AActor *Source, AActor *Target, float DeltaSeconds,

														 float InterpSpeed, bool bYawOnly, float ToleranceDeg, FRotator &OutNewRotation, bool &bWithinTolerance)

{

	OutNewRotation = FRotator::ZeroRotator;

	bWithinTolerance = false;

	if (!Source || !Target || DeltaSeconds <= 0.f || InterpSpeed <= 0.f)

	{

		return;
	}

	const FVector SrcLoc = Source->GetActorLocation();

	const FVector TgtLoc = Target->GetActorLocation();

	const FRotator Current = Source->GetActorRotation();

	FRotator Desired = UKismetMathLibrary::FindLookAtRotation(SrcLoc, TgtLoc);

	if (bYawOnly)

	{

		Desired.Pitch = 0.f;

		Desired.Roll = 0.f;
	}

	// Interp with shortest path

	const FRotator NewRot = FMath::RInterpTo(Current, Desired, DeltaSeconds, InterpSpeed);

	// Are we close enough?

	const FRotator DeltaRot = UKismetMathLibrary::NormalizedDeltaRotator(Desired, NewRot);

	const float AngleErr = bYawOnly ? FMath::Abs(DeltaRot.Yaw)

									: FMath::Max3(FMath::Abs(DeltaRot.Pitch), FMath::Abs(DeltaRot.Yaw), FMath::Abs(DeltaRot.Roll));

	bWithinTolerance = AngleErr <= ToleranceDeg;

	OutNewRotation = NewRot;
}
