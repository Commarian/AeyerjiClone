// ItemPickup.cpp

#include "World/ItemPickup.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Items/InventoryComponent.h"
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
		return;
	}

	if (UAeyerjiInventoryComponent* Inventory = OtherActor->FindComponentByClass<UAeyerjiInventoryComponent>())
	{
		Inventory->Server_AddItem(Item);
		Destroy();
	}
}
