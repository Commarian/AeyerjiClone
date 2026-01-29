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
#include "GameFramework/Character.h"
#include "AIController.h"
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
#include "Systems/LootService.h"
#include "Engine/AssetManager.h"
#include "Engine/World.h"

//This works together with @EAeyerjiStat in CharacterStatsLibrary.h
namespace
{
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

		case EAeyerjiStat::Ailment:
			return UAeyerjiAttributeSet::GetAilmentAttribute();

		case EAeyerjiStat::CritChance:
			return UAeyerjiAttributeSet::GetCritChanceAttribute();

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

		case EAeyerjiStat::AilmentDPS:
			return UAeyerjiAttributeSet::GetAilmentDPSAttribute();

		case EAeyerjiStat::AilmentDuration:
			return UAeyerjiAttributeSet::GetAilmentDurationAttribute();

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

	/* 1ï¸âƒ£  Does the PlayerState already own a pawn? */

	APawn *Pawn = PS->GetPawn();

	if (!Pawn)

	{

		UE_LOG(LogTemp, Error,

			   TEXT("LoadAeyerjiChar  PS %s has no Pawn yet (OnRep_PlayerState fired before possession)"),

			   *GetNameSafe(PS));

		return nullptr;
	}

	/* 2ï¸âƒ£  Does that pawn actually expose an ASC? */

	const bool bImplementsASI = Pawn->GetClass()->ImplementsInterface(UAbilitySystemInterface::StaticClass());

	if (!bImplementsASI)

	{

		UE_LOG(LogTemp, Error,

			   TEXT("LoadAeyerjiChar  Pawn %s does NOT implement IAbilitySystemInterface"),

			   *GetNameSafe(Pawn));

		return nullptr;
	}

	/* 3ï¸âƒ£  Is the component pointer valid? */

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

FString UCharacterStatsLibrary::MakeStableCharSlotName(const APlayerState *PS)

{

	if (!PS)
		return TEXT("UNKNOWN_Char");

	if (const AAeyerjiPlayerState *AeyerjiPS = Cast<AAeyerjiPlayerState>(PS))
	{
		const FString &OverrideSlot = AeyerjiPS->GetSaveSlotOverride();
		if (!OverrideSlot.IsEmpty())
		{
			return OverrideSlot;
		}
	}

	// Prefer stable online IDs if available.

	const FUniqueNetIdRepl &NetId = PS->GetUniqueId();

	if (NetId.IsValid())

	{

		const FUniqueNetId &Raw = *NetId.GetUniqueNetId();

		if (!Raw.GetType().IsEqual(FName("NULL"), ENameCase::IgnoreCase))

		{

			const FString SafeNetId = FPaths::MakeValidFileName(Raw.ToString());

			if (!SafeNetId.IsEmpty())

			{

				return SafeNetId + TEXT("_Char");
			}
		}
	}

	// Development fallback: keep a deterministic per-machine slot.

	const FString DevToken = GetLocalDevSlotToken();

	if (!DevToken.IsEmpty())

	{

		return DevToken + TEXT("_Char");
	}

	// Last resorts: player name or numeric id.

	FString FallbackName = FPaths::MakeValidFileName(PS->GetPlayerName());

	if (!FallbackName.IsEmpty())

	{

		return FallbackName + TEXT("_Char");
	}

	const int32 StableIndex = FMath::Max(0, PS->GetPlayerId());

	return FString::Printf(TEXT("Player%d_Char"), StableIndex);
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

	// TODO add somewhere here a function that can look at previous day's saves if this one doesn't exist yet.

	if (!Data)

	{

		UE_LOG(LogTemp, Warning, TEXT("UCharacterStatsLibrary::LoadAeyerjiChar: Called with null Data. Aborting load."));

		return;
	}

	if (!PS)

	{

		UE_LOG(LogTemp, Warning, TEXT("UCharacterStatsLibrary::LoadAeyerjiChar: Called with null PlayerState. Aborting load."));

		return;
	}

	if (!ASC)

	{

		UE_LOG(LogTemp, Warning, TEXT("UCharacterStatsLibrary::LoadAeyerjiChar: Called with null ASC. Aborting load."));

		return;
	}

	// Restore persisted difficulty selection so UI sliders can reflect the saved choice.
	if (PS->GetWorld())
	{
		if (UAeyerjiGameInstance* GI = Cast<UAeyerjiGameInstance>(PS->GetWorld()->GetGameInstance()))
		{
			if (Data->bHasDifficultySelection)
			{
				GI->SetDifficultySlider(Data->DifficultySlider);
			}
		}
	}

	// Restore lifetime loot stats into the player state component if available.
	if (UPlayerStatsTrackingComponent* StatsComp = PS->FindComponentByClass<UPlayerStatsTrackingComponent>())
	{
		StatsComp->LoadLootStats(Data->LootStats);
	}
	else
	{
		UE_LOG(LogTemp, Verbose, TEXT("LoadAeyerjiChar: PlayerState %s missing PlayerStatsTrackingComponent; skipping loot stats load"), *GetNameSafe(PS));
	}

	// Some older/new slots may not have been initialised with the fixed number of action bar entries.
	// Pad the save data to match the current expected slot count so widgets and selections work.
	const int32 ExpectedSlots = PS->ActionBar.Num();
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

	const int32 SavedLevel = FMath::Max(1, Data->Attributes.Level);

	UE_LOG(LogTemp, Log, TEXT("LoadAeyerjiChar: Restoring XP to %f, Level to %d"), SavedXP, SavedLevel);

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

		// Ensure any level-scaled infinite effects are up-to-date after load

		if (Leveling)

		{

			Leveling->ForceRefreshForCurrentLevel();
		}
	}

