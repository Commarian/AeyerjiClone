#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AeyerjiNavSafetyLibrary.generated.h"

class APawn;

USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiNavSafetyResolveParams
{
	GENERATED_BODY()

	/** Search extents used when projecting desired points to the navigation mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Navigation")
	FVector ProjectionExtent = FVector(200.f, 200.f, 500.f);

	/** Maximum distance around the desired point used when the direct projection fails. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Navigation", meta=(ClampMin="0.0", Units="cm"))
	float SearchRadius = 600.f;

	/** Distance between radial recovery rings while looking for nearby nav. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Navigation", meta=(ClampMin="1.0", Units="cm"))
	float SearchStep = 150.f;

	/** Additional upward sweep distance used while grounding the projected nav point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Navigation", meta=(ClampMin="0.0", Units="cm"))
	float GroundTraceHeight = 300.f;

	/** Additional downward sweep distance used while grounding the projected nav point. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Navigation", meta=(ClampMin="0.0", Units="cm"))
	float GroundTraceDepth = 500.f;

	/** Extra upward clearance applied after grounding to avoid post-teleport penetration. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Navigation", meta=(ClampMin="0.0", Units="cm"))
	float AdditionalGroundOffset = 2.f;

	/** Extra capsule inflation used when checking whether the resolved spot is clear. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Navigation", meta=(ClampMin="0.0", Units="cm"))
	float CapsuleInflation = 0.f;

	/** Maximum 2D delta between the pawn and its nav projection before the pawn is considered off-nav. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Navigation", meta=(ClampMin="0.0", Units="cm"))
	float MaxCurrentProjection2D = 120.f;

	/** When true, the grounded capsule location must be collision-clear for the pawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Navigation")
	bool bRequireClearLocation = true;
};

USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiNavSafetyResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Navigation")
	bool bSuccess = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Navigation")
	FVector RequestedLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Navigation")
	FVector NavLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Navigation")
	FVector GroundedLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Navigation")
	FName FailureReason = NAME_None;
};

/**
 * Shared navigation safety helpers for player and enemy pawns.
 */
UCLASS()
class AEYERJI_API UAeyerjiNavSafetyLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Resolves a desired point to a nav-projected, grounded, collision-clear pawn location. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Navigation", meta=(WorldContext="WorldContextObject"))
	static bool ResolveSafeNavLocationForPawn(
		const UObject* WorldContextObject,
		const FVector& DesiredLocation,
		const APawn* Pawn,
		const FAeyerjiNavSafetyResolveParams& Params,
		FAeyerjiNavSafetyResult& OutResult);

	/** Resolves arbitrary actors/items to the nearest nav-mesh point without requiring pawn capsule clearance. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Navigation", meta=(WorldContext="WorldContextObject"))
	static bool ResolveNearestNavGroundLocation(
		const UObject* WorldContextObject,
		const FVector& DesiredLocation,
		const FAeyerjiNavSafetyResolveParams& Params,
		FAeyerjiNavSafetyResult& OutResult);

	/** Returns true when the pawn is currently close enough to usable nav and can be grounded safely. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Navigation")
	static bool IsPawnOnUsableNav(
		APawn* Pawn,
		const FAeyerjiNavSafetyResolveParams& Params,
		FAeyerjiNavSafetyResult& OutResult);

	/** Ensures the pawn is on nav, optionally recovering it to the nearest safe location. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Navigation")
	static bool EnsurePawnOnSafeNav(
		APawn* Pawn,
		const FAeyerjiNavSafetyResolveParams& Params,
		bool bRecoverIfOffNav,
		FVector& OutSafeLocation);

	/** Teleports an authority-owned pawn to nearby nav, preferring the supplied last-known safe fallback when needed. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Navigation")
	static bool RecoverPawnToSafeNav(
		APawn* Pawn,
		const FVector& PreferredLocation,
		const FVector& LastKnownSafeLocation,
		bool bUseLastKnownSafeLocation,
		const FAeyerjiNavSafetyResolveParams& Params,
		FVector& OutRecoveredLocation);

	/** Builds projection extents large enough for the pawn capsule while preserving configured minimums. */
	static FVector ResolveProjectionExtentForPawn(const APawn* Pawn, const FVector& ConfiguredExtent);
};
