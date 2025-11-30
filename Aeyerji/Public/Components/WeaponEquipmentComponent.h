#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Items/ItemTypes.h"

#include "WeaponEquipmentComponent.generated.h"

class AAeyerjiWeaponActor;
class UAeyerjiItemInstance;
class UItemDefinition;

/**
 * Handles equipping and unequipping of weapon visuals & animation overrides.
 * Listens to inventory events and swaps anim BP / movement settings accordingly.
 */
UCLASS(ClassGroup = (Aeyerji), meta = (BlueprintSpawnableComponent))
class AEYERJI_API UWeaponEquipmentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWeaponEquipmentComponent();

	virtual void BeginPlay() override;

	/** Applies the weapon configuration associated with the given item. */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void EquipFromItem(UAeyerjiItemInstance* Item);

	/** Reverts to the stored default state (unarmed). */
	UFUNCTION(BlueprintCallable, Category = "Weapon")
	void UnequipWeapon();

	/** Delegate hook to integrate with inventory components. */
	void HandleEquippedItemChanged(EEquipmentSlot Slot, int32 SlotIndex, UAeyerjiItemInstance* Item);

protected:
	UPROPERTY(VisibleAnywhere, Category = "Weapon")
	TObjectPtr<AAeyerjiWeaponActor> EquippedWeaponActor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	TSubclassOf<AAeyerjiWeaponActor> DefaultWeaponActorClass;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Weapon")
	FName DefaultWeaponSocket = FName(TEXT("WeaponRHandSocket"));

	UPROPERTY(Transient)
	TWeakObjectPtr<UAeyerjiItemInstance> CurrentItem;

	UPROPERTY(Transient)
	TWeakObjectPtr<class ACharacter> CachedCharacter;

	UPROPERTY(Transient)
	TWeakObjectPtr<class UCharacterMovementComponent> CachedMovement;

	UPROPERTY(Transient)
	TSubclassOf<class UAnimInstance> DefaultAnimClass;

	struct FMovementSnapshot
	{
		float MaxWalkSpeed = 0.f;
		bool bOrientRotationToMovement = false;
		bool bUseControllerDesiredRotation = false;

		void Capture(const class UCharacterMovementComponent* Movement);
	};

	FMovementSnapshot DefaultMovementState;

	void ApplyAnimClass(TSubclassOf<UAnimInstance> AnimClass);
	void ApplyMovementSettings(const FWeaponMovementSettings& Settings);
	void SpawnOrUpdateWeaponActor(const UItemDefinition* Definition, const FWeaponEquipmentConfig& Config, FName SocketName);
	void DestroyWeaponActor();
};
