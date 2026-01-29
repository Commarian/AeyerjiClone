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
#include "Components/TextRenderComponent.h"
#include "Components/WidgetComponent.h"
#include "GameFramework/PlayerController.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
#include "Materials/MaterialInterface.h"
#include "Components/OutlineHighlightComponent.h"
#include "Engine/ActorChannel.h"
#include "Engine/Engine.h"
#include "Engine/EngineTypes.h"
#include "Engine/World.h"
#include "Items/InventoryComponent.h"
#include "Items/ItemDefinition.h"
#include "Items/ItemGenerator.h"
#include "Systems/LootTable.h"
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
#include "PhysicsEngine/BodySetup.h"
#include "PhysicalMaterials/PhysicalMaterial.h"

namespace
{
	static constexpr ECollisionChannel InteractTraceChannel = ECC_GameTraceChannel1;
}

AAeyerjiLootPickup::AAeyerjiLootPickup()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	bReplicateUsingRegisteredSubObjectList = true;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics; // update before physics for smoother motion
	PrimaryActorTick.TickInterval = 0.f;        // run every frame
	SetNetUpdateFrequency(60.f);                 // higher replication rate for smoother client motion
	SetMinNetUpdateFrequency(30.f);
	SetReplicateMovement(true);

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	PreviewMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PreviewMesh"));
	PreviewMesh->SetupAttachment(Root);
	// Use the pickup volume for interaction traces; keep the mesh collision-free unless physics handoff is active.
	PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
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
	// Make cursor traces more forgiving: block interact/visibility/camera so hover + click can hit the sphere even if the mesh is small.
	PickupSphere->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block); // interact
	PickupSphere->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	PickupSphere->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
	PickupSphere->OnComponentBeginOverlap.AddDynamic(this, &AAeyerjiLootPickup::HandlePickupSphereOverlap);

	LootLabel = CreateDefaultSubobject<UWidgetComponent>(TEXT("LootLabel"));
	LootLabel->SetupAttachment(Root);
	LootLabel->SetDrawAtDesiredSize(true);
	LootLabel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LootLabel->SetWidgetSpace(EWidgetSpace::Screen);
	LootLabel->SetTwoSided(true);
	LootLabel->SetPivot(FVector2D(0.5f, 0.f));
	LootLabel->SetRelativeLocation(FVector(0.f, 0.f, LabelHeightOffset));
	LootLabel->SetUsingAbsoluteRotation(true);
	LootLabel->SetCanEverAffectNavigation(false);

	LootLabelText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("LootLabelText"));
	LootLabelText->SetupAttachment(Root);
	LootLabelText->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
	LootLabelText->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
	LootLabelText->SetTextRenderColor(FColor::White);
	LootLabelText->SetWorldSize(24.f);
	LootLabelText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LootLabelText->SetVisibility(false);
	LootLabelText->SetHiddenInGame(true);
	LootLabelText->SetCanEverAffectNavigation(false);

	LootLabelOutline = CreateDefaultSubobject<UTextRenderComponent>(TEXT("LootLabelOutline"));
	LootLabelOutline->SetupAttachment(Root);
	LootLabelOutline->SetHorizontalAlignment(EHorizTextAligment::EHTA_Center);
	LootLabelOutline->SetVerticalAlignment(EVerticalTextAligment::EVRTA_TextCenter);
	LootLabelOutline->SetTextRenderColor(FColor::Black);
	LootLabelOutline->SetWorldSize(25.f);
	LootLabelOutline->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LootLabelOutline->SetVisibility(false);
	LootLabelOutline->SetHiddenInGame(true);
	LootLabelOutline->SetCanEverAffectNavigation(false);

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
		Controller->Server_RequestPickupActor(this);
	}
}

FText AAeyerjiLootPickup::GetDisplayName() const
{
	FText DisplayName;
	if (ItemInstance)
	{
		DisplayName = ItemInstance->GetDisplayName();
	}
	else if (ItemDefinition)
	{
		DisplayName = ItemDefinition->DisplayName;
	}

	if (!DisplayName.IsEmpty())
	{
		return DisplayName;
	}

	if (ItemDefinition)
	{
		return FText::FromString(ItemDefinition->GetName());
	}

	if (ItemInstance)
	{
		return FText::FromString(ItemInstance->GetName());
	}

	return FText::FromString(TEXT("Loot"));
}

void AAeyerjiLootPickup::SetLabelVisible(bool bVisible)
{
	bForceLabelVisible = bVisible;
	UpdateLabelVisibility();
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
	UpdateLabelVisibility();
}

int32 AAeyerjiLootPickup::DebugRefreshItemScaling(const UAeyerjiLootTable& LootTable)
{
	if (!HasAuthority() || !ItemInstance)
	{
		return 0;
	}

	ItemInstance->RebuildAggregation();
	ItemInstance->ApplyLootStatScaling(&LootTable);
	ItemInstance->ForceItemChangedForUI();
	MARK_PROPERTY_DIRTY_FROM_NAME(AAeyerjiLootPickup, ItemInstance, this);

	return 1;
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
		UE_LOG(LogAeyerji, Display, TEXT("AeyerjiLootPickup BeginPlay rolling item - Def=%s Level=%d Rarity=%d"),
			*GetNameSafe(ItemDefinition),
			ItemLevel,
			static_cast<int32>(ItemRarity));
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

	UE_LOG(LogAeyerji, Display, TEXT("AeyerjiLootPickup BeginPlay init - Loc=%s Authority=%d NetMode=%d Replicates=%d EnableDrop=%d AutoDrop=%d"),
		*GetActorLocation().ToString(),
		HasAuthority() ? 1 : 0,
		GetNetMode(),
		GetIsReplicated() ? 1 : 0,
		bEnableDropMotion ? 1 : 0,
		bAutoStartDrop ? 1 : 0);

	// Start drop on server if enabled
	if (HasAuthority() && bEnableDropMotion && bAutoStartDrop)
	{
		UE_LOG(LogAeyerji, Display, TEXT("AeyerjiLootPickup BeginPlay auto-starting drop - Enable=%d Auto=%d"),
			bEnableDropMotion ? 1 : 0,
			bAutoStartDrop ? 1 : 0);
		StartDropToGround();
	}
	else
	{
		UE_LOG(LogAeyerji, Display, TEXT("AeyerjiLootPickup BeginPlay drop skipped - HasAuthority=%d Enable=%d Auto=%d"),
			HasAuthority() ? 1 : 0,
			bEnableDropMotion ? 1 : 0,
			bAutoStartDrop ? 1 : 0);
	}
}

void AAeyerjiLootPickup::NotifyActorBeginCursorOver()
{
	Super::NotifyActorBeginCursorOver();
	AJ_LOG(this, TEXT("NotifyActorBeginCursorOver"));
	SetHighlighted(true);
}

void AAeyerjiLootPickup::StartDropToGround()
{
	if (!HasAuthority() || !bEnableDropMotion)
	{
		UE_LOG(LogAeyerji, Display, TEXT("AeyerjiLootPickup StartDropToGround blocked - HasAuthority=%d Enable=%d"),
			HasAuthority() ? 1 : 0,
			bEnableDropMotion ? 1 : 0);
		return;
	}

	// Ditch the tick-based arc/trace: immediately kick the mesh into physics with a small randomized toss.
	bIsDropping = false;
	bLoggedDropSkip = false;
	bLoggedFirstTick = false;
	bLoggedMidTick = false;
	bPhysicsHandoffStarted = false;
	bPhysicsHandoffAttempted = true;

	const FVector SpawnLoc = GetActorLocation();
	const float RandomYaw = FMath::FRandRange(-180.f, 180.f);
	const FVector SideDir = FRotator(0.f, RandomYaw, 0.f).Vector();
	const FVector LaunchStart = SpawnLoc + (SideDir * SideImpulseDistance) + FVector(0.f, 0.f, UpImpulseHeight);

	SetActorLocation(LaunchStart, false, nullptr, ETeleportType::TeleportPhysics);
	UpdateLootBeamAnchor();

	UE_LOG(LogAeyerji, Display, TEXT("AeyerjiLootPickup StartDropToGround physics-only - Start=%s"), *LaunchStart.ToString());

	if (!StartPhysicsHandoff(/*bForceImmediate=*/false))
	{
		UE_LOG(LogAeyerji, Warning, TEXT("AeyerjiLootPickup StartDropToGround physics handoff failed - snapping to ground"));
		FinalSnapToGround();
	}
}

