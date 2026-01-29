// PlayerParentNative.h
#pragma once

#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"
#include "AeyerjiCharacter.h"
#include "GUI/W_ActionBar.h"
#include "GameplayTagContainer.h"
#include "Items/ItemTypes.h"
#include "Items/ItemInstance.h"
#include "TimerManager.h"

#include "PlayerParentNative.generated.h"

class UAeyerjiInventoryComponent;
class UAeyerjiItemInstance;
class UAeyerjiLevelingComponent;
class UGameplayEffect;
class USkeletalMeshComponent;
class UWeaponEquipmentComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPlayerInventoryComponentReady, UAeyerjiInventoryComponent*, Inventory);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnPlayerEquippedItemChanged, EEquipmentSlot, Slot, int32, SlotIndex, UAeyerjiItemInstance*, Item);

/**
 * Native base pawn for any playable character (Blueprint children = players)
 */
UCLASS()
class AEYERJI_API APlayerParentNative : public AAeyerjiCharacter, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	APlayerParentNative();

	virtual void SetGenericTeamId(const FGenericTeamId& NewID) override { TeamId = NewID; }
	virtual void PostInitializeComponents() override;

	/** Called after possession or on client replication */
	void InitAbilityActorInfo();

	/** Action bar widget instance (one persistent instance per local player) */
	UPROPERTY(BlueprintReadOnly, Category = "AEYERJI|GUI", meta = (AllowPrivateAccess = "true"))
	UW_ActionBar* ActionBarWidget = nullptr;

	UFUNCTION(BlueprintPure, Category = "AEYERJI|GUI")
	UW_ActionBar* GetActionBarWidget() const { return ActionBarWidget; }

	UFUNCTION(BlueprintCallable, Category = "Action Bar")
	bool ActivateActionBarSlot(int32 SlotIndex);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "AEYERJI|GUI")
	TSubclassOf<UW_ActionBar> ActionBarClass;

	/** Client -> Server request to load the given SaveGame slot. */
	UFUNCTION(Server, Reliable)
	void Server_RequestLoadCharacter();

	/** Optional: server tells the owning client that loading finished. */
	UFUNCTION(Client, Reliable)
	void Client_OnSaveLoaded(bool bSuccess);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aeyerji|Inventory", meta = (AllowPrivateAccess = "true"), BlueprintGetter = GetInventoryComponent)
	TObjectPtr<UAeyerjiInventoryComponent> InventoryComponent = nullptr;

	/** Ensures a replicated inventory component exists and is ready for use. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Inventory")
	UAeyerjiInventoryComponent* EnsureInventoryComponent();

	UFUNCTION(BlueprintPure, Category = "Aeyerji|Inventory")
	UAeyerjiInventoryComponent* GetInventoryComponent() const;

	UPROPERTY(BlueprintAssignable, Category = "Aeyerji|Inventory")
	FOnPlayerInventoryComponentReady OnInventoryComponentReady;

	UPROPERTY(BlueprintAssignable, Category = "Aeyerji|Inventory")
	FOnPlayerEquippedItemChanged OnInventoryEquippedItemChanged;

	UFUNCTION(BlueprintImplementableEvent, Category = "Aeyerji|Inventory")
	void BP_OnInventoryComponentReady(UAeyerjiInventoryComponent* ReadyInventory);

	UFUNCTION(BlueprintImplementableEvent, Category = "Aeyerji|Inventory")
	void BP_OnInventoryEquippedItemChanged(EEquipmentSlot Slot, int32 SlotIndex, UAeyerjiItemInstance* Item);

	/* ---------------- Leveling (BP child component access) ---------------- */
	UFUNCTION(BlueprintPure, Category = "Aeyerji|Leveling")
	UAeyerjiLevelingComponent* GetLevelingComponent() const;

protected:
	/** Cached pointer to the child BP's leveling component */
	UPROPERTY(Transient)
	TObjectPtr<UAeyerjiLevelingComponent> CachedLevelingComponent = nullptr;

	/** Optional: default level for newly created save slots (>=1). */
	UPROPERTY(EditAnywhere, Category = "Aeyerji|Leveling")
	int32 StartLevelOnBeginPlay = 1;

	/** Optional: if set, inject this infinite GE into leveling on startup. */
	UPROPERTY(EditDefaultsOnly, Category = "Aeyerji|Leveling")
	TSubclassOf<UGameplayEffect> GE_PrimaryAttributes_Infinite;

	/* === AActor / ACharacter overrides === */
	virtual void BeginPlay() override;

	UFUNCTION()
	void TryLoadingSave();

	virtual void PossessedBy(AController* NewController) override;
	virtual void OnRep_PlayerState() override;
	virtual void PawnClientRestart() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aeyerji|Meshes", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* RHandMeshComp = nullptr;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aeyerji|Equipment", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UWeaponEquipmentComponent> WeaponEquipmentComponent = nullptr;

	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnDeath_Implementation();

	/* Optional: expose current health % to BP/UMG */
	UFUNCTION(BlueprintPure, Category = "GAS")
	static float GetHealthPercent();

	UFUNCTION()
	void HandleASCReady();

	/* Keep track so we don't double-grant on seamless travel */
	bool bStartupGiven = false;

	UPROPERTY(EditDefaultsOnly, Category = "AI")
	uint8 TeamId = 0; // 0 = Players by convention

private:
	void QueueInitAbilityActorInfoRetry();
	void CancelInitAbilityActorInfoRetry();
	void RetryInitAbilityActorInfo();

	void HandleInventoryComponentResolved(UAeyerjiInventoryComponent* ResolvedComponent);
	UAeyerjiInventoryComponent* ResolveInventoryComponent();
	void BindInventoryDelegates();

	/** Name of the manually-added inventory component to bind (can be overridden in BP defaults). */
	UPROPERTY(EditDefaultsOnly, Category = "Aeyerji|Inventory", meta = (AllowPrivateAccess = "true"))
	FName InventoryComponentName = TEXT("AeyerjiInventory");

	UFUNCTION()
	void HandleInventoryEquippedItemChanged(EEquipmentSlot Slot, int32 SlotIndex, UAeyerjiItemInstance* Item);

	FTimerHandle ASCInitRetryHandle;
	bool bASCInitRetryQueued = false;
	bool bInventoryBindingsInitialized = false;
	TWeakObjectPtr<UAeyerjiInventoryComponent> LastBroadcastInventory;
	// Guards against repeated load RPCs and duplicate server loads.
	bool bSaveLoadRequested = false;
	bool bSaveLoaded = false;
};
