// AeyerjiCameraOcclusionFadeComponent.cpp

#include "Components/AeyerjiCameraOcclusionFadeComponent.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/CapsuleComponent.h"
#include "Components/PrimitiveComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Logging/AeyerjiLog.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

// Configure default tick behavior for fade interpolation.
UAeyerjiCameraOcclusionFadeComponent::UAeyerjiCameraOcclusionFadeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

// Initialize the trace timer for local clients only.
void UAeyerjiCameraOcclusionFadeComponent::BeginPlay()
{
	Super::BeginPlay();

	if (ShouldRunLocal())
	{
		if (TraceInterval > 0.f)
		{
			if (UWorld* World = GetWorld())
			{
				World->GetTimerManager().SetTimer(
					TraceTimer,
					this,
					&UAeyerjiCameraOcclusionFadeComponent::EvaluateOccluders,
					TraceInterval,
					true);
			}
		}

		EvaluateOccluders();
	}
}

// Restore all tracked components to full visibility on shutdown.
void UAeyerjiCameraOcclusionFadeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(TraceTimer);
	}

	for (auto& Pair : OccluderStates)
	{
		if (UPrimitiveComponent* Comp = Pair.Key.Get())
		{
			ApplyFade(Comp, Pair.Value, 1.f, true);
			RestoreComponentVisibility(Comp, Pair.Value);
		}
	}

	OccluderStates.Empty();

	Super::EndPlay(EndPlayReason);
}

// Tick to smoothly interpolate fades and cleanup stale entries.
void UAeyerjiCameraOcclusionFadeComponent::TickComponent(
	float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTick)
{
	Super::TickComponent(DeltaTime, TickType, ThisTick);

	if (!ShouldRunLocal())
	{
		return;
	}

	if (TraceInterval > 0.f)
	{
		if (UWorld* World = GetWorld())
		{
			if (!World->GetTimerManager().IsTimerActive(TraceTimer))
			{
				World->GetTimerManager().SetTimer(
					TraceTimer,
					this,
					&UAeyerjiCameraOcclusionFadeComponent::EvaluateOccluders,
					TraceInterval,
					true);
			}
		}
	}

	if (TraceInterval <= 0.f)
	{
		EvaluateOccluders();
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const double Now = World->GetTimeSeconds();
	TArray<TWeakObjectPtr<UPrimitiveComponent>> ToRemove;

	for (auto& Pair : OccluderStates)
	{
		UPrimitiveComponent* Comp = Pair.Key.Get();
		FOccluderFadeState& State = Pair.Value;

		if (!IsValid(Comp))
		{
			ToRemove.Add(Pair.Key);
			continue;
		}

		UpdateFadeValue(State, DeltaTime);
		ApplyFade(Comp, State, State.CurrentFade, false);

		if (bDrawDebugBounds && (State.CurrentFade < 0.99f || State.TargetFade < 1.f))
		{
			DrawDebugBox(World, Comp->Bounds.Origin, Comp->Bounds.BoxExtent, FColor::Blue, false, DebugDrawDuration);
		}

		if (State.TargetFade >= 1.f && State.CurrentFade >= 0.999f)
		{
			const double NotSeenDuration = (State.LastSeenTime >= 0.0) ? (Now - State.LastSeenTime) : 0.0;
			if (CleanupDelay <= 0.f || NotSeenDuration >= CleanupDelay)
			{
				RestoreComponentVisibility(Comp, State);
				ToRemove.Add(Pair.Key);
			}
		}
	}

	for (const TWeakObjectPtr<UPrimitiveComponent>& Key : ToRemove)
	{
		OccluderStates.Remove(Key);
	}
}

// Run traces from camera to pawn samples and update occluder targets.
void UAeyerjiCameraOcclusionFadeComponent::EvaluateOccluders()
{
	FVector CameraLoc = FVector::ZeroVector;
	FVector CameraDir = FVector::ForwardVector;
	APawn* Pawn = nullptr;
	APlayerController* PC = nullptr;

	if (!ResolveViewContext(CameraLoc, CameraDir, Pawn, PC))
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !Pawn)
	{
		return;
	}

	TArray<FVector> Samples;
	BuildSamplePoints(Pawn, CameraLoc, Samples);
	if (Samples.Num() == 0)
	{
		return;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(CameraOcclusionFade), bTraceComplex);
	Params.AddIgnoredActor(Pawn);
	if (AActor* Owner = GetOwner())
	{
		Params.AddIgnoredActor(Owner);
	}

	TSet<TWeakObjectPtr<UPrimitiveComponent>> NewOccluders;
	TArray<FHitResult> Hits;
	const FVector PawnLoc = Pawn->GetActorLocation();

	for (const FVector& Sample : Samples)
	{
		Hits.Reset();
		const bool bHit = World->SweepMultiByChannel(
			Hits,
			CameraLoc,
			Sample,
			FQuat::Identity,
			RoofTraceChannel,
			FCollisionShape::MakeSphere(TraceRadius),
			Params);

		if (bDrawDebugTraces)
		{
			const FColor LineColor = bHit ? FColor::Red : FColor::Green;
			DrawDebugLine(World, CameraLoc, Sample, LineColor, false, DebugDrawDuration, 0, 1.0f);
			DrawDebugSphere(World, Sample, TraceRadius, 12, LineColor, false, DebugDrawDuration);
		}

		if (!bHit)
		{
			continue;
		}

		for (const FHitResult& Hit : Hits)
		{
			if (!Hit.bBlockingHit)
			{
				continue;
			}

			UPrimitiveComponent* Comp = Hit.GetComponent();
			if (!IsValidOccluder(Comp))
			{
				continue;
			}

			if (MaxOccluderDistance > 0.f)
			{
				const float DistSq = FVector::DistSquared(PawnLoc, Comp->Bounds.Origin);
				if (DistSq > FMath::Square(MaxOccluderDistance))
				{
					continue;
				}
			}

			if (MinOccluderBoundsExtent > 0.f)
			{
				if (Comp->Bounds.BoxExtent.GetMax() < MinOccluderBoundsExtent)
				{
					continue;
				}
			}

			NewOccluders.Add(Comp);
		}
	}

	if (MaxHiddenComponents > 0)
	{
		EnforceOccluderCap(NewOccluders, PawnLoc);
	}

	const double Now = World->GetTimeSeconds();
	UpdateTargets(NewOccluders, Now);

	if (bPrintOccluderCount)
	{
		AJ_LOG(this, TEXT("Roof occluders: %d tracked=%d"), NewOccluders.Num(), OccluderStates.Num());
	}
}

// Resolve the local player controller, pawn, and camera viewpoint.
bool UAeyerjiCameraOcclusionFadeComponent::ResolveViewContext(
	FVector& OutCameraLoc, FVector& OutCameraDir, APawn*& OutPawn, APlayerController*& OutPC) const
{
	OutPawn = nullptr;
	OutPC = nullptr;

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	if (!(World->IsGameWorld() || World->WorldType == EWorldType::PIE))
	{
		return false;
	}

	if (World->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}

	if (APlayerController* OwnerPC = Cast<APlayerController>(GetOwner()))
	{
		if (!OwnerPC->IsLocalController())
		{
			return false;
		}
		OutPC = OwnerPC;
		OutPawn = OwnerPC->GetPawn();
	}
	else if (APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		APlayerController* PawnPC = Cast<APlayerController>(OwnerPawn->GetController());
		if (!PawnPC || !PawnPC->IsLocalController())
		{
			return false;
		}
		OutPC = PawnPC;
		OutPawn = OwnerPawn;
	}
	else
	{
		return false;
	}

	if (!OutPawn || !OutPC)
	{
		return false;
	}

	FRotator CameraRot;
	OutPC->GetPlayerViewPoint(OutCameraLoc, CameraRot);
	OutCameraDir = CameraRot.Vector();
	return true;
}

// Build sample points around the pawn to reduce edge flicker.
void UAeyerjiCameraOcclusionFadeComponent::BuildSamplePoints(
	const APawn* Pawn, const FVector& CameraLoc, TArray<FVector>& OutSamples) const
{
	OutSamples.Reset();
	if (!Pawn)
	{
		return;
	}

	FVector PawnLoc = Pawn->GetActorLocation();
	float CapsuleRadius = 0.f;
	float CapsuleHalfHeight = 0.f;

	if (const UCapsuleComponent* Capsule = Pawn->FindComponentByClass<UCapsuleComponent>())
	{
		CapsuleRadius = Capsule->GetScaledCapsuleRadius();
		CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	}
	else
	{
		FVector Origin, Extents;
		Pawn->GetActorBounds(true, Origin, Extents);
		PawnLoc = Origin;
		CapsuleRadius = FMath::Max(Extents.X, Extents.Y);
		CapsuleHalfHeight = Extents.Z;
	}

	const float UseRadius = (SampleOffsetRadius > 0.f) ? SampleOffsetRadius : CapsuleRadius;

	OutSamples.Add(PawnLoc);
	OutSamples.Add(PawnLoc + FVector(0.f, 0.f, CapsuleHalfHeight));

	if (UseRadius > 0.f)
	{
		const FVector Forward = Pawn->GetActorForwardVector();
		const FVector Right = Pawn->GetActorRightVector();

		OutSamples.Add(PawnLoc + Forward * UseRadius);
		OutSamples.Add(PawnLoc - Forward * UseRadius);
		OutSamples.Add(PawnLoc + Right * UseRadius);
		OutSamples.Add(PawnLoc - Right * UseRadius);
	}

	if (bIncludeCameraForwardSample && CameraForwardSampleDistance > 0.f)
	{
		FVector ToCamera = CameraLoc - PawnLoc;
		ToCamera.Z = 0.f;
		if (ToCamera.Normalize())
		{
			OutSamples.Add(PawnLoc + ToCamera * CameraForwardSampleDistance);
		}
	}
}

// Apply occluder filter rules to a candidate component.
bool UAeyerjiCameraOcclusionFadeComponent::IsValidOccluder(const UPrimitiveComponent* Comp) const
{
	if (!IsValid(Comp))
	{
		return false;
	}

	if (!ExcludedComponentTag.IsNone() && Comp->ComponentHasTag(ExcludedComponentTag))
	{
		return false;
	}

	switch (OccluderFilter)
	{
	case EAeyerjiRoofOccluderFilter::ComponentTag:
		return !RoofComponentTag.IsNone() && Comp->ComponentHasTag(RoofComponentTag);
	case EAeyerjiRoofOccluderFilter::ActorTag:
		return Comp->GetOwner() && !RoofActorTag.IsNone() && Comp->GetOwner()->ActorHasTag(RoofActorTag);
	case EAeyerjiRoofOccluderFilter::CollisionChannel:
	default:
		return true;
	}
}

// Gate local-only behavior and avoid editor preview worlds.
bool UAeyerjiCameraOcclusionFadeComponent::ShouldRunLocal() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	if (!(World->IsGameWorld() || World->WorldType == EWorldType::PIE))
	{
		return false;
	}

	if (World->GetNetMode() == NM_DedicatedServer)
	{
		return false;
	}

	if (const APlayerController* OwnerPC = Cast<APlayerController>(GetOwner()))
	{
		return OwnerPC->IsLocalController();
	}

	if (const APawn* OwnerPawn = Cast<APawn>(GetOwner()))
	{
		const APlayerController* PawnPC = Cast<APlayerController>(OwnerPawn->GetController());
		return PawnPC && PawnPC->IsLocalController();
	}

	return false;
}

// Build and cache MIDs for any material slots that support the fade parameter.
bool UAeyerjiCameraOcclusionFadeComponent::EnsureFadeState(UPrimitiveComponent* Comp, FOccluderFadeState& State)
{
	if (State.bInitialized)
	{
		return true;
	}

	State.bInitialized = true;
	State.Materials.Reset();
	State.bHardHide = false;

	if (!Comp || RoofFadeParameter.IsNone())
	{
		return false;
	}

	const int32 NumMaterials = Comp->GetNumMaterials();
	bool bAnySupported = false;
	bool bAnyMissing = false;

	for (int32 Index = 0; Index < NumMaterials; ++Index)
	{
		UMaterialInterface* Material = Comp->GetMaterial(Index);
		if (!Material)
		{
			continue;
		}

		const bool bSupports = MaterialSupportsFadeParam(Material);
		if (!bSupports)
		{
			bAnyMissing = true;
			continue;
		}

		bAnySupported = true;
		UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Material);
		if (!MID)
		{
			MID = Comp->CreateAndSetMaterialInstanceDynamic(Index);
		}

		if (MID)
		{
			FMaterialSlot Slot;
			Slot.MaterialIndex = Index;
			Slot.MID = MID;
			State.Materials.Add(Slot);
		}
	}

	if (bAnyMissing && MissingParamPolicy == EAeyerjiFadeMissingParamPolicy::SkipComponent)
	{
		return false;
	}

	if (bAnyMissing && MissingParamPolicy == EAeyerjiFadeMissingParamPolicy::HardHideComponent)
	{
		State.bHardHide = true;
	}

	if (!bAnySupported && !State.bHardHide)
	{
		return false;
	}

	return true;
}

// Check if a material exposes the configured fade parameter.
bool UAeyerjiCameraOcclusionFadeComponent::MaterialSupportsFadeParam(const UMaterialInterface* Material) const
{
	if (!Material || RoofFadeParameter.IsNone())
	{
		return false;
	}

	float Value = 0.f;
	const FMaterialParameterInfo Info(RoofFadeParameter);
	return Material->GetScalarParameterValue(Info, Value);
}

// Drive the fade parameter on all cached MIDs, and optionally hard-hide the component.
void UAeyerjiCameraOcclusionFadeComponent::ApplyFade(
	UPrimitiveComponent* Comp, FOccluderFadeState& State, float FadeValue, bool bForce)
{
	if (!Comp)
	{
		return;
	}

	const float Clamped = FMath::Clamp(FadeValue, 0.f, 1.f);
	const bool bNeedsMaterialUpdate = bForce || FMath::Abs(Clamped - State.LastAppliedFade) > FadeUpdateThreshold;

	if (bNeedsMaterialUpdate)
	{
		for (const FMaterialSlot& Slot : State.Materials)
		{
			if (UMaterialInstanceDynamic* MID = Slot.MID.Get())
			{
				MID->SetScalarParameterValue(RoofFadeParameter, Clamped);
			}
		}
		State.LastAppliedFade = Clamped;
	}

	if (State.bHardHide)
	{
		if (!State.bCachedHiddenState)
		{
			State.bOriginalHiddenInGame = Comp->bHiddenInGame;
			State.bCachedHiddenState = true;
		}

		const float HideThreshold = FMath::Clamp(HardHideThreshold, 0.f, 1.f);
		const bool bShouldHide = Clamped <= HideThreshold;
		if (bShouldHide != State.bHiddenBySystem)
		{
			Comp->SetHiddenInGame(bShouldHide ? true : State.bOriginalHiddenInGame, true);
			State.bHiddenBySystem = bShouldHide;
		}
	}
}

// Restore visibility state if hard-hide was engaged.
void UAeyerjiCameraOcclusionFadeComponent::RestoreComponentVisibility(
	UPrimitiveComponent* Comp, FOccluderFadeState& State)
{
	if (!Comp)
	{
		return;
	}

	if (State.bHardHide && State.bCachedHiddenState)
	{
		Comp->SetHiddenInGame(State.bOriginalHiddenInGame, true);
		State.bHiddenBySystem = false;
	}
}

// Update target fades based on hysteresis and the current occluder set.
void UAeyerjiCameraOcclusionFadeComponent::UpdateTargets(
	const TSet<TWeakObjectPtr<UPrimitiveComponent>>& NewOccluders, double Now)
{
	for (const TWeakObjectPtr<UPrimitiveComponent>& WeakComp : NewOccluders)
	{
		UPrimitiveComponent* Comp = WeakComp.Get();
		if (!Comp)
		{
			continue;
		}

		FOccluderFadeState& State = OccluderStates.FindOrAdd(WeakComp);
		if (!State.bInitialized)
		{
			if (!EnsureFadeState(Comp, State))
			{
				OccluderStates.Remove(WeakComp);
				continue;
			}

			State.CurrentFade = FMath::Clamp(State.CurrentFade, 0.f, 1.f);
			State.TargetFade = State.CurrentFade;
			State.LastAppliedFade = State.CurrentFade;
			State.LastSeenTime = Now;
			State.LastNotSeenTime = Now;
			ApplyFade(Comp, State, State.CurrentFade, true);
		}

		State.LastSeenTime = Now;

		if (State.TargetFade > 0.f)
		{
			const bool bDelayElapsed = (FadeOutDelay <= 0.f)
				|| (State.LastNotSeenTime >= 0.0 && (Now - State.LastNotSeenTime) >= FadeOutDelay);
			if (bDelayElapsed)
			{
				State.TargetFade = 0.f;
			}
		}
	}

	for (auto& Pair : OccluderStates)
	{
		const TWeakObjectPtr<UPrimitiveComponent>& WeakComp = Pair.Key;
		FOccluderFadeState& State = Pair.Value;

		if (NewOccluders.Contains(WeakComp))
		{
			continue;
		}

		State.LastNotSeenTime = Now;

		if (State.TargetFade < 1.f)
		{
			const bool bDelayElapsed = (FadeInDelay <= 0.f)
				|| (State.LastSeenTime >= 0.0 && (Now - State.LastSeenTime) >= FadeInDelay);
			if (bDelayElapsed)
			{
				State.TargetFade = 1.f;
			}
		}
	}
}

// Enforce a maximum count by keeping the nearest occluders to the player.
void UAeyerjiCameraOcclusionFadeComponent::EnforceOccluderCap(
	TSet<TWeakObjectPtr<UPrimitiveComponent>>& Occluders, const FVector& PlayerLoc) const
{
	if (MaxHiddenComponents <= 0 || Occluders.Num() <= MaxHiddenComponents)
	{
		return;
	}

	struct FCandidate
	{
		TWeakObjectPtr<UPrimitiveComponent> Comp;
		float DistSq = 0.f;
	};

	TArray<FCandidate> Candidates;
	Candidates.Reserve(Occluders.Num());

	for (const TWeakObjectPtr<UPrimitiveComponent>& WeakComp : Occluders)
	{
		if (const UPrimitiveComponent* Comp = WeakComp.Get())
		{
			const float DistSq = FVector::DistSquared(PlayerLoc, Comp->Bounds.Origin);
			Candidates.Add({ WeakComp, DistSq });
		}
	}

	Candidates.Sort([](const FCandidate& A, const FCandidate& B)
	{
		return A.DistSq < B.DistSq;
	});

	Occluders.Reset();
	const int32 MaxCount = FMath::Min(MaxHiddenComponents, Candidates.Num());
	for (int32 Index = 0; Index < MaxCount; ++Index)
	{
		Occluders.Add(Candidates[Index].Comp);
	}
}

// Move the fade value toward its target using separate in/out speeds.
bool UAeyerjiCameraOcclusionFadeComponent::UpdateFadeValue(FOccluderFadeState& State, float DeltaSeconds)
{
	const float Target = FMath::Clamp(State.TargetFade, 0.f, 1.f);
	const float Current = FMath::Clamp(State.CurrentFade, 0.f, 1.f);
	const float Speed = (Target < Current) ? FadeSpeedOut : FadeSpeedIn;

	float NewFade = Target;
	if (Speed > 0.f && DeltaSeconds > 0.f)
	{
		NewFade = FMath::FInterpConstantTo(Current, Target, DeltaSeconds, Speed);
	}

	State.CurrentFade = NewFade;
	return !FMath::IsNearlyEqual(Current, NewFade, KINDA_SMALL_NUMBER);
}
