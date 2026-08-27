// SPDX-License-Identifier: MIT
#include "Environment/NeonRailFlickerComponent.h"

#include "Components/SplineMeshComponent.h"
#include "Environment/NeonRailBuilderComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "TimerManager.h"

namespace
{
	constexpr int32 MaxFlickerSegments = 4096;
	constexpr float MinFlickerDelay = 0.01f;
	constexpr float MaxFlickerDelay = 3600.f;

	float SafeFlickerValue(const float Value, const float DefaultValue, const float MinValue, const float MaxValue)
	{
		return FMath::Clamp(FMath::IsFinite(Value) ? Value : DefaultValue, MinValue, MaxValue);
	}
}

UNeonRailFlickerComponent::UNeonRailFlickerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UNeonRailFlickerComponent::BeginPlay()
{
	Super::BeginPlay();
	if (GetWorld() && GetWorld()->IsNetMode(NM_DedicatedServer))
	{
		return;
	}

	ResolveBuilder();
	RefreshSegments();
}

void UNeonRailFlickerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UNeonRailBuilderComponent* ExistingBuilder = BoundBuilder.Get())
	{
		ExistingBuilder->OnRailRebuilt.RemoveAll(this);
	}
	bHasBoundDelegate = false;
	BoundBuilder.Reset();

	ClearFlickerState();

	Super::EndPlay(EndPlayReason);
}

void UNeonRailFlickerComponent::ResolveBuilder()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		Builder = nullptr;
		return;
	}

	if (IsValid(Builder) && Builder->GetOwner() == Owner && Builder->GetWorld() == GetWorld())
	{
		return;
	}

	Builder = Owner->FindComponentByClass<UNeonRailBuilderComponent>();
}

void UNeonRailFlickerComponent::RefreshSegments()
{
	ResolveBuilder();
	if (GetWorld() && GetWorld()->IsNetMode(NM_DedicatedServer))
	{
		ClearFlickerState();
		return;
	}

	if (UNeonRailBuilderComponent* ExistingBuilder = BoundBuilder.Get(); ExistingBuilder && ExistingBuilder != Builder)
	{
		ExistingBuilder->OnRailRebuilt.RemoveAll(this);
		bHasBoundDelegate = false;
		BoundBuilder.Reset();
	}
	else if (bHasBoundDelegate && !BoundBuilder.IsValid())
	{
		bHasBoundDelegate = false;
	}

	if (!Builder)
	{
		ClearFlickerState();
		return;
	}

	if (!bHasBoundDelegate)
	{
		Builder->OnRailRebuilt.AddUniqueDynamic(this, &UNeonRailFlickerComponent::HandleRailRebuilt);
		bHasBoundDelegate = true;
		BoundBuilder = Builder;
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

	const int32 SegmentCapacity = FMath::Min(Segments.Num(), MaxFlickerSegments);
	TrackedSegments.Reserve(SegmentCapacity);
	SegmentMaterials.Reserve(SegmentCapacity);
	TSet<USplineMeshComponent*> SeenSegments;

	for (USplineMeshComponent* Segment : Segments)
	{
		if (TrackedSegments.Num() >= SegmentCapacity)
		{
			break;
		}

		if (!IsValid(Segment) || Segment->GetOwner() != GetOwner() || SeenSegments.Contains(Segment))
		{
			continue;
		}
		SeenSegments.Add(Segment);

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
					DynMaterial->SetScalarParameterValue(
						EmissiveParameterName,
						SafeFlickerValue(EmissiveOnValue, 5.f, -1000000.f, 1000000.f));
				}
			}
		}

		SegmentMaterials.Add(DynMaterial);
	}

	for (int32 Index = 0; Index < TrackedSegments.Num(); ++Index)
	{
		const float InitialDelay = bRandomiseInitialDelay
			? GetRandomOnTime()
			: SafeFlickerValue(MinOnTime, 0.35f, MinFlickerDelay, MaxFlickerDelay);
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
				DynMaterial->SetScalarParameterValue(
					EmissiveParameterName,
					SafeFlickerValue(
						bIsLit ? EmissiveOnValue : EmissiveOffValue,
						bIsLit ? 5.f : 0.f,
						-1000000.f,
						1000000.f));
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
		const float RequestedDelay = FMath::IsFinite(OverrideDelay) && OverrideDelay >= 0.f
			? OverrideDelay
			: (Entry.bIsLit ? GetRandomOnTime() : GetRandomOffTime());
		const float Delay = SafeFlickerValue(RequestedDelay, MinFlickerDelay, MinFlickerDelay, MaxFlickerDelay);

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
	const float SafeMin = SafeFlickerValue(MinOnTime, 0.35f, MinFlickerDelay, MaxFlickerDelay);
	const float SafeMax = SafeFlickerValue(MaxOnTime, SafeMin, MinFlickerDelay, MaxFlickerDelay);
	return FMath::FRandRange(FMath::Min(SafeMin, SafeMax), FMath::Max(SafeMin, SafeMax));
}

float UNeonRailFlickerComponent::GetRandomOffTime() const
{
	const float SafeMin = SafeFlickerValue(MinOffTime, 0.05f, MinFlickerDelay, MaxFlickerDelay);
	const float SafeMax = SafeFlickerValue(MaxOffTime, SafeMin, MinFlickerDelay, MaxFlickerDelay);
	return FMath::FRandRange(FMath::Min(SafeMin, SafeMax), FMath::Max(SafeMin, SafeMax));
}
