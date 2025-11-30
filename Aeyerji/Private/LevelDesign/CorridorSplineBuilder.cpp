// CorridorSplineBuilder.cpp

#include "LevelDesign/CorridorSplineBuilder.h"   // <<— note the subfolder
#include "Engine/StaticMesh.h"

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
	BuildCorridor(); // auto-update while editing
}

void ACorridorSplineBuilder::ClearCorridor()
{
	for (USplineMeshComponent* C : GeneratedSegments)
	{
		if (C) { C->DestroyComponent(); }
	}
	GeneratedSegments.Empty();
}

void ACorridorSplineBuilder::GatherOrCreateSegments(int32 NumNeeded)
{
	while (GeneratedSegments.Num() < NumNeeded)
	{
		USplineMeshComponent* SMC = NewObject<USplineMeshComponent>(this);
		SMC->SetMobility(EComponentMobility::Static);
		SMC->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
		SMC->RegisterComponent();
		GeneratedSegments.Add(SMC);
	}
}

void ACorridorSplineBuilder::HideExtraSegments(int32 StartIndex)
{
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
	if (!SegmentMesh || Spline->GetNumberOfSplinePoints() < 2)
	{
		HideExtraSegments(0);
		return;
	}

	const float TotalLen = Spline->GetSplineLength();
	const int32 NumSegments = FMath::Max(1, FMath::CeilToInt(TotalLen / FMath::Max(5.f, SegmentLength)));
	GatherOrCreateSegments(NumSegments);

	int32 Built = 0;
	for (int32 n = 0; n < NumSegments; ++n)
	{
		const float D0 = n * TotalLen / NumSegments;
		const float D1 = (n + 1) * TotalLen / NumSegments;

		const FVector P0 = Spline->GetLocationAtDistanceAlongSpline(D0, ESplineCoordinateSpace::Local);
		const FVector P1 = Spline->GetLocationAtDistanceAlongSpline(D1, ESplineCoordinateSpace::Local);
		const FVector T0 = Spline->GetTangentAtDistanceAlongSpline(D0, ESplineCoordinateSpace::Local);
		const FVector T1 = Spline->GetTangentAtDistanceAlongSpline(D1, ESplineCoordinateSpace::Local);

		USplineMeshComponent* C = GeneratedSegments[Built++];
		C->SetHiddenInGame(false);
		C->SetVisibility(true, true);
		C->SetStaticMesh(SegmentMesh);
		C->SetForwardAxis(ForwardAxis, true);
		C->SetStartAndEnd(P0, T0, P1, T1, true);
		C->SetStartScale(FVector2D(WidthScale, WidthScale));
		C->SetEndScale  (FVector2D(WidthScale, WidthScale));
		C->SetCollisionEnabled(bCreateCollision ? ECollisionEnabled::QueryAndPhysics
												: ECollisionEnabled::NoCollision);
		C->SetGenerateOverlapEvents(bCreateCollision);
	}

	HideExtraSegments(Built);
}
