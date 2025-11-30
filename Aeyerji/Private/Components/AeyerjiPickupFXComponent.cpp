#include "Components/AeyerjiPickupFXComponent.h"

#include "Components/OutlineHighlightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Logging/AeyerjiLog.h"

UAeyerjiPickupFXComponent::UAeyerjiPickupFXComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
}

void UAeyerjiPickupFXComponent::BeginPlay()
{
	Super::BeginPlay();
	CacheOwnerComponents();
}

void UAeyerjiPickupFXComponent::CacheOwnerComponents()
{
	if (ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner()))
	{
		CachedMesh = CharacterOwner->GetMesh();
	}

	if (!CachedMesh.IsValid() && GetOwner())
	{
		CachedMesh = GetOwner()->FindComponentByClass<USkeletalMeshComponent>();
	}

	if (GetOwner())
	{
		CachedOutline = GetOwner()->FindComponentByClass<UOutlineHighlightComponent>();
	}
}

USkeletalMeshComponent* UAeyerjiPickupFXComponent::ResolveAttachMesh() const
{
	if (CachedMesh.IsValid())
	{
		return CachedMesh.Get();
	}

	if (AActor* Owner = GetOwner())
	{
		if (USkeletalMeshComponent* MeshComp = Owner->FindComponentByClass<USkeletalMeshComponent>())
		{
			return MeshComp;
		}
	}

	return nullptr;
}

void UAeyerjiPickupFXComponent::CleanupStaleFX()
{
	for (int32 Index = ActiveFX.Num() - 1; Index >= 0; --Index)
	{
		if (!ActiveFX[Index].Component.IsValid())
		{
			ActiveFX.RemoveAtSwap(Index);
		}
	}
}

void UAeyerjiPickupFXComponent::PlayPickupFX(const FAeyerjiPickupVisualConfig& VisualConfig)
{
	if (!GetOwner() || !VisualConfig.HasPickupVisuals())
	{
		AJ_LOG(this, TEXT("PickupFXComponent skipped - Owner=%s PickupVisuals=%d"),
			*GetNameSafe(GetOwner()),
			VisualConfig.HasPickupVisuals() ? 1 : 0);
		return;
	}

	CleanupStaleFX();

	if (!CachedMesh.IsValid() || !CachedOutline.IsValid())
	{
		CacheOwnerComponents();
	}

	if (VisualConfig.PickupGrantedSystem)
	{
		if (UNiagaraComponent* Comp = SpawnNiagara(
			VisualConfig.PickupGrantedSystem,
			VisualConfig,
			VisualConfig.AttachSocket,
			VisualConfig.SpawnOffset))
		{
			RegisterActiveFX(Comp, false);
			AJ_LOG(this, TEXT("Spawned PickupGrantedSystem %s on %s (Socket=%s)"),
				*GetNameSafe(VisualConfig.PickupGrantedSystem),
				*GetNameSafe(Comp),
				*VisualConfig.AttachSocket.ToString());
		}
		else
		{
			AJ_LOG(this, TEXT("Failed to spawn PickupGrantedSystem %s"), *GetNameSafe(VisualConfig.PickupGrantedSystem));
		}
	}

	ApplyOutlinePulse(VisualConfig);
}

void UAeyerjiPickupFXComponent::PlayEquipFX(const FAeyerjiPickupVisualConfig& VisualConfig, EEquipmentSlot Slot, int32 SlotIndex)
{
	if (!GetOwner() || !VisualConfig.HasEquipVisuals())
	{
		AJ_LOG(this, TEXT("PickupFXComponent equip skipped - Owner=%s EquipVisuals=%d"),
			*GetNameSafe(GetOwner()),
			VisualConfig.HasEquipVisuals() ? 1 : 0);
		return;
	}

	const int32 SanitizedSlotIndex = FMath::Max(0, SlotIndex);

	CleanupStaleFX();
	StopEquipFX(Slot, SanitizedSlotIndex);

	if (!CachedMesh.IsValid() || !CachedOutline.IsValid())
	{
		CacheOwnerComponents();
	}

	const FName EquipSocket = VisualConfig.SecondaryAttachSocket.IsNone()
		? VisualConfig.AttachSocket
		: VisualConfig.SecondaryAttachSocket;

	if (VisualConfig.InventoryGrantedSystem)
	{
		if (UNiagaraComponent* Comp = SpawnNiagara(
			VisualConfig.InventoryGrantedSystem,
			VisualConfig,
			EquipSocket,
			VisualConfig.SpawnOffset))
		{
			RegisterActiveFX(Comp, true, Slot, SanitizedSlotIndex);
			AJ_LOG(this, TEXT("Spawned InventoryGrantedSystem %s (Socket=%s) for slot %d/%d"),
				*GetNameSafe(VisualConfig.InventoryGrantedSystem),
				*EquipSocket.ToString(),
				static_cast<int32>(Slot),
				SanitizedSlotIndex);
		}
		else
		{
			AJ_LOG(this, TEXT("Failed to spawn InventoryGrantedSystem %s"), *GetNameSafe(VisualConfig.InventoryGrantedSystem));
		}
	}

	ApplyOutlinePulse(VisualConfig);
}

void UAeyerjiPickupFXComponent::StopEquipFX(EEquipmentSlot Slot, int32 SlotIndex)
{
	const int32 SanitizedSlotIndex = FMath::Max(0, SlotIndex);

	for (int32 Index = ActiveFX.Num() - 1; Index >= 0; --Index)
	{
		FAeyerjiActiveFXEntry& Entry = ActiveFX[Index];
		if (!Entry.Component.IsValid())
		{
			ActiveFX.RemoveAtSwap(Index);
			continue;
		}

		if (!Entry.bIsEquipFX || Entry.Slot != Slot || Entry.SlotIndex != SanitizedSlotIndex)
		{
			continue;
		}

		if (UNiagaraComponent* Comp = Entry.Component.Get())
		{
			Comp->Deactivate();
			Comp->DestroyComponent();
		}

		ActiveFX.RemoveAtSwap(Index);
	}
}

void UAeyerjiPickupFXComponent::StopAllEquipFX()
{
	for (int32 Index = ActiveFX.Num() - 1; Index >= 0; --Index)
	{
		FAeyerjiActiveFXEntry& Entry = ActiveFX[Index];
		if (!Entry.Component.IsValid())
		{
			ActiveFX.RemoveAtSwap(Index);
			continue;
		}

		if (!Entry.bIsEquipFX)
		{
			continue;
		}

		if (UNiagaraComponent* Comp = Entry.Component.Get())
		{
			Comp->Deactivate();
			Comp->DestroyComponent();
		}

		ActiveFX.RemoveAtSwap(Index);
	}
}

UNiagaraComponent* UAeyerjiPickupFXComponent::SpawnNiagara(
	UNiagaraSystem* System,
	const FAeyerjiPickupVisualConfig& VisualConfig,
	FName SocketOverride,
	const FVector& LocalOffset)
{
	if (!System)
	{
		return nullptr;
	}

	USkeletalMeshComponent* MeshToUse = ResolveAttachMesh();
	USceneComponent* AttachParent = MeshToUse ? static_cast<USceneComponent*>(MeshToUse) : GetOwner() ? GetOwner()->GetRootComponent() : nullptr;

	if (!AttachParent)
	{
		AJ_LOG(this, TEXT("SpawnNiagara failed - no attach parent on %s"), *GetNameSafe(GetOwner()));
		return nullptr;
	}

	const FName AttachSocket = SocketOverride;

	UNiagaraComponent* NiagaraComp = UNiagaraFunctionLibrary::SpawnSystemAttached(
		System,
		AttachParent,
		AttachSocket,
		LocalOffset,
		FRotator::ZeroRotator,
		EAttachLocation::KeepRelativeOffset,
		true);

	if (NiagaraComp)
	{
		NiagaraComp->SetAutoDestroy(true);

		if (!VisualConfig.ColorParameter.IsNone())
		{
			NiagaraComp->SetVariableLinearColor(VisualConfig.ColorParameter, VisualConfig.FXColor);
		}
	}

	return NiagaraComp;
}

void UAeyerjiPickupFXComponent::ApplyOutlinePulse(const FAeyerjiPickupVisualConfig& VisualConfig)
{
	if (!VisualConfig.bPulseOutline || VisualConfig.OutlinePulseDuration <= 0.f)
	{
		return;
	}

	if (!CachedOutline.IsValid())
	{
		if (AActor* Owner = GetOwner())
		{
			CachedOutline = Owner->FindComponentByClass<UOutlineHighlightComponent>();
		}
	}

	if (CachedOutline.IsValid())
	{
		CachedOutline->PulseHighlight(
			VisualConfig.OutlinePulseDuration,
			VisualConfig.OutlinePulseFadeTime,
			VisualConfig.OutlineStencilOverride);
	}
}

void UAeyerjiPickupFXComponent::RegisterActiveFX(UNiagaraComponent* Component, bool bIsEquipFX, EEquipmentSlot Slot, int32 SlotIndex)
{
	if (!Component)
	{
		return;
	}

	FAeyerjiActiveFXEntry& NewEntry = ActiveFX.Emplace_GetRef();
	NewEntry.Component = Component;
	NewEntry.bIsEquipFX = bIsEquipFX;
	NewEntry.Slot = Slot;
	NewEntry.SlotIndex = SlotIndex;
}
