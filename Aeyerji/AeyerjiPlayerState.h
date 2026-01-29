// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "Abilities/AeyerjiAbilitySlot.h"
#include "AeyerjiPlayerState.generated.h"

class UGameplayAbility;
class UPlayerStatsTrackingComponent;

/**
 * 
 */
UCLASS()
class AEYERJI_API AAeyerjiPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	/** Lifetime loot stats holder for this player. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aeyerji|Stats")
	TObjectPtr<UPlayerStatsTrackingComponent> PlayerStatsTracking = nullptr;

	/** 7 slots (including potion slot) - replicated and saved. */
	UPROPERTY(ReplicatedUsing = OnRep_ActionBar, SaveGame, BlueprintReadWrite)
	TArray<FAeyerjiAbilitySlot> ActionBar;
	
	/** Called on every client *after* ActionBar is updated. */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
		FOnActionBarChanged, const TArray<FAeyerjiAbilitySlot>&, NewBar);

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(
		FOnActionBarSwapBlocked, FText, Reason, TSubclassOf<UGameplayAbility>, AbilityClass);

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
		FOnSaveSlotOverrideChanged, const FString&, NewSlot);

	AAeyerjiPlayerState();

	/** Blueprint-assignable notification. */
	UPROPERTY(EditAnywhere, Category = "Aeyerji|Events")
	FOnActionBarChanged OnActionBarChanged;

	UPROPERTY(BlueprintAssignable, Category = "Aeyerji|ActionBar")
	FOnActionBarSwapBlocked OnActionBarSwapBlocked;

	UPROPERTY(BlueprintAssignable, Category = "Aeyerji|SaveGame")
	FOnSaveSlotOverrideChanged OnSaveSlotOverrideChanged;

	/** Public read-only accessor for C++ callers. */
	const TArray<FAeyerjiAbilitySlot>& GetActionBar() const { return ActionBar; }

	/** Server-side helper that overwrites the bar and replicates it. */
	UFUNCTION(BlueprintCallable, Server, Reliable)
	void Server_SetActionBar(const TArray<FAeyerjiAbilitySlot>& NewBar);

	/** Client notify when a swap/removal was blocked by cooldown. */
	UFUNCTION(Client, Reliable)
	void Client_ActionBarSwapBlocked(const FText& Reason, TSubclassOf<UGameplayAbility> AbilityClass);
	
	/** Server-side: make sure the owning pawn's ASC actually owns this ability. */
	UFUNCTION(Server, Reliable)
	void Server_GrantAbilityFromSlot(const FAeyerjiAbilitySlot& Slot);

	/** Client-callable helper to request a specific save slot name (per player). */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|SaveGame")
	void RequestSetSaveSlotOverride(const FString& NewSlot);

	UFUNCTION(Server, Reliable)
	void Server_SetSaveSlotOverride(const FString& NewSlot);

	UFUNCTION(BlueprintPure, Category = "Aeyerji|SaveGame")
	const FString& GetSaveSlotOverride() const { return SaveSlotOverride; }

	/** Accessor for the loot stats component. */
	UFUNCTION(BlueprintPure, Category = "Aeyerji|Stats")
	UPlayerStatsTrackingComponent* GetPlayerStatsTrackingComponent() const { return PlayerStatsTracking; }

	/* ---------- Passives ---------- */
	/** Passive options the character can choose from (IDs used in save/replication). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Passives")
	TArray<FName> PassiveOptions;

	/** Currently selected passive ID (must be in PassiveOptions). */
	UPROPERTY(ReplicatedUsing=OnRep_SelectedPassive, SaveGame, BlueprintReadOnly, Category="Aeyerji|Passives")
	FName SelectedPassiveId;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPassiveChanged, FName, PassiveId);

	/** Blueprint-assignable notification when the selected passive changes. */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|Passives")
	FOnPassiveChanged OnPassiveChanged;

	UFUNCTION(Server, Reliable, BlueprintCallable, Category="Aeyerji|Passives")
	void Server_SelectPassive(FName PassiveId);

	UFUNCTION(BlueprintCallable, Category="Aeyerji|Passives")
	void SetPassiveLocal(FName PassiveId);

	UFUNCTION(BlueprintPure, Category="Aeyerji|Passives")
	FName GetSelectedPassiveId() const { return SelectedPassiveId; }

	/* ---------- Run Flow ---------- */
	/** Client-callable: requests the server to transition PreRun -> InRun and start the level run. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Run")
	void RequestStartRun();

	/** Server RPC for RequestStartRun(). */
	UFUNCTION(Server, Reliable)
	void Server_RequestStartRun();

	/** Client-callable: requests the server to transition to RunComplete and snapshot results (useful for a "Quit Run" button). */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Run")
	void RequestEndRun();

	/** Server RPC for RequestEndRun(). */
	UFUNCTION(Server, Reliable)
	void Server_RequestEndRun();

	/** Client-callable: requests the server to transition RunComplete -> ReturnToMenu. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Run")
	void RequestReturnToMenu();

	/** Server RPC for RequestReturnToMenu(). */
	UFUNCTION(Server, Reliable)
	void Server_RequestReturnToMenu();

protected:

	/* ---------- Replication ---------- */
	virtual void GetLifetimeReplicatedProps(
		TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Automatic callback generated by the ReplicatedUsing tag. */
	UFUNCTION()
	void OnRep_ActionBar();

	/* ---------- Saved & replicated data ---------- */

	UFUNCTION()
	void OnRep_SaveSlotOverride();

	UPROPERTY(ReplicatedUsing = OnRep_SaveSlotOverride)
	FString SaveSlotOverride;

	UFUNCTION()
	void OnRep_SelectedPassive();

private:
	void ApplySaveSlotOverride(const FString& NewSlot);
	void ApplySelectedPassive(FName PassiveId, bool bBroadcast);
};
