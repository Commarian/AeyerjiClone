#include "Combat/AeyerjiWeaponActor.h"

#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Items/ItemDefinition.h"

AAeyerjiWeaponActor::AAeyerjiWeaponActor()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;

	USceneComponent* SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = SceneRoot;

	USkeletalMeshComponent* Skeletal = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("WeaponSkeletalMesh"));
	Skeletal->SetupAttachment(RootComponent);
	Skeletal->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Skeletal->SetGenerateOverlapEvents(false);
	Skeletal->bCastDynamicShadow = true;
	Skeletal->SetIsReplicated(true);
	SkeletalMeshComponent = Skeletal;

	StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponStaticMesh"));
	StaticMeshComponent->SetupAttachment(RootComponent);
	StaticMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	StaticMeshComponent->SetGenerateOverlapEvents(false);
	StaticMeshComponent->SetIsReplicated(true);
	StaticMeshComponent->SetVisibility(false);
}

void AAeyerjiWeaponActor::InitializeFromDefinition(const UItemDefinition* Definition)
{
	if (!Definition)
	{
		SetWeaponMeshes(nullptr, nullptr);
		return;
	}

	SetWeaponMeshes(Definition->WorldMesh, Definition->WorldSkeletalMesh);
}

void AAeyerjiWeaponActor::SetWeaponMeshes(UStaticMesh* StaticMesh, USkeletalMesh* SkeletalMesh)
{
	if (SkeletalMeshComponent)
	{
		SkeletalMeshComponent->SetSkeletalMesh(SkeletalMesh);
		SkeletalMeshComponent->SetVisibility(SkeletalMesh != nullptr);
	}

	if (StaticMeshComponent)
	{
		StaticMeshComponent->SetStaticMesh(StaticMesh);
		const bool bUseStaticMesh = StaticMesh != nullptr && SkeletalMesh == nullptr;
		StaticMeshComponent->SetVisibility(bUseStaticMesh);
	}
}
