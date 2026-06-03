// ItemPickup.cpp

#include "World/ItemPickup.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Items/InventoryComponent.h"
#include "Items/ItemDefinition.h"
#include "Items/ItemInstance.h"
#include "Net/UnrealNetwork.h"

AItemPickup::AItemPickup()
{
	bReplicates = true;

	SphereComponent = CreateDefaultSubobject<USphereComponent>(TEXT("Sphere"));
	SetRootComponent(SphereComponent);
	SphereComponent->InitSphereRadius(80.f);
	SphereComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	SphereComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
	SphereComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	SphereComponent->OnComponentBeginOverlap.AddDynamic(this, &AItemPickup::HandleSphereOverlap);

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	MeshComponent->SetupAttachment(RootComponent);
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AItemPickup::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AItemPickup, Item);
}

void AItemPickup::SetItem(UAeyerjiItemInstance* InItem)
{
	if (HasAuthority())
	{
		Item = InItem;
	}
}

void AItemPickup::HandleSphereOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
                                      UPrimitiveComponent* OtherComp, int32 OtherBodyIndex, bool bFromSweep,
                                      const FHitResult& SweepResult)
{
	if (!HasAuthority() || !Item || !OtherActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("[InventoryPickup] Legacy ItemPickup overlap ignored Authority=%d Item=%s Other=%s"),
			HasAuthority() ? 1 : 0,
			*GetNameSafe(Item),
			*GetNameSafe(OtherActor));
		return;
	}

	if (UAeyerjiInventoryComponent* Inventory = OtherActor->FindComponentByClass<UAeyerjiInventoryComponent>())
	{
		UE_LOG(LogTemp, Display, TEXT("[InventoryPickup] Legacy ItemPickup attempting grant Pickup=%s Other=%s Inventory=%s Item=%s Def=%s UniqueId=%s"),
			*GetNameSafe(this),
			*GetNameSafe(OtherActor),
			*GetNameSafe(Inventory),
			*GetNameSafe(Item),
			*GetNameSafe(Item->Definition.Get()),
			Item->UniqueId.IsValid() ? *Item->UniqueId.ToString() : TEXT("Invalid"));

		if (Inventory->AddItemInstance(Item))
		{
			UE_LOG(LogTemp, Display, TEXT("[InventoryPickup] Legacy ItemPickup grant succeeded Pickup=%s Item=%s"),
				*GetNameSafe(this),
				*GetNameSafe(Item));
			Destroy();
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[InventoryPickup] Legacy ItemPickup grant rejected; pickup kept alive Pickup=%s Item=%s Inventory=%s"),
				*GetNameSafe(this),
				*GetNameSafe(Item),
				*GetNameSafe(Inventory));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[InventoryPickup] Legacy ItemPickup overlap found no inventory Other=%s Pickup=%s"),
			*GetNameSafe(OtherActor),
			*GetNameSafe(this));
	}
}
