// AeyerjiLootPickup.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Items/ItemTypes.h"
#include "Components/OutlineHighlightComponent.h"
#include "GameplayTagContainer.h"
#include "GameplayEffectTypes.h"

#include "AeyerjiLootPickup.generated.h"

class UAeyerjiItemInstance;
class UItemDefinition;
class UAeyerjiInventoryComponent;
class USphereComponent;
class UWidgetComponent;
class UShapeComponent;
class UStaticMeshComponent;
class AAeyerjiPlayerController;
class UPrimitiveComponent;
class UMeshComponent;
class UNiagaraComponent;
class UNiagaraSystem;

/**
 * Authoritative loot pickup actor that works with the native inventory system.
 * Provides highlight/material hooks and Blueprint-friendly accessors for UI.
 */
UCLASS()
class AEYERJI_API AAeyerjiLootPickup : public AActor
{
	GENERATED_BODY()

public:
	AAeyerjiLootPickup();

	// --- Spawning --------------------------------------------------------

	/** Spawn a pickup from an existing item instance (SERVER only). */
	static AAeyerjiLootPickup* SpawnFromInstance(
		UWorld& World,
		UAeyerjiItemInstance* ItemInstance,
		const FTransform& SpawnTransform,
		TSubclassOf<AAeyerjiLootPickup> PickupClass = nullptr);

	/** Spawn a pickup by rolling an item from a definition (SERVER only). */
	static AAeyerjiLootPickup* SpawnFromDefinition(
		UWorld& World,
		UItemDefinition* Definition,
		int32 ItemLevel,
		EItemRarity Rarity,
		const FTransform& SpawnTransform,
		int32 SeedOverride = 0,
		TSubclassOf<AAeyerjiLootPickup> PickupClass = nullptr);

	// --- Interaction ----------------------------------------------------

	UFUNCTION(Server, Reliable)
	void Server_AddPickupIntent(AAeyerjiPlayerController* Controller);

	UFUNCTION(Server, Reliable)
	void Server_RemovePickupIntent(AAeyerjiPlayerController* Controller);

	void AddPickupIntent(AAeyerjiPlayerController* Controller);
	void RemovePickupIntent(AAeyerjiPlayerController* Controller);

	/** Attempt to pick up the loot (SERVER only). */
	void ExecutePickup(AAeyerjiPlayerController* Controller);

	void RequestPickupFromClient(AAeyerjiPlayerController* Controller);

	// --- UI Helpers -----------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "Loot")
	UAeyerjiItemInstance* GetItemInstance() const { return ItemInstance; }

	UFUNCTION(BlueprintPure, Category = "Loot")
	UItemDefinition* GetItemDefinition() const { return ItemDefinition; }

	UFUNCTION(BlueprintPure, Category = "Loot")
	FText GetDisplayName() const;

	UFUNCTION(BlueprintCallable, Category = "Loot|UI")
	void SetLabelVisible(bool bVisible);

	UFUNCTION(BlueprintCallable, Category = "Loot|UI")
	void SetHighlighted(bool bInHighlighted);

	UFUNCTION(BlueprintPure, Category = "Loot")
	FVector GetPickupNavCenter() const;

	UFUNCTION(BlueprintPure, Category = "Loot")
	float GetPickupNavRadius() const;

	bool IsHoverTargetComponent(const UPrimitiveComponent* Component) const;

	// --- AActor overrides -----------------------------------------------

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch, FReplicationFlags* RepFlags) override;
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void NotifyActorBeginCursorOver() override;
	virtual void NotifyActorEndCursorOver() override;

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot")
	TObjectPtr<USphereComponent> PickupSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot")
	TObjectPtr<UWidgetComponent> LootLabel;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot")
	TObjectPtr<UStaticMeshComponent> PreviewMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot|Highlight")
	TObjectPtr<UOutlineHighlightComponent> OutlineHighlight;

	/** Optional per-rarity stencil remap. Leave empty to use the component defaults. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Highlight")
	TMap<EItemRarity, uint8> RarityToStencilOverrides;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot|Visual")
	TObjectPtr<UNiagaraComponent> LootBeamFX;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Visual")
	TObjectPtr<UNiagaraSystem> LootBeamSystem;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Visual")
	TMap<EItemRarity, FLinearColor> RarityToBeamColor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Visual")
	FName BeamColorParameter = TEXT("BeamColor");

	/** When true the actor-level override below is used instead of the item definition visuals. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Visual")
	bool bUsePickupVisualOverride = false;

	/** Optional override when the item definition lacks pickup FX. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Visual")
	FAeyerjiPickupVisualConfig PickupVisualOverride;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	bool bAutoPickup = false;

	/** Optional gameplay event tag to broadcast when this pickup successfully grants its loot. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Events")
	FGameplayTag PickupGrantedEventTag;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot")
	float PickupRadius = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Highlight")
	TArray<TObjectPtr<UMeshComponent>> AdditionalHighlightMeshes;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|AutoPickup")
	float AutoPickupRadius = 140.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|AutoPickup")
	TObjectPtr<UShapeComponent> PickupVolumeOverride;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|AutoPickup")
	TObjectPtr<UShapeComponent> AutoPickupVolumeOverride;

	UPROPERTY(ReplicatedUsing = OnRep_ItemInstance, VisibleAnywhere, BlueprintReadOnly, Instanced, Category = "Loot")
	TObjectPtr<UAeyerjiItemInstance> ItemInstance;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Loot", meta = (AllowedClasses = "/Script/Aeyerji.ItemDefinition"))
	TObjectPtr<UItemDefinition> ItemDefinition;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Loot")
	int32 ItemLevel = 1;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, ReplicatedUsing = OnRep_ItemRarity, Category = "Loot")
	EItemRarity ItemRarity = EItemRarity::Common;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Replicated, Category = "Loot")
	int32 SeedOverride = 0;

	UPROPERTY()
	TSet<TWeakObjectPtr<AAeyerjiPlayerController>> PickupIntents;

	UPROPERTY()
	bool bHighlighted = false;

	UPROPERTY(Transient)
	TObjectPtr<UShapeComponent> ActivePickupVolume;

	UPROPERTY(Transient)
	TObjectPtr<UShapeComponent> ActiveAutoPickupVolume;

	UFUNCTION()
	void OnRep_ItemInstance();

	UFUNCTION()
	void HandlePickupSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	                               UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	                               bool bFromSweep, const FHitResult& SweepResult);

	void ApplyHighlight(bool bInHighlighted);
	void BuildHighlightMeshList(TArray<UMeshComponent*>& OutMeshes) const;
	void RefreshOutlineTargets();
	void ConfigureOutlineComponent();
	void RefreshRarityVisuals();
	FLinearColor ResolveBeamColor() const;
	bool IsOverlappingPawn(APawn* Pawn) const;
	bool CanPawnLoot(const AAeyerjiPlayerController* Controller) const;
	void ApplyDefinitionMesh();
	UAeyerjiItemInstance* GiveLootToInventory(AAeyerjiPlayerController* Controller, UAeyerjiItemInstance* GrantedItem);
	void BroadcastPickupEvent(AAeyerjiPlayerController* Controller, UAeyerjiItemInstance* GrantedItem);
	void TriggerPickupFX(AAeyerjiPlayerController* Controller, const FAeyerjiPickupVisualConfig& VisualConfig);
	FAeyerjiPickupVisualConfig ResolvePickupVisualConfig(const UAeyerjiItemInstance* InstanceOverride = nullptr) const;

	void ConfigureVolumes();
	void SetLabelFromItem();
	void DestroyIfEmpty();
	UFUNCTION()
	void OnRep_ItemRarity();

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayPickupFX(AActor* FXTarget, const FAeyerjiPickupVisualConfig& VisualConfig);

public:
	FORCEINLINE bool IsHighlighted() const { return bHighlighted; }
};