	/* ---------- Restore any other replicated data ---------- */

	if (ASC->GetOwnerRole() == ROLE_Authority)
	{
		PS->Server_SetActionBar_Implementation(Data->ActionBar);

		for (const FAeyerjiAbilitySlot &Slot : Data->ActionBar)
		{
			if (!Slot.Class)
			{
				AJ_LOG(PS, TEXT("LoadAeyerjiChar: Slot %s has no Class; skipping ability grant"),
					   *Slot.Description.ToString());
				continue;
			}

			AJ_LOG(PS, TEXT("LoadAeyerjiChar: Granting ability %s from slot %s (Tag=%s)"),
				   *Slot.Class->GetName(),
				   *Slot.Description.ToString(),
				   *Slot.Tag.ToString());
			PS->Server_GrantAbilityFromSlot_Implementation(Slot);
		}

		if (!Data->SelectedPassiveId.IsNone())
		{
			PS->SetPassiveLocal(Data->SelectedPassiveId);
		}

		APawn *Pawn = PS->GetPawn();
		if (UAeyerjiInventoryComponent *Inventory = Pawn ? Pawn->FindComponentByClass<UAeyerjiInventoryComponent>() : nullptr)
		{
			Inventory->ApplySaveData(Data->Inventory);
		}
		else
		{
			AJ_LOG(PS, TEXT("LoadAeyerjiChar: No inventory component found while loading slot %s"), *PS->GetSaveSlotOverride());
		}
	}
}

/* ---------- save ---------- */

void UCharacterStatsLibrary::SaveAeyerjiChar(

	UAeyerjiSaveGame *Data,

	const AAeyerjiPlayerState *PS,

	const FString Slot)