void AAeyerjiLootPickup::ComputeDropEndpoints()
{
	const FVector SpawnLoc = GetActorLocation();
	UE_LOG(LogAeyerji, Display, TEXT("AeyerjiLootPickup ComputeDropEndpoints spawn loc %s"), *SpawnLoc.ToString());

	const float RandomYaw = FMath::FRandRange(-180.f, 180.f);
	const FVector SideDir = FRotator(0.f, RandomYaw, 0.f).Vector();

	const FVector SideOffset = SideDir * SideImpulseDistance;
	const FVector UpOffset = FVector(0.f, 0.f, UpImpulseHeight);

	DropStart = SpawnLoc + SideOffset + UpOffset;

	SetActorLocation(DropStart, false, nullptr, ETeleportType::TeleportPhysics);

	const float TraceUpOffset = 200.f;
	const float TraceDownDistance = 100000.f;
	const FVector TraceStart = DropStart + FVector(0.f, 0.f, TraceUpOffset);
	const FVector TraceEnd = DropStart + FVector(0.f, 0.f, -TraceDownDistance);
	UE_LOG(LogAeyerji, Display, TEXT("AeyerjiLootPickup ComputeDropEndpoints trace Start=%s End=%s"), *TraceStart.ToString(), *TraceEnd.ToString());

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LootDropTrace), false, this);
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);

	const bool bHit = GetWorld()->LineTraceSingleByObjectType(
		Hit,
		TraceStart,
		TraceEnd,
		ObjectParams,
		Params);

	if (bHit && Hit.bBlockingHit)
	{
		DropEnd = Hit.ImpactPoint + FVector(0.f, 0.f, 5.f);
		UE_LOG(LogAeyerji, Display, TEXT("AeyerjiLootPickup ComputeDropEndpoints hit ground - Impact=%s Normal=%s"),
			*Hit.ImpactPoint.ToString(),
			*Hit.ImpactNormal.ToString());
	}
	else
	{
		DropEnd = DropStart + FVector(0.f, 0.f, -500.f);
		UE_LOG(LogAeyerji, Display, TEXT("AeyerjiLootPickup ComputeDropEndpoints no ground hit - Using fallback End=%s (TraceDown=%.0f)"),
			*DropEnd.ToString(),
			TraceDownDistance);
	}

	if (DropEnd.Z > DropStart.Z + KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogAeyerji, Display, TEXT("AeyerjiLootPickup ComputeDropEndpoints upward path detected - StartZ=%.2f EndZ=%.2f (SpawnZ=%.2f)"),
			DropStart.Z,
			DropEnd.Z,
			SpawnLoc.Z);
	}
}

void AAeyerjiLootPickup::FinalSnapToGround()
{
	const FVector Loc = GetActorLocation();
	const FVector TraceStart = Loc + FVector(0.f, 0.f, 20.f);
	const FVector TraceEnd = Loc + FVector(0.f, 0.f, -50.f);

	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LootDropSnapTrace), false, this);
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);

	const bool bHit = GetWorld()->LineTraceSingleByObjectType(
		Hit,
		TraceStart,
		TraceEnd,
		ObjectParams,
		Params);

	if (bHit && Hit.bBlockingHit)
	{
		const FVector SnapLoc = Hit.ImpactPoint + FVector(0.f, 0.f, 2.f);
		SetActorLocation(SnapLoc, false, nullptr, ETeleportType::TeleportPhysics);
		UE_LOG(LogAeyerji, Display, TEXT("AeyerjiLootPickup FinalSnapToGround snapped to %s"), *SnapLoc.ToString());
	}
	else
	{
		UE_LOG(LogAeyerji, Display, TEXT("AeyerjiLootPickup FinalSnapToGround no hit from %s -> %s"), *TraceStart.ToString(), *TraceEnd.ToString());
	}

	UpdateLootBeamAnchor();
}

