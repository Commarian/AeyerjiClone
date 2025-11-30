#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Items/ItemTypes.h"

#include "AeyerjiPickupFXComponent.generated.h"

class USkeletalMeshComponent;
class UNiagaraSystem;
class UNiagaraComponent;
class UOutlineHighlightComponent;

USTRUCT()
struct AEYERJI_API FAeyerjiActiveFXEntry
{
	GENERATED_BODY()

	UPROPERTY()
	TWeakObjectPtr<UNiagaraComponent> Component;

	UPROPERTY()
	bool bIsEquipFX = false;

	UPROPERTY()
	EEquipmentSlot Slot = EEquipmentSlot::Offense;

	UPROPERTY()
	int32 SlotIndex = 0;
};

/**
 * Character-side cosmetic driver that plays pickup FX on the owning pawn's existing meshes.
 * This replaces per-item pickup meshes / actors spawned in the world.
 */
UCLASS(ClassGroup = (Aeyerji), meta = (BlueprintSpawnableComponent))
class AEYERJI_API UAeyerjiPickupFXComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAeyerjiPickupFXComponent();

	/** Plays the configured FX locally on the owning character (no gameplay impact). */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Pickup|FX")
	void PlayPickupFX(const FAeyerjiPickupVisualConfig& VisualConfig);

	/** Plays equip confirmation FX on the owning character (triggered when an item is equipped). */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Pickup|FX")
	void PlayEquipFX(const FAeyerjiPickupVisualConfig& VisualConfig, EEquipmentSlot Slot, int32 SlotIndex);

	/** Stops any active equip FX tied to the provided equipment slot/index. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Pickup|FX")
	void StopEquipFX(EEquipmentSlot Slot, int32 SlotIndex);

	/** Stops all currently playing equip FX regardless of slot. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Pickup|FX")
	void StopAllEquipFX();

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(Transient)
	TArray<FAeyerjiActiveFXEntry> ActiveFX;

	UPROPERTY(Transient)
	TWeakObjectPtr<USkeletalMeshComponent> CachedMesh;

	UPROPERTY(Transient)
	TWeakObjectPtr<UOutlineHighlightComponent> CachedOutline;

	void CacheOwnerComponents();
	USkeletalMeshComponent* ResolveAttachMesh() const;
	UNiagaraComponent* SpawnNiagara(UNiagaraSystem* System, const FAeyerjiPickupVisualConfig& VisualConfig, FName SocketOverride, const FVector& LocalOffset);
	void ApplyOutlinePulse(const FAeyerjiPickupVisualConfig& VisualConfig);
	void CleanupStaleFX();
	void RegisterActiveFX(UNiagaraComponent* Component, bool bIsEquipFX, EEquipmentSlot Slot = EEquipmentSlot::Offense, int32 SlotIndex = 0);
};
