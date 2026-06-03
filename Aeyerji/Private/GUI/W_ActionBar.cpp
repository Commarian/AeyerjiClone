#include "GUI/W_ActionBar.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Abilities/AeyerjiAbilityTuning.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayAbilitySpec.h"
#include "Aeyerji/AeyerjiPlayerController.h"
#include "GUI/W_ActionSlotNative.h"
#include "GUI/W_AbilitySelectionNative.h"
#include "GUI/AbilityTooltipData.h"

#include "Components/HorizontalBox.h"
#include "DrawDebugHelpers.h"
#include "Engine/GameInstance.h"
#include "Aeyerji/AeyerjiPlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Logging/AeyerjiLog.h"
#include "GameFramework/Pawn.h"

namespace
{
	void ShowActionBarDebugMessage(const UObject* WorldContextObject, const FString& Message)
	{
		if (GEngine && IsValid(WorldContextObject))
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.f, FColor::Red, Message);
		}
	}
}

UW_ActionBar::UW_ActionBar(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
}



/* ----------------------------- Refresh() -------------------------------- */
void UW_ActionBar::Refresh(const TArray<FAeyerjiAbilitySlot> &NewBar)
{
	if (!SlotsBox)
	{
		AJ_LOG(this, "Refresh aborted – SlotsBox == nullptr");
		return;
	}

	const int32 ChildCount = SlotsBox->GetChildrenCount();
	const int32 IncomingSize = NewBar.Num();

	/* Normal update logic – copy data into each slot widget */
	const int32 IterCount = FMath::Min(ChildCount, IncomingSize);
	for (int32 Idx = 0; Idx < IterCount; ++Idx)
	{
		if (UW_ActionSlotNative *SlotWidget = Cast<UW_ActionSlotNative>(SlotsBox->GetChildAt(Idx)))
		{
			SlotWidget->StoredSlotIndex = Idx;
			SlotWidget->StoredSlotData = NewBar[Idx];
			SlotWidget->ClearCooldownDisplay();

			if (NewBar[Idx].Icon)
			{
				SlotWidget->SetIcon(NewBar[Idx].Icon);
			}
			else
			{
				SlotWidget->SetPlaceholderIcon();
			}
		}
		else
		{
			AJ_LOG(this, "Widget at %d is NOT a UW_ActionSlotNative - skipped", Idx);
		}
	}

	if (ChildCount != IncomingSize)
	{
		AJ_LOG(this, "Widget count (%d) ? SaveData count (%d). Check save/load path!", ChildCount, IncomingSize);
	}

	EnsureDefaultPotionSlot(NewBar);
	UpdateCooldowns();
}

/* ----------------------- InitWithPlayerState() --------------------------- */
void UW_ActionBar::InitWithPlayerState(AAeyerjiPlayerState *PS)
{
	if (!PS)
	{
		AJ_LOG(this, TEXT("UW_ActionBar::InitWithPlayerState() no PS"));
		return;
	}
	// AJ_LOG(this, TEXT("UW_ActionBar::InitWithPlayerState() good to go"));
	//  Attach right-click delegates once (idempotent)
	for (int32 i = 0; i < SlotsBox->GetChildrenCount(); ++i)
	{
		if (UW_ActionSlotNative *SlotW = Cast<UW_ActionSlotNative>(SlotsBox->GetChildAt(i)))
		{
			SlotW->OnSlotRightClicked.RemoveDynamic(this, &UW_ActionBar::HandleSlotRightClicked);
			SlotW->OnSlotRightClicked.AddDynamic(this, &UW_ActionBar::HandleSlotRightClicked);

			SlotW->OnSlotLeftClicked.RemoveDynamic(this, &UW_ActionBar::HandleSlotLeftClicked);
			SlotW->OnSlotLeftClicked.AddDynamic(this, &UW_ActionBar::HandleSlotLeftClicked);
		}
	}

	if (PS == CachedPS)
	{
		AJ_LOG(this, "InitWithPlayerState called with SAME PS – forcing refresh");
		Refresh(PS->GetActionBar());
		return;
	}

	if (CachedPS)
	{
		CachedPS->OnActionBarChanged.RemoveDynamic(this, &UW_ActionBar::Refresh);
		CachedPS->OnActionBarSwapBlocked.RemoveDynamic(this, &UW_ActionBar::HandleSwapBlocked);
	}

	CachedPS = PS;

	ResetCachedAbilitySystem();
	CooldownTickAccumulator = CooldownTickInterval;
	CachedPS->OnActionBarChanged.AddDynamic(this, &UW_ActionBar::Refresh);
	CachedPS->OnActionBarSwapBlocked.AddDynamic(this, &UW_ActionBar::HandleSwapBlocked);

	AJ_LOG(this, "Bound to PlayerState=%s", *GetNameSafe(CachedPS));

	Refresh(CachedPS->GetActionBar());
}

void UW_ActionBar::HandleSwapBlocked(FText Reason, TSubclassOf<UGameplayAbility> AbilityClass)
{
	AJ_LOG(this, TEXT("Action bar swap blocked for %s (%s)"),
		*GetNameSafe(AbilityClass),
		*Reason.ToString());

	if (AAeyerjiPlayerController* PC = GetOwningPlayer<AAeyerjiPlayerController>())
	{
		constexpr float DefaultToastDuration = 2.f;
		PC->ShowPopupMessage(Reason, DefaultToastDuration);
	}
}

/* ------------------------- NativeConstruct() ----------------------------- */
void UW_ActionBar::NativeConstruct()
{
	Super::NativeConstruct();


	ResetCachedAbilitySystem();
	CachedPotionSlot.Reset();
	CooldownTickAccumulator = CooldownTickInterval;
	UpdateCooldowns();
}

void UW_ActionBar::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (CooldownTickInterval <= 0.f)
	{
		UpdateCooldowns();
		if (CachedPS && CachedPS->IsProfileLoadApplied())
		{
			EnsureDefaultPotionSlot(CachedPS->GetActionBar());
		}
		return;
	}

	CooldownTickAccumulator += InDeltaTime;
	if (CooldownTickAccumulator >= CooldownTickInterval)
	{
		CooldownTickAccumulator = 0.f;
		UpdateCooldowns();
	}

	if (CachedPS && CachedPS->IsProfileLoadApplied())
	{
		EnsureDefaultPotionSlot(CachedPS->GetActionBar());
	}
}/* -------------------- Context-menu & Ability Picker ---------------------- */
void UW_ActionBar::HandleSlotRightClicked(int32 Index)
{
	if (!PickerClass)
	{
		AJ_LOG(this, "PickerClass not set!");
		return;
	}

	APlayerController *PC = GetOwningPlayer();
	if (!PC)
	{
		AJ_LOG(this, "HandleSlotRightClicked – GetOwningPlayer() == nullptr");
		return;
	}

	if (PickerInstance && PickerInstance->IsInViewport())
	{
		PickerInstance->SetVisibility(ESlateVisibility::Visible);
		PickerInstance->SetFocus();
	}
	else
	{
		PickerInstance = CreateWidget<UW_AbilitySelectionNative>(PC, PickerClass);
		if (!PickerInstance)
		{
			AJ_LOG(this, "Failed to spawn PickerInstance");
			return;
		}

		PickerInstance->OnAbilityPicked.AddDynamic(this, &UW_ActionBar::HandleAbilityPicked);
		PickerInstance->AddToViewport(100);
		PickerInstance->SetVisibility(ESlateVisibility::Visible);
		PickerInstance->SetFocus();
	}

	PickerInstance->EditingSlotIndex = Index;
	PickerInstance->SetAbilitySystemForTooltip(ResolveAbilitySystem());
}

bool UW_ActionBar::ActivateSlotByIndex(int32 SlotIndex)

{

	if (!CachedPS)

	{

		AJ_LOG(this, TEXT("ActivateSlotByIndex() no CachedPS"));

		return false;
	}

	AJ_LOG(this, TEXT("ActivateSlotByIndex() request for index %d"), SlotIndex);

	const TArray<FAeyerjiAbilitySlot> Bar = CachedPS->GetActionBar();

	if (Bar.IsValidIndex(SlotIndex))

	{

		if (ExecuteAbilitySlot(Bar[SlotIndex]))

		{

			AJ_LOG(this, TEXT("ActivateSlotByIndex() succeeded via PlayerState for %d"), SlotIndex);

			return true;
		}

		AJ_LOG(this, TEXT("ActivateSlotByIndex() PlayerState data failed for index %d"), SlotIndex);
	}

	AJ_LOG(this, TEXT("ActivateSlotByIndex() unable to resolve slot %d"), SlotIndex);

	return false;
}

UW_ActionSlotNative *UW_ActionBar::GetSlotWidget(int32 SlotIndex) const
{
	if (!SlotsBox)
	{
		AJ_LOG(this, TEXT("GetSlotWidget() SlotsBox == nullptr"));
		return nullptr;
	}

	if (SlotIndex < 0 || SlotIndex >= SlotsBox->GetChildrenCount())
	{
		AJ_LOG(this, TEXT("GetSlotWidget() index %d out of range"), SlotIndex);
		return nullptr;
	}

	if (UW_ActionSlotNative *SlotWidget = Cast<UW_ActionSlotNative>(SlotsBox->GetChildAt(SlotIndex)))
	{
		return SlotWidget;
	}

	AJ_LOG(this, TEXT("GetSlotWidget() child %d is not UW_ActionSlotNative"), SlotIndex);
	return nullptr;
}

void UW_ActionBar::ShowAbilityTooltip(const FAeyerjiAbilitySlot& SlotData, FVector2D ScreenPosition, UWidget* SourceWidget)
{
	if (SlotData.Tag.IsEmpty())
	{
		return;
	}

	LastAbilityTooltipData = FAeyerjiAbilityTooltipData::FromSlot(
		ResolveAbilitySystem(),
		SlotData,
		EAbilityTooltipSource::ActionBar);

	SetActiveAbilityTooltipSource(SourceWidget);
	BP_ShowAbilityTooltip(LastAbilityTooltipData, ScreenPosition, SourceWidget);
}

void UW_ActionBar::HideAbilityTooltip(UWidget* SourceWidget)
{
	if (ActiveAbilityTooltipSource.IsValid() && SourceWidget && ActiveAbilityTooltipSource.Get() != SourceWidget)
	{
		return;
	}

	BP_HideAbilityTooltip(LastAbilityTooltipData, SourceWidget);
	ActiveAbilityTooltipSource.Reset();
	LastAbilityTooltipData = FAeyerjiAbilityTooltipData();
}

void UW_ActionBar::SetActiveAbilityTooltipSource(UWidget* SourceWidget)
{
	if (!SourceWidget)
	{
		ActiveAbilityTooltipSource.Reset();
		return;
	}

	ActiveAbilityTooltipSource = SourceWidget;
}

void UW_ActionBar::HandleSlotLeftClicked(UW_ActionSlotNative *MySlot)

{

	/* ---------- basic sanity ---------- */

	if (!MySlot)

	{

		AJ_LOG(this, TEXT("HandleSlotLeftClicked() invalid slot"));

		return;
	}

	if (CachedPS && MySlot->StoredSlotIndex != INDEX_NONE)

	{

		const TArray<FAeyerjiAbilitySlot> &Bar = CachedPS->GetActionBar();

		if (Bar.IsValidIndex(MySlot->StoredSlotIndex))

		{

			AJ_LOG(this, TEXT("HandleSlotLeftClicked() attempting PlayerState index %d"), MySlot->StoredSlotIndex);

			const bool bExecutedFromPlayerState = ExecuteAbilitySlot(Bar[MySlot->StoredSlotIndex]);

			if (bExecutedFromPlayerState)

			{

				AJ_LOG(this, TEXT("HandleSlotLeftClicked() succeeded via PlayerState for %d"), MySlot->StoredSlotIndex);

				return;
			}

			AJ_LOG(this, TEXT("HandleSlotLeftClicked() PlayerState data consumed but did not activate for index %d"), MySlot->StoredSlotIndex);
			return;
		}
	}

	AJ_LOG(this, TEXT("HandleSlotLeftClicked() no valid PlayerState slot (CachedPS=%s Index=%d)"),
		*GetNameSafe(CachedPS),
		MySlot->StoredSlotIndex);
}

