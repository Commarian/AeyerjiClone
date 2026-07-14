// Copyright (c) 2025 Aeyerji.

#include "Inventory/AeyerjiGoldPickup.h"

#include "../../AeyerjiPlayerController.h"
#include "../../AeyerjiPlayerState.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "GUI/AeyerjiStringLibrary.h"

AAeyerjiGoldPickup::AAeyerjiGoldPickup()
{
	bReplicates = true;
	SetReplicateMovement(true);
	PrimaryActorTick.bCanEverTick = false;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	PickupSphere = CreateDefaultSubobject<USphereComponent>(TEXT("PickupSphere"));
	PickupSphere->SetupAttachment(Root);
	PickupSphere->SetSphereRadius(PickupRadius);
	PickupSphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	PickupSphere->SetCollisionObjectType(ECC_WorldDynamic);
	PickupSphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	PickupSphere->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	PickupSphere->SetCollisionResponseToChannel(ECC_GameTraceChannel1, ECR_Block);
	PickupSphere->SetGenerateOverlapEvents(true);
	PickupSphere->SetCanEverAffectNavigation(false);

	PreviewMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PreviewMesh"));
	PreviewMesh->SetupAttachment(Root);
	PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewMesh->SetCanEverAffectNavigation(false);

	GoldLabelText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("GoldLabelText"));
	GoldLabelText->SetupAttachment(Root);
	GoldLabelText->SetHorizontalAlignment(EHTA_Center);
	GoldLabelText->SetVerticalAlignment(EVRTA_TextCenter);
	GoldLabelText->SetRelativeLocation(FVector(0.f, 0.f, 90.f));
	GoldLabelText->SetWorldSize(24.f);
	GoldLabelText->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	GoldBeamFX = CreateDefaultSubobject<UNiagaraComponent>(TEXT("GoldBeamFX"));
	GoldBeamFX->SetupAttachment(Root);
	GoldBeamFX->SetAutoActivate(false);
}

AAeyerjiGoldPickup* AAeyerjiGoldPickup::SpawnGold(
	UWorld& World,
	const int64 Amount,
	const FTransform& SpawnTransform,
	TSubclassOf<AAeyerjiGoldPickup> PickupClass,
	APlayerState* EligiblePlayer)
{
	if (Amount <= 0)
	{
		return nullptr;
	}

	UClass* ClassToSpawn = PickupClass ? PickupClass.Get() : StaticClass();
	if (!ClassToSpawn)
	{
		return nullptr;
	}

	AAeyerjiGoldPickup* Pickup = World.SpawnActorDeferred<AAeyerjiGoldPickup>(ClassToSpawn, SpawnTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (!Pickup)
	{
		return nullptr;
	}

	Pickup->GoldAmount = FMath::Max<int64>(1, Amount);
	Pickup->EligiblePlayerState = EligiblePlayer;
	UGameplayStatics::FinishSpawningActor(Pickup, SpawnTransform);
	return Pickup;
}

void AAeyerjiGoldPickup::BeginPlay()
{
	Super::BeginPlay();

	if (PickupSphere)
	{
		PickupSphere->SetSphereRadius(PickupRadius);
		if (HasAuthority())
		{
			PickupSphere->OnComponentBeginOverlap.RemoveDynamic(this, &AAeyerjiGoldPickup::HandlePickupSphereBeginOverlap);
			PickupSphere->OnComponentBeginOverlap.AddDynamic(this, &AAeyerjiGoldPickup::HandlePickupSphereBeginOverlap);
		}
	}

	if (GoldBeamFX && GoldBeamSystem)
	{
		GoldBeamFX->SetAsset(GoldBeamSystem);
		GoldBeamFX->Activate(true);
	}

	UpdateGoldLabel();
}

void AAeyerjiGoldPickup::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	if (PickupSphere)
	{
		PickupSphere->SetSphereRadius(PickupRadius);
	}
	UpdateGoldLabel();
}

void AAeyerjiGoldPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AAeyerjiGoldPickup, GoldAmount);
	DOREPLIFETIME(AAeyerjiGoldPickup, EligiblePlayerState);
}

bool AAeyerjiGoldPickup::CanInteract_Implementation(AAeyerjiPlayerController* Controller)
{
	return GoldAmount > 0 && IsControllerEligible(Controller);
}

FVector AAeyerjiGoldPickup::GetInteractionLocation_Implementation()
{
	return PickupSphere ? PickupSphere->GetComponentLocation() : GetActorLocation();
}

float AAeyerjiGoldPickup::GetInteractionRadius_Implementation()
{
	return PickupRadius;
}

void AAeyerjiGoldPickup::Interact_Implementation(AAeyerjiPlayerController* Controller)
{
	TryGrantToController(Controller);
}

void AAeyerjiGoldPickup::SetGoldAmount(const int64 NewAmount)
{
	if (!HasAuthority())
	{
		return;
	}

	GoldAmount = FMath::Max<int64>(1, NewAmount);
	OnRep_GoldAmount();
	ForceNetUpdate();
}

void AAeyerjiGoldPickup::SetEligiblePlayer(APlayerState* NewEligiblePlayer)
{
	if (!HasAuthority())
	{
		return;
	}

	EligiblePlayerState = NewEligiblePlayer;
	ForceNetUpdate();
}

void AAeyerjiGoldPickup::OnRep_GoldAmount()
{
	UpdateGoldLabel();
	BP_OnGoldAmountChanged(GoldAmount);
}

void AAeyerjiGoldPickup::HandlePickupSphereBeginOverlap(
	UPrimitiveComponent* OverlappedComponent,
	AActor* OtherActor,
	UPrimitiveComponent* OtherComp,
	const int32 OtherBodyIndex,
	const bool bFromSweep,
	const FHitResult& SweepResult)
{
	static_cast<void>(OverlappedComponent);
	static_cast<void>(OtherComp);
	static_cast<void>(OtherBodyIndex);
	static_cast<void>(bFromSweep);
	static_cast<void>(SweepResult);

	if (!HasAuthority() || !bAutoPickup)
	{
		return;
	}

	APawn* Pawn = Cast<APawn>(OtherActor);
	AAeyerjiPlayerController* Controller = Pawn ? Cast<AAeyerjiPlayerController>(Pawn->GetController()) : nullptr;
	TryGrantToController(Controller);
}

void AAeyerjiGoldPickup::Multicast_PlayPickupEffects_Implementation(AActor* PickupTarget, const int64 PickedUpGold)
{
	if (PickupFXSystem)
	{
		UNiagaraFunctionLibrary::SpawnSystemAtLocation(this, PickupFXSystem, GetActorLocation());
	}

	BP_OnGoldPickedUp(PickupTarget, PickedUpGold);
}

void AAeyerjiGoldPickup::Multicast_HidePickupAfterGranted_Implementation()
{
	SetActorEnableCollision(false);
	SetActorHiddenInGame(true);
}

void AAeyerjiGoldPickup::UpdateGoldLabel()
{
	if (!GoldLabelText)
	{
		return;
	}

	// Resolve from GlobalStringTable.csv ("GoldAmountLabel"). Reimport string table asset after CSV edits.
	GoldLabelText->SetText(FText::Format(AeyerjiStringLibrary::GetGlobalStringTableText(TEXT("GoldAmountLabel")), FText::AsNumber(GoldAmount)));
}

bool AAeyerjiGoldPickup::IsControllerEligible(const AAeyerjiPlayerController* Controller) const
{
	if (!Controller)
	{
		return false;
	}

	if (!EligiblePlayerState)
	{
		return true;
	}

	return Controller->PlayerState == EligiblePlayerState;
}

bool AAeyerjiGoldPickup::TryGrantToController(AAeyerjiPlayerController* Controller)
{
	if (!HasAuthority() || GoldAmount <= 0 || !IsControllerEligible(Controller))
	{
		return false;
	}

	AAeyerjiPlayerState* AeyerjiPS = Controller ? Controller->GetPlayerState<AAeyerjiPlayerState>() : nullptr;
	if (!AeyerjiPS)
	{
		return false;
	}

	const int64 PickedUpGold = GoldAmount;
	GoldAmount = 0;
	AeyerjiPS->AddGold(PickedUpGold, FName(TEXT("GoldPickup")));
	Multicast_PlayPickupEffects(Controller->GetPawn(), PickedUpGold);
	Multicast_HidePickupAfterGranted();
	SetLifeSpan(FMath::Max(0.01f, LifeSecondsAfterPickup));
	ForceNetUpdate();
	return true;
}
