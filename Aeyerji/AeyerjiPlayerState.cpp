#include "AeyerjiPlayerState.h"

#include "Logging/AeyerjiLog.h"
#include "Net/UnrealNetwork.h"
#include "Player/PlayerParentNative.h"
#include "CharacterStatsLibrary.h"

AAeyerjiPlayerState::AAeyerjiPlayerState()
{
	ActionBar.SetNum(6);
	bReplicates = true;
}

void AAeyerjiPlayerState::Server_SetActionBar_Implementation(
		const TArray<FAeyerjiAbilitySlot>& NewBar)
{
	AJ_LOG(this, TEXT("AAeyerjiPlayerState::Server_SetActionBar_Implementation"));

	TArray<FAeyerjiAbilitySlot> Sanitized = NewBar;

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

	// 2) Block changes for abilities currently on cooldown (cannot remove/swap while cooling).
	for (int32 Idx = 0; Idx < Sanitized.Num(); ++Idx)
	{
		const FAeyerjiAbilitySlot& OldSlot = ActionBar.IsValidIndex(Idx) ? ActionBar[Idx] : FAeyerjiAbilitySlot();
		FAeyerjiAbilitySlot& NewSlot = Sanitized[Idx];

		const bool bClassChanged = (OldSlot.Class != NewSlot.Class) || (OldSlot.Tag != NewSlot.Tag);
		if (bClassChanged && OldSlot.Class && IsAbilityOnCooldown(ASC, OldSlot.Class))
		{
			// Keep the old slot; reject the change while cooldown is active
			NewSlot = OldSlot;
			AJ_LOG(this, TEXT("Swap blocked for %s (cooldown)"), *GetNameSafe(OldSlot.Class));
			static const FText SwapBlockedReason = NSLOCTEXT(
				"AAeyerjiPlayerState",
				"SwapBlockedCooldown",
				"Cannot swap: ability is on cooldown.");
			OnActionBarSwapBlocked.Broadcast(SwapBlockedReason, OldSlot.Class);
			Client_ActionBarSwapBlocked(SwapBlockedReason, OldSlot.Class);
		}
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

	ActionBar = Sanitized;     // replicated property
	OnRep_ActionBar();         // run locally on the server for symmetry
}

void AAeyerjiPlayerState::Client_ActionBarSwapBlocked_Implementation(
		const FText& Reason, TSubclassOf<UGameplayAbility> AbilityClass)
{
	OnActionBarSwapBlocked.Broadcast(Reason, AbilityClass);
}

void AAeyerjiPlayerState::Server_GrantAbilityFromSlot_Implementation(
		const FAeyerjiAbilitySlot& AbilitySlot)
{
	// Valid pawn that implements IAbilitySystemInterface?
	APawn* Pawn = GetPawn();
	if (!Pawn)                           return;
	IAbilitySystemInterface* ASI = Cast<IAbilitySystemInterface>(Pawn);
	if (!ASI)                            return;

	UAbilitySystemComponent* ASC = ASI->GetAbilitySystemComponent();
	if (!ASC || !AbilitySlot.Class)      return;

	// Avoid duplicates – compare by class
	for (const FGameplayAbilitySpec& Spec : ASC->GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.Ability->GetClass() == AbilitySlot.Class)
			return;                      // already owned → done
	}

	// Grant at level 1 (you can pass Slot.Level later if needed)
	FGameplayAbilitySpec NewSpec(AbilitySlot.Class, 1, INDEX_NONE, this);
	ASC->GiveAbility(NewSpec);
	ASC->MarkAbilitySpecDirty(NewSpec);  // replicate to clients
}

void AAeyerjiPlayerState::RequestSetSaveSlotOverride(const FString& NewSlot)
{
	if (HasAuthority())
	{
		ApplySaveSlotOverride(NewSlot);
	}
	else
	{
		Server_SetSaveSlotOverride(NewSlot);
	}
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
	if (PassiveId.IsNone())
	{
		return;
	}

	if (PassiveOptions.Num() > 0 && !PassiveOptions.Contains(PassiveId))
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
	DOREPLIFETIME(AAeyerjiPlayerState, SelectedPassiveId);
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

void AAeyerjiPlayerState::OnRep_SelectedPassive()
{
	ApplySelectedPassive(SelectedPassiveId, /*bBroadcast*/true);
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
