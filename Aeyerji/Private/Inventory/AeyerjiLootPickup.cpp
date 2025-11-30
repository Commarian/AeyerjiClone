// AeyerjiLootPickup.cpp

#include "Inventory/AeyerjiLootPickup.h"

#include "Aeyerji/AeyerjiPlayerController.h"
#include "Inventory/AeyerjiInventoryBPFL.h"
#include "AeyerjiCharacter.h"
#include "Components/AeyerjiPickupFXComponent.h"
#include "Components/ShapeComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/MeshComponent.h"
#include "Components/WidgetComponent.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Components/OutlineHighlightComponent.h"
#include "Engine/ActorChannel.h"
#include "Engine/World.h"
#include "Items/InventoryComponent.h"
#include "Items/ItemDefinition.h"
#include "Items/ItemGenerator.h"
#include "Items/ItemInstance.h"
#include "Net/Core/PushModel/PushModel.h"
#include "Net/UnrealNetwork.h"
#include "Logging/AeyerjiLog.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"
#include "Animation/AnimationAsset.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "Systems/AeyerjiGameplayEventSubsystem.h"

namespace
{
	static constexpr ECollisionChannel InteractTraceChannel = ECC_GameTraceChannel1;
}

AAeyerjiLootPickup::AAeyerjiLootPickup()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	bReplicateUsingRegisteredSubObjectList = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	PreviewMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PreviewMesh"));
	PreviewMesh->SetupAttachment(Root);
	PreviewMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PreviewMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	PreviewMesh->SetCollisionResponseToChannel(InteractTraceChannel, ECR_Block);
	PreviewMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	PreviewMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
	PreviewMesh->SetGenerateOverlapEvents(false);
	PreviewMesh->SetCanEverAffectNavigation(false);
	PreviewMesh->SetIsReplicated(true);
	PreviewMesh->SetVisibility(false, true);

	PickupSphere = CreateDefaultSubobject<USphereComponent>(TEXT("PickupSphere"));
	PickupSphere->SetupAttachment(Root);
	PickupSphere->SetSphereRadius(PickupRadius);
	PickupSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PickupSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	PickupSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	PickupSphere->OnComponentBeginOverlap.AddDynamic(this, &AAeyerjiLootPickup::HandlePickupSphereOverlap);

	LootLabel = CreateDefaultSubobject<UWidgetComponent>(TEXT("LootLabel"));
	LootLabel->SetupAttachment(Root);
	LootLabel->SetDrawAtDesiredSize(true);
	LootLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	LootBeamFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("LootBeamFX"));
	LootBeamFX->SetupAttachment(Root);
	LootBeamFX->SetAutoActivate(false);
	LootBeamFX->SetAutoDestroy(false);
	LootBeamFX->SetCanEverAffectNavigation(false);

	OutlineHighlight = CreateDefaultSubobject<UOutlineHighlightComponent>(TEXT("OutlineHighlight"));
	if (OutlineHighlight)
	{
		OutlineHighlight->bAffectAllPrimitivesIfNoExplicitTargets = false;
	}

	if (RarityToBeamColor.Num() == 0)
	{
		RarityToBeamColor.Add(EItemRarity::Common, FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("BDBDBDFF"))));
		RarityToBeamColor.Add(EItemRarity::Uncommon, FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("37FF00FF"))));
		RarityToBeamColor.Add(EItemRarity::Rare, FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("2A66FFFF"))));
		RarityToBeamColor.Add(EItemRarity::Epic, FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("6C32FFFF"))));
		RarityToBeamColor.Add(EItemRarity::Pure, FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("F6E84AFF"))));
		RarityToBeamColor.Add(EItemRarity::Legendary, FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("FF9627FF"))));
		RarityToBeamColor.Add(EItemRarity::PerfectLegendary, FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("FF3A1CFF"))));
		RarityToBeamColor.Add(EItemRarity::Celestial, FLinearColor::FromSRGBColor(FColor::FromHex(TEXT("22F3FFFF"))));
	}

}