bool UW_ActionBar::ExecuteAbilitySlot(const FAeyerjiAbilitySlot &SlotData)
{
	if (!CachedPS)
	{
		AJ_LOG(this, TEXT("ExecuteAbilitySlot() invalid PS"));
		return false;
	}

	/* ---------- pause gate (time-dilation 1.0 == NOT paused) ---------- */
	if (!FMath::IsNearlyEqual(UGameplayStatics::GetGlobalTimeDilation(GetWorld()), 1.f))
	{
		ShowActionBarDebugMessage(this, TEXT("Game is Paused"));
		return false; // abort - don't try to cast while paused
	}

	FAeyerjiAbilitySlot EffectiveSlotData = SlotData;
	if (!EffectiveSlotData.Tag.IsEmpty())
	{
		FGameplayTagContainer NormalizedTags;
		for (const FGameplayTag& Tag : EffectiveSlotData.Tag)
		{
			const FGameplayTag NormalizedTag = UAeyerjiAbilityTuningSubsystem::NormalizeAbilityTag(Tag);
			if (NormalizedTag.IsValid())
			{
				NormalizedTags.AddTag(NormalizedTag);
			}
		}
		EffectiveSlotData.Tag = NormalizedTags;
	}

	if (!EffectiveSlotData.Tag.IsValid() && !EffectiveSlotData.Class)
	{
		AJ_LOG(this, TEXT("ExecuteAbilitySlot() slot has no tag or class"));
		ShowActionBarDebugMessage(this, TEXT("No ability in this slot"));
		return false;
	}

	const FString TagString = EffectiveSlotData.Tag.ToString();
	const int32 TargetModeValue = static_cast<int32>(EffectiveSlotData.TargetMode);
	AJ_LOG(this, TEXT("ExecuteAbilitySlot() Tag=%s TargetMode=%d Level=%d"), *TagString, TargetModeValue, EffectiveSlotData.Level);

	/* ---------- find the owner's ASC ---------- */
	UAbilitySystemComponent *ASC = nullptr;
	if (APawn *Pawn = CachedPS->GetPawn())
	{
		if (IAbilitySystemInterface *ASI = Cast<IAbilitySystemInterface>(Pawn))
		{
			ASC = ASI->GetAbilitySystemComponent();
		}
	}

	if (!ASC)
	{
		AJ_LOG(this, TEXT("ExecuteAbilitySlot() ASC not found"));
		ShowActionBarDebugMessage(this, TEXT("Ability system not ready"));
		return false;
	}

	bool bBlockedByCooldown = false;
	bool bFoundMatchingAbility = false;
	const FGameplayAbilityActorInfo* ActorInfo = ASC->AbilityActorInfo.Get();
	if (ActorInfo)
	{
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			const UGameplayAbility* AbilityCDO = Spec.Ability;
			if (!AbilityCDO)
			{
				continue;
			}

			const bool bMatchesClass = EffectiveSlotData.Class && AbilityCDO->GetClass() == EffectiveSlotData.Class;
			const bool bMatchesTags = EffectiveSlotData.Tag.IsValid() && AbilityCDO->GetAssetTags().HasAny(EffectiveSlotData.Tag);
			if (!bMatchesClass && !bMatchesTags)
			{
				continue;
			}

			bFoundMatchingAbility = true;
			bBlockedByCooldown = !AbilityCDO->CheckCooldown(Spec.Handle, ActorInfo, nullptr);
			break;
		}
	}

	if (bBlockedByCooldown)
	{
		AJ_LOG(this, TEXT("ExecuteAbilitySlot() blocked by cooldown (Tag=%s Class=%s)"),
			*TagString,
			*GetNameSafe(EffectiveSlotData.Class));
		ShowActionBarDebugMessage(this, TEXT("Ability is on cooldown"));
		return false;
	}

	if (EffectiveSlotData.Tag.IsValid())
	{
		int32 MatchingSpecs = 0;
		for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
		{
			const UGameplayAbility* AbilityCDO = Spec.Ability;
			if (!AbilityCDO)
			{
				continue;
			}

			const FGameplayTagContainer& AbilityAssetTags = AbilityCDO->GetAssetTags();
			if (!AbilityAssetTags.HasAny(EffectiveSlotData.Tag))
			{
				continue;
			}

			++MatchingSpecs;
			FGameplayTagContainer FailureTags;
			const bool bCanActivate = ActorInfo
				? AbilityCDO->CanActivateAbility(Spec.Handle, ActorInfo, nullptr, nullptr, &FailureTags)
				: false;
			AJ_LOG(this, TEXT("ExecuteAbilitySlot() Tag match %s CanActivate=%s FailTags=%s"),
				*GetNameSafe(AbilityCDO),
				bCanActivate ? TEXT("true") : TEXT("false"),
				*FailureTags.ToString());

			if (!bCanActivate)
			{
				FGameplayTagContainer OwnedTags;
				ASC->GetOwnedGameplayTags(OwnedTags);

				const bool bCooldownOk = ActorInfo ? AbilityCDO->CheckCooldown(Spec.Handle, ActorInfo, nullptr) : false;
				const bool bCostOk = ActorInfo ? AbilityCDO->CheckCost(Spec.Handle, ActorInfo, nullptr) : false;
			}
		}

		if (MatchingSpecs == 0)
		{
			AJ_LOG(this, TEXT("ExecuteAbilitySlot() no owned abilities match tag %s"), *TagString);
			if (!bFoundMatchingAbility)
			{
				ShowActionBarDebugMessage(this, TEXT("Ability not learned"));
			}
		}
	}

	auto IsAbilityOnCooldown = [](UAbilitySystemComponent* InASC, TSubclassOf<UGameplayAbility> AbilityClass) -> bool
	{
		if (!InASC || !AbilityClass)
		{
			return false;
		}

		if (FGameplayAbilitySpec* Spec = InASC->FindAbilitySpecFromClass(AbilityClass))
		{
			if (const UGameplayAbility* AbilityCDO = Spec->Ability)
			{
				if (const FGameplayAbilityActorInfo* ActorInfo = InASC->AbilityActorInfo.Get())
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

	/* ---------- Switch on targeting mode (Ground target etc.) ---------- */
	switch (EffectiveSlotData.TargetMode)
	{
	case EAeyerjiTargetMode::Instant:
	{
		/* ---------- activate by tag ---------- */
		if (AAeyerjiPlayerController* PC = GetOwningPlayer<AAeyerjiPlayerController>())
		{
			if (UWorld* World = GetWorld())
			{
				const ENetMode NetMode = World->GetNetMode();
				if (NetMode == NM_Client)
				{
					DrawAbilityDebugShape(EffectiveSlotData);
					PC->Server_ActivateAbilityInstant(EffectiveSlotData);
					return true;
				}
			}
		}

		bool bActivated = false;

		if (EffectiveSlotData.Tag.IsValid())
		{
			bActivated = ASC->TryActivateAbilitiesByTag(EffectiveSlotData.Tag, /*bAllowRemoteActivation=*/true);
		}

		if (!bActivated && EffectiveSlotData.Class)
		{
			bActivated = ASC->TryActivateAbilityByClass(EffectiveSlotData.Class, /*bAllowRemoteActivation=*/true);
			AJ_LOG(this, TEXT("ExecuteAbilitySlot() TryActivateAbilityByClass %s (Class=%s)"),
				bActivated ? TEXT("succeeded") : TEXT("failed"),
				*GetNameSafe(EffectiveSlotData.Class));
		}

		if (bActivated)
		{
			DrawAbilityDebugShape(EffectiveSlotData);
		}

		CooldownTickAccumulator = CooldownTickInterval;
		UpdateCooldowns();
		AJ_LOG(this, TEXT("ExecuteAbilitySlot() TryActivateAbilitiesByTag %s (Tag=%s)"), bActivated ? TEXT("succeeded") : TEXT("failed"), *TagString);
		return bActivated;
	}

	case EAeyerjiTargetMode::GroundLocation:
	case EAeyerjiTargetMode::EnemyActor:
	case EAeyerjiTargetMode::FriendlyActor:
	{
		if (IsAbilityOnCooldown(ASC, EffectiveSlotData.Class))
		{
			AJ_LOG(this, TEXT("ExecuteAbilitySlot() %s on cooldown, blocking targeting"),
				*GetNameSafe(EffectiveSlotData.Class));
			ShowActionBarDebugMessage(this, TEXT("Ability is on cooldown"));
			return false;
		}

		if (auto *PC = GetOwningPlayer<AAeyerjiPlayerController>())
		{
			AJ_LOG(this, TEXT("ExecuteAbilitySlot() routing to targeting flow (Tag=%s Mode=%d)"), *TagString, TargetModeValue);
			DrawAbilityDebugShape(EffectiveSlotData);
			PC->BeginAbilityTargeting(EffectiveSlotData);
			return true;
		}

		AJ_LOG(this, TEXT("ExecuteAbilitySlot() PlayerController missing for targeting"));
		ShowActionBarDebugMessage(this, TEXT("Player controller missing"));
		return false;
	}

	default:
		AJ_LOG(this, TEXT("ExecuteAbilitySlot() unrecognised TargetMode!"));
		ShowActionBarDebugMessage(this, TEXT("Unsupported ability target mode"));
		return false;
	}
}

void UW_ActionBar::HandleAbilityPicked(int32 SlotIndex, FAeyerjiAbilitySlot Pick)
{
	AJ_LOG(this, "Ability picked for Slot %d (Icon=%s)", SlotIndex, *GetNameSafe(Pick.Icon));

	if (!Pick.Tag.IsEmpty())
	{
		FGameplayTagContainer NormalizedTags;
		for (const FGameplayTag& Tag : Pick.Tag)
		{
			const FGameplayTag NormalizedTag = UAeyerjiAbilityTuningSubsystem::NormalizeAbilityTag(Tag);
			if (NormalizedTag.IsValid())
			{
				NormalizedTags.AddTag(NormalizedTag);
			}
		}
		Pick.Tag = NormalizedTags;
	}

	if (APlayerController *PC = GetOwningPlayer())
	{
		if (AAeyerjiPlayerState *PS = PC->GetPlayerState<AAeyerjiPlayerState>())
		{
			if (PS->GetActionBar().IsValidIndex(SlotIndex))
			{
				Pick.CaptureStableReferences();
				Pick.ResolveSavedReferences();

				// Send only the edited slot so a stale client-side replicated bar cannot wipe other slots.
				PS->Server_SetActionBarSlot(SlotIndex, Pick);

				CooldownTickAccumulator = CooldownTickInterval;
				UpdateCooldowns();
			}
		}
	}
}



void UW_ActionBar::UpdateCooldowns()
{
	if (!SlotsBox)
	{
		AJ_LOG(this, TEXT("UpdateCooldowns aborted - SlotsBox null"));
		return;
	}

	UAbilitySystemComponent* AbilitySystem = ResolveAbilitySystem();
	//AJ_LOG(this, TEXT("UpdateCooldowns ASC=%s"), *GetNameSafe(AbilitySystem));
	const int32 ChildCount = SlotsBox->GetChildrenCount();

	for (int32 Idx = 0; Idx < ChildCount; ++Idx)
	{
		if (UW_ActionSlotNative* SlotWidget = Cast<UW_ActionSlotNative>(SlotsBox->GetChildAt(Idx)))
		{
			if (!AbilitySystem)
			{
				AJ_LOG(this, TEXT("UpdateCooldowns slot %d no ASC - clearing display"), Idx);
				SlotWidget->ClearCooldownDisplay();
				continue;
			}

			if (!TryUpdateSlotCooldown(*AbilitySystem, *SlotWidget))
			{
				//AJ_LOG(this, TEXT("UpdateCooldowns slot %d failed to resolve cooldown - clearing display"), Idx);
				SlotWidget->ClearCooldownDisplay();
			}
			// else
			// {
			// 	AJ_LOG(this, TEXT("UpdateCooldowns slot %d refreshed cooldown display (Remaining=%.2f Total=%.2f Percent=%.2f)"),
			// 		Idx,
			// 		SlotWidget->CooldownTimeRemaining,
			// 		SlotWidget->CooldownTotalTime,
			// 		SlotWidget->CooldownPercent);
			// }
		}
		else
		{
			AJ_LOG(this, TEXT("UpdateCooldowns child %d not UW_ActionSlotNative"), Idx);
		}
	}
}

bool UW_ActionBar::TryUpdateSlotCooldown(UAbilitySystemComponent& AbilitySystem, UW_ActionSlotNative& SlotWidget) const
{
	const FAeyerjiAbilitySlot& SlotData = SlotWidget.StoredSlotData;
	if (!SlotData.Class)
	{
		//AJ_LOG(this, TEXT("TryUpdateSlotCooldown SlotIndex=%d no ability class"), SlotWidget.StoredSlotIndex);
		return false;
	}

	float TimeRemaining = 0.f;
	float TotalDuration = 0.f;

	if (FGameplayAbilitySpec* Spec = AbilitySystem.FindAbilitySpecFromClass(SlotData.Class))
	{
		if (Spec->Ability)
		{
			if (const FGameplayAbilityActorInfo* ActorInfo = AbilitySystem.AbilityActorInfo.Get())
			{
				Spec->Ability->GetCooldownTimeRemainingAndDuration(Spec->Handle, ActorInfo, TimeRemaining, TotalDuration);
				// AJ_LOG(this, TEXT("TryUpdateSlotCooldown SlotIndex=%d Class=%s Remaining=%.2f Total=%.2f"),
				// 	SlotWidget.StoredSlotIndex,
				// 	*GetNameSafe(SlotData.Class),
				// 	TimeRemaining,
				// 	TotalDuration);
			}
			else
			{
				AJ_LOG(this, TEXT("TryUpdateSlotCooldown SlotIndex=%d missing ActorInfo for class %s"),
					SlotWidget.StoredSlotIndex,
					*GetNameSafe(SlotData.Class));
			}
		}
		else
		{
			AJ_LOG(this, TEXT("TryUpdateSlotCooldown SlotIndex=%d Spec ability null for class %s"),
				SlotWidget.StoredSlotIndex,
				*GetNameSafe(SlotData.Class));
		}
	}
	else
	{
		AJ_LOG(this, TEXT("TryUpdateSlotCooldown SlotIndex=%d no spec found for class %s"),
			SlotWidget.StoredSlotIndex,
			*GetNameSafe(SlotData.Class));
	}

	const bool bValidDuration = TotalDuration > KINDA_SMALL_NUMBER;
	const bool bValidRemaining = TimeRemaining > KINDA_SMALL_NUMBER;

	if (!bValidDuration || !bValidRemaining)
	{
		// AJ_LOG(this, TEXT("TryUpdateSlotCooldown SlotIndex=%d invalid cooldown data (Remaining=%.2f Total=%.2f)"),
		// 	SlotWidget.StoredSlotIndex,
		// 	TimeRemaining,
		// 	TotalDuration);
		return false;
	}

	SlotWidget.UpdateCooldownDisplay(TimeRemaining, TotalDuration);
	return true;
}

void UW_ActionBar::DrawAbilityDebugShape(const FAeyerjiAbilitySlot& SlotData) const
{
	UWorld* World = GetWorld();
	APawn* Pawn = CachedPS ? CachedPS->GetPawn() : nullptr;
	if (!World || !Pawn || SlotData.Tag.IsEmpty())
	{
		return;
	}

	const FAeyerjiAbilityTableRow* Row = nullptr;

	if (UGameInstance* GameInstance = GetGameInstance())
	{
		if (const UAeyerjiAbilityTuningSubsystem* Tuning = GameInstance->GetSubsystem<UAeyerjiAbilityTuningSubsystem>())
		{
			for (const FGameplayTag& Tag : SlotData.Tag)
			{
				const FGameplayTag NormalizedTag = UAeyerjiAbilityTuningSubsystem::NormalizeAbilityTag(Tag);
				if (NormalizedTag.IsValid())
				{
					Row = Tuning->FindAbilityRow(NormalizedTag);
					if (Row)
					{
						break;
					}
				}
			}
		}
	}

	if (!Row)
	{
		const UDataTable* Table = UAeyerjiAbilityTuningSubsystem::ResolveConfiguredTable();
		for (const FGameplayTag& Tag : SlotData.Tag)
		{
			const FGameplayTag NormalizedTag = UAeyerjiAbilityTuningSubsystem::NormalizeAbilityTag(Tag);
			if (NormalizedTag.IsValid())
			{
				Row = UAeyerjiAbilityTuningSubsystem::FindAbilityRowInTable(Table, NormalizedTag);
				if (Row)
				{
					break;
				}
			}
		}
	}

	if (!Row)
	{
		return;
	}

	const FVector Origin = Pawn->GetActorLocation();
	const FVector Forward = Pawn->GetActorForwardVector();
	constexpr float LifeTime = 1.5f;
	constexpr float Thickness = 2.f;

	switch (Row->Shape)
	{
	case EAeyerjiAbilityTargetShape::OwnerCone:
	{
		const float Range = FMath::Max3(Row->MaxRange, Row->Radius, Row->PreviewRange);
		if (Range > KINDA_SMALL_NUMBER)
		{
			const float HalfAngleRadians = FMath::DegreesToRadians(FMath::Clamp(Row->ArcAngleDegrees * 0.5f, 1.f, 179.f));
			DrawDebugCone(World, Origin, Forward, Range, HalfAngleRadians, HalfAngleRadians, 32, FColor::Orange, false, LifeTime, 0, Thickness);
		}
		break;
	}
	case EAeyerjiAbilityTargetShape::OwnerRadius:
	case EAeyerjiAbilityTargetShape::GroundRadius:
	{
		const float Radius = Row->Radius > KINDA_SMALL_NUMBER ? Row->Radius : Row->PreviewRange;
		if (Radius > KINDA_SMALL_NUMBER)
		{
			DrawDebugSphere(World, Origin, Radius, 48, FColor::Cyan, false, LifeTime, 0, Thickness);
		}
		break;
	}
	case EAeyerjiAbilityTargetShape::SingleActor:
	default:
	{
		const float Range = Row->MaxRange > KINDA_SMALL_NUMBER ? Row->MaxRange : Row->PreviewRange;
		const FVector End = Origin + Forward * FMath::Max(100.f, Range);
		DrawDebugLine(World, Origin, End, FColor::Yellow, false, LifeTime, 0, Thickness);
		DrawDebugSphere(World, End, 50.f, 16, FColor::Yellow, false, LifeTime, 0, Thickness);
		break;
	}
	}
}

UAbilitySystemComponent* UW_ActionBar::ResolveAbilitySystem()
{
	if (CachedAbilitySystem.IsValid())
	{
		return CachedAbilitySystem.Get();
	}

	APawn* PawnToQuery = nullptr;

	if (CachedPS)
	{
		PawnToQuery = CachedPS->GetPawn();
	}

	if (!PawnToQuery)
	{
		if (APlayerController* PC = GetOwningPlayer())
		{
			PawnToQuery = PC->GetPawn();
		}
	}

	if (PawnToQuery && (!CachedPawn.IsValid() || CachedPawn.Get() != PawnToQuery))
	{
		CachedPawn = PawnToQuery;
		CachedAbilitySystem.Reset();
	}

	if (PawnToQuery)
	{
		if (IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(PawnToQuery))
		{
			if (UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent())
			{
				CachedAbilitySystem = ASC;
				return ASC;
			}
		}
	}

	CachedAbilitySystem.Reset();
	return nullptr;
}

void UW_ActionBar::ResetCachedAbilitySystem()
{
	CachedAbilitySystem.Reset();
	CachedPawn.Reset();
}

bool UW_ActionBar::IsDefaultPotionSlotConfigured() const
{
	const bool bHasTag = !DefaultPotionSlot.Tag.IsEmpty();
	const bool bHasClass = DefaultPotionSlot.Class != nullptr || !DefaultPotionSlot.SavedAbilityClass.IsNull();

	if (!bHasTag && !bHasClass && !DefaultPotionSlot.Icon && DefaultPotionSlot.SavedIcon.IsNull())
	{
		return false;
	}

	if (!bHasTag)
	{
		AJ_LOG(this, TEXT("DefaultPotionSlot missing Tag; cannot auto-assign"));
	}

	if (!bHasClass)
	{
		AJ_LOG(this, TEXT("DefaultPotionSlot missing Class; cannot auto-assign"));
	}

	return bHasTag && bHasClass;
}

bool UW_ActionBar::IsAbilitySlotEmpty(const FAeyerjiAbilitySlot& SlotData) const
{
	return SlotData.Tag.IsEmpty() && SlotData.Class == nullptr && SlotData.SavedAbilityClass.IsNull();
}

UW_ActionSlotNative* UW_ActionBar::ResolvePotionSlotWidget()
{
	if (CachedPotionSlot.IsValid())
	{
		return CachedPotionSlot.Get();
	}

	if (PotionSlotWidgetName.IsNone())
	{
		return nullptr;
	}

	if (UWidget* FoundWidget = GetWidgetFromName(PotionSlotWidgetName))
	{
		if (UW_ActionSlotNative* SlotWidget = Cast<UW_ActionSlotNative>(FoundWidget))
		{
			CachedPotionSlot = SlotWidget;
			return SlotWidget;
		}

		AJ_LOG(this, TEXT("Potion slot widget %s is not a UW_ActionSlotNative"), *PotionSlotWidgetName.ToString());
	}

	return nullptr;
}

int32 UW_ActionBar::ResolvePotionSlotIndex()
{
	if (!SlotsBox)
	{
		return INDEX_NONE;
	}

	if (UW_ActionSlotNative* SlotWidget = ResolvePotionSlotWidget())
	{
		if (SlotWidget->StoredSlotIndex != INDEX_NONE)
		{
			return SlotWidget->StoredSlotIndex;
		}

		return SlotsBox->GetChildIndex(SlotWidget);
	}

	return INDEX_NONE;
}

void UW_ActionBar::EnsureDefaultPotionSlot(const TArray<FAeyerjiAbilitySlot>& NewBar)
{
	if (bApplyingDefaultPotionSlot || !bAutoAssignDefaultPotionSlot)
	{
		return;
	}

	if (!CachedPS)
	{
		return;
	}

	if (!CachedPS->IsProfileLoadApplied())
	{
		return;
	}

	if (!IsDefaultPotionSlotConfigured())
	{
		return;
	}

	const int32 PotionSlotIndex = ResolvePotionSlotIndex();
	if (PotionSlotIndex == INDEX_NONE)
	{
		return;
	}

	if (!NewBar.IsValidIndex(PotionSlotIndex))
	{
		AJ_LOG(this, TEXT("Potion slot index %d missing from action bar (size %d)"),
			PotionSlotIndex,
			NewBar.Num());
		return;
	}

	if (!IsAbilitySlotEmpty(NewBar[PotionSlotIndex]))
	{
		return;
	}

	bApplyingDefaultPotionSlot = true;
	FAeyerjiAbilitySlot NormalizedDefaultPotionSlot = DefaultPotionSlot;
	NormalizedDefaultPotionSlot.CaptureStableReferences();
	NormalizedDefaultPotionSlot.ResolveSavedReferences();
	CachedPS->Server_SetActionBarSlot(PotionSlotIndex, NormalizedDefaultPotionSlot);
	bApplyingDefaultPotionSlot = false;
}