{

	if (!Data || !PS)

	{

		UE_LOG(LogTemp, Error, TEXT("SaveAeyerjiChar: Called with null Data or PlayerState. Aborting save."));

		return;
	}

	// Capture the current difficulty slider for persistence.
	Data->bHasDifficultySelection = false;
	if (PS->GetWorld())
	{
		if (const UAeyerjiGameInstance* GI = Cast<UAeyerjiGameInstance>(PS->GetWorld()->GetGameInstance()))
		{
			if (GI->HasDifficultySelection())
			{
				Data->DifficultySlider = GI->GetDifficultySlider();
				Data->bHasDifficultySelection = true;
			}
		}
	}

	const APawn *Pawn = PS->GetPawn();

	if (Pawn)

	{

		if (UAeyerjiInventoryComponent *Inventory = Pawn->FindComponentByClass<UAeyerjiInventoryComponent>())
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

		if (const IAbilitySystemInterface *ASI = Cast<IAbilitySystemInterface>(Pawn))

		{

			if (const UAbilitySystemComponent *ASC = ASI->GetAbilitySystemComponent())

			{

				for (UAttributeSet *Set : ASC->GetSpawnedAttributes())

				{

					if (IsValid(Set) && Cast<UAeyerjiAttributeSet>(Set))

					{

						UAeyerjiAttributeSet *AeyerjiSet = Cast<UAeyerjiAttributeSet>(Set);

						UE_LOG(LogTemp, Log, TEXT("SaveAeyerjiChar: Found Attribute XP found '%f'"), AeyerjiSet->GetXP());

						Data->Attributes.XP = AeyerjiSet->GetXP();

						Data->Attributes.Level = FMath::RoundToInt(AeyerjiSet->GetLevel());

						Data->Attributes.Level = FMath::RoundToInt(AeyerjiSet->GetLevel());
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

	// Save action bar data

	Data->ActionBar = PS->ActionBar;
	Data->SelectedPassiveId = PS->GetSelectedPassiveId();

	UE_LOG(LogTemp, Display, TEXT("SaveAeyerjiChar(BeforeWrite): Slot=%s PS=%s Pawn=%s DataXP=%f DataLevel=%d"),
		   *Slot,
		   *GetNameSafe(PS),
		   *GetNameSafe(PS->GetPawn()),
		   Data->Attributes.XP,
		   Data->Attributes.Level);

	FString FirstSlotName = PS->ActionBar.Num() ? PS->ActionBar[0].Description.ToString()

												: TEXT("Empty");

	FString SecondSlotName = PS->ActionBar.Num() ? PS->ActionBar[1].Description.ToString()

												 : TEXT("Empty");

	UE_LOGFMT(LogTemp, Log,

			  "Saving to slot '{Slot}'. XP={XP}, Level={Level}. SaveGameName='{SaveGameName}', First item='{First}', Second item='{Second}'",

			  ("Slot", Slot),

			  ("XP", Data->Attributes.XP),

			  ("Level", Data->Attributes.Level),

			  ("SaveGameName", Data->GetName()),

			  ("First", PS->ActionBar.Num() ? PS->ActionBar[0].Description.ToString() : TEXT("Empty")),

			  ("Second", PS->ActionBar.Num() > 1 ? PS->ActionBar[1].Description.ToString() : TEXT("Empty")));

	UE_LOG(LogTemp, Display, TEXT("SaveAeyerjiChar(AfterGather): Slot=%s XP=%f Level=%d (FirstSlot=%s SecondSlot=%s)"),
		   *Slot,
		   Data->Attributes.XP,
		   Data->Attributes.Level,
		   PS->ActionBar.Num() ? *PS->ActionBar[0].Description.ToString() : TEXT("Empty"),
		   PS->ActionBar.Num() > 1 ? *PS->ActionBar[1].Description.ToString() : TEXT("Empty"));

	// If you ever introduce multiple characters per account,

	// extend the struct with CharacterId etc.

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
		return Stats->HasPickedUpItemId(ItemId);
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

	if (Result.ItemId != NAME_None)
	{
		return ResolveItemDefinitionById(nullptr, Result.ItemId);
	}

	return nullptr;
}

UItemDefinition* UCharacterStatsLibrary::ResolveItemDefinitionById(UObject* WorldContextObject, FName ItemId)
{
	if (ItemId.IsNone())
	{
		return nullptr;
	}

	UAssetManager& Manager = UAssetManager::Get();
	const FPrimaryAssetType AssetType(UItemDefinition::StaticClass()->GetFName());

	TArray<FPrimaryAssetId> AssetIds;
	Manager.GetPrimaryAssetIdList(AssetType, AssetIds);

	// First try asset name match (fast, avoids loads).
	for (const FPrimaryAssetId& Id : AssetIds)
	{
		if (Id.PrimaryAssetName == ItemId)
		{
			if (UItemDefinition* Def = Cast<UItemDefinition>(Manager.GetPrimaryAssetObject(Id)))
			{
				return Def;
			}

			// Try a synchronous load if not resident yet.
			const FSoftObjectPath Path = Manager.GetPrimaryAssetPath(Id);
			if (Path.IsValid())
			{
				if (UItemDefinition* Loaded = Cast<UItemDefinition>(Manager.GetStreamableManager().LoadSynchronous(Path, false)))
				{
					return Loaded;
				}
			}
		}
	}

	// Fallback: load candidates and compare their ItemId property.
	for (const FPrimaryAssetId& Id : AssetIds)
	{
		if (UItemDefinition* Def = Cast<UItemDefinition>(Manager.GetPrimaryAssetObject(Id)))
		{
			if (Def->ItemId == ItemId)
			{
				return Def;
			}
		}
		else
		{
			const FSoftObjectPath Path = Manager.GetPrimaryAssetPath(Id);
			if (Path.IsValid())
			{
				if (UItemDefinition* Loaded = Cast<UItemDefinition>(Manager.GetStreamableManager().LoadSynchronous(Path, false)))
				{
					if (Loaded->ItemId == ItemId)
					{
						return Loaded;
					}
				}
			}
		}
	}

	return nullptr;
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
	StopAtPercentOfRange *= 0.01;

	volatile float Range = GetAttackRangeFromActorASC(SelfActor, FallbackRange) * FMath::Max(StopAtPercentOfRange, 0.f);

	if (Range <= 0.f)
		return false;

	const FVector A = SelfActor->GetActorLocation();

	const FVector B = TargetActor->GetActorLocation();

	return FVector::DistSquared(A, B) <= FMath::Square(Range);
}

bool UCharacterStatsLibrary::GetSavedDifficulty(const UObject* WorldContextObject, float& OutSlider, float& OutScale)
{
	OutSlider = 0.f;
	OutScale = 0.f;

	if (!WorldContextObject)
	{
		return false;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World)
	{
		return false;
	}

	if (const UAeyerjiGameInstance* GI = Cast<UAeyerjiGameInstance>(World->GetGameInstance()))
	{
		if (GI->HasDifficultySelection())
		{
			OutSlider = GI->GetDifficultySlider();
			OutScale = GI->GetDifficultyScale();
			return true;
		}
	}

	return false;
}

bool UCharacterStatsLibrary::RecordBestRunTimeSecondsForDifficulty(const AAeyerjiPlayerState* PS, float RunTimeSeconds, float DifficultySlider)
{
	if (!PS || !PS->GetWorld() || PS->GetWorld()->GetNetMode() == NM_Client)
	{
		return false;
	}

	if (RunTimeSeconds <= 0.f)
	{
		return false;
	}

	const FString Slot = MakeStableCharSlotName(PS);
	if (Slot.IsEmpty())
	{
		return false;
	}

	bool bLoadedExisting = false;
	UAeyerjiSaveGame* Data = LoadOrCreateAeyerjiSave(Slot, bLoadedExisting);
	if (!Data)
	{
		return false;
	}

	const int32 DifficultyKey = FMath::Clamp(FMath::RoundToInt(DifficultySlider), 0, 1000);
	const float ExistingBest = Data->BestRunTimeSecondsByDifficulty.FindRef(DifficultyKey);

	const bool bHasExisting = Data->BestRunTimeSecondsByDifficulty.Contains(DifficultyKey) && ExistingBest > 0.f;
	const bool bIsNewBest = !bHasExisting || RunTimeSeconds < ExistingBest;
	if (!bIsNewBest)
	{
		return true;
	}

	Data->BestRunTimeSecondsByDifficulty.Add(DifficultyKey, RunTimeSeconds);
	return UGameplayStatics::SaveGameToSlot(Data, Slot, 0);
}

bool UCharacterStatsLibrary::GetBestRunTimeSecondsForDifficulty(const AAeyerjiPlayerState* PS, float DifficultySlider, float& OutBestRunTimeSeconds)
{
	OutBestRunTimeSeconds = 0.f;

	if (!PS)
	{
		return false;
	}

	const FString Slot = MakeStableCharSlotName(PS);
	if (Slot.IsEmpty() || !UGameplayStatics::DoesSaveGameExist(Slot, 0))
	{
		return false;
	}

	USaveGame* RawSave = UGameplayStatics::LoadGameFromSlot(Slot, 0);
	const UAeyerjiSaveGame* Data = Cast<UAeyerjiSaveGame>(RawSave);
	if (!Data)
	{
		return false;
	}

	const int32 DifficultyKey = FMath::Clamp(FMath::RoundToInt(DifficultySlider), 0, 1000);
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
