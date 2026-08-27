// CorridorSplineBuilder.cpp

#include "LevelDesign/CorridorSplineBuilder.h"
#include "Engine/StaticMesh.h"

namespace
{
	constexpr int32 MaxCorridorSegments = 4096;

	bool IsFiniteCorridorVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}
}

ACorridorSplineBuilder::ACorridorSplineBuilder()
{
	PrimaryActorTick.bCanEverTick = false;

	Spline = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
	SetRootComponent(Spline);
	Spline->bDrawDebug = true;
	Spline->SetClosedLoop(false);
}

void ACorridorSplineBuilder::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	BuildCorridor(); // auto-update while editing
}

void ACorridorSplineBuilder::ClearCorridor()
{
	for (USplineMeshComponent* C : GeneratedSegments)
	{
		if (IsValid(C) && C->GetOwner() == this)
		{
			C->DestroyComponent();
		}
	}
	GeneratedSegments.Reset();
}

void ACorridorSplineBuilder::GatherOrCreateSegments(int32 NumNeeded)
{
	GeneratedSegments.RemoveAll([](const TObjectPtr<USplineMeshComponent>& Segment)
	{
		return !IsValid(Segment.Get());
	});
	NumNeeded = FMath::Clamp(NumNeeded, 0, MaxCorridorSegments);

	while (GeneratedSegments.Num() < NumNeeded)
	{
		const FName SegmentName = MakeUniqueObjectName(this, USplineMeshComponent::StaticClass(), TEXT("CorridorSegment"));
		USplineMeshComponent* SMC = NewObject<USplineMeshComponent>(
			this, USplineMeshComponent::StaticClass(), SegmentName, RF_Transactional);
		if (!SMC)
		{
			break;
		}
		SMC->CreationMethod = EComponentCreationMethod::UserConstructionScript;
		SMC->SetMobility(EComponentMobility::Static);
		SMC->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
		SMC->RegisterComponent();
		GeneratedSegments.Add(SMC);
	}
}

void ACorridorSplineBuilder::HideExtraSegments(int32 StartIndex)
{
	StartIndex = FMath::Clamp(StartIndex, 0, GeneratedSegments.Num());
	for (int32 i = StartIndex; i < GeneratedSegments.Num(); ++i)
	{
		if (GeneratedSegments[i])
		{
			GeneratedSegments[i]->SetStaticMesh(nullptr);
			GeneratedSegments[i]->SetHiddenInGame(true);
			GeneratedSegments[i]->SetVisibility(false, true);
		}
	}
}

void ACorridorSplineBuilder::BuildCorridor()
{
	if (!IsValid(Spline) || !SegmentMesh || Spline->GetNumberOfSplinePoints() < 2)
	{
		HideExtraSegments(0);
		return;
	}

	const double TotalLen = Spline->GetSplineLength();
	if (!FMath::IsFinite(TotalLen) || TotalLen <= UE_KINDA_SMALL_NUMBER)
	{
		HideExtraSegments(0);
		return;
	}

	const double SafeSegmentLength = FMath::IsFinite(SegmentLength)
		? FMath::Clamp(static_cast<double>(SegmentLength), 5.0, 10000000.0)
		: 300.0;
	const int32 NumSegments = FMath::Clamp(FMath::CeilToInt(FMath::Min(
		TotalLen / SafeSegmentLength, static_cast<double>(MaxCorridorSegments))), 1, MaxCorridorSegments);
	GatherOrCreateSegments(NumSegments);
	const float SafeWidthScale = FMath::IsFinite(WidthScale)
		? FMath::Clamp(WidthScale, 0.01f, 1000.f)
		: 1.f;
	const uint8 RawForwardAxis = ForwardAxis.GetValue();
	const ESplineMeshAxis::Type SafeForwardAxis = RawForwardAxis <= ESplineMeshAxis::Z
		? static_cast<ESplineMeshAxis::Type>(RawForwardAxis)
		: ESplineMeshAxis::X;

	int32 Built = 0;
	for (int32 n = 0; n < NumSegments && GeneratedSegments.IsValidIndex(Built); ++n)
	{
		const float D0 = static_cast<float>(n * TotalLen / NumSegments);
		const float D1 = static_cast<float>((n + 1) * TotalLen / NumSegments);

		const FVector P0 = Spline->GetLocationAtDistanceAlongSpline(D0, ESplineCoordinateSpace::Local);
		const FVector P1 = Spline->GetLocationAtDistanceAlongSpline(D1, ESplineCoordinateSpace::Local);
		const FVector T0 = Spline->GetTangentAtDistanceAlongSpline(D0, ESplineCoordinateSpace::Local);
		const FVector T1 = Spline->GetTangentAtDistanceAlongSpline(D1, ESplineCoordinateSpace::Local);
		if (!IsFiniteCorridorVector(P0) || !IsFiniteCorridorVector(P1)
			|| !IsFiniteCorridorVector(T0) || !IsFiniteCorridorVector(T1))
		{
			continue;
		}

		USplineMeshComponent* C = GeneratedSegments[Built++];
		if (!IsValid(C))
		{
			continue;
		}
		C->SetHiddenInGame(false);
		C->SetVisibility(true, true);
		C->SetStaticMesh(SegmentMesh);
		C->SetForwardAxis(SafeForwardAxis, true);
		C->SetStartAndEnd(P0, T0, P1, T1, true);
		C->SetStartScale(FVector2D(SafeWidthScale, SafeWidthScale));
		C->SetEndScale(FVector2D(SafeWidthScale, SafeWidthScale));
		C->SetCollisionEnabled(bCreateCollision ? ECollisionEnabled::QueryAndPhysics
												: ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(bCreateCollision);
	}

	HideExtraSegments(Built);
}