AAeyerjiLootPickup* AAeyerjiLootPickup::SpawnFromInstance(
	UWorld& World,
	UAeyerjiItemInstance* InItemInstance,
	const FTransform& SpawnTransform,
	TSubclassOf<AAeyerjiLootPickup> PickupClass)
{
	if (!InItemInstance)
	{
		return nullptr;
	}

	UClass* ClassToSpawn = PickupClass
		? *PickupClass
		: AAeyerjiLootPickup::StaticClass();

	if (!ClassToSpawn)
	{
		AJ_LOG(&World, TEXT("SpawnFromInstance aborted - ClassToSpawn null, PickupClass=%s"), *GetNameSafe(PickupClass.Get()));
		return nullptr;
	}

	AAeyerjiLootPickup* Pickup = World.SpawnActorDeferred<AAeyerjiLootPickup>(
		ClassToSpawn, SpawnTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (!Pickup)
	{
		AJ_LOG(&World, TEXT("SpawnFromInstance failed - spawn deferred returned null for class %s"), *GetNameSafe(ClassToSpawn));
		return nullptr;
	}

	Pickup->ItemInstance = InItemInstance;
	Pickup->ItemDefinition = InItemInstance->Definition;
	Pickup->ItemLevel = InItemInstance->ItemLevel;
	Pickup->ItemRarity = InItemInstance->Rarity;
	Pickup->ApplyDefinitionMesh();

	InItemInstance->Rename(nullptr, Pickup);
	Pickup->FinishSpawning(SpawnTransform);
	return Pickup;
}

AAeyerjiLootPickup* AAeyerjiLootPickup::SpawnFromDefinition(
	UWorld& World,
	UItemDefinition* Definition,
	int32 ItemLevel,
	EItemRarity Rarity,
	const FTransform& SpawnTransform,
	int32 SeedOverride,
	TSubclassOf<AAeyerjiLootPickup> PickupClass)
{
	if (!Definition)
	{
		return nullptr;
	}

	UClass* ClassToSpawn = PickupClass
		? *PickupClass
		: AAeyerjiLootPickup::StaticClass();

	if (!ClassToSpawn)
	{
		AJ_LOG(&World, TEXT("SpawnFromDefinition aborted - ClassToSpawn null, PickupClass=%s"), *GetNameSafe(PickupClass.Get()));
		return nullptr;
	}

	AAeyerjiLootPickup* Pickup = World.SpawnActorDeferred<AAeyerjiLootPickup>(
		ClassToSpawn, SpawnTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);

	if (!Pickup)
	{
		AJ_LOG(&World, TEXT("SpawnFromDefinition failed - spawn deferred returned null for class %s"), *GetNameSafe(ClassToSpawn));
		return nullptr;
	}

	Pickup->ItemDefinition = Definition;
	Pickup->ItemLevel = ItemLevel;
	Pickup->ItemRarity = Rarity;
	Pickup->SeedOverride = SeedOverride;
	Pickup->ApplyDefinitionMesh();

	UAeyerjiItemInstance* Rolled = UItemGenerator::RollItemInstance(
		Pickup,
		Definition,
		ItemLevel,
		Rarity,
		SeedOverride,
		Definition->DefaultSlot);

	if (!Rolled)
	{
		Pickup->Destroy();
		return nullptr;
	}

	Pickup->ItemInstance = Rolled;
	Pickup->FinishSpawning(SpawnTransform);
	return Pickup;
}

void AAeyerjiLootPickup::Server_AddPickupIntent_Implementation(AAeyerjiPlayerController* Controller)
{
	AddPickupIntent(Controller);
}

void AAeyerjiLootPickup::Server_RemovePickupIntent_Implementation(AAeyerjiPlayerController* Controller)
{
	RemovePickupIntent(Controller);
}

void AAeyerjiLootPickup::AddPickupIntent(AAeyerjiPlayerController* Controller)
{
	if (!HasAuthority() || !Controller)
	{
		return;
	}

	PickupIntents.Add(Controller);
}

void AAeyerjiLootPickup::RemovePickupIntent(AAeyerjiPlayerController* Controller)
{
	if (!HasAuthority() || !Controller)
	{
		return;
	}

	PickupIntents.Remove(Controller);
}

void AAeyerjiLootPickup::ExecutePickup(AAeyerjiPlayerController* Controller)
{
	if (!HasAuthority() || !Controller || !ItemInstance)
	{
		AJ_LOG(this, TEXT("ExecutePickup aborted - Authority=%d Controller=%s Item=%s"),
			HasAuthority(),
			*GetNameSafe(Controller),
			ItemInstance ? *ItemInstance->GetName() : TEXT("NULL"));
		return;
	}

	if (!CanPawnLoot(Controller))
	{
		AJ_LOG(this, TEXT("ExecutePickup denied - pawn not eligible (%s)"), *GetNameSafe(Controller));
		return;
	}

	UAeyerjiItemInstance* GrantedItem = ItemInstance;
	const FAeyerjiPickupVisualConfig VisualConfig = ResolvePickupVisualConfig(GrantedItem);

	ItemInstance = nullptr;
	MARK_PROPERTY_DIRTY_FROM_NAME(AAeyerjiLootPickup, ItemInstance, this);

	if (UAeyerjiItemInstance* GrantedInventoryItem = GiveLootToInventory(Controller, GrantedItem))
	{
		AJ_LOG(this, TEXT("ExecutePickup success - granted to %s"), *GetNameSafe(Controller));
		PickupIntents.Empty();
		BroadcastPickupEvent(Controller, GrantedInventoryItem);
		TriggerPickupFX(Controller, VisualConfig);

		if (IsReplicatedSubObjectRegistered(GrantedInventoryItem))
		{
			RemoveReplicatedSubObject(GrantedInventoryItem);
		}

		Destroy();
	}
	else
	{
		// Restore the item so the pickup remains usable.
		ItemInstance = GrantedItem;
		MARK_PROPERTY_DIRTY_FROM_NAME(AAeyerjiLootPickup, ItemInstance, this);
		AJ_LOG(this, TEXT("ExecutePickup failed - inventory rejected for %s"), *GetNameSafe(Controller));
	}
}

void AAeyerjiLootPickup::RequestPickupFromClient(AAeyerjiPlayerController* Controller)
{
	if (!Controller)
	{
		return;
	}

	if (Controller->IsLocalController())
	{
		AJ_LOG(this, TEXT("RequestPickupFromClient - asking server via %s"), *GetNameSafe(Controller));
		Controller->Server_RequestPickup(GetFName());
	}
}

FText AAeyerjiLootPickup::GetDisplayName() const
{
	if (ItemInstance)
	{
		return ItemInstance->GetDisplayName();
	}

	if (ItemDefinition)
	{
		return ItemDefinition->DisplayName;
	}

	return FText::FromString(TEXT("Loot"));
}

void AAeyerjiLootPickup::SetLabelVisible(bool bVisible)
{
	if (LootLabel)
	{
		LootLabel->SetVisibility(bVisible);
	}
}

void AAeyerjiLootPickup::SetHighlighted(bool bInHighlighted)
{
	if (bHighlighted == bInHighlighted)
	{
		return;
	}

	bHighlighted = bInHighlighted;
	AJ_LOG(this, TEXT("SetHighlighted %s -> %s"), *GetName(), bHighlighted ? TEXT("ON") : TEXT("OFF"));
	ApplyHighlight(bHighlighted);
}

void AAeyerjiLootPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AAeyerjiLootPickup, ItemInstance);
	DOREPLIFETIME(AAeyerjiLootPickup, ItemDefinition);
	DOREPLIFETIME(AAeyerjiLootPickup, ItemLevel);
	DOREPLIFETIME(AAeyerjiLootPickup, ItemRarity);
	DOREPLIFETIME(AAeyerjiLootPickup, SeedOverride);
}

bool AAeyerjiLootPickup::ReplicateSubobjects(UActorChannel* Channel, FOutBunch* Bunch, FReplicationFlags* RepFlags)
{
	bool bWroteSomething = Super::ReplicateSubobjects(Channel, Bunch, RepFlags);

	if (ItemInstance && ItemInstance->GetOuter() == this)
	{
		bWroteSomething |= Channel->ReplicateSubobject(ItemInstance, *Bunch, *RepFlags);
	}

	return bWroteSomething;
}

void AAeyerjiLootPickup::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	ConfigureVolumes();
	ApplyDefinitionMesh();
	SetLabelFromItem();
	RefreshOutlineTargets();
	RefreshRarityVisuals();
}

void AAeyerjiLootPickup::BeginPlay()
{
	Super::BeginPlay();

	ConfigureVolumes();
	ApplyDefinitionMesh();

	if (HasAuthority() && !ItemInstance && ItemDefinition)
	{
		ItemInstance = UItemGenerator::RollItemInstance(
			this,
			ItemDefinition,
			ItemLevel,
			ItemRarity,
			SeedOverride,
			ItemDefinition->DefaultSlot);
	}

	if (ItemInstance)
	{
		ItemDefinition = ItemInstance->Definition;
	}

	ApplyDefinitionMesh();
	SetLabelFromItem();
	RefreshOutlineTargets();
	RefreshRarityVisuals();
}

