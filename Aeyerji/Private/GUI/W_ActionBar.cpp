#include "GUI/W_ActionBar.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystemInterface.h"
#include "Abilities/GameplayAbility.h"
#include "GameplayAbilitySpec.h"
#include "Aeyerji/AeyerjiPlayerController.h"
#include "GUI/W_ActionSlotNative.h"
#include "GUI/W_AbilitySelectionNative.h"

#include "Components/HorizontalBox.h"
#include "Aeyerji/AeyerjiPlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Logging/AeyerjiLog.h"
#include "GameFramework/Pawn.h"

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
	CooldownTickAccumulator = CooldownTickInterval;
	UpdateCooldowns();
}

void UW_ActionBar::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	if (CooldownTickInterval <= 0.f)
	{
		UpdateCooldowns();
		return;
	}

	CooldownTickAccumulator += InDeltaTime;
	if (CooldownTickAccumulator >= CooldownTickInterval)
	{
		CooldownTickAccumulator = 0.f;
		UpdateCooldowns();
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

		AJ_LOG(this, TEXT("ActivateSlotByIndex() PlayerState data failed, falling back (index %d)"), SlotIndex);
	}

	if (UW_ActionSlotNative *SlotWidget = GetSlotWidget(SlotIndex))

	{

		const bool bResult = ExecuteAbilitySlot(SlotWidget->StoredSlotData);

		AJ_LOG(this, TEXT("ActivateSlotByIndex() widget fallback %s for %d"), bResult ? TEXT("succeeded") : TEXT("failed"), SlotIndex);

		return bResult;
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

void UW_ActionBar::HandleSlotLeftClicked(UW_ActionSlotNative *MySlot)

{

	/* ---------- basic sanity ---------- */

	if (!MySlot)

	{

		AJ_LOG(this, TEXT("HandleSlotLeftClicked() invalid slot"));

		return;
	}

	bool bExecutedFromPlayerState = false;

	if (CachedPS && MySlot->StoredSlotIndex != INDEX_NONE)

	{

		const TArray<FAeyerjiAbilitySlot> &Bar = CachedPS->GetActionBar();

		if (Bar.IsValidIndex(MySlot->StoredSlotIndex))

		{

			AJ_LOG(this, TEXT("HandleSlotLeftClicked() attempting PlayerState index %d"), MySlot->StoredSlotIndex);

			bExecutedFromPlayerState = ExecuteAbilitySlot(Bar[MySlot->StoredSlotIndex]);

			if (bExecutedFromPlayerState)

			{

				AJ_LOG(this, TEXT("HandleSlotLeftClicked() succeeded via PlayerState for %d"), MySlot->StoredSlotIndex);

				return;
			}

			AJ_LOG(this, TEXT("HandleSlotLeftClicked() fallback to widget data (index %d)"), MySlot->StoredSlotIndex);
		}
	}

	if (!bExecutedFromPlayerState)

	{

		if (!CachedPS)

		{

			AJ_LOG(this, TEXT("HandleSlotLeftClicked() CachedPS missing - using widget data"));
		}

		else if (MySlot->StoredSlotIndex == INDEX_NONE)

		{

			AJ_LOG(this, TEXT("HandleSlotLeftClicked() slot index not initialised - using widget data"));
		}

		else if (!CachedPS->GetActionBar().IsValidIndex(MySlot->StoredSlotIndex))

		{

			AJ_LOG(this, TEXT("HandleSlotLeftClicked() index %d not in PlayerState bar - using widget data"), MySlot->StoredSlotIndex);
		}

		const bool bWidgetResult = ExecuteAbilitySlot(MySlot->StoredSlotData);

		AJ_LOG(this, TEXT("HandleSlotLeftClicked() widget fallback %s"), bWidgetResult ? TEXT("succeeded") : TEXT("failed"));
	}
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
		if (GEngine && IsValid(GetWorld()))
		{
			GEngine->AddOnScreenDebugMessage(
				/*Key*/ -1, /*Time*/ 2.f, FColor::Red, TEXT("Game is Paused"));
		}
		return false; // abort - don't try to cast while paused
	}

	if (!SlotData.Tag.IsValid())
	{
		AJ_LOG(this, TEXT("ExecuteAbilitySlot() slot has no tag"));
		return false;
	}

	const FString TagString = SlotData.Tag.ToString();
	const int32 TargetModeValue = static_cast<int32>(SlotData.TargetMode);
	AJ_LOG(this, TEXT("ExecuteAbilitySlot() Tag=%s TargetMode=%d Level=%d"), *TagString, TargetModeValue, SlotData.Level);

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
		return false;
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
	switch (SlotData.TargetMode)
	{
	case EAeyerjiTargetMode::Instant:
	{
		/* ---------- activate by tag ---------- */
		const bool bActivated = ASC->TryActivateAbilitiesByTag(SlotData.Tag, /*bAllowRemoteActivation=*/false);
		CooldownTickAccumulator = CooldownTickInterval;
		UpdateCooldowns();
		AJ_LOG(this, TEXT("ExecuteAbilitySlot() TryActivateAbilitiesByTag %s (Tag=%s)"), bActivated ? TEXT("succeeded") : TEXT("failed"), *TagString);
		return bActivated;
	}

	case EAeyerjiTargetMode::GroundLocation:
	case EAeyerjiTargetMode::EnemyActor:
	case EAeyerjiTargetMode::FriendlyActor:
	{
		if (IsAbilityOnCooldown(ASC, SlotData.Class))
		{
			AJ_LOG(this, TEXT("ExecuteAbilitySlot() %s on cooldown, blocking targeting"),
				*GetNameSafe(SlotData.Class));
			return false;
		}

		if (auto *PC = GetOwningPlayer<AAeyerjiPlayerController>())
		{
			AJ_LOG(this, TEXT("ExecuteAbilitySlot() routing to targeting flow (Tag=%s Mode=%d)"), *TagString, TargetModeValue);
			PC->BeginAbilityTargeting(SlotData);
			return true;
		}

		AJ_LOG(this, TEXT("ExecuteAbilitySlot() PlayerController missing for targeting"));
		return false;
	}

	default:
		AJ_LOG(this, TEXT("ExecuteAbilitySlot() unrecognised TargetMode!"));
		return false;
	}
}

void UW_ActionBar::HandleAbilityPicked(int32 SlotIndex, FAeyerjiAbilitySlot Pick)
{
	AJ_LOG(this, "Ability picked for Slot %d (Icon=%s)", SlotIndex, *GetNameSafe(Pick.Icon));

	if (APlayerController *PC = GetOwningPlayer())
	{
		if (AAeyerjiPlayerState *PS = PC->GetPlayerState<AAeyerjiPlayerState>())
		{
			if (TArray<FAeyerjiAbilitySlot> Bar = PS->GetActionBar(); Bar.IsValidIndex(SlotIndex))
			{
				Bar[SlotIndex] = Pick;
				PS->Server_SetActionBar(Bar); // RPC + replication

				// Ask the server to grant the underlying GA once
				CachedPS->Server_GrantAbilityFromSlot(Pick);

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