void AAeyerjiLootPickup::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if ((LootLabelText && LootLabelText->IsVisible()) || (LootLabelOutline && LootLabelOutline->IsVisible()))
	{
		if (UWorld* World = GetWorld())
		{
			if (World->GetNetMode() != NM_DedicatedServer)
			{
				UpdateLootBeamAnchor();
			}
		}
	}

	// Server-only physics handoff: let the mesh simulate freely; only snap actor when physics ends.
	if (HasAuthority() && bPhysicsHandoffStarted)
	{
		if (!PreviewMesh)
		{
			UE_LOG(LogAeyerji, Display, TEXT("AeyerjiLootPickup Tick physics sync aborted - PreviewMesh null"));
			bPhysicsHandoffStarted = false;
			return;
		}

		if (!PreviewMesh->IsSimulatingPhysics())
		{
			UE_LOG(LogAeyerji, Display, TEXT("AeyerjiLootPickup Tick physics sync aborted - PreviewMesh not simulating physics"));
			bPhysicsHandoffStarted = false;
			return;
		}

		// Keep the actor/collision aligned with the simulated mesh so clicks, labels, and outlines stay on the visible ball.
		const FVector MeshLocation = PreviewMesh->GetComponentLocation();
		if (!MeshLocation.Equals(GetActorLocation(), 0.5f))
		{
			SetActorLocation(MeshLocation);
			UpdateLootBeamAnchor();
		}

		if (!PreviewMesh->IsAnyRigidBodyAwake())
		{
			FinalSnapToGround();

			PreviewMesh->SetSimulatePhysics(false);
			if (Root)
			{
				PreviewMesh->AttachToComponent(Root, FAttachmentTransformRules::KeepWorldTransform);
			}
			else
			{
			PreviewMesh->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
		}
			PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			PreviewMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
			PreviewMesh->SetGenerateOverlapEvents(false);

			UpdateLootBeamAnchor();

			bPhysicsHandoffStarted = false;
			UE_LOG(LogAeyerji, Display, TEXT("AeyerjiLootPickup Tick physics sync finished - FinalLoc=%s"), *GetActorLocation().ToString());
		}

		return;
	}

	if (!HasAuthority() || !bIsDropping || !bEnableDropMotion)
	{
		if (!bLoggedDropSkip)
		{
			UE_LOG(LogAeyerji, Display, TEXT("AeyerjiLootPickup Tick skip - HasAuthority=%d Enable=%d Dropping=%d"),
				HasAuthority() ? 1 : 0,
				bEnableDropMotion ? 1 : 0,
				bIsDropping ? 1 : 0);
			bLoggedDropSkip = true;
		}
		return;
	}

	if (DropDuration <= KINDA_SMALL_NUMBER)
	{
		ElapsedDropTime = DropDuration;
	}
	else
	{
		ElapsedDropTime += DeltaSeconds;
	}

	const float AlphaRaw = (DropDuration > KINDA_SMALL_NUMBER)
		? (ElapsedDropTime / DropDuration)
		: 1.f;

	const float Alpha = FMath::Clamp(AlphaRaw, 0.f, 1.f);

	const float AlphaAccel = Alpha * Alpha;
	const FVector BaseLoc = FMath::Lerp(DropStart, DropEnd, AlphaAccel);

	float ArcAlpha = Alpha * (1.f - Alpha) * 4.f;
	ArcAlpha = FMath::Clamp(ArcAlpha, 0.f, 1.f);

	const float HeightZ = ArcAlpha * ArcHeight;
	const FVector NewLoc = BaseLoc + FVector(0.f, 0.f, HeightZ);

	SetActorLocation(NewLoc, true);
	UpdateLootBeamAnchor();

	const float YawDelta = RandomSpinYawSpeed * DeltaSeconds;
	AddActorLocalRotation(FRotator(0.f, YawDelta, 0.f));

	if (bEnablePhysicsHandoff && !bPhysicsHandoffStarted && !bPhysicsHandoffAttempted && (Alpha >= PhysicsHandoffAlpha))
	{
		bPhysicsHandoffAttempted = true;
		if (StartPhysicsHandoff())
		{
			return;
		}
	}

	if (!bLoggedFirstTick)
	{
		UE_LOG(LogAeyerji, Display, TEXT("AeyerjiLootPickup Tick start - Alpha=%.2f Elapsed=%.2f NewZ=%.2f StartZ=%.2f EndZ=%.2f ArcAlpha=%.2f"),
			Alpha,
			ElapsedDropTime,
			NewLoc.Z,
			DropStart.Z,
			DropEnd.Z,
			ArcAlpha);
		bLoggedFirstTick = true;
	}
	else if (!bLoggedMidTick && Alpha >= 0.5f)
	{
		UE_LOG(LogAeyerji, Display, TEXT("AeyerjiLootPickup Tick mid - Alpha=%.2f Elapsed=%.2f NewZ=%.2f StartZ=%.2f EndZ=%.2f ArcAlpha=%.2f"),
			Alpha,
			ElapsedDropTime,
			NewLoc.Z,
			DropStart.Z,
			DropEnd.Z,
			ArcAlpha);
		bLoggedMidTick = true;
	}

	if (Alpha >= 1.f)
	{
		FinalSnapToGround();

		bIsDropping = false;
		SetActorTickEnabled(false);
		UE_LOG(LogAeyerji, Display, TEXT("AeyerjiLootPickup Tick finished drop - FinalLoc=%s"), *GetActorLocation().ToString());
	}
}

