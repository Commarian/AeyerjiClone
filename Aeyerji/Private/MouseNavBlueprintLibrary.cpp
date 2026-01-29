#include "MouseNavBlueprintLibrary.h"
#include "Aeyerji/AeyerjiPlayerController.h"
#include "NavigationSystem.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Components/PrimitiveComponent.h"
#include "Components/CapsuleComponent.h"
#include "CollisionShape.h"
#include "CollisionQueryParams.h"
#include "NavigationPath.h"

EMouseNavResult UMouseNavBlueprintLibrary::GetMouseNavContext(
		const UObject*      WorldContextObject,
		APlayerController*  PlayerController,
		FVector&            OutNavLocation,
		FVector&            OutCursorLocation,
		APawn*&             OutPawn)
{
	OutNavLocation = FVector::ZeroVector;
	OutCursorLocation = FVector::ZeroVector;
	OutPawn = nullptr;

	if (!WorldContextObject) { return EMouseNavResult::None; }

	if (!PlayerController)
	{
		UWorld* World = WorldContextObject->GetWorld();
		PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	}
	if (!IsValid(PlayerController)) { return EMouseNavResult::None; }

	auto ReportToServer = [&](EMouseNavResult Result, const FVector& Nav, const FVector& Cursor, APawn* Pawn)
	{
		if (AAeyerjiPlayerController* AyerPC = Cast<AAeyerjiPlayerController>(PlayerController))
		{
			AyerPC->ReportMouseNavContextToServer(Result, Nav, Cursor, Pawn);
		}
	};

	if (!PlayerController->IsLocalController())
	{
		if (AAeyerjiPlayerController* AyerPC = Cast<AAeyerjiPlayerController>(PlayerController))
		{
			EMouseNavResult CachedResult = EMouseNavResult::None;
			if (AyerPC->GetCachedMouseNavContext(CachedResult, OutNavLocation, OutCursorLocation, OutPawn))
			{
				return CachedResult;
			}
		}

		const APawn* LocalPawn = PlayerController->GetPawn();
		const FVector FallbackLocation = LocalPawn ? LocalPawn->GetActorLocation()
		                                       : PlayerController->GetFocalLocation();
		OutNavLocation = FallbackLocation;
		OutCursorLocation = FallbackLocation;
		OutPawn = nullptr;
		return EMouseNavResult::None;
	}

	UWorld* World = PlayerController->GetWorld();
	if (!World)
	{
		return EMouseNavResult::None;
	}

	FVector WorldOrigin;
	FVector WorldDir;
	if (!PlayerController->DeprojectMousePositionToWorld(WorldOrigin, WorldDir))
	{
		return EMouseNavResult::None;
	}

	auto ShouldIgnoreActor = [&](const AActor* Actor) -> bool
	{
		if (!Actor)
		{
			return false;
		}

		if (const APawn* Pawn = Cast<APawn>(Actor))
		{
			return Pawn->IsPlayerControlled();
		}

		const AActor* Owner = Actor->GetOwner();
		while (Owner)
		{
			if (const APawn* OwnerPawn = Cast<APawn>(Owner))
			{
				if (OwnerPawn->IsPlayerControlled())
				{
					return true;
				}
			}
			Owner = Owner->GetOwner();
		}

		return false;
	};

	FCollisionQueryParams Params(SCENE_QUERY_STAT(MouseNavCursor), /*bTraceComplex=*/true);
	if (const APawn* MyPawn = PlayerController->GetPawn())
	{
		Params.AddIgnoredActor(MyPawn);
	}

	const FVector TraceStart = WorldOrigin;
	const FVector TraceEnd = TraceStart + WorldDir * 100000.f;

	// Step 1: pawn-only object trace
	FCollisionObjectQueryParams PawnParams;
	PawnParams.AddObjectTypesToQuery(ECC_Pawn);

	FHitResult Hit;
	for (int32 Pass = 0; Pass < 4; ++Pass)
	{
		if (!World->LineTraceSingleByObjectType(Hit, TraceStart, TraceEnd, PawnParams, Params))
		{
			break;
		}

		if (ShouldIgnoreActor(Hit.GetActor()))
		{
			Params.AddIgnoredActor(Hit.GetActor());
			continue;
		}

		if (APawn* HitPawn = Cast<APawn>(Hit.GetActor()))
		{
			OutNavLocation = HitPawn->GetActorLocation();
			OutCursorLocation = Hit.Location;
			OutPawn = HitPawn;

			ReportToServer(EMouseNavResult::ClickedPawn, OutNavLocation, OutCursorLocation, HitPawn);
			return EMouseNavResult::ClickedPawn;
		}

		break;
	}

	// Step 2: fallback visibility trace for ground hits
	if (!World->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, Params))
	{
		return EMouseNavResult::None; // nothing under cursor at all
	}

	OutCursorLocation = Hit.Location;
	const FVector DesiredPoint = Hit.ImpactPoint;

	UNavigationSystemV1* NavSys =
		FNavigationSystem::GetCurrent<UNavigationSystemV1>(PlayerController->GetWorld());
	if (!NavSys)
	{
		return EMouseNavResult::None;
	}

	FNavLocation Projected;
	constexpr float ExtentXY = 50.f;
	constexpr float ExtentZ = 200.f;

	if (NavSys->ProjectPointToNavigation(
			DesiredPoint, Projected, FVector(ExtentXY, ExtentXY, ExtentZ)))
	{
		OutNavLocation = Projected.Location;

#if WITH_EDITOR
		DrawDebugSphere(PlayerController->GetWorld(), OutNavLocation,
				        25.f, 12, FColor::Green, false, 0.25f);
#endif
		ReportToServer(EMouseNavResult::NavLocation, OutNavLocation, OutCursorLocation, nullptr);
		return EMouseNavResult::NavLocation;
	}

	return EMouseNavResult::None;
}
bool UMouseNavBlueprintLibrary::GetClosestNavigableLocationInRange(
        const UObject*     WorldContextObject,
        APlayerController* PlayerController,
        const FVector&     OriginLocation,
        const FVector&     DesiredLocation,
        float              MaxRange,
        bool               bStraightLine,
        FVector&           OutNavLocation,
        FVector&           OutTeleportLocation,
        FVector            NavProjectionExtent,
        float              TraceHeight,
        float              TraceDepth,
        float              AdditionalOffset)
{
	OutNavLocation = FVector::ZeroVector;
	OutTeleportLocation = FVector::ZeroVector;

	if (!WorldContextObject) { return false; }

	UWorld* World = WorldContextObject->GetWorld();
	if (!World) { return false; }

	if (!PlayerController)
	{
		PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	}

	const APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!Pawn)
	{
		Pawn = Cast<APawn>(WorldContextObject);
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys) { return false; }

	const float ClampedRange = FMath::Max(MaxRange, 0.f);

	const FVector Direction = DesiredLocation - OriginLocation;
	const float DistanceToDesired = Direction.Size();

	FVector TargetPoint = DesiredLocation;
	if (ClampedRange > 0.f && DistanceToDesired > ClampedRange && Direction.GetSafeNormal().IsNearlyZero() == false)
	{
		TargetPoint = OriginLocation + Direction.GetSafeNormal() * ClampedRange;
	}

	FNavLocation Projected;
	bool bHasCandidate = false;

	if (bStraightLine)
	{
		const FVector StepDirection = (TargetPoint - OriginLocation).GetSafeNormal();
		const float TravelDistance = (ClampedRange > 0.f)
				? FMath::Min(ClampedRange, DistanceToDesired)
				: DistanceToDesired;

		if (StepDirection.IsNearlyZero() || TravelDistance <= KINDA_SMALL_NUMBER)
		{
			if (NavSys->ProjectPointToNavigation(OriginLocation, Projected, NavProjectionExtent))
			{
				OutNavLocation = Projected.Location;
				bHasCandidate = true;
			}
		}
		else
		{
			const float StepSize = FMath::Max(50.f, NavProjectionExtent.Size2D() * 0.5f);
			float Traversed = 0.f;
			FVector FurthestValid = OriginLocation;

			while (Traversed <= TravelDistance)
			{
				const FVector Sample = OriginLocation + StepDirection * Traversed;
				if (NavSys->ProjectPointToNavigation(Sample, Projected, NavProjectionExtent))
				{
					FurthestValid = Projected.Location;
					bHasCandidate = true;
				}

				Traversed += StepSize;
			}

			if (NavSys->ProjectPointToNavigation(TargetPoint, Projected, NavProjectionExtent))
			{
				if (!bHasCandidate ||
				    FVector::DistSquared(OriginLocation, Projected.Location) >
				    FVector::DistSquared(OriginLocation, FurthestValid))
				{
					FurthestValid = Projected.Location;
					bHasCandidate = true;
				}
			}

			if (bHasCandidate)
			{
				OutNavLocation = FurthestValid;
			}
		}
	}
	else
	{
		AActor* PathContext = Pawn ? const_cast<APawn*>(Pawn) : nullptr;
		UNavigationPath* Path = NavSys->FindPathToLocationSynchronously(World, OriginLocation, TargetPoint, PathContext);
		if (Path && Path->IsValid() && Path->PathPoints.Num() > 1)
		{
			float Accumulated = 0.f;
			FVector Last = Path->PathPoints[0];
			OutNavLocation = Last;
			bHasCandidate = true;

			for (int32 Idx = 1; Idx < Path->PathPoints.Num(); ++Idx)
			{
				FVector Next = Path->PathPoints[Idx];
				const float Segment = FVector::Dist(Last, Next);

				if (ClampedRange > 0.f && Accumulated + Segment >= ClampedRange)
				{
					const float Remaining = ClampedRange - Accumulated;
					const float Alpha = (Segment > KINDA_SMALL_NUMBER) ? Remaining / Segment : 0.f;
					Next = FMath::Lerp(Last, Next, Alpha);
					Accumulated = ClampedRange;
					OutNavLocation = Next;
					break;
				}

				Accumulated += Segment;
				OutNavLocation = Next;
				Last = Next;
			}

			if (NavSys->ProjectPointToNavigation(OutNavLocation, Projected, NavProjectionExtent))
			{
				OutNavLocation = Projected.Location;
			}
			else
			{
				bHasCandidate = false;
			}
		}
	}

	if (!bHasCandidate)
	{
		if (!NavSys->ProjectPointToNavigation(TargetPoint, Projected, NavProjectionExtent))
		{
			return false;
		}

		OutNavLocation = Projected.Location;
	}

	if (ClampedRange > 0.f)
	{
		const FVector FromOrigin = OutNavLocation - OriginLocation;
		const float Dist = FromOrigin.Size();
		if (Dist > ClampedRange && FromOrigin.GetSafeNormal().IsNearlyZero() == false)
		{
			const FVector Adjusted = OriginLocation + FromOrigin.GetSafeNormal() * ClampedRange;
			if (NavSys->ProjectPointToNavigation(Adjusted, Projected, NavProjectionExtent))
			{
				OutNavLocation = Projected.Location;
			}
			else
			{
				OutNavLocation = Adjusted;
			}
		}
	}

	if (!ResolveGroundedTeleportLocation(WorldContextObject, OutNavLocation, Pawn, OutTeleportLocation, TraceHeight, TraceDepth, AdditionalOffset))
	{
		OutTeleportLocation = OutNavLocation;
	}

	if (Pawn)
	{
		if (const UCapsuleComponent* Capsule = Pawn->FindComponentByClass<UCapsuleComponent>())
		{
			const float CapsuleRadius = Capsule->GetScaledCapsuleRadius();
			const float CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
			if (CapsuleRadius > KINDA_SMALL_NUMBER && CapsuleHalfHeight > KINDA_SMALL_NUMBER)
			{
				if (UWorld* OverlapWorld = World)
				{
					const FCollisionShape CapsuleShape = FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight);
					FCollisionQueryParams Params(SCENE_QUERY_STAT(BlinkSafeCheck), false, Pawn);
					Params.AddIgnoredActor(Pawn);

					FCollisionObjectQueryParams ObjectParams;
					ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
					ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
					ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);
					ObjectParams.AddObjectTypesToQuery(ECC_Pawn);

					const bool bOverlaps = OverlapWorld->OverlapAnyTestByObjectType(
						OutTeleportLocation,
						FQuat::Identity,
						ObjectParams,
						CapsuleShape,
						Params);

					if (bOverlaps)
					{
						OutNavLocation = FVector::ZeroVector;
						OutTeleportLocation = FVector::ZeroVector;
						return false;
					}
				}
			}
		}
	}

	return true;
}
bool UMouseNavBlueprintLibrary::ResolveGroundedTeleportLocation(
        const UObject* WorldContextObject,
        const FVector& NavLocation,
        const APawn*   Pawn,
        FVector&       OutTeleportLocation,
        float          TraceHeight,
        float          TraceDepth,
        float          AdditionalOffset)
{
	OutTeleportLocation = NavLocation;

	if (!WorldContextObject) { return false; }

	UWorld* World = WorldContextObject->GetWorld();
	if (!World) { return false; }

	TraceHeight = FMath::Max(TraceHeight, 0.f);
	TraceDepth = FMath::Max(TraceDepth, 0.f);

	float CapsuleRadius = 0.f;
	float CapsuleHalfHeight = 0.f;
	if (Pawn)
	{
		if (const UCapsuleComponent* Capsule = Pawn->FindComponentByClass<UCapsuleComponent>())
		{
			CapsuleRadius = Capsule->GetScaledCapsuleRadius();
			CapsuleHalfHeight = Capsule->GetScaledCapsuleHalfHeight();
		}
		else
		{
			FVector Origin;
			FVector Extents;
			Pawn->GetActorBounds(true, Origin, Extents);
			CapsuleRadius = FMath::Max(Extents.X, Extents.Y);
			CapsuleHalfHeight = Extents.Z;
		}
	}

	const bool bHasCapsule = CapsuleRadius > KINDA_SMALL_NUMBER && CapsuleHalfHeight > KINDA_SMALL_NUMBER;

	// Default hover keeps capsule lifted above the surface to avoid post-teleport depenetration pushing downwards.
	const float DefaultHover = bHasCapsule ? (CapsuleHalfHeight * 0.25f + 10.f) : 10.f;
	AdditionalOffset = FMath::Max(AdditionalOffset, DefaultHover);

	// Start the sweep high enough so the capsule is clear of the ground even on uneven surfaces.
	const FVector UpVector = FVector::UpVector;
	const float ExtraHeadroom = bHasCapsule ? CapsuleHalfHeight * 0.5f + AdditionalOffset : AdditionalOffset;
	const float EffectiveTraceHeight = TraceHeight + (bHasCapsule ? CapsuleHalfHeight : 0.f) + ExtraHeadroom;
	const float EffectiveTraceDepth = TraceDepth + (bHasCapsule ? CapsuleHalfHeight : 0.f) + AdditionalOffset;
	const FVector TraceStart = NavLocation + UpVector * EffectiveTraceHeight;
	const FVector TraceEnd = NavLocation - UpVector * EffectiveTraceDepth;

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(ResolveGroundedTeleportLocation), false, Pawn);
	QueryParams.bTraceComplex = false;
	QueryParams.AddIgnoredActor(Pawn);

	// Look for world geometry only; ignoring pawns prevents enemy capsules from stealing the hit.
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_WorldStatic);
	ObjectParams.AddObjectTypesToQuery(ECC_WorldDynamic);
	ObjectParams.AddObjectTypesToQuery(ECC_PhysicsBody);

	FHitResult GroundHit;
	bool bHitGround = false;

	if (bHasCapsule)
	{
		const FCollisionShape CapsuleShape = FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight);
		bHitGround = World->SweepSingleByObjectType(
			GroundHit,
			TraceStart,
			TraceEnd,
			FQuat::Identity,
			ObjectParams,
			CapsuleShape,
			QueryParams);
	}
	else
	{
		bHitGround = World->LineTraceSingleByObjectType(GroundHit, TraceStart, TraceEnd, ObjectParams, QueryParams);
	}

	if (!bHitGround || !GroundHit.IsValidBlockingHit())
	{
		// Fallback to visibility trace in case the project uses custom object channels for terrain.
		if (bHasCapsule)
		{
			const FCollisionShape CapsuleShape = FCollisionShape::MakeCapsule(CapsuleRadius, CapsuleHalfHeight);
			bHitGround = World->SweepSingleByChannel(
				GroundHit,
				TraceStart,
				TraceEnd,
				FQuat::Identity,
				ECC_Visibility,
				CapsuleShape,
				QueryParams);
		}
		else
		{
			bHitGround = World->LineTraceSingleByChannel(GroundHit, TraceStart, TraceEnd, ECC_Visibility, QueryParams);
		}

		if (!bHitGround || !GroundHit.IsValidBlockingHit())
		{
			return false;
		}
	}

	if (GroundHit.bStartPenetrating)
	{
		const FVector ResolveOffset = GroundHit.Normal * (GroundHit.PenetrationDepth + AdditionalOffset);
		OutTeleportLocation = GroundHit.TraceStart + ResolveOffset;
	}
	else if (bHasCapsule)
	{
		// Sweep hit gives the capsule centre at impact; lifting avoids post-teleport penetration and floor seams.
		OutTeleportLocation = GroundHit.Location + UpVector * AdditionalOffset;
	}
	else
	{
		OutTeleportLocation = FVector(NavLocation.X, NavLocation.Y, GroundHit.ImpactPoint.Z + AdditionalOffset);
	}

	OutTeleportLocation.X = NavLocation.X;
	OutTeleportLocation.Y = NavLocation.Y;

	return true;
}




