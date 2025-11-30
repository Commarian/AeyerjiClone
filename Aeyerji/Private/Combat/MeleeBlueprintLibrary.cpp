//  MeleeBlueprintLibrary.cpp
#include "Combat/MeleeBlueprintLibrary.h"

#include "DrawDebugHelpers.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"

bool UMeleeBlueprintLibrary::GetActorsInMeleeCone(
		const UObject*					 WorldContextObject,
		const FVector&					 Origin,
		const FVector&					 ForwardVector,
		float							 Range,
		float							 HalfAngleDeg,
		const TArray<TEnumAsByte<EObjectTypeQuery>>& ObjectTypes,
		const TArray<AActor*>&			 ActorsToIgnore,
		TArray<AActor*>&					 OutHitActors,
		bool							 bDrawDebug)
{
	OutHitActors.Reset();

	if (!WorldContextObject || Range <= 0.f || HalfAngleDeg <= 0.f)
	{
		return false;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World) { return false; }

	/* ------------------------------------------------------------------ */
	/* 1. Sphere trace (broad phase)                                       */
	/* ------------------------------------------------------------------ */
	const FVector End = Origin + ForwardVector.GetSafeNormal() * Range;

	TArray<FHitResult> HitResults;
	const bool bTraceHit =
		UKismetSystemLibrary::SphereTraceMultiForObjects(
			World, Origin, End, Range, ObjectTypes,
			/*bTraceComplex=*/false, ActorsToIgnore,
			EDrawDebugTrace::None, HitResults,
			/*bIgnoreSelf=*/true);

	if (!bTraceHit)
	{
		return false;	// Nothing hit at all.
	}

	/* ------------------------------------------------------------------ */
	/* 2. Narrow phase: cone (dot-product) check                           */
	/* ------------------------------------------------------------------ */
	const float CosThreshold = FMath::Cos(FMath::DegreesToRadians(HalfAngleDeg));

	for (const FHitResult& Hit : HitResults)
	{
		AActor* HitActor = Hit.GetActor();
		if (!IsValid(HitActor)) { continue; }

		/* Ensure uniqueness */
		if (OutHitActors.Contains(HitActor)) { continue; }

		const FVector ToTarget = (HitActor->GetActorLocation() - Origin).GetSafeNormal();
		const float	 Dot	   = FVector::DotProduct(ForwardVector.GetSafeNormal(), ToTarget);

		if (Dot >= CosThreshold)
		{
			OutHitActors.Add(HitActor);
		}
	}

	/* ------------------------------------------------------------------ */
	/* 3. Optional debug draw                                              */
	/* ------------------------------------------------------------------ */
#if WITH_EDITOR
	if (bDrawDebug)
	{
		const FColor ConeColor = OutHitActors.Num() ? FColor::Green : FColor::Red;

		/* Draw cone outline (simple lines) */
		const int32   NumSegments = 16;
		const float   AngleStep   = (HalfAngleDeg * 2.f) / NumSegments;
		FVector PrevEdge;

		for (int32 i = 0; i <= NumSegments; ++i)
		{
			const float Angle = -HalfAngleDeg + i * AngleStep;
			const FVector Rotated = ForwardVector.RotateAngleAxis(Angle, FVector::UpVector).GetSafeNormal();
			const FVector EdgeEnd = Origin + Rotated * Range;

			DrawDebugLine(World, Origin, EdgeEnd, ConeColor, false, 0.1f, 0, 1.f);

			if (i > 0)
			{
				DrawDebugLine(World, PrevEdge, EdgeEnd, ConeColor, false, 0.1f, 0, 0.5f);
			}
			PrevEdge = EdgeEnd;
		}
	}
#endif

	return OutHitActors.Num() > 0;
}
