// Copyright (c) 2025 Aeyerji.
#include "Director/AeyerjiSpawnRegion.h"

#include "Components/BoxComponent.h"

const FName AAeyerjiSpawnRegion::RiftExcludedActorTag(TEXT("Rift.Excluded"));

AAeyerjiSpawnRegion::AAeyerjiSpawnRegion()
{
	PrimaryActorTick.bCanEverTick = false;

	RegionBounds = CreateDefaultSubobject<UBoxComponent>(TEXT("RegionBounds"));
	SetRootComponent(RegionBounds);

	if (RegionBounds)
	{
		RegionBounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		RegionBounds->SetBoxExtent(FVector(2000.f, 2000.f, 500.f));
	}
}

FBox AAeyerjiSpawnRegion::GetRegionBounds() const
{
	if (RegionBounds)
	{
		return RegionBounds->Bounds.GetBox();
	}

	return FBox(GetActorLocation(), GetActorLocation());
}

bool AAeyerjiSpawnRegion::IsRiftEncounterEligible() const
{
	return RegionWeight > 0.f
		&& !Tags.Contains(RiftExcludedActorTag)
		&& GetRegionBounds().IsValid;
}
