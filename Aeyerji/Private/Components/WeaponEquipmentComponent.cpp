#include "Components/WeaponEquipmentComponent.h"

#include "Combat/AeyerjiWeaponActor.h"
#include "Engine/World.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "GameFramework/Character.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "GameFramework/CharacterMovementComponent.h"
#include "Items/ItemDefinition.h"
#include "Items/ItemInstance.h"
#include "Components/SkeletalMeshComponent.h"

void UWeaponEquipmentComponent::FMovementSnapshot::Capture(const UCharacterMovementComponent* Movement)
{
	if (!Movement)
	{
		return;
	}

	MaxWalkSpeed = Movement->MaxWalkSpeed;
	bOrientRotationToMovement = Movement->bOrientRotationToMovement;
	bUseControllerDesiredRotation = Movement->bUseControllerDesiredRotation;
}

UWeaponEquipmentComponent::UWeaponEquipmentComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
	DefaultWeaponActorClass = AAeyerjiWeaponActor::StaticClass();
}

void UWeaponEquipmentComponent::BeginPlay()
{
	Super::BeginPlay();

	CachedCharacter = Cast<ACharacter>(GetOwner());
	if (ACharacter* Character = CachedCharacter.Get())
	{
		CachedMovement = Character->GetCharacterMovement();
		DefaultMovementState.Capture(CachedMovement.Get());

		if (USkeletalMeshComponent* Mesh = Character->GetMesh())
		{
			if (UAnimInstance* AnimInstance = Mesh->GetAnimInstance())
			{
				DefaultAnimClass = AnimInstance->GetClass();
			}
			else if (Mesh->AnimClass)
			{
				DefaultAnimClass = Mesh->AnimClass;
			}
		}
	}

	if (!DefaultWeaponActorClass)
	{
		DefaultWeaponActorClass = AAeyerjiWeaponActor::StaticClass();
	}
}

void UWeaponEquipmentComponent::EquipFromItem(UAeyerjiItemInstance* Item)
{
	CurrentItem = Item;

	const UItemDefinition* Definition = Item ? Item->Definition.Get() : nullptr;
	if (!Definition)
	{
		UnequipWeapon();
		return;
	}

	if (Definition->ItemCategory != EItemCategory::Assault)
	{
		UnequipWeapon();
		return;
	}

	ApplyAnimClass(DefaultAnimClass);
	DestroyWeaponActor();
}

void UWeaponEquipmentComponent::UnequipWeapon()
{
	CurrentItem = nullptr;
	ApplyAnimClass(DefaultAnimClass);

	if (CachedMovement.IsValid())
	{
		CachedMovement->MaxWalkSpeed = DefaultMovementState.MaxWalkSpeed;
		CachedMovement->bOrientRotationToMovement = DefaultMovementState.bOrientRotationToMovement;
		CachedMovement->bUseControllerDesiredRotation = DefaultMovementState.bUseControllerDesiredRotation;
	}

	DestroyWeaponActor();
}

void UWeaponEquipmentComponent::HandleEquippedItemChanged(EEquipmentSlot Slot, int32 /*SlotIndex*/, UAeyerjiItemInstance* Item)
{
	if (Slot != EEquipmentSlot::Assault)
	{
		return;
	}

	if (Item)
	{
		EquipFromItem(Item);
	}
	else
	{
		UnequipWeapon();
	}
}

void UWeaponEquipmentComponent::ApplyAnimClass(TSubclassOf<UAnimInstance> AnimClass)
{
	if (!CachedCharacter.IsValid())
	{
		return;
	}

	if (USkeletalMeshComponent* Mesh = CachedCharacter->GetMesh())
	{
		if (*AnimClass)
		{
			Mesh->SetAnimInstanceClass(AnimClass);
		}
		else if (*DefaultAnimClass)
		{
			Mesh->SetAnimInstanceClass(DefaultAnimClass);
		}
	}
}

void UWeaponEquipmentComponent::SpawnOrUpdateWeaponActor(const UItemDefinition* Definition, FName SocketName)
{
	if (!GetOwner() || !Definition)
	{
		return;
	}

	const bool bHasAuthority = GetOwner()->HasAuthority();
	if (bHasAuthority)
	{
		TSubclassOf<AAeyerjiWeaponActor> ClassToSpawn = DefaultWeaponActorClass;
		if (!*ClassToSpawn)
		{
			DestroyWeaponActor();
			return;
		}

		const bool bNeedsNewActor = !EquippedWeaponActor || EquippedWeaponActor->GetClass() != ClassToSpawn;
		if (bNeedsNewActor)
		{
			DestroyWeaponActor();

			FActorSpawnParameters SpawnParams;
			SpawnParams.Owner = GetOwner();
			SpawnParams.Instigator = Cast<APawn>(GetOwner());
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

			if (UWorld* World = GetWorld())
			{
				EquippedWeaponActor = World->SpawnActor<AAeyerjiWeaponActor>(ClassToSpawn, FTransform::Identity, SpawnParams);
			}
		}

		if (EquippedWeaponActor)
		{
			EquippedWeaponActor->InitializeFromDefinition(Definition);

			if (CachedCharacter.IsValid() && !SocketName.IsNone())
			{
				if (USkeletalMeshComponent* Mesh = CachedCharacter->GetMesh())
				{
					EquippedWeaponActor->AttachToComponent(Mesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale, SocketName);
				}
			}
		}
	}
}

void UWeaponEquipmentComponent::DestroyWeaponActor()
{
	if (EquippedWeaponActor && GetOwner() && GetOwner()->HasAuthority())
	{
		EquippedWeaponActor->Destroy();
	}

	EquippedWeaponActor = nullptr;
}
