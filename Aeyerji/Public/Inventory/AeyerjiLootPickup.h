// AeyerjiLootPickup.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Items/ItemTypes.h"
#include "Components/OutlineHighlightComponent.h"
#include "GameplayTagContainer.h"
#include "GameplayEffectTypes.h"
#include "Interaction/AeyerjiInteractable.h"

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
class UPhysicalMaterial;
class UAeyerjiLootTable;
class UTextRenderComponent;
class APlayerState;

/**
 * Authoritative loot pickup actor that works with the native inventory system.
 * Provides highlight/material hooks and Blueprint-friendly accessors for UI.
 */
UCLASS()
class AEYERJI_API AAeyerjiLootPickup : public AActor, public IAeyerjiInteractable
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

	/** Returns whether this pickup still contains an item for the supplied player. */
	virtual bool CanInteract_Implementation(AAeyerjiPlayerController* Controller) override;

	/** Returns the pickup volume center used for pathing and server validation. */
	virtual FVector GetInteractionLocation_Implementation() override;

	/** Returns the pickup volume radius used for pathing and server validation. */
	virtual float GetInteractionRadius_Implementation() override;

	/** Executes the server-authoritative inventory transfer. */
	virtual void Interact_Implementation(AAeyerjiPlayerController* Controller) override;

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

	/** Debug-only: recompute stat scaling using the current loot table (authority only). */
	int32 DebugRefreshItemScaling(const UAeyerjiLootTable& LootTable);

	UFUNCTION(BlueprintPure, Category = "Loot")
	FVector GetPickupNavCenter() const;

	UFUNCTION(BlueprintPure, Category = "Loot")
	float GetPickupNavRadius() const;

	bool IsHoverTargetComponent(const UPrimitiveComponent* Component) const;

	/** Reserves this pickup for one authoritative PlayerState; all other manual/auto pickup attempts are rejected. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Loot|Ownership")
	void ReserveForPlayerState(APlayerState* PlayerState);

	/**
	 * Enables or disables the existing authoritative auto-pickup flow for a runtime-spawned pickup.
	 * Existing overlap and inventory-transfer validation still decide whether the item can be granted.
	 */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Loot|AutoPickup")
	void ConfigureRuntimeAutoPickup(bool bEnableAutoPickup, float InAutoPickupRadius);

	UFUNCTION(BlueprintPure, Category="Loot|Ownership")
	APlayerState* GetReservedPlayerState() const { return ReservedPlayerState; }

	// --- Drop motion -------------------------------------------------------

	/** Start the fling+arc drop from the current actor location (SERVER only). */
	UFUNCTION(BlueprintCallable, Category = "Loot|Drop")
	void StartDropToGround();

	// --- AActor overrides -----------------------------------------------

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual bool ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch, FReplicationFlags* RepFlags) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
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

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot|UI")
	TObjectPtr<UTextRenderComponent> LootLabelText;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Loot|UI")
	TObjectPtr<UTextRenderComponent> LootLabelOutline;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|UI")
	bool bForceTextLabel = false;

	/** Height offset above the mesh top where the loot label is anchored (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|UI")
	float LabelHeightOffset = 32.f;

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

	// --- Drop motion config ---------------------------------------------

	/** Enable the fling/arc drop animation on spawn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop")
	bool bEnableDropMotion = true;

	/** If true, the drop starts automatically in BeginPlay on the server. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop", meta = (EditCondition = "bEnableDropMotion"))
	bool bAutoStartDrop = true;

	/** Enables detailed drop-motion logs for debugging physics/arc placement. Warnings still log when disabled. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop")
	bool bVerboseDropMotionLogging = false;

	/** How long the drop animation lasts, in seconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop", meta = (EditCondition = "bEnableDropMotion"))
	float DropDuration = 0.6f;

	/** Max additional height of the arc above the straight line to the ground. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop", meta = (EditCondition = "bEnableDropMotion"))
	float ArcHeight = 120.f;

	/** Sideways fling distance from the spawn point. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop", meta = (EditCondition = "bEnableDropMotion"))
	float SideImpulseDistance = 40.f;

	/** Upward offset from spawn before starting the fall. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop", meta = (EditCondition = "bEnableDropMotion"))
	float UpImpulseHeight = 80.f;

	/** Additional height offset applied to the beam base above the mesh top (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Visual")
	float BeamBaseZOffset = 2.f;

	/** Min random spin speed around yaw, in deg/sec. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop", meta = (EditCondition = "bEnableDropMotion"))
	float MinSpinYawSpeed = 360.f;

	/** Max random spin speed around yaw, in deg/sec. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop", meta = (EditCondition = "bEnableDropMotion"))
	float MaxSpinYawSpeed = 720.f;

	// --- Drop motion -> physics handoff ---------------------------------

	/** If true, hands off to physics simulation near the end of the arc drop (SERVER only). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop|Physics", meta = (EditCondition = "bEnableDropMotion"))
	bool bEnablePhysicsHandoff = true;

	/** When true, skip the arc/drop math and just let gravity/physics handle the fall (useful for simple pickups). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop|Physics")
	bool bUseSimplePhysicsDrop = false;

	/** At/after this normalized time, arc-drop stops and physics takes over. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop|Physics", meta = (EditCondition = "bEnableDropMotion && bEnablePhysicsHandoff", ClampMin = "0.0", ClampMax = "1.0"))
	float PhysicsHandoffAlpha = 0.85f;

	/** Random horizontal velocity kick applied when physics starts (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop|Physics", meta = (EditCondition = "bEnableDropMotion && bEnablePhysicsHandoff", ClampMin = "0.0"))
	float PhysicsLinearVelocityMin = 40.f;

	/** Random horizontal velocity kick applied when physics starts (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop|Physics", meta = (EditCondition = "bEnableDropMotion && bEnablePhysicsHandoff", ClampMin = "0.0"))
	float PhysicsLinearVelocityMax = 180.f;

	/** Downward velocity applied when physics starts (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop|Physics", meta = (EditCondition = "bEnableDropMotion && bEnablePhysicsHandoff", ClampMin = "0.0"))
	float PhysicsDownwardVelocityMin = 80.f;

	/** Downward velocity applied when physics starts (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop|Physics", meta = (EditCondition = "bEnableDropMotion && bEnablePhysicsHandoff", ClampMin = "0.0"))
	float PhysicsDownwardVelocityMax = 220.f;

	/** Random angular velocity applied when physics starts (deg/s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop|Physics", meta = (EditCondition = "bEnableDropMotion && bEnablePhysicsHandoff", ClampMin = "0.0"))
	float PhysicsAngularVelocityMin = 90.f;

	/** Random angular velocity applied when physics starts (deg/s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop|Physics", meta = (EditCondition = "bEnableDropMotion && bEnablePhysicsHandoff", ClampMin = "0.0"))
	float PhysicsAngularVelocityMax = 360.f;

	/** Linear damping applied while physics is simulating (higher = stops faster). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop|Physics", meta = (EditCondition = "bEnableDropMotion && bEnablePhysicsHandoff", ClampMin = "0.0"))
	float PhysicsLinearDamping = 0.2f;

	/** Angular damping applied while physics is simulating (higher = stops spinning faster). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop|Physics", meta = (EditCondition = "bEnableDropMotion && bEnablePhysicsHandoff", ClampMin = "0.0"))
	float PhysicsAngularDamping = 0.8f;

	/** Runtime physical material friction override used during physics handoff. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop|Physics", meta = (EditCondition = "bEnableDropMotion && bEnablePhysicsHandoff", ClampMin = "0.0"))
	float PhysicsFriction = 8.0f;

	/** Runtime physical material restitution (bounciness) override used during physics handoff. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop|Physics", meta = (EditCondition = "bEnableDropMotion && bEnablePhysicsHandoff", ClampMin = "0.0", ClampMax = "1.0"))
	float PhysicsRestitution = 0.0f;

	/** When true, physics drops are recovered if they fall too far, approach KillZ, or run too long without sleeping. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop|Safety", meta = (EditCondition = "bEnableDropMotion && bEnablePhysicsHandoff"))
	bool bEnableDropSafetyGuard = true;

	/** Max distance a physics drop or safety recovery ground may sit below its original drop anchor before being reset to the anchor. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop|Safety", meta = (EditCondition = "bEnableDropMotion && bEnablePhysicsHandoff && bEnableDropSafetyGuard", ClampMin = "0.0"))
	float DropSafetyMaxFallBelowAnchor = 180.f;

	/** Max distance safety recovery is allowed to snap downward from the saved anchor; farther hits are treated as under-map surfaces. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop|Safety", meta = (EditCondition = "bEnableDropMotion && bEnablePhysicsHandoff && bEnableDropSafetyGuard", ClampMin = "0.0"))
	float DropSafetyMaxRecoverySnapBelowAnchor = 180.f;

	/** Max seconds a physics drop may simulate before being forced to settle. Set to 0 to disable the timeout guard. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop|Safety", meta = (EditCondition = "bEnableDropMotion && bEnablePhysicsHandoff && bEnableDropSafetyGuard", ClampMin = "0.0"))
	float DropSafetyMaxPhysicsSeconds = 5.f;

	/** Height above the saved drop anchor used when searching for recovery ground. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop|Safety", meta = (EditCondition = "bEnableDropMotion && bEnablePhysicsHandoff && bEnableDropSafetyGuard", ClampMin = "0.0"))
	float DropSafetyGroundTraceStartHeight = 1000.f;

	/** Distance below the saved drop anchor used when searching for recovery ground. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop|Safety", meta = (EditCondition = "bEnableDropMotion && bEnablePhysicsHandoff && bEnableDropSafetyGuard", ClampMin = "0.0"))
	float DropSafetyGroundTraceDistance = 3000.f;

	/** Height above recovered ground where the pickup is placed after a safety reset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop|Safety", meta = (EditCondition = "bEnableDropMotion && bEnablePhysicsHandoff && bEnableDropSafetyGuard", ClampMin = "0.0"))
	float DropSafetyGroundOffset = 2.f;

	/** Extra KillZ buffer; crossing this server-side threshold triggers a safety reset before world cleanup can remove the pickup. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Loot|Drop|Safety", meta = (EditCondition = "bEnableDropMotion && bEnablePhysicsHandoff && bEnableDropSafetyGuard", ClampMin = "0.0"))
	float DropSafetyKillZBuffer = 250.f;

	UPROPERTY(Transient)
	TObjectPtr<UPhysicalMaterial> RuntimePhysicsMaterial;

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

	/** Explicit recipient for per-player rewards. Null preserves legacy public-loot behavior. */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category="Loot|Ownership")
	TObjectPtr<APlayerState> ReservedPlayerState = nullptr;

	UPROPERTY()
	bool bHighlighted = false;

	bool bForceLabelVisible = false;

	UPROPERTY(Transient)
	TObjectPtr<UShapeComponent> ActivePickupVolume;

	UPROPERTY(Transient)
	TObjectPtr<UShapeComponent> ActiveAutoPickupVolume;

	/** Runtime volume currently bound to the authoritative auto-pickup overlap handler. */
	UPROPERTY(Transient)
	TObjectPtr<UShapeComponent> BoundAutoPickupVolume;

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
	/** Transfers this item to the player's inventory after authoritative eligibility checks. */
	void ExecutePickup(AAeyerjiPlayerController* Controller);
	void ApplyDefinitionMesh();
	UAeyerjiItemInstance* GiveLootToInventory(AAeyerjiPlayerController* Controller, UAeyerjiItemInstance* GrantedItem);
	void BroadcastPickupEvent(AAeyerjiPlayerController* Controller, UAeyerjiItemInstance* GrantedItem);
	void TriggerPickupFX(AAeyerjiPlayerController* Controller, const FAeyerjiPickupVisualConfig& VisualConfig);
	FAeyerjiPickupVisualConfig ResolvePickupVisualConfig(const UAeyerjiItemInstance* InstanceOverride = nullptr) const;

	void ConfigureVolumes();
	void SanitizeRuntimeSettings();
	void SetLabelFromItem();
	void DestroyIfEmpty();
	UFUNCTION()
	void OnRep_ItemRarity();

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayPickupFX(AActor* FXTarget, const FAeyerjiPickupVisualConfig& VisualConfig);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_HidePickupAfterGranted();

private:
	// --- Drop motion runtime state --------------------------------------

	bool bIsDropping = false;
	float ElapsedDropTime = 0.f;
	float RandomSpinYawSpeed = 0.f;
	FVector DropStart = FVector::ZeroVector;
	FVector DropEnd = FVector::ZeroVector;
	bool bLoggedDropSkip = false;
	bool bLoggedFirstTick = false;
	bool bLoggedMidTick = false;
	bool bPhysicsHandoffStarted = false;
	bool bPhysicsHandoffAttempted = false;
	bool bHasDropSafetyAnchor = false;
	float PhysicsHandoffElapsedSeconds = 0.f;
	FVector DropSafetyAnchorLocation = FVector::ZeroVector;
	FTransform PrePhysicsMeshRelative = FTransform::Identity;
	FRotator PrePhysicsActorRotation = FRotator::ZeroRotator;

	void ComputeDropEndpoints();
	void FinalSnapToGround();
	bool StartPhysicsHandoff(bool bForceImmediate = false);
	bool TryFindDropSafetyGround(const FVector& DesiredLocation, FVector& OutGroundLocation) const;
	void FinishPhysicsDropAtLocation(const FVector& FinalLocation, const TCHAR* Reason, bool bWarn);
	bool TrySettlePhysicsDropAtCurrentLocation(const TCHAR* Reason, bool bWarn);
	void AbortPhysicsDropToSafeLocation(const TCHAR* Reason);
	void PrepareForPickupRemoval(const TCHAR* Reason);
	void UpdateLootBeamAnchor();
	void UpdateLabelVisibility();

public:
	FORCEINLINE bool IsHighlighted() const { return bHighlighted; }
};
