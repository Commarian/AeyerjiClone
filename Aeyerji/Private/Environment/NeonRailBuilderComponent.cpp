// SPDX-License-Identifier: MIT
#include "Environment/NeonRailBuilderComponent.h"

#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UNeonRailBuilderComponent::UNeonRailBuilderComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	bTickInEditor = true;
}

void UNeonRailBuilderComponent::OnRegister()
{
	Super::OnRegister();

	CacheSplineVersion();
	BuildRail();
}

void UNeonRailBuilderComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

#if WITH_EDITOR
	if (!GetWorld() || GetWorld()->IsGameWorld())
	{
		return;
	}

	USplineComponent* UseSpline = ResolveSpline();
	if (!UseSpline)
	{
		return;
	}

	const uint32 CurrentVersion = UseSpline->SplineCurves.Version;
	if (!bHasCachedVersion || CurrentVersion != CachedSplineVersion)
	{
		CachedSplineVersion = CurrentVersion;
		bHasCachedVersion = true;
		BuildRail();
	}
#endif
}

#if WITH_EDITOR
void UNeonRailBuilderComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	CacheSplineVersion();
	BuildRail();
}
#endif

FName UNeonRailBuilderComponent::RailTag()
{
	static const FName Tag(TEXT("NeonRailPiece"));
	return Tag;
}

USplineComponent* UNeonRailBuilderComponent::ResolveSpline() const
{
	if (Spline)
	{
		return Spline;
	}

	if (AActor* Owner = GetOwner())
	{
		return Owner->FindComponentByClass<USplineComponent>();
	}

	return nullptr;
}

void UNeonRailBuilderComponent::ClearPreviousMeshes()
{
	SpawnedSegments.Reset();

	if (!bClearPrevious)
	{
		return;
	}

	if (AActor* Owner = GetOwner())
	{
		TArray<UActorComponent*> Components;
		Owner->GetComponents(Components);

		for (UActorComponent* Component : Components)
		{
			if (USplineMeshComponent* SplineMesh = Cast<USplineMeshComponent>(Component))
			{
				if (SplineMesh->ComponentHasTag(RailTag()))
				{
					SplineMesh->DestroyComponent();
				}
			}
		}
	}
}

void UNeonRailBuilderComponent::BuildRail()
{
	USplineComponent* UseSpline = ResolveSpline();
	if (!UseSpline || SegmentLength <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	ClearPreviousMeshes();

	const float SplineLength = UseSpline->GetSplineLength();
	if (SplineLength <= KINDA_SMALL_NUMBER)
	{
		OnRailRebuilt.Broadcast(this);
		CacheSplineVersion();
		return;
	}

	const int32 NumSteps = FMath::CeilToInt(SplineLength / SegmentLength);
	if (NumSteps <= 0)
	{
		OnRailRebuilt.Broadcast(this);
		CacheSplineVersion();
		return;
	}

	for (int32 Index = 0; Index < NumSteps; ++Index)
	{
		const float StartDistance = Index * SegmentLength;
		const float EndDistance = FMath::Min(StartDistance + SegmentLength, SplineLength);

		if (EndDistance <= StartDistance + KINDA_SMALL_NUMBER)
		{
			continue;
		}

		SpawnOneSegment(StartDistance, EndDistance);
	}

	OnRailRebuilt.Broadcast(this);
	CacheSplineVersion();
}

void UNeonRailBuilderComponent::SpawnOneSegment(const float T0, const float T1)
{
	USplineComponent* UseSpline = ResolveSpline();
	if (!UseSpline)
	{
		return;
	}

	FVector StartLocation = UseSpline->GetLocationAtDistanceAlongSpline(T0, ESplineCoordinateSpace::Local);
	FVector EndLocation = UseSpline->GetLocationAtDistanceAlongSpline(T1, ESplineCoordinateSpace::Local);

	FVector StartTangent = UseSpline->GetTangentAtDistanceAlongSpline(T0, ESplineCoordinateSpace::Local);
	FVector EndTangent = UseSpline->GetTangentAtDistanceAlongSpline(T1, ESplineCoordinateSpace::Local);

	StartLocation.Z += Height;
	EndLocation.Z += Height;

	if (!FMath::IsNearlyEqual(TangentScale, 1.f))
	{
		StartTangent *= TangentScale;
		EndTangent *= TangentScale;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	USplineMeshComponent* SplineMesh = NewObject<USplineMeshComponent>(Owner, USplineMeshComponent::StaticClass(), NAME_None, RF_Transactional);
	if (!SplineMesh)
	{
		return;
	}

	SplineMesh->SetMobility(EComponentMobility::Movable);
	SplineMesh->CreationMethod = EComponentCreationMethod::UserConstructionScript;
	SplineMesh->ComponentTags.Add(RailTag());

	SplineMesh->AttachToComponent(UseSpline, FAttachmentTransformRules(EAttachmentRule::KeepRelative, true));

	if (TubeMesh)
	{
		SplineMesh->SetStaticMesh(TubeMesh);
	}

	if (NeonMaterial)
	{
		SplineMesh->SetMaterial(0, NeonMaterial);
	}

	const ESplineMeshAxis::Type ForwardAxisType = static_cast<ESplineMeshAxis::Type>(ForwardAxis.GetValue());
	SplineMesh->SetForwardAxis(ForwardAxisType, true);
	SplineMesh->SetStartAndEnd(StartLocation, StartTangent, EndLocation, EndTangent, true);

	SplineMesh->RegisterComponent();

	SpawnedSegments.Add(SplineMesh);
}

void UNeonRailBuilderComponent::CacheTaggedSegmentsIfNeeded() const
{
	if (SpawnedSegments.Num() > 0)
	{
		return;
	}

	if (AActor* Owner = GetOwner())
	{
		TArray<UActorComponent*> Components;
		Owner->GetComponents(Components);

		for (UActorComponent* Component : Components)
		{
			if (USplineMeshComponent* SplineMesh = Cast<USplineMeshComponent>(Component))
			{
				if (SplineMesh->ComponentHasTag(RailTag()))
				{
					SpawnedSegments.Add(SplineMesh);
				}
			}
		}
	}
}

void UNeonRailBuilderComponent::GetSpawnedSegments(TArray<USplineMeshComponent*>& OutSegments) const
{
	OutSegments.Reset();

	CacheTaggedSegmentsIfNeeded();

	for (const TWeakObjectPtr<USplineMeshComponent>& WeakSegment : SpawnedSegments)
	{
		if (USplineMeshComponent* Segment = WeakSegment.Get())
		{
			OutSegments.Add(Segment);
		}
	}
}

void UNeonRailBuilderComponent::CacheSplineVersion()
{
	if (USplineComponent* UseSpline = ResolveSpline())
	{
		CachedSplineVersion = UseSpline->SplineCurves.Version;
		bHasCachedVersion = true;
	}
	else
	{
		bHasCachedVersion = false;
	}
}