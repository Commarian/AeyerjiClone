// SPDX-License-Identifier: MIT
#include "Environment/NeonRailFlickerComponent.h"

#include "Components/SplineMeshComponent.h"
#include "Environment/NeonRailBuilderComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"

UNeonRailFlickerComponent::UNeonRailFlickerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UNeonRailFlickerComponent::BeginPlay()
{
	Super::BeginPlay();

	ResolveBuilder();
	RefreshSegments();
}

void UNeonRailFlickerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (Builder && bHasBoundDelegate)
	{
		Builder->OnRailRebuilt.RemoveAll(this);
		bHasBoundDelegate = false;
	}

	ClearFlickerState();

	Super::EndPlay(EndPlayReason);
}

void UNeonRailFlickerComponent::ResolveBuilder()
{
	if (Builder)
	{
		return;
	}

	if (AActor* Owner = GetOwner())
	{
		Builder = Owner->FindComponentByClass<UNeonRailBuilderComponent>();
	}
}

void UNeonRailFlickerComponent::RefreshSegments()
{
	ResolveBuilder();

	if (!Builder)
	{
		ClearFlickerState();
		return;
	}

	if (!bHasBoundDelegate)
	{
		Builder->OnRailRebuilt.AddDynamic(this, &UNeonRailFlickerComponent::HandleRailRebuilt);
		bHasBoundDelegate = true;
	}

	TArray<USplineMeshComponent*> Segments;
	Builder->GetSpawnedSegments(Segments);
	SetupForSegments(Segments);
}

void UNeonRailFlickerComponent::HandleRailRebuilt(UNeonRailBuilderComponent* InBuilder)
{
	if (InBuilder != Builder)
	{
		return;
	}

	RefreshSegments();
}

void UNeonRailFlickerComponent::SetupForSegments(const TArray<USplineMeshComponent*>& Segments)
{
	ClearFlickerState();

	if (Segments.Num() == 0)
	{
		return;
	}

	TrackedSegments.Reserve(Segments.Num());
	SegmentMaterials.Reserve(Segments.Num());

	for (USplineMeshComponent* Segment : Segments)
	{
		if (!Segment)
		{
			continue;
		}

		Segment->SetVisibility(true, true);

		TrackedSegments.Emplace(Segment);

		UMaterialInstanceDynamic* DynMaterial = nullptr;

		if (bAffectMaterial && Segment->GetStaticMesh())
		{
			UMaterialInterface* Material = Segment->GetMaterial(0);
			if (Material)
			{
				DynMaterial = Segment->CreateDynamicMaterialInstance(0, Material);
				if (DynMaterial && EmissiveParameterName != NAME_None)
				{
					DynMaterial->SetScalarParameterValue(EmissiveParameterName, EmissiveOnValue);
				}
			}
		}

		SegmentMaterials.Add(DynMaterial);
	}

	for (int32 Index = 0; Index < TrackedSegments.Num(); ++Index)
	{
		const float InitialDelay = bRandomiseInitialDelay ? FMath::FRandRange(MinOnTime, MaxOnTime) : GetRandomOnTime();
		ScheduleNextToggle(Index, InitialDelay);
	}
}

void UNeonRailFlickerComponent::ClearFlickerState()
{
	if (UWorld* World = GetWorld())
	{
		for (FFlickerSegment& Segment : TrackedSegments)
		{
			World->GetTimerManager().ClearTimer(Segment.TimerHandle);
		}
	}

	TrackedSegments.Reset();
	SegmentMaterials.Reset();
}

void UNeonRailFlickerComponent::ApplySegmentState(int32 Index, bool bIsLit)
{
	if (!TrackedSegments.IsValidIndex(Index))
	{
		return;
	}

	FFlickerSegment& Entry = TrackedSegments[Index];
	USplineMeshComponent* Segment = Entry.Segment.Get();
	if (!Segment)
	{
		return;
	}

	Entry.bIsLit = bIsLit;

	if (bToggleVisibility)
	{
		Segment->SetVisibility(bIsLit, true);
	}

	if (bAffectMaterial && SegmentMaterials.IsValidIndex(Index))
	{
		if (UMaterialInstanceDynamic* DynMaterial = SegmentMaterials[Index])
		{
			if (EmissiveParameterName != NAME_None)
			{
				DynMaterial->SetScalarParameterValue(EmissiveParameterName, bIsLit ? EmissiveOnValue : EmissiveOffValue);
			}
		}
	}
}

void UNeonRailFlickerComponent::ScheduleNextToggle(int32 Index, float OverrideDelay)
{
	if (!TrackedSegments.IsValidIndex(Index))
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		FFlickerSegment& Entry = TrackedSegments[Index];
		const float Delay = (OverrideDelay >= 0.f) ? OverrideDelay : (Entry.bIsLit ? GetRandomOnTime() : GetRandomOffTime());

		if (Delay <= KINDA_SMALL_NUMBER)
		{
			ToggleSegment(Index);
			return;
		}

		World->GetTimerManager().SetTimer(
			Entry.TimerHandle,
			FTimerDelegate::CreateUObject(this, &UNeonRailFlickerComponent::ToggleSegment, Index),
			Delay,
			false);
	}
}

void UNeonRailFlickerComponent::ToggleSegment(int32 Index)
{
	if (!TrackedSegments.IsValidIndex(Index))
	{
		return;
	}

	FFlickerSegment& Entry = TrackedSegments[Index];
	USplineMeshComponent* Segment = Entry.Segment.Get();
	if (!Segment)
	{
		return;
	}

	const bool bNextState = !Entry.bIsLit;
	ApplySegmentState(Index, bNextState);
	ScheduleNextToggle(Index);
}

float UNeonRailFlickerComponent::GetRandomOnTime() const
{
	return (MaxOnTime <= MinOnTime) ? MinOnTime : FMath::FRandRange(MinOnTime, MaxOnTime);
}

float UNeonRailFlickerComponent::GetRandomOffTime() const
{
	return (MaxOffTime <= MinOffTime) ? MinOffTime : FMath::FRandRange(MinOffTime, MaxOffTime);
}