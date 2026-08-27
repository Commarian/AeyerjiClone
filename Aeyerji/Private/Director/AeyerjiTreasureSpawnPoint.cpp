#include "Director/AeyerjiTreasureSpawnPoint.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Inventory/AeyerjiRewardPresentationActor.h"
#include "NavigationSystem.h"

namespace
{
	bool IsFiniteTreasurePointVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	FVector ResolveSafeProjectionExtent(const FVector& InExtent)
	{
		return FVector(
			FMath::Clamp(FMath::IsFinite(InExtent.X) ? FMath::Abs(InExtent.X) : 250.f, 1.f, 100000.f),
			FMath::Clamp(FMath::IsFinite(InExtent.Y) ? FMath::Abs(InExtent.Y) : 250.f, 1.f, 100000.f),
			FMath::Clamp(FMath::IsFinite(InExtent.Z) ? FMath::Abs(InExtent.Z) : 500.f, 1.f, 100000.f));
	}
}

AAeyerjiTreasureSpawnPoint::AAeyerjiTreasureSpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	SetActorEnableCollision(false);

	USceneComponent* SpawnPointRoot = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(SpawnPointRoot);

	PreviewMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("PreviewMesh"));
	PreviewMesh->SetupAttachment(SpawnPointRoot);
	PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewMesh->SetGenerateOverlapEvents(false);
	PreviewMesh->SetCanEverAffectNavigation(false);
	PreviewMesh->SetHiddenInGame(true);
	PreviewMesh->SetIsVisualizationComponent(true);
}

void AAeyerjiTreasureSpawnPoint::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	RefreshPreviewVisibility();
}

bool AAeyerjiTreasureSpawnPoint::IsEligibleForRiftZone(const FName ActiveRiftZoneId) const
{
	return RiftZoneId.IsNone() || RiftZoneId == ActiveRiftZoneId;
}

TSubclassOf<AAeyerjiRewardPresentationActor> AAeyerjiTreasureSpawnPoint::ResolveChestClass(
	const TSubclassOf<AAeyerjiRewardPresentationActor> DefaultChestClass) const
{
	return ChestClassOverride ? ChestClassOverride : DefaultChestClass;
}

FDataTableRowHandle AAeyerjiTreasureSpawnPoint::ResolveLootProfileRow(
	const FDataTableRowHandle& DefaultLootProfileRow) const
{
	// Only a completely empty override inherits the Rift default. Keeping a partial handle lets
	// authoritative validation report the authoring error instead of silently rolling the default row.
	return LootProfileRowOverride.IsNull() ? DefaultLootProfileRow : LootProfileRowOverride;
}

bool AAeyerjiTreasureSpawnPoint::ResolveNavigationAnchor(
	const FVector& ProjectionExtent,
	FVector& OutNavigationAnchor) const
{
	OutNavigationAnchor = FVector::ZeroVector;
	UWorld* World = GetWorld();
	if (!World || !IsFiniteTreasurePointVector(GetActorLocation()))
	{
		return false;
	}

	const UNavigationSystemV1* NavigationSystem = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavigationSystem)
	{
		return false;
	}

	FNavLocation ProjectedLocation;
	if (!NavigationSystem->ProjectPointToNavigation(
		GetActorLocation(),
		ProjectedLocation,
		ResolveSafeProjectionExtent(ProjectionExtent))
		|| !IsFiniteTreasurePointVector(ProjectedLocation.Location))
	{
		return false;
	}

	OutNavigationAnchor = ProjectedLocation.Location;
	return true;
}

void AAeyerjiTreasureSpawnPoint::RecordValidationState(
	const EAeyerjiTreasureSpawnPointValidationState InState,
	const FVector& InNavigationAnchor,
	const FString& InMessage)
{
	LastValidationState = InState;
	LastNavigationAnchor = IsFiniteTreasurePointVector(InNavigationAnchor)
		? InNavigationAnchor
		: FVector::ZeroVector;
	LastValidationMessage = InMessage;
}

void AAeyerjiTreasureSpawnPoint::ValidateNavigationAnchor()
{
	if (!bEnabled)
	{
		RecordValidationState(
			EAeyerjiTreasureSpawnPointValidationState::Disabled,
			FVector::ZeroVector,
			TEXT("Point is disabled."));
		return;
	}

	FVector NavigationAnchor;
	if (ResolveNavigationAnchor(FVector(250.f, 250.f, 500.f), NavigationAnchor))
	{
		RecordValidationState(
			EAeyerjiTreasureSpawnPointValidationState::Valid,
			NavigationAnchor,
			TEXT("Nearby NavMesh anchor resolved."));
		return;
	}

	RecordValidationState(
		EAeyerjiTreasureSpawnPointValidationState::OutsideNavigation,
		FVector::ZeroVector,
		TEXT("No nearby NavMesh anchor was found."));
}

void AAeyerjiTreasureSpawnPoint::RefreshPreviewVisibility()
{
	if (!PreviewMesh)
	{
		return;
	}

	PreviewMesh->SetVisibility(bShowPreview);
	PreviewMesh->SetHiddenInGame(true);
	PreviewMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PreviewMesh->SetCanEverAffectNavigation(false);
}
