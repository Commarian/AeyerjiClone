// SPDX-License-Identifier: MIT
#include "Environment/NeonRailActor.h"

#include "Components/SplineComponent.h"
#include "Environment/NeonRailBuilderComponent.h"
#include "Environment/NeonRailFlickerComponent.h"

namespace
{
	void EnsureDefaultSplineShape(USplineComponent* Spline)
	{
		if (!Spline)
		{
			return;
		}

		if (Spline->GetNumberOfSplinePoints() >= 2)
		{
			return;
		}

		Spline->ClearSplinePoints(false);
		Spline->AddSplinePoint(FVector::ZeroVector, ESplineCoordinateSpace::Local, false);
		Spline->AddSplinePoint(FVector(0.f, 400.f, 0.f), ESplineCoordinateSpace::Local, false);
		Spline->SetSplinePointType(0, ESplinePointType::Curve, false);
		Spline->SetSplinePointType(1, ESplinePointType::Curve, false);
		Spline->UpdateSpline();
	}
}

ANeonRailActor::ANeonRailActor()
{
	PrimaryActorTick.bCanEverTick = false;

	Spline = CreateDefaultSubobject<USplineComponent>(TEXT("Spline"));
	RootComponent = Spline;
	Spline->bDrawDebug = true;
	Spline->SetDrawDebug(true);
	Spline->SetRelativeScale3D(FVector(1.f));

	RailBuilder = CreateDefaultSubobject<UNeonRailBuilderComponent>(TEXT("NeonRailBuilder"));
	RailBuilder->Spline = Spline;

	Flicker = CreateDefaultSubobject<UNeonRailFlickerComponent>(TEXT("NeonRailFlicker"));
	Flicker->Builder = RailBuilder;

	EnsureDefaultSplineShape(Spline);
}

void ANeonRailActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);

	EnsureDefaultSplineShape(Spline);

	if (bAutoRebuildOnConstruction && RailBuilder)
	{
		RailBuilder->BuildRail();
	}

	if (Flicker)
	{
		Flicker->RefreshSegments();
	}
}

#if WITH_EDITOR
void ANeonRailActor::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	EnsureDefaultSplineShape(Spline);

	if (RailBuilder && bAutoRebuildOnConstruction)
	{
		RailBuilder->BuildRail();
	}

	if (Flicker)
	{
		Flicker->RefreshSegments();
	}
}
#endif