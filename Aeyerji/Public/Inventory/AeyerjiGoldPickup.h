// Copyright (c) 2025 Aeyerji.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/AeyerjiInteractable.h"
#include "AeyerjiGoldPickup.generated.h"

class AAeyerjiPlayerController;
class APlayerState;
class UNiagaraComponent;
class UNiagaraSystem;
class USphereComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

/** Replicated gold pickup that grants profile-persistent currency to an eligible player. */
UCLASS(Blueprintable)
class AEYERJI_API AAeyerjiGoldPickup : public AActor, public IAeyerjiInteractable
{
	GENERATED_BODY()

public:
	AAeyerjiGoldPickup();

	/** Spawns a server-authoritative gold pickup with optional per-player eligibility. */
	static AAeyerjiGoldPickup* SpawnGold(
		UWorld& World,
		int64 Amount,
		const FTransform& SpawnTransform,
		TSubclassOf<AAeyerjiGoldPickup> PickupClass = nullptr,
		APlayerState* EligiblePlayer = nullptr);

	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/** Returns whether this pickup can currently grant gold to the supplied player. */
	virtual bool CanInteract_Implementation(AAeyerjiPlayerController* Controller) override;

	/** Returns the pickup sphere center used for click-to-move and server validation. */
	virtual FVector GetInteractionLocation_Implementation() override;

	/** Returns the server-side maximum interaction distance in centimeters. */
	virtual float GetInteractionRadius_Implementation() override;

	/** Grants gold after server-side range and eligibility validation. */
	virtual void Interact_Implementation(AAeyerjiPlayerController* Controller) override;

	/** Returns the replicated gold amount this pickup grants. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Gold")
	int64 GetGoldAmount() const { return GoldAmount; }

	/** Sets the authority-side gold amount before or shortly after spawning. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Gold")
	void SetGoldAmount(int64 NewAmount);

	/** Assigns a specific eligible player. Leave unset to allow any player to collect it. */
	UFUNCTION(BlueprintCallable, BlueprintAuthorityOnly, Category="Aeyerji|Gold")
	void SetEligiblePlayer(APlayerState* NewEligiblePlayer);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Gold")
	TObjectPtr<USceneComponent> Root;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Gold")
	TObjectPtr<USphereComponent> PickupSphere;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Gold|Visual")
	TObjectPtr<UStaticMeshComponent> PreviewMesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Gold|Visual")
	TObjectPtr<UTextRenderComponent> GoldLabelText;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Gold|Visual")
	TObjectPtr<UNiagaraComponent> GoldBeamFX;

	/** Optional persistent beam or sparkle effect shown while the pickup is available. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Gold|Visual")
	TObjectPtr<UNiagaraSystem> GoldBeamSystem;

	/** Optional burst effect played when the pickup successfully grants gold. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Gold|Visual")
	TObjectPtr<UNiagaraSystem> PickupFXSystem;

	/** When true, walking over the pickup grants it without clicking. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Gold")
	bool bAutoPickup = true;

	/** Radius used for click-to-move validation and overlap pickup. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Gold", meta=(ClampMin="1.0", Units="cm"))
	float PickupRadius = 120.f;

	/** Delay before destroy after a successful pickup so replicated FX can play. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Gold", meta=(ClampMin="0.0", Units="s"))
	float LifeSecondsAfterPickup = 0.25f;

	/** Profile-persistent gold amount granted by this pickup. */
	UPROPERTY(ReplicatedUsing=OnRep_GoldAmount, EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Gold", meta=(ClampMin="1"))
	int64 GoldAmount = 1;

	/** Optional per-player ownership. When set, only this PlayerState can collect the pickup. */
	UPROPERTY(Replicated, BlueprintReadOnly, Category="Aeyerji|Gold")
	TObjectPtr<APlayerState> EligiblePlayerState = nullptr;

	UFUNCTION()
	void OnRep_GoldAmount();

	UFUNCTION()
	void HandlePickupSphereBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayPickupEffects(AActor* PickupTarget, int64 PickedUpGold);

	UFUNCTION(NetMulticast, Reliable)
	void Multicast_HidePickupAfterGranted();

	/** Blueprint hook for updating labels/mesh/beam after the replicated amount changes. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|Gold", meta=(DisplayName="On Gold Amount Changed"))
	void BP_OnGoldAmountChanged(int64 NewAmount);

	/** Blueprint hook for pickup audio, numbers, and cosmetic bursts. */
	UFUNCTION(BlueprintImplementableEvent, Category="Aeyerji|Gold", meta=(DisplayName="On Gold Picked Up"))
	void BP_OnGoldPickedUp(AActor* PickupTarget, int64 PickedUpGold);

	void UpdateGoldLabel();
	bool IsControllerEligible(const AAeyerjiPlayerController* Controller) const;
	bool TryGrantToController(AAeyerjiPlayerController* Controller);
};