void AAeyerjiLootPickup::NotifyActorBeginCursorOver()
{
	Super::NotifyActorBeginCursorOver();
	AJ_LOG(this, TEXT("NotifyActorBeginCursorOver"));
	SetHighlighted(true);
}

void AAeyerjiLootPickup::NotifyActorEndCursorOver()
{
	Super::NotifyActorEndCursorOver();
	AJ_LOG(this, TEXT("NotifyActorEndCursorOver"));
	SetHighlighted(false);
}

void AAeyerjiLootPickup::OnRep_ItemInstance()
{
	if (ItemInstance)
	{
		ItemDefinition = ItemInstance->Definition;
	}

	ApplyDefinitionMesh();
	SetLabelFromItem();
	RefreshOutlineTargets();
	RefreshRarityVisuals();
}

void AAeyerjiLootPickup::OnRep_ItemRarity()
{
	ApplyDefinitionMesh();
	RefreshOutlineTargets();
	RefreshRarityVisuals();
}

void AAeyerjiLootPickup::ApplyDefinitionMesh()
{
	if (!PreviewMesh)
	{
		return;
	}

	// Only override when the definition provides a mesh; otherwise leave whatever the designer set in the BP.
	if (ItemDefinition && ItemDefinition->WorldMesh)
	{
		PreviewMesh->SetStaticMesh(ItemDefinition->WorldMesh);
		PreviewMesh->SetVisibility(true, true);
		PreviewMesh->SetHiddenInGame(false);
		PreviewMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);

		const bool bHasOffset = !ItemDefinition->WorldMeshOffset.IsNearlyZero();
		const bool bHasRotation = !ItemDefinition->WorldMeshRotation.IsNearlyZero();
		const bool bHasScale = !ItemDefinition->WorldMeshScale.Equals(FVector(1.f));
		if (bHasOffset || bHasRotation || bHasScale)
		{
			const FTransform MeshTransform(
				ItemDefinition->WorldMeshRotation,
				ItemDefinition->WorldMeshOffset,
				ItemDefinition->WorldMeshScale);
			PreviewMesh->SetRelativeTransform(MeshTransform);
		}
		else
		{
			PreviewMesh->SetRelativeTransform(FTransform::Identity);
		}
	}
	else if (PreviewMesh)
	{
		PreviewMesh->SetRelativeTransform(FTransform::Identity);
	}
}

void AAeyerjiLootPickup::HandlePickupSphereOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	int32 OtherBodyIndex,
	bool bFromSweep,
	const FHitResult& SweepResult)
{
	if (!HasAuthority() || !bAutoPickup || !ItemInstance)
	{
		AJ_LOG(this, TEXT("HandlePickupSphereOverlap ignored - Authority=%d Auto=%d Item=%s"),
			HasAuthority(), bAutoPickup ? 1 : 0, ItemInstance ? *ItemInstance->GetName() : TEXT("NULL"));
		return;
	}

	APawn* Pawn = Cast<APawn>(OtherActor);
	if (!Pawn)
	{
		AJ_LOG(this, TEXT("HandlePickupSphereOverlap ignored - %s is not a pawn"), *GetNameSafe(OtherActor));
		return;
	}

	AAeyerjiPlayerController* Controller = Cast<AAeyerjiPlayerController>(Pawn->GetController());
	if (!Controller)
	{
		AJ_LOG(this, TEXT("HandlePickupSphereOverlap ignored - pawn %s lacks player controller"), *GetNameSafe(Pawn));
		return;
	}

	if (CanPawnLoot(Controller))
	{
		AJ_LOG(this, TEXT("HandlePickupSphereOverlap auto-looting for %s"), *GetNameSafe(Controller));
		ExecutePickup(Controller);
	}
	else
	{
		AJ_LOG(this, TEXT("HandlePickupSphereOverlap - %s not yet eligible (distance/volume)"), *GetNameSafe(Controller));
	}
}

