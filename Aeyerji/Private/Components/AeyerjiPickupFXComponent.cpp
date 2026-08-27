#include "Components/AeyerjiPickupFXComponent.h"

#include "Components/OutlineHighlightComponent.h"
#include "Components/SkeletalMeshComponent.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "GameFramework/Character.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "NiagaraSystem.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "Logging/AeyerjiLog.h"

namespace
{
	constexpr int32 MaxActivePickupFX = 128;
	constexpr int32 MaxPickupFXSlotIndex = 1000000;
	constexpr float MaxPickupFXOffset = 1000000.f;
	constexpr float MaxPickupFXDuration = 60.f;

	bool IsFinitePickupFXVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	bool IsValidPickupFXSlot(const EEquipmentSlot Slot)
	{
		const UEnum* Enum = StaticEnum<EEquipmentSlot>();
		return Enum && Enum->IsValidEnumValue(static_cast<int64>(Slot));
	}
}

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

void UAeyerjiPickupFXComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopAllEquipFX();
	for (FAeyerjiActiveFXEntry& Entry : ActiveFX)
	{
		if (UNiagaraComponent* Component = Entry.Component.Get())
		{
			Component->Deactivate();
			Component->DestroyComponent();
		}
	}
	ActiveFX.Reset();
	CachedMesh.Reset();
	CachedOutline.Reset();
	Super::EndPlay(EndPlayReason);
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
	if (!IsValid(GetOwner()) || GetNetMode() == NM_DedicatedServer || !VisualConfig.HasPickupVisuals())
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
	if (!IsValid(GetOwner()) || GetNetMode() == NM_DedicatedServer || !VisualConfig.HasEquipVisuals()
		|| !IsValidPickupFXSlot(Slot))
	{
		AJ_LOG(this, TEXT("PickupFXComponent equip skipped - Owner=%s EquipVisuals=%d"),
			*GetNameSafe(GetOwner()),
			VisualConfig.HasEquipVisuals() ? 1 : 0);
		return;
	}

	const int32 SanitizedSlotIndex = FMath::Clamp(SlotIndex, 0, MaxPickupFXSlotIndex);

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
	if (!IsValidPickupFXSlot(Slot))
	{
		return;
	}
	const int32 SanitizedSlotIndex = FMath::Clamp(SlotIndex, 0, MaxPickupFXSlotIndex);

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
	if (!IsValid(System) || !IsFinitePickupFXVector(LocalOffset))
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
		LocalOffset.BoundToBox(FVector(-MaxPickupFXOffset), FVector(MaxPickupFXOffset)),
		FRotator::ZeroRotator,
		EAttachLocation::KeepRelativeOffset,
		true);

	if (NiagaraComp)
	{
		NiagaraComp->SetAutoDestroy(true);

		if (!VisualConfig.ColorParameter.IsNone())
		{
			const FLinearColor SafeColor(
				FMath::IsFinite(VisualConfig.FXColor.R) ? VisualConfig.FXColor.R : 1.f,
				FMath::IsFinite(VisualConfig.FXColor.G) ? VisualConfig.FXColor.G : 1.f,
				FMath::IsFinite(VisualConfig.FXColor.B) ? VisualConfig.FXColor.B : 1.f,
				FMath::IsFinite(VisualConfig.FXColor.A) ? VisualConfig.FXColor.A : 1.f);
			NiagaraComp->SetVariableLinearColor(VisualConfig.ColorParameter, SafeColor);
		}
	}

	return NiagaraComp;
}

void UAeyerjiPickupFXComponent::ApplyOutlinePulse(const FAeyerjiPickupVisualConfig& VisualConfig)
{
	if (!VisualConfig.bPulseOutline || !FMath::IsFinite(VisualConfig.OutlinePulseDuration)
		|| VisualConfig.OutlinePulseDuration <= 0.f)
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
			FMath::Clamp(VisualConfig.OutlinePulseDuration, 0.f, MaxPickupFXDuration),
			FMath::Clamp(FMath::IsFinite(VisualConfig.OutlinePulseFadeTime) ? VisualConfig.OutlinePulseFadeTime : 0.f, 0.f, MaxPickupFXDuration),
			FMath::Clamp(VisualConfig.OutlineStencilOverride, -1, 255));
	}
}

void UAeyerjiPickupFXComponent::RegisterActiveFX(UNiagaraComponent* Component, bool bIsEquipFX, EEquipmentSlot Slot, int32 SlotIndex)
{
	if (!Component)
	{
		return;
	}
	CleanupStaleFX();
	while (ActiveFX.Num() >= MaxActivePickupFX)
	{
		if (UNiagaraComponent* OldestComponent = ActiveFX[0].Component.Get())
		{
			OldestComponent->Deactivate();
			OldestComponent->DestroyComponent();
		}
		ActiveFX.RemoveAt(0, 1, EAllowShrinking::No);
	}

	FAeyerjiActiveFXEntry& NewEntry = ActiveFX.Emplace_GetRef();
	NewEntry.Component = Component;
	NewEntry.bIsEquipFX = bIsEquipFX;
	NewEntry.Slot = Slot;
	NewEntry.SlotIndex = SlotIndex;
}
