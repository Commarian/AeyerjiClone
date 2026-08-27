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
	constexpr float MaxNavDistance = 1000000.f;
	constexpr int32 MaxNavSearchRings = 64;
	constexpr int32 MaxNavSamplesPerRing = 128;

	float AeyerjiNavSafety_SafeValue(
		const float Value,
		const float DefaultValue,
		const float MinValue,
		const float MaxValue)
	{
		return FMath::Clamp(FMath::IsFinite(Value) ? Value : DefaultValue, MinValue, MaxValue);
	}

	bool AeyerjiNavSafety_IsFiniteVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	FAeyerjiNavSafetyResolveParams AeyerjiNavSafety_SanitizeParams(const FAeyerjiNavSafetyResolveParams& Params)
	{
		FAeyerjiNavSafetyResolveParams Result = Params;
		Result.ProjectionExtent.X = AeyerjiNavSafety_SafeValue(FMath::Abs(Result.ProjectionExtent.X), 200.f, 1.f, MaxNavDistance);
		Result.ProjectionExtent.Y = AeyerjiNavSafety_SafeValue(FMath::Abs(Result.ProjectionExtent.Y), 200.f, 1.f, MaxNavDistance);
		Result.ProjectionExtent.Z = AeyerjiNavSafety_SafeValue(FMath::Abs(Result.ProjectionExtent.Z), 500.f, 1.f, MaxNavDistance);
		Result.SearchRadius = AeyerjiNavSafety_SafeValue(Result.SearchRadius, 600.f, 0.f, MaxNavDistance);
		Result.SearchStep = AeyerjiNavSafety_SafeValue(Result.SearchStep, 150.f, 50.f, MaxNavDistance);
		Result.GroundTraceHeight = AeyerjiNavSafety_SafeValue(Result.GroundTraceHeight, 300.f, 0.f, MaxNavDistance);
		Result.GroundTraceDepth = AeyerjiNavSafety_SafeValue(Result.GroundTraceDepth, 500.f, 0.f, MaxNavDistance);
		Result.AdditionalGroundOffset = AeyerjiNavSafety_SafeValue(Result.AdditionalGroundOffset, 2.f, 0.f, 100000.f);
		Result.CapsuleInflation = AeyerjiNavSafety_SafeValue(Result.CapsuleInflation, 0.f, 0.f, 100000.f);
		Result.MaxCurrentProjection2D = AeyerjiNavSafety_SafeValue(Result.MaxCurrentProjection2D, 120.f, 0.f, MaxNavDistance);
		return Result;
	}

	bool AeyerjiNavSafety_IsNearlyZeroExtent(const FVector& Extent)
	{
		return !AeyerjiNavSafety_IsFiniteVector(Extent)
			|| Extent.X <= KINDA_SMALL_NUMBER
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
		if (!NavSys || !AeyerjiNavSafety_IsFiniteVector(Candidate))
		{
			OutResult.FailureReason = TEXT("InvalidCandidate");
			return false;
		}

		FNavLocation ProjectedLocation;
		if (!NavSys->ProjectPointToNavigation(Candidate, ProjectedLocation, ProjectionExtent))
		{
			OutResult.FailureReason = TEXT("ProjectPointToNavigationFailed");
			return false;
		}
		if (!AeyerjiNavSafety_IsFiniteVector(ProjectedLocation.Location))
		{
			OutResult.FailureReason = TEXT("InvalidProjectedLocation");
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
		if (!AeyerjiNavSafety_IsFiniteVector(GroundedLocation))
		{
			OutResult.FailureReason = TEXT("InvalidGroundedLocation");
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
		if (!NavSys || !AeyerjiNavSafety_IsFiniteVector(Candidate))
		{
			OutResult.FailureReason = TEXT("InvalidCandidate");
			return false;
		}

		FNavLocation ProjectedLocation;
		if (!NavSys->ProjectPointToNavigation(Candidate, ProjectedLocation, ProjectionExtent))
		{
			OutResult.FailureReason = TEXT("ProjectPointToNavigationFailed");
			return false;
		}
		if (!AeyerjiNavSafety_IsFiniteVector(ProjectedLocation.Location))
		{
			OutResult.FailureReason = TEXT("InvalidProjectedLocation");
			return false;
		}

		OutResult.bSuccess = true;
		OutResult.NavLocation = ProjectedLocation.Location;
		OutResult.GroundedLocation = ProjectedLocation.Location + FVector(0.f, 0.f, Params.AdditionalGroundOffset);
		if (!AeyerjiNavSafety_IsFiniteVector(OutResult.GroundedLocation))
		{
			OutResult.bSuccess = false;
			OutResult.FailureReason = TEXT("InvalidGroundedLocation");
			return false;
		}
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
	FAeyerjiNavSafetyResolveParams ExtentParams;
	ExtentParams.ProjectionExtent = ConfiguredExtent;
	FVector ResolvedExtent = AeyerjiNavSafety_SanitizeParams(ExtentParams).ProjectionExtent;

	if (!IsValid(Pawn))
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

	Radius = AeyerjiNavSafety_SafeValue(Radius, 0.f, 0.f, MaxNavDistance);
	HalfHeight = AeyerjiNavSafety_SafeValue(HalfHeight, 0.f, 0.f, MaxNavDistance);
	if (Radius > 0.f)
	{
		ResolvedExtent.X = FMath::Max(ResolvedExtent.X, Radius + 80.f);
		ResolvedExtent.Y = FMath::Max(ResolvedExtent.Y, Radius + 80.f);
	}

	if (HalfHeight > 0.f)
	{
		ResolvedExtent.Z = FMath::Max(ResolvedExtent.Z, HalfHeight + 250.f);
	}
	ResolvedExtent.X = FMath::Min(ResolvedExtent.X, MaxNavDistance);
	ResolvedExtent.Y = FMath::Min(ResolvedExtent.Y, MaxNavDistance);
	ResolvedExtent.Z = FMath::Min(ResolvedExtent.Z, MaxNavDistance);

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
	OutResult.RequestedLocation = AeyerjiNavSafety_IsFiniteVector(DesiredLocation)
		? DesiredLocation
		: FVector::ZeroVector;

	const UObject* ResolvedContext = WorldContextObject ? WorldContextObject : static_cast<const UObject*>(Pawn);
	UWorld* World = ResolvedContext ? ResolvedContext->GetWorld() : nullptr;
	if (!World || !AeyerjiNavSafety_IsFiniteVector(DesiredLocation)
		|| !IsValid(Pawn) || Pawn->GetWorld() != World)
	{
		OutResult.FailureReason = World ? TEXT("InvalidRequest") : TEXT("MissingWorld");
		return false;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys || !NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate))
	{
		OutResult.FailureReason = TEXT("MissingNavigationSystem");
		return false;
	}

	const FAeyerjiNavSafetyResolveParams SafeParams = AeyerjiNavSafety_SanitizeParams(Params);
	const FVector ProjectionExtent = ResolveProjectionExtentForPawn(Pawn, SafeParams.ProjectionExtent);
	if (AeyerjiNavSafety_TryResolveCandidate(ResolvedContext, DesiredLocation, Pawn, SafeParams, NavSys, ProjectionExtent, OutResult))
	{
		return true;
	}

	const float SearchRadius = SafeParams.SearchRadius;
	const float SearchStep = SafeParams.SearchStep;
	const int32 RingCount = SearchRadius > 0.f
		? FMath::Clamp(FMath::CeilToInt(FMath::Min(
			SearchRadius / SearchStep, static_cast<float>(MaxNavSearchRings))), 1, MaxNavSearchRings)
		: 0;
	for (int32 RingIndex = 1; RingIndex <= RingCount; ++RingIndex)
	{
		const float Radius = SearchRadius * static_cast<float>(RingIndex) / static_cast<float>(RingCount);
		const int32 SampleCount = FMath::Clamp(RingIndex * 8, 8, MaxNavSamplesPerRing);
		for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
		{
			const float Angle = (2.f * PI * SampleIndex) / static_cast<float>(SampleCount);
			const FVector Offset(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
			if (AeyerjiNavSafety_TryResolveCandidate(ResolvedContext, DesiredLocation + Offset, Pawn, SafeParams, NavSys, ProjectionExtent, OutResult))
			{
				return true;
			}
		}
	}

	if (SearchRadius > 0.f)
	{
		FNavLocation RandomNavLocation;
		if (NavSys->GetRandomPointInNavigableRadius(DesiredLocation, SearchRadius, RandomNavLocation)
			&& AeyerjiNavSafety_TryResolveCandidate(ResolvedContext, RandomNavLocation.Location, Pawn, SafeParams, NavSys, ProjectionExtent, OutResult))
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
	OutResult.RequestedLocation = AeyerjiNavSafety_IsFiniteVector(DesiredLocation)
		? DesiredLocation
		: FVector::ZeroVector;

	UWorld* World = WorldContextObject ? WorldContextObject->GetWorld() : nullptr;
	if (!World || !AeyerjiNavSafety_IsFiniteVector(DesiredLocation))
	{
		OutResult.FailureReason = World ? TEXT("InvalidRequest") : TEXT("MissingWorld");
		return false;
	}

	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!NavSys || !NavSys->GetDefaultNavDataInstance(FNavigationSystem::DontCreate))
	{
		OutResult.FailureReason = TEXT("MissingNavigationSystem");
		return false;
	}

	const FAeyerjiNavSafetyResolveParams SafeParams = AeyerjiNavSafety_SanitizeParams(Params);
	const FVector ProjectionExtent = ResolveProjectionExtentForPawn(nullptr, SafeParams.ProjectionExtent);
	if (AeyerjiNavSafety_TryResolveNavGroundCandidate(DesiredLocation, NavSys, ProjectionExtent, SafeParams, OutResult))
	{
		return true;
	}

	const float SearchRadius = SafeParams.SearchRadius;
	const float SearchStep = SafeParams.SearchStep;
	const int32 RingCount = SearchRadius > 0.f
		? FMath::Clamp(FMath::CeilToInt(FMath::Min(
			SearchRadius / SearchStep, static_cast<float>(MaxNavSearchRings))), 1, MaxNavSearchRings)
		: 0;
	for (int32 RingIndex = 1; RingIndex <= RingCount; ++RingIndex)
	{
		const float Radius = SearchRadius * static_cast<float>(RingIndex) / static_cast<float>(RingCount);
		const int32 SampleCount = FMath::Clamp(RingIndex * 8, 8, MaxNavSamplesPerRing);
		for (int32 SampleIndex = 0; SampleIndex < SampleCount; ++SampleIndex)
		{
			const float Angle = (2.f * PI * SampleIndex) / static_cast<float>(SampleCount);
			const FVector Offset(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
			if (AeyerjiNavSafety_TryResolveNavGroundCandidate(DesiredLocation + Offset, NavSys, ProjectionExtent, SafeParams, OutResult))
			{
				return true;
			}
		}
	}

	if (SearchRadius > 0.f)
	{
		FNavLocation RandomNavLocation;
		if (NavSys->GetRandomPointInNavigableRadius(DesiredLocation, SearchRadius, RandomNavLocation)
			&& AeyerjiNavSafety_TryResolveNavGroundCandidate(RandomNavLocation.Location, NavSys, ProjectionExtent, SafeParams, OutResult))
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
	if (!IsValid(Pawn))
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
	if (!AeyerjiNavSafety_IsFiniteVector(PawnLocation))
	{
		OutResult.FailureReason = TEXT("InvalidPawnLocation");
		return false;
	}
	OutResult.RequestedLocation = PawnLocation;

	const FAeyerjiNavSafetyResolveParams SafeParams = AeyerjiNavSafety_SanitizeParams(Params);
	const FVector ProjectionExtent = ResolveProjectionExtentForPawn(Pawn, SafeParams.ProjectionExtent);
	FNavLocation ProjectedLocation;
	if (!NavSys->ProjectPointToNavigation(PawnLocation, ProjectedLocation, ProjectionExtent))
	{
		OutResult.FailureReason = TEXT("CurrentLocationNotProjected");
		return false;
	}

	if (!AeyerjiNavSafety_IsFiniteVector(ProjectedLocation.Location))
	{
		OutResult.FailureReason = TEXT("InvalidProjectedLocation");
		return false;
	}
	if (FVector::DistSquared2D(PawnLocation, ProjectedLocation.Location)
		> FMath::Square(SafeParams.MaxCurrentProjection2D))
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
			SafeParams.GroundTraceHeight,
			SafeParams.GroundTraceDepth,
			SafeParams.AdditionalGroundOffset))
	{
		OutResult.NavLocation = ProjectedLocation.Location;
		OutResult.FailureReason = TEXT("CurrentLocationGroundingFailed");
		return false;
	}
	if (!AeyerjiNavSafety_IsFiniteVector(GroundedLocation))
	{
		OutResult.NavLocation = ProjectedLocation.Location;
		OutResult.FailureReason = TEXT("InvalidGroundedLocation");
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
	OutSafeLocation = IsValid(Pawn) && AeyerjiNavSafety_IsFiniteVector(Pawn->GetActorLocation())
		? Pawn->GetActorLocation()
		: FVector::ZeroVector;

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
		OutSafeLocation,
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
	OutRecoveredLocation = IsValid(Pawn) && AeyerjiNavSafety_IsFiniteVector(Pawn->GetActorLocation())
		? Pawn->GetActorLocation()
		: FVector::ZeroVector;
	if (!IsValid(Pawn) || !Pawn->HasAuthority()
		|| !AeyerjiNavSafety_IsFiniteVector(PreferredLocation)
		|| (bUseLastKnownSafeLocation && !AeyerjiNavSafety_IsFiniteVector(LastKnownSafeLocation)))
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
	if (!AeyerjiNavSafety_IsFiniteVector(ResolveResult.GroundedLocation))
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