void AAeyerjiLootPickup::ApplyHighlight(bool bInHighlighted)
{
	if (!OutlineHighlight)
	{
		return;
	}

	if (bInHighlighted)
	{
		RefreshOutlineTargets();
	}
	else
	{
		OutlineHighlight->SetHighlighted(false);
	}
}

void AAeyerjiLootPickup::BuildHighlightMeshList(TArray<UMeshComponent*>& OutMeshes) const
{
	OutMeshes.Reset();

	auto AddMeshIfValid = [&OutMeshes](UMeshComponent* Mesh)
	{
		if (Mesh && !OutMeshes.Contains(Mesh))
		{
			OutMeshes.Add(Mesh);
		}
	};

		AddMeshIfValid(PreviewMesh);

	for (UMeshComponent* Mesh : AdditionalHighlightMeshes)
	{
		AddMeshIfValid(Mesh);
	}
}

void AAeyerjiLootPickup::RefreshOutlineTargets()
{
	if (!OutlineHighlight)
	{
		return;
	}

	ConfigureOutlineComponent();

	const bool bRestoreHighlight = bHighlighted;

	// Disable highlight on previously tracked meshes before we switch targets.
	OutlineHighlight->SetHighlighted(false);

	OutlineHighlight->ExplicitTargets.Reset();

	TArray<UMeshComponent*> MeshComponents;
	BuildHighlightMeshList(MeshComponents);

	for (UMeshComponent* Mesh : MeshComponents)
	{
		if (Mesh)
		{
			if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Mesh))
			{
				OutlineHighlight->ExplicitTargets.Add(Primitive);
			}
		}
	}

	OutlineHighlight->InitializeFromRarityIndex(static_cast<int32>(ItemRarity));
	OutlineHighlight->SetHighlighted(bRestoreHighlight);
}

void AAeyerjiLootPickup::ConfigureOutlineComponent()
{
	if (!OutlineHighlight)
	{
		return;
	}

	if (RarityToStencilOverrides.Num() > 0)
	{
		for (const TPair<EItemRarity, uint8>& Pair : RarityToStencilOverrides)
		{
			const int32 Clamped = FMath::Clamp(static_cast<int32>(Pair.Value), 0, 255);
			OutlineHighlight->RarityIndexToStencil.FindOrAdd(static_cast<int32>(Pair.Key)) = Clamped;
		}
	}
}

void AAeyerjiLootPickup::RefreshRarityVisuals()
{
	if (!LootBeamFX)
	{
		return;
	}

	UNiagaraSystem* SystemToUse = LootBeamSystem ? LootBeamSystem.Get() : LootBeamFX->GetAsset();

	if (SystemToUse)
	{
		if (LootBeamFX->GetAsset() != SystemToUse)
		{
			LootBeamFX->SetAsset(SystemToUse);
		}

		const FLinearColor BeamColor = ResolveBeamColor();

		if (!BeamColorParameter.IsNone())
		{
			LootBeamFX->SetVariableLinearColor(BeamColorParameter, BeamColor);
		}

		LootBeamFX->SetVisibility(true);
		LootBeamFX->SetHiddenInGame(false);

		LootBeamFX->ActivateSystem();
	}
	else
	{
		LootBeamFX->DeactivateImmediate();
		LootBeamFX->SetVisibility(false);
		LootBeamFX->SetHiddenInGame(true);
	}
}

FLinearColor AAeyerjiLootPickup::ResolveBeamColor() const
{
	if (const FLinearColor* Found = RarityToBeamColor.Find(ItemRarity))
	{
		return *Found;
	}

	if (const FLinearColor* Fallback = RarityToBeamColor.Find(EItemRarity::Common))
	{
		return *Fallback;
	}

	return FLinearColor::White;
}

bool AAeyerjiLootPickup::IsOverlappingPawn(APawn* Pawn) const
{
	if (!Pawn)
	{
		return false;
	}

	if (USceneComponent* PawnRootScene = Pawn->GetRootComponent())
	{
		if (UPrimitiveComponent* PawnRoot = Cast<UPrimitiveComponent>(PawnRootScene))
		{
			if (ActivePickupVolume && ActivePickupVolume->IsOverlappingComponent(PawnRoot))
			{
				return true;
			}

			if (ActiveAutoPickupVolume && ActiveAutoPickupVolume->IsOverlappingComponent(PawnRoot))
			{
				return true;
			}
		}
	}

	return false;
}

bool AAeyerjiLootPickup::CanPawnLoot(const AAeyerjiPlayerController* Controller) const
{
	if (!Controller)
	{
		return false;
	}

	const APawn* Pawn = Controller->GetPawn();
	if (!Pawn)
	{
		return false;
	}

	const FVector PawnLocation = Pawn->GetActorLocation();
	const float DistanceSq = FVector::DistSquared2D(PawnLocation, GetActorLocation());
	const float AcceptRadius = Controller->PickupAcceptRadius;

	const bool bWithinRadius = DistanceSq <= FMath::Square(AcceptRadius);
	const bool bInsideVolume = IsOverlappingPawn(const_cast<APawn*>(Pawn));

	const bool bCanLoot = bWithinRadius || bInsideVolume || bAutoPickup;
	if (!bCanLoot)
	{
		AJ_LOG(this, TEXT("CanPawnLoot false - Controller=%s Dist=%.1f Accept=%.1f InVolume=%d Auto=%d"),
			*GetNameSafe(Controller),
			FMath::Sqrt(DistanceSq),
			AcceptRadius,
			bInsideVolume ? 1 : 0,
			bAutoPickup ? 1 : 0);
	}
	return bCanLoot;
}

UAeyerjiItemInstance* AAeyerjiLootPickup::GiveLootToInventory(AAeyerjiPlayerController* Controller, UAeyerjiItemInstance* GrantedItem)
{
	if (!Controller || !GrantedItem)
	{
		AJ_LOG(this, TEXT("GiveLootToInventory failed - Controller=%s Item=%s"),
			*GetNameSafe(Controller),
			GrantedItem ? *GrantedItem->GetName() : TEXT("NULL"));
		return nullptr;
	}

	APawn* Pawn = Controller->GetPawn();
	if (!Pawn)
	{
		AJ_LOG(this, TEXT("GiveLootToInventory failed - %s has no pawn"), *GetNameSafe(Controller));
		return nullptr;
	}

	UAeyerjiInventoryComponent* Inventory = Pawn->FindComponentByClass<UAeyerjiInventoryComponent>();
	if (!Inventory)
	{
		AJ_LOG(this, TEXT("GiveLootToInventory failed - pawn %s missing inventory component"), *GetNameSafe(Pawn));
		return nullptr;
	}

	const FName UniqueName = MakeUniqueObjectName(Inventory, UAeyerjiItemInstance::StaticClass(), GrantedItem->GetFName());
	UAeyerjiItemInstance* TransferItem = DuplicateObject<UAeyerjiItemInstance>(GrantedItem, Inventory, UniqueName);
	if (!TransferItem)
	{
		AJ_LOG(this, TEXT("GiveLootToInventory failed - duplicate of %s could not be created"), *GetNameSafe(GrantedItem));
		return nullptr;
	}

	TransferItem->SetNetAddressable();
	TransferItem->UniqueId = FGuid::NewGuid();
	AJ_LOG(this, TEXT("GiveLootToInventory prepared duplicate %s (UniqueId=%s) for %s"),
		*TransferItem->GetPathName(),
		*TransferItem->UniqueId.ToString(),
		*GetNameSafe(Inventory));

	const EAeyerjiAddItemResult Result = UAeyerjiInventoryBPFL::EquipFirstThenBag(Inventory, TransferItem);
	const UEnum* ResultEnum = StaticEnum<EAeyerjiAddItemResult>();
	const FString ResultName = ResultEnum
		? ResultEnum->GetNameStringByValue(static_cast<int64>(Result))
		: FString::Printf(TEXT("Value_%d"), static_cast<int32>(Result));
	AJ_LOG(this, TEXT("GiveLootToInventory result for %s -> %s"),
		*GetNameSafe(Controller),
		*ResultName);
	if (Result == EAeyerjiAddItemResult::Equipped || Result == EAeyerjiAddItemResult::Bagged)
	{
		return TransferItem;
	}

	TransferItem->MarkAsGarbage();

	return nullptr;
}

void AAeyerjiLootPickup::BroadcastPickupEvent(AAeyerjiPlayerController* Controller, UAeyerjiItemInstance* GrantedItem)
{
	if (!PickupGrantedEventTag.IsValid())
	{
		return;
	}

	if (UAeyerjiGameplayEventSubsystem* EventSubsystem = UAeyerjiGameplayEventSubsystem::Get(this))
	{
		FGameplayEventData Payload;
		Payload.EventTag = PickupGrantedEventTag;
		Payload.EventMagnitude = 1.f;
		Payload.Instigator = Controller ? Controller->GetPawn() : nullptr;
		Payload.Target = this;
		Payload.OptionalObject = GrantedItem;
		Payload.OptionalObject2 = Controller;

		EventSubsystem->BroadcastEvent(PickupGrantedEventTag, Payload);
	}
}

FAeyerjiPickupVisualConfig AAeyerjiLootPickup::ResolvePickupVisualConfig(const UAeyerjiItemInstance* InstanceOverride) const
{
	const UAeyerjiItemInstance* SourceInstance = InstanceOverride ? InstanceOverride : ItemInstance;

	if (bUsePickupVisualOverride)
	{
		if (PickupVisualOverride.HasAnyVisuals())
		{
			AJ_LOG(this, TEXT("ResolvePickupVisualConfig: using actor override for %s"), *GetName());
			return PickupVisualOverride;
		}

		AJ_LOG(this, TEXT("ResolvePickupVisualConfig: override enabled on %s but no visuals configured"), *GetName());
	}

	if (SourceInstance)
	{
		const FAeyerjiPickupVisualConfig FromInstance = SourceInstance->GetPickupVisualConfig();
		AJ_LOG(this, TEXT("ResolvePickupVisualConfig: using item instance %s (Definition=%s, System=%s)"),
			*GetNameSafe(SourceInstance),
			*GetNameSafe(SourceInstance->Definition),
			*GetNameSafe(FromInstance.PickupGrantedSystem));
		return FromInstance;
	}

	if (ItemDefinition)
	{
		AJ_LOG(this, TEXT("ResolvePickupVisualConfig: using fallback definition %s (System=%s)"),
			*GetNameSafe(ItemDefinition),
			*GetNameSafe(ItemDefinition->PickupVisuals.PickupGrantedSystem));
		return ItemDefinition->PickupVisuals;
	}

	AJ_LOG(this, TEXT("ResolvePickupVisualConfig: no visuals configured"));
	return FAeyerjiPickupVisualConfig();
}

void AAeyerjiLootPickup::TriggerPickupFX(AAeyerjiPlayerController* Controller, const FAeyerjiPickupVisualConfig& VisualConfig)
{
	if (!Controller)
	{
		AJ_LOG(this, TEXT("TriggerPickupFX aborted - controller null"));
		return;
	}

	APawn* Pawn = Controller->GetPawn();
	if (!Pawn)
	{
		AJ_LOG(this, TEXT("TriggerPickupFX aborted - %s lacks pawn"), *GetNameSafe(Controller));
		return;
	}

	if (!VisualConfig.HasPickupVisuals())
	{
		AJ_LOG(this, TEXT("TriggerPickupFX skipped - no pickup visuals configured"));
		return;
	}

	AJ_LOG(this, TEXT("TriggerPickupFX -> Pawn=%s System=%s"),
		*GetNameSafe(Pawn),
		*GetNameSafe(VisualConfig.PickupGrantedSystem));

	Multicast_PlayPickupFX(Pawn, VisualConfig);
}

void AAeyerjiLootPickup::Multicast_PlayPickupFX_Implementation(
	AActor* FXTarget,
	const FAeyerjiPickupVisualConfig& VisualConfig)
{
	if (!FXTarget || !VisualConfig.HasPickupVisuals())
	{
		AJ_LOG(this, TEXT("Multicast_PlayPickupFX ignored - Target=%s PickupVisuals=%d"),
			*GetNameSafe(FXTarget),
			VisualConfig.HasPickupVisuals() ? 1 : 0);
		return;
	}

	if (AAeyerjiCharacter* Character = Cast<AAeyerjiCharacter>(FXTarget))
	{
		if (UAeyerjiPickupFXComponent* PickupFX = Character->GetPickupFXComponent())
		{
			AJ_LOG(this, TEXT("Multicast_PlayPickupFX -> Character %s via component %s"),
				*GetNameSafe(Character),
				*GetNameSafe(PickupFX));
			PickupFX->PlayPickupFX(VisualConfig);
			return;
		}
	}

	if (UAeyerjiPickupFXComponent* PickupFX = FXTarget->FindComponentByClass<UAeyerjiPickupFXComponent>())
	{
		AJ_LOG(this, TEXT("Multicast_PlayPickupFX -> Generic actor %s via component %s"),
			*GetNameSafe(FXTarget),
			*GetNameSafe(PickupFX));
		PickupFX->PlayPickupFX(VisualConfig);
	}
	else
	{
		AJ_LOG(this, TEXT("Multicast_PlayPickupFX failed - %s lacks pickup FX component"), *GetNameSafe(FXTarget));
	}
}

FVector AAeyerjiLootPickup::GetPickupNavCenter() const
{
	if (ActivePickupVolume)
	{
		return ActivePickupVolume->GetComponentLocation();
	}

	return GetActorLocation();
}

float AAeyerjiLootPickup::GetPickupNavRadius() const
{
	if (ActivePickupVolume)
	{
		return ActivePickupVolume->Bounds.SphereRadius;
	}

	return PickupRadius;
}

bool AAeyerjiLootPickup::IsHoverTargetComponent(const UPrimitiveComponent* Component) const
{
	if (!Component)
	{
		// Pointer can be null when trace hits actor but not a specific primitive
		return true;
	}

	if (Component == PreviewMesh)
	{
		return true;
	}

	if (ActivePickupVolume && Component == ActivePickupVolume)
	{
		return true;
	}

	return Component->GetOwner() == this;
}

void AAeyerjiLootPickup::ConfigureVolumes()
{
	ActivePickupVolume = PickupVolumeOverride ? PickupVolumeOverride : PickupSphere;

	if (ActivePickupVolume)
	{
		ActivePickupVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		ActivePickupVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
		ActivePickupVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	}

	if (ActivePickupVolume == PickupSphere)
	{
		PickupSphere->SetSphereRadius(PickupRadius);
	}

	ActiveAutoPickupVolume = AutoPickupVolumeOverride ? AutoPickupVolumeOverride : ActivePickupVolume;

	if (ActiveAutoPickupVolume && ActiveAutoPickupVolume != ActivePickupVolume)
	{
		ActiveAutoPickupVolume->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		ActiveAutoPickupVolume->SetCollisionResponseToAllChannels(ECR_Ignore);
		ActiveAutoPickupVolume->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	}

	if (USphereComponent* AutoSphere = Cast<USphereComponent>(ActiveAutoPickupVolume))
	{
		AutoSphere->SetSphereRadius(AutoPickupRadius);
	}
}

void AAeyerjiLootPickup::SetLabelFromItem()
{
	// Widgets typically bind directly to the pickup/instance, so nothing required here.
	// This method exists to preserve API compatibility and future customization.
}

void AAeyerjiLootPickup::DestroyIfEmpty()
{
	if (!ItemInstance)
	{
		Destroy();
	}
}