bool AAeyerjiLootPickup::StartPhysicsHandoff(bool bForceImmediate /*=false*/)
{
	if (!HasAuthority() || bPhysicsHandoffStarted)
	{
		return false;
	}

	if (!PreviewMesh)
	{
		UE_LOG(LogAeyerji, Display, TEXT("AeyerjiLootPickup StartPhysicsHandoff aborted - PreviewMesh null"));
		return false;
	}

	if (!PreviewMesh->GetStaticMesh())
	{
		UE_LOG(LogAeyerji, Display, TEXT("AeyerjiLootPickup StartPhysicsHandoff aborted - PreviewMesh has no StaticMesh"));
		return false;
	}

	if (UBodySetup* BodySetup = PreviewMesh->GetBodySetup())
	{
		if (BodySetup->CollisionTraceFlag == CTF_UseComplexAsSimple)
		{
			UE_LOG(LogAeyerji, Warning, TEXT("AeyerjiLootPickup StartPhysicsHandoff skipped - PreviewMesh uses ComplexAsSimple collision (physics not supported)."));
			if (GEngine)
			{
				const FString Msg = FString::Printf(
					TEXT("Loot physics drop skipped for '%s': mesh uses ComplexAsSimple collision. Enable simple collision on the mesh asset."),
					*GetNameSafe(this));
				GEngine->AddOnScreenDebugMessage(static_cast<int32>(GetUniqueID()), 10.f, FColor::Red, Msg, true);
			}
			return false;
		}
	}

	// Store an upright rotation for the actor so attached components stay stable (mesh will detach and tumble independently).
	PrePhysicsActorRotation = GetActorRotation();
	PrePhysicsActorRotation.Pitch = 0.f;
	PrePhysicsActorRotation.Roll = 0.f;
	SetActorRotation(PrePhysicsActorRotation, ETeleportType::TeleportPhysics);

	bPhysicsHandoffStarted = true;
	bIsDropping = false;
	SetActorTickEnabled(true);

	PrePhysicsMeshRelative = PreviewMesh->GetRelativeTransform();

	// Simulating physics on a child component detaches it; we sync actor transform to the detached mesh during sim.
	PreviewMesh->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);

	// Use full query+physics so we definitely collide with WorldStatic floors while simulating.
	PreviewMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	PreviewMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	PreviewMesh->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	PreviewMesh->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);
	PreviewMesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	PreviewMesh->SetCollisionObjectType(ECC_PhysicsBody);
	PreviewMesh->BodyInstance.bUseCCD = true;
	PreviewMesh->SetGenerateOverlapEvents(false);
	PreviewMesh->SetMobility(EComponentMobility::Movable);
	PreviewMesh->SetEnableGravity(true);
	PreviewMesh->SetLinearDamping(PhysicsLinearDamping);
	PreviewMesh->SetAngularDamping(PhysicsAngularDamping);

	// Apply a high-friction runtime physical material to encourage fun rolling without sliding away forever.
	if (!RuntimePhysicsMaterial)
	{
		RuntimePhysicsMaterial = NewObject<UPhysicalMaterial>(this);
		RuntimePhysicsMaterial->Friction = PhysicsFriction;
		RuntimePhysicsMaterial->Restitution = PhysicsRestitution;
		RuntimePhysicsMaterial->FrictionCombineMode = EFrictionCombineMode::Multiply;
		// UE uses the same combine-mode enum for both friction and restitution.
		RuntimePhysicsMaterial->RestitutionCombineMode = EFrictionCombineMode::Average;
	}
	PreviewMesh->SetPhysMaterialOverride(RuntimePhysicsMaterial);
	PreviewMesh->SetSimulatePhysics(true);

	if (!PreviewMesh->IsSimulatingPhysics())
	{
		UE_LOG(LogAeyerji, Display, TEXT("AeyerjiLootPickup StartPhysicsHandoff aborted - physics could not be enabled (no body setup?)"));
		bPhysicsHandoffStarted = false;
		bIsDropping = true;

		if (Root)
		{
			PreviewMesh->AttachToComponent(Root, FAttachmentTransformRules::KeepWorldTransform);
		}
		else
		{
			PreviewMesh->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepWorldTransform);
		}
		PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PreviewMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		PreviewMesh->SetGenerateOverlapEvents(false);

		return false;
	}

	if (!bForceImmediate)
	{
		const FVector SideDir = FVector(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), 0.f).GetSafeNormal();
		const float LinearSpeed = FMath::FRandRange(PhysicsLinearVelocityMin, PhysicsLinearVelocityMax);
		const float DownSpeed = FMath::FRandRange(PhysicsDownwardVelocityMin, PhysicsDownwardVelocityMax);
		const FVector LinearVel = (SideDir * LinearSpeed) + FVector(0.f, 0.f, -DownSpeed);
		PreviewMesh->SetPhysicsLinearVelocity(LinearVel, false, NAME_None);

		const float AngularSpeed = FMath::FRandRange(PhysicsAngularVelocityMin, PhysicsAngularVelocityMax);
		const FVector AngularAxis = FVector(
			FMath::FRandRange(-1.f, 1.f),
			FMath::FRandRange(-1.f, 1.f),
			FMath::FRandRange(-1.f, 1.f)).GetSafeNormal();
		const FVector AngularVelDeg = AngularAxis * AngularSpeed;
		PreviewMesh->SetPhysicsAngularVelocityInDegrees(AngularVelDeg, false, NAME_None);
		PreviewMesh->WakeAllRigidBodies();

		UE_LOG(LogAeyerji, Display, TEXT("AeyerjiLootPickup StartPhysicsHandoff started - LinVel=%s AngVelDeg=%s Loc=%s"),
			*LinearVel.ToString(),
			*AngularVelDeg.ToString(),
			*GetActorLocation().ToString());
	}
	else
	{
		PreviewMesh->SetPhysicsLinearVelocity(FVector::ZeroVector, false, NAME_None);
		PreviewMesh->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector, false, NAME_None);
		PreviewMesh->WakeAllRigidBodies();

		UE_LOG(LogAeyerji, Display, TEXT("AeyerjiLootPickup StartPhysicsHandoff started (simple gravity) Loc=%s"),
			*GetActorLocation().ToString());
	}

	return true;
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

	if (PreviewMesh->IsSimulatingPhysics() || bPhysicsHandoffStarted)
	{
		UE_LOG(LogAeyerji, Display, TEXT("AeyerjiLootPickup ApplyDefinitionMesh skipped - mesh is simulating physics"));
		return;
	}

	// Only override when the definition provides a mesh; otherwise leave whatever the designer set in the BP.
	if (ItemDefinition && ItemDefinition->WorldMesh)
	{
		UStaticMesh* NewMesh = ItemDefinition->WorldMesh;
		PreviewMesh->SetStaticMesh(NewMesh);
		PreviewMesh->SetVisibility(true, true);
		PreviewMesh->SetHiddenInGame(false);
		PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		PreviewMesh->SetCollisionResponseToAllChannels(ECR_Ignore);

		const bool bHasOffset = !ItemDefinition->WorldMeshOffset.IsNearlyZero();
		const bool bHasRotation = !ItemDefinition->WorldMeshRotation.IsNearlyZero();
		const bool bHasScale = !ItemDefinition->WorldMeshScale.Equals(FVector(1.f));

		// Respect author-provided offsets; otherwise auto-ground the mesh so its bottom rests at the actor root.
		FTransform MeshTransform;
		if (bHasOffset || bHasRotation || bHasScale)
		{
			MeshTransform = FTransform(
				ItemDefinition->WorldMeshRotation,
				ItemDefinition->WorldMeshOffset,
				ItemDefinition->WorldMeshScale);
		}
		else
		{
			const FBoxSphereBounds Bounds = NewMesh->GetBounds();
			const float MinZ = Bounds.Origin.Z - Bounds.BoxExtent.Z;
			const FVector AutoOffset(0.f, 0.f, -MinZ);
			MeshTransform = FTransform(FRotator::ZeroRotator, AutoOffset, FVector(1.f));
		}

		PreviewMesh->SetRelativeTransform(MeshTransform);

		// Keep the pickup volume centered on the visible mesh so cursor traces don't require "pixel-perfect" hits on the sphere.
		// We intentionally keep the mesh collision disabled so ComplexAsSimple meshes don't cause trace stutter.
	if (PickupSphere && ActivePickupVolume == PickupSphere && NewMesh)
	{
		const FBoxSphereBounds MeshBounds = NewMesh->GetBounds();
		const FVector CenterInRootSpace = MeshTransform.TransformPosition(MeshBounds.Origin);
		PickupSphere->SetRelativeLocation(CenterInRootSpace);
		PickupSphere->SetSphereRadius(FMath::Max(PickupRadius, MeshBounds.SphereRadius * 1.15f)); // slight padding over mesh size
	}
	}
	else if (PreviewMesh)
	{
		PreviewMesh->SetRelativeTransform(FTransform::Identity);
	}

	// Apply rarity-based preview material (hard-coded lookup of glow materials).
	if (UMaterialInterface* RarityMat = UItemDefinition::ResolvePreviewMaterial(ItemRarity))
	{
		PreviewMesh->SetMaterial(0, RarityMat);
	}

	UpdateLootBeamAnchor();
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
	UNiagaraSystem* SystemToUse = LootBeamSystem ? LootBeamSystem.Get() : (LootBeamFX ? LootBeamFX->GetAsset() : nullptr);

	if (LootBeamFX)
	{
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

	UpdateLootBeamAnchor();
}

