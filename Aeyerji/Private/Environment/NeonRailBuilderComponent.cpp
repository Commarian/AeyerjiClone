// SPDX-License-Identifier: MIT
#include "Environment/NeonRailBuilderComponent.h"

#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
	constexpr int32 MaxRailSegments = 4096;

	bool IsFiniteRailVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}
}

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
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	if (IsValid(Spline) && Spline->GetOwner() == Owner && Spline->GetWorld() == GetWorld())
	{
		return Spline;
	}

	return Owner->FindComponentByClass<USplineComponent>();
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
	if (!UseSpline || !FMath::IsFinite(SegmentLength) || SegmentLength <= KINDA_SMALL_NUMBER)
	{
		return;
	}

	ClearPreviousMeshes();
	if (!bClearPrevious)
	{
		CacheTaggedSegmentsIfNeeded();
	}

	const int32 AvailableSegmentSlots = MaxRailSegments - SpawnedSegments.Num();
	if (AvailableSegmentSlots <= 0)
	{
		OnRailRebuilt.Broadcast(this);
		CacheSplineVersion();
		return;
	}

	const double SplineLength = UseSpline->GetSplineLength();
	if (!FMath::IsFinite(SplineLength) || SplineLength <= UE_KINDA_SMALL_NUMBER)
	{
		OnRailRebuilt.Broadcast(this);
		CacheSplineVersion();
		return;
	}

	const int32 NumSteps = FMath::Clamp(
		FMath::CeilToInt(FMath::Min(SplineLength / static_cast<double>(SegmentLength), static_cast<double>(AvailableSegmentSlots))),
		1,
		AvailableSegmentSlots);
	if (NumSteps <= 0)
	{
		OnRailRebuilt.Broadcast(this);
		CacheSplineVersion();
		return;
	}

	for (int32 Index = 0; Index < NumSteps; ++Index)
	{
		const float StartDistance = static_cast<float>(Index * SplineLength / NumSteps);
		const float EndDistance = static_cast<float>((Index + 1) * SplineLength / NumSteps);

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

	const float SafeHeight = FMath::IsFinite(Height) ? FMath::Clamp(Height, -10000000.f, 10000000.f) : 0.f;
	StartLocation.Z += SafeHeight;
	EndLocation.Z += SafeHeight;

	const float SafeTangentScale = FMath::IsFinite(TangentScale)
		? FMath::Clamp(TangentScale, 0.f, 1000.f)
		: 1.f;
	if (!FMath::IsNearlyEqual(SafeTangentScale, 1.f))
	{
		StartTangent *= SafeTangentScale;
		EndTangent *= SafeTangentScale;
	}

	if (!IsFiniteRailVector(StartLocation) || !IsFiniteRailVector(EndLocation)
		|| !IsFiniteRailVector(StartTangent) || !IsFiniteRailVector(EndTangent))
	{
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	const FName SplineMeshName = MakeUniqueObjectName(Owner, USplineMeshComponent::StaticClass(), TEXT("NeonRailSplineMesh"));
	USplineMeshComponent* SplineMesh = NewObject<USplineMeshComponent>(Owner, USplineMeshComponent::StaticClass(), SplineMeshName, RF_Transactional);
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

	const uint8 RawForwardAxis = ForwardAxis.GetValue();
	const ESplineMeshAxis::Type ForwardAxisType = RawForwardAxis <= ESplineMeshAxis::Z
		? static_cast<ESplineMeshAxis::Type>(RawForwardAxis)
		: ESplineMeshAxis::X;
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
					if (SpawnedSegments.Num() < MaxRailSegments)
					{
						SpawnedSegments.Add(SplineMesh);
					}
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
