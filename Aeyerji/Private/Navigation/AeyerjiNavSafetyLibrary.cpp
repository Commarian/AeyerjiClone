#include "Navigation/AeyerjiNavSafetyLibrary.h"

#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PawnMovementComponent.h"
#include "MouseNavBlueprintLibrary.h"
#include "NavigationSystem.h"

namespace
{
	bool AeyerjiNavSafety_IsNearlyZeroExtent(const FVector& Extent)
	{
		return Extent.X <= KINDA_SMALL_NUMBER
			|| Extent.Y <= KINDA_SMALL_NUMBER
			|| Extent.Z <= KINDA_SMALL_NUMBER;
	}

	bool AeyerjiNavSafety_TryResolveCandidate(
		const UObject* WorldContextObject,
		const FVector& Candidate,
		const APawn* Pawn,
		const FAeyerjiNavSafetyResolveParams& Params,
		UNavigationSystemV1* NavSys,
		const FVector& ProjectionExtent,
		FAeyerjiNavSafetyResult& OutResult)
	{
		FNavLocation ProjectedLocation;
		if (!NavSys->ProjectPointToNavigation(Candidate, ProjectedLocation, ProjectionExtent))
		{
			OutResult.FailureReason = TEXT("ProjectPointToNavigationFailed");
			return false;
		}

		FVector GroundedLocation = ProjectedLocation.Location;
		if (!UMouseNavBlueprintLibrary::ResolveGroundedTeleportLocation(
				WorldContextObject,
				ProjectedLocation.Location,
				Pawn,
				GroundedLocation,
				Params.GroundTraceHeight,
				Params.GroundTraceDepth,
				Params.AdditionalGroundOffset))
		{
			OutResult.FailureReason = TEXT("GroundingFailed");
			return false;
		}

		if (Params.bRequireClearLocation
			&& !UMouseNavBlueprintLibrary::IsTeleportLocationClear(
				WorldContextObject,
				GroundedLocation,
				Pawn,
				Params.CapsuleInflation))
		{
			OutResult.FailureReason = TEXT("LocationBlocked");
			return false;
		}

		OutResult.bSuccess = true;
		OutResult.NavLocation = ProjectedLocation.Location;
		OutResult.GroundedLocation = GroundedLocation;
		OutResult.FailureReason = NAME_None;
		return true;
	}

	bool AeyerjiNavSafety_TryResolveNavGroundCandidate(
		const FVector& Candidate,
		UNavigationSystemV1* NavSys,
		const FVector& ProjectionExtent,
		const FAeyerjiNavSafetyResolveParams& Params,
		FAeyerjiNavSafetyResult& OutResult)
	{
		FNavLocation ProjectedLocation;
		if (!NavSys->ProjectPointToNavigation(Candidate, ProjectedLocation, ProjectionExtent))
		{
			OutResult.FailureReason = TEXT("ProjectPointToNavigationFailed");
			return false;
		}

		OutResult.bSuccess = true;
		OutResult.NavLocation = ProjectedLocation.Location;
		OutResult.GroundedLocation = ProjectedLocation.Location + FVector(0.f, 0.f, FMath::Max(0.f, Params.AdditionalGroundOffset));
		OutResult.FailureReason = NAME_None;
		return true;
	}

	void AeyerjiNavSafety_StopPawnMovement(APawn* Pawn)
	{
		if (!Pawn)
		{
			return;
		}

		if (AController* Controller = Pawn->GetController())
		{
			Controller->StopMovement();
		}

		if (ACharacter* Character = Cast<ACharacter>(Pawn))
		{
			if (UCharacterMovementComponent* MovementComponent = Character->GetCharacterMovement())
			{
				MovementComponent->StopMovementImmediately();
				MovementComponent->UpdateComponentVelocity();
			}
		}
		else if (UPawnMovementComponent* MovementComponent = Pawn->GetMovementComponent())
		{
			MovementComponent->StopMovementImmediately();
			MovementComponent->UpdateComponentVelocity();
		}
	}
}

FVector UAeyerjiNavSafetyLibrary::ResolveProjectionExtentForPawn(const APawn* Pawn, const FVector& ConfiguredExtent)
{
	FVector ResolvedExtent = ConfiguredExtent;
	if (AeyerjiNavSafety_IsNearlyZeroExtent(ResolvedExtent))
	{
		ResolvedExtent = FVector(200.f, 200.f, 500.f);
	}

	if (!Pawn)
	{
		return ResolvedExtent;
	}

	float Radius = 0.f;
	float HalfHeight = 0.f;
	if (const UCapsuleComponent* Capsule = Pawn->FindComponentByClass<UCapsuleComponent>())
	{
		Radius = Capsule->GetScaledCapsuleRadius();
		HalfHeight = Capsule->GetScaledCapsuleHalfHeight();
	}
	else
	{
		Radius = Pawn->GetSimpleCollisionRadius();
		HalfHeight = Pawn->GetSimpleCollisionHalfHeight();
	}

	if (Radius > 0.f)
	{
		ResolvedExtent.X = FMath::Max(ResolvedExtent.X, Radius + 80.f);
		ResolvedExtent.Y = FMath::Max(ResolvedExtent.Y, Radius + 80.f);
	}

	if (HalfHeight > 0.f)
	{
		ResolvedExtent.Z = FMath::Max(ResolvedExtent.Z, HalfHeight + 250.f);
	}

	return ResolvedExtent;
}

bool UAeyerjiNavSafetyLibrary::ResolveSafeNavLocationForPawn(
	const UObject* WorldContextObject,
	const FVector& DesiredLocation,
	const APawn* Pawn,
	const FAeyerjiNavSafetyResolveParams& Params,
	FAeyerjiNavSafetyResult& OutResult)
{
	OutResult = FAeyerjiNavSafetyResult();
	OutResult.RequestedLocation = DesiredLocation;

	const UObject* ResolvedContext = WorldContextObject ? WorldContextObject : static_cast<const UObject*>(Pawn);
	UWorld* World = ResolvedContext ? ResolvedContext->GetWorld() : nullptr;
	if (!World)
	{
		OutResult.FailureReason = TEXT("MissingWorld");
		return false;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys || !NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate))
	{
		OutResult.FailureReason = TEXT("MissingNavigationSystem");
		return false;
	}

	const FVector ProjectionExtent = ResolveProjectionExtentForPawn(Pawn, Params.ProjectionExtent);
	if (AeyerjiNavSafety_TryResolveCandidate(ResolvedContext, DesiredLocation, Pawn, Params, NavSys, ProjectionExtent, OutResult))
	{
		return true;
	}

	const float SearchRadius = FMath::Max(0.f, Params.SearchRadius);
	const float SearchStep = FMath::Max(50.f, Params.SearchStep);
	const int32 RingCount = SearchRadius > 0.f ? FMath::Max(1, FMath::CeilToInt(SearchRadius / SearchStep)) : 0;
	for (int32 RingIndex = 1; RingIndex <= RingCount; ++RingIndex)
	{
		const float Radius = FMath::Min(SearchRadius, SearchStep * RingIndex);
		const int32 SampleCount = FMath::Max(8, RingIndex * 8);
		for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
		{
			const float Angle = (2.f * PI * SampleIndex) / static_cast<float>(SampleCount);
			const FVector Offset(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
			if (AeyerjiNavSafety_TryResolveCandidate(ResolvedContext, DesiredLocation + Offset, Pawn, Params, NavSys, ProjectionExtent, OutResult))
			{
				return true;
			}
		}
	}

	if (SearchRadius > 0.f)
	{
		FNavLocation RandomNavLocation;
		if (NavSys->GetRandomPointInNavigableRadius(DesiredLocation, SearchRadius, RandomNavLocation)
			&& AeyerjiNavSafety_TryResolveCandidate(ResolvedContext, RandomNavLocation.Location, Pawn, Params, NavSys, ProjectionExtent, OutResult))
		{
			return true;
		}
	}

	return false;
}

bool UAeyerjiNavSafetyLibrary::ResolveNearestNavGroundLocation(
	const UObject* WorldContextObject,
	const FVector& DesiredLocation,
	const FAeyerjiNavSafetyResolveParams& Params,
	FAeyerjiNavSafetyResult& OutResult)
{
	OutResult = FAeyerjiNavSafetyResult();
	OutResult.RequestedLocation = DesiredLocation;

	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World)
	{
		OutResult.FailureReason = TEXT("MissingWorld");
		return false;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys || !NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate))
	{
		OutResult.FailureReason = TEXT("MissingNavigationSystem");
		return false;
	}

	const FVector ProjectionExtent = ResolveProjectionExtentForPawn(nullptr, Params.ProjectionExtent);
	if (AeyerjiNavSafety_TryResolveNavGroundCandidate(DesiredLocation, NavSys, ProjectionExtent, Params, OutResult))
	{
		return true;
	}

	const float SearchRadius = FMath::Max(0.f, Params.SearchRadius);
	const float SearchStep = FMath::Max(50.f, Params.SearchStep);
	const int32 RingCount = SearchRadius > 0.f ? FMath::Max(1, FMath::CeilToInt(SearchRadius / SearchStep)) : 0;
	for (int32 RingIndex = 1; RingIndex <= RingCount; ++RingIndex)
	{
		const float Radius = FMath::Min(SearchRadius, SearchStep * RingIndex);
		const int32 SampleCount = FMath::Max(8, RingIndex * 8);
		for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
		{
			const float Angle = (2.f * PI * SampleIndex) / static_cast<float>(SampleCount);
			const FVector Offset(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
			if (AeyerjiNavSafety_TryResolveNavGroundCandidate(DesiredLocation + Offset, NavSys, ProjectionExtent, Params, OutResult))
			{
				return true;
			}
		}
	}

	if (SearchRadius > 0.f)
	{
		FNavLocation RandomNavLocation;
		if (NavSys->GetRandomPointInNavigableRadius(DesiredLocation, SearchRadius, RandomNavLocation)
			&& AeyerjiNavSafety_TryResolveNavGroundCandidate(RandomNavLocation.Location, NavSys, ProjectionExtent, Params, OutResult))
		{
			return true;
		}
	}

	return false;
}

bool UAeyerjiNavSafetyLibrary::IsPawnOnUsableNav(
	APawn* Pawn,
	const FAeyerjiNavSafetyResolveParams& Params,
	FAeyerjiNavSafetyResult& OutResult)
{
	OutResult = FAeyerjiNavSafetyResult();
	if (!Pawn)
	{
		OutResult.FailureReason = TEXT("MissingPawn");
		return false;
	}

	UWorld* World = Pawn->GetWorld();
	if (!World)
	{
		OutResult.FailureReason = TEXT("MissingWorld");
		return false;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys || !NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate))
	{
		OutResult.FailureReason = TEXT("MissingNavigationSystem");
		return false;
	}

	const FVector PawnLocation = Pawn->GetActorLocation();
	OutResult.RequestedLocation = PawnLocation;

	const FVector ProjectionExtent = ResolveProjectionExtentForPawn(Pawn, Params.ProjectionExtent);
	FNavLocation ProjectedLocation;
	if (!NavSys->ProjectPointToNavigation(PawnLocation, ProjectedLocation, ProjectionExtent))
	{
		OutResult.FailureReason = TEXT("CurrentLocationNotProjected");
		return false;
	}

	if (FVector::DistSquared2D(PawnLocation, ProjectedLocation.Location) > FMath::Square(FMath::Max(0.f, Params.MaxCurrentProjection2D)))
	{
		OutResult.NavLocation = ProjectedLocation.Location;
		OutResult.FailureReason = TEXT("CurrentLocationTooFarFromNav");
		return false;
	}

	FVector GroundedLocation = ProjectedLocation.Location;
	if (!UMouseNavBlueprintLibrary::ResolveGroundedTeleportLocation(
			Pawn,
			ProjectedLocation.Location,
			Pawn,
			GroundedLocation,
			Params.GroundTraceHeight,
			Params.GroundTraceDepth,
			Params.AdditionalGroundOffset))
	{
		OutResult.NavLocation = ProjectedLocation.Location;
		OutResult.FailureReason = TEXT("CurrentLocationGroundingFailed");
		return false;
	}

	OutResult.bSuccess = true;
	OutResult.NavLocation = ProjectedLocation.Location;
	OutResult.GroundedLocation = GroundedLocation;
	OutResult.FailureReason = NAME_None;
	return true;
}

bool UAeyerjiNavSafetyLibrary::EnsurePawnOnSafeNav(
	APawn* Pawn,
	const FAeyerjiNavSafetyResolveParams& Params,
	const bool bRecoverIfOffNav,
	FVector& OutSafeLocation)
{
	OutSafeLocation = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;

	FAeyerjiNavSafetyResult CurrentResult;
	if (IsPawnOnUsableNav(Pawn, Params, CurrentResult))
	{
		OutSafeLocation = CurrentResult.GroundedLocation;
		return true;
	}

	if (!bRecoverIfOffNav)
	{
		return false;
	}

	return RecoverPawnToSafeNav(
		Pawn,
		Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector,
		FVector::ZeroVector,
		/*bUseLastKnownSafeLocation=*/false,
		Params,
		OutSafeLocation);
}

bool UAeyerjiNavSafetyLibrary::RecoverPawnToSafeNav(
	APawn* Pawn,
	const FVector& PreferredLocation,
	const FVector& LastKnownSafeLocation,
	const bool bUseLastKnownSafeLocation,
	const FAeyerjiNavSafetyResolveParams& Params,
	FVector& OutRecoveredLocation)
{
	OutRecoveredLocation = Pawn ? Pawn->GetActorLocation() : FVector::ZeroVector;
	if (!Pawn || !Pawn->HasAuthority())
	{
		return false;
	}

	FAeyerjiNavSafetyResult ResolveResult;
	bool bResolved = ResolveSafeNavLocationForPawn(Pawn, PreferredLocation, Pawn, Params, ResolveResult);
	if (!bResolved && bUseLastKnownSafeLocation)
	{
		bResolved = ResolveSafeNavLocationForPawn(Pawn, LastKnownSafeLocation, Pawn, Params, ResolveResult);
	}

	if (!bResolved)
	{
		return false;
	}

	AeyerjiNavSafety_StopPawnMovement(Pawn);
	const bool bMoved = Pawn->SetActorLocation(
		ResolveResult.GroundedLocation,
		/*bSweep=*/false,
		nullptr,
		ETeleportType::TeleportPhysics);

	if (!bMoved)
	{
		return false;
	}

	OutRecoveredLocation = ResolveResult.GroundedLocation;
	Pawn->ForceNetUpdate();
	if (AController* Controller = Pawn->GetController())
	{
		Controller->ForceNetUpdate();
	}

	return true;
}