void AAeyerjiLootPickup::UpdateLootBeamAnchor()
{
	if (!PreviewMesh)
	{
		return;
	}

	const FBoxSphereBounds Bounds = PreviewMesh->Bounds;
	const float MeshTopZ = Bounds.GetBox().Max.Z;

	const FVector Anchor(Bounds.Origin.X, Bounds.Origin.Y, MeshTopZ);

	// Keep the beam centered in XY over the mesh, and place the beam origin at the top of the mesh (Z).
	if (LootBeamFX)
	{
		LootBeamFX->SetWorldLocation(Anchor + FVector(0.f, 0.f, BeamBaseZOffset));
		LootBeamFX->SetWorldRotation(FRotator::ZeroRotator);
	}

	// Float the label above the mesh so it's readable even when the pickup is resting on the ground.
	if (LootLabel)
	{
		LootLabel->SetWorldLocation(Anchor + FVector(0.f, 0.f, LabelHeightOffset));
		LootLabel->SetWorldRotation(FRotator::ZeroRotator);
	}

	if (LootLabelText)
	{
		const FVector LabelLocation = Anchor + FVector(0.f, 0.f, LabelHeightOffset);
		LootLabelText->SetWorldLocation(LabelLocation);

		if (LootLabelText->IsVisible())
		{
			APlayerController* PC = nullptr;
			if (UWorld* World = GetWorld())
			{
				PC = World->GetFirstPlayerController();
			}

			if (PC)
			{
				FVector CamLoc;
				FRotator CamRot;
				PC->GetPlayerViewPoint(CamLoc, CamRot);
				FRotator FaceRot = (CamLoc - LabelLocation).Rotation();
				FaceRot.Roll = 0.f;
				LootLabelText->SetWorldRotation(FaceRot);
			}
			else
			{
				LootLabelText->SetWorldRotation(FRotator::ZeroRotator);
			}
		}
	}

	if (LootLabelOutline)
	{
		const FVector LabelLocation = Anchor + FVector(0.f, 0.f, LabelHeightOffset);
		FVector OutlineLocation = LabelLocation;

		if (LootLabelText && LootLabelText->IsVisible())
		{
			APlayerController* PC = nullptr;
			if (UWorld* World = GetWorld())
			{
				PC = World->GetFirstPlayerController();
			}

			if (PC)
			{
				FVector CamLoc;
				FRotator CamRot;
				PC->GetPlayerViewPoint(CamLoc, CamRot);
				const FVector ToCam = (CamLoc - LabelLocation).GetSafeNormal();
				OutlineLocation = LabelLocation - (ToCam * 0.6f);
				FRotator FaceRot = (CamLoc - LabelLocation).Rotation();
				FaceRot.Roll = 0.f;
				LootLabelOutline->SetWorldRotation(FaceRot);
			}
			else
			{
				LootLabelOutline->SetWorldRotation(FRotator::ZeroRotator);
			}
		}

		LootLabelOutline->SetWorldLocation(OutlineLocation);
	}
}

void AAeyerjiLootPickup::UpdateLabelVisibility()
{
	const bool bShouldShow = bHighlighted || bForceLabelVisible;
	bool bWidgetConfigured = false;

	if (LootLabel)
	{
		LootLabel->SetVisibility(bShouldShow);
		LootLabel->SetHiddenInGame(!bShouldShow);
		bWidgetConfigured = (LootLabel->GetWidget() != nullptr);
	}

	if (LootLabelText)
	{
		const bool bShowFallback = bShouldShow && (bForceTextLabel || !bWidgetConfigured);
		LootLabelText->SetVisibility(bShowFallback);
		LootLabelText->SetHiddenInGame(!bShowFallback);
	}

	if (LootLabelOutline)
	{
		const bool bShowFallback = bShouldShow && (bForceTextLabel || !bWidgetConfigured);
		LootLabelOutline->SetVisibility(bShowFallback);
		LootLabelOutline->SetHiddenInGame(!bShowFallback);
	}

	if (bShouldShow)
	{
		UpdateLootBeamAnchor();
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
	const FVector PickupCenter = GetPickupNavCenter();
	const float DistanceSq = FVector::DistSquared2D(PawnLocation, PickupCenter);
	const float AcceptRadiusValue = Controller->PickupAcceptRadius;
	const float AcceptRadiusSq = (AcceptRadiusValue > 5000.f) ? AcceptRadiusValue : FMath::Square(AcceptRadiusValue);
	const float AcceptRadius = FMath::Sqrt(FMath::Max(0.f, AcceptRadiusSq));

	const bool bWithinRadius = DistanceSq <= AcceptRadiusSq;
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
		ActivePickupVolume->SetCollisionResponseToChannel(InteractTraceChannel, ECR_Block);
		ActivePickupVolume->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		ActivePickupVolume->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
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
	if (LootLabelText)
	{
		LootLabelText->SetText(GetDisplayName());
		const FLinearColor LabelColor = UAeyerjiInventoryBPFL::GetRarityColor(ItemRarity);
		LootLabelText->SetTextRenderColor(LabelColor.ToFColor(false));
	}

	if (LootLabelOutline)
	{
		LootLabelOutline->SetText(GetDisplayName());
		LootLabelOutline->SetTextRenderColor(FColor::Black);
	}

	UpdateLootBeamAnchor();
}

void AAeyerjiLootPickup::DestroyIfEmpty()
{
	if (!ItemInstance)
	{
		Destroy();
	}
}
