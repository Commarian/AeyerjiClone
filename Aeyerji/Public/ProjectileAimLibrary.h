// Fill out your copyright notice in the Description page of Project Settings.

//  ProjectileAimLibrary.h
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "ProjectileAimLibrary.generated.h"


/** Which aiming solution was chosen */
UENUM(BlueprintType)
enum class EAimResult : uint8
{
	StraightShot   UMETA(DisplayName="No Target"),            // fire straight ahead
	StationaryHit  UMETA(DisplayName="Stationary Target"),    // simple look-at
	PredictedHit   UMETA(DisplayName="Moving Target")         // lead computation
};

/**
 * Calculates a launch-velocity in world space for dumb projectiles.
 * Will pick the best of three solutions and expose it as exec pins.
 *
 *   • StraightShot  – no target actor (player just shoots forward)
 *   • StationaryHit – target exists but its velocity ≈ 0
 *   • PredictedHit  – target moving → solves lead equation
 *
 * @param  MuzzleLocation          World position of the spawn point
 * @param  MuzzleForward           Forward vector to use when StraightShot
 * @param  TargetActor             May be nullptr
 * @param  ProjectileSpeed         Units / second (read from attribute)
 * @param  ExtraLeadSeconds        Added on top of the computed time-to-impact
 * @param  OutLaunchVelocity       (World-space) vector to feed into SetVelocityInWorldSpace
 *
 * @return Exec-enum so Blueprint shows three separate white pins
 */
UCLASS()
class AEYERJI_API UProjectileAimLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category="Projectile",
			  meta=(WorldContext="WorldContextObject",
			  	ToolTip="Calculates a launch-velocity in world space for dumb projectiles.\nWill pick the best of three solutions and expose it as exec pins.\n• StraightShot  – no target actor (player just shoots forward)\n• StationaryHit – target exists but its velocity ≈ 0\n• PredictedHit  – target moving → solves lead equation\n@param  MuzzleLocation          World position of the spawn point\n@param  MuzzleForward           Forward vector to use when StraightShot\n@param  TargetActor             May be nullptr\n@param  ProjectileSpeed         Units / second (read from attribute)\n param  ExtraLeadSeconds        Added on top of the computed time-to-impact\n@param  OutLaunchVelocity       (World-space) vector to feed into SetVelocityInWorldSpace\n@return Exec-enum so Blueprint shows three separate white pins",
					ExpandEnumAsExecs="ReturnValue"))
	static EAimResult GetLaunchVelocity(
			const UObject* WorldContextObject,
			const FVector& MuzzleLocation,
			const FVector& MuzzleForward,
			AActor*        TargetActor,
			float          ProjectileSpeed,
			float          ExtraLeadSeconds,
			FVector&       OutLaunchVelocity);

	
	UFUNCTION(BlueprintCallable,
			  Category   = "Projectile",
			  DisplayName= "Set Velocity (World Space, With Prediction)",
			  meta=(WorldContext="WorldContextObject",
					ToolTip     ="Sets velocity in world space and optionally predicts a moving target.\nReturns true when a prediction was found; otherwise returns false and fires straight."))
	static bool LaunchProjectileTowards(
		UObject*                          WorldContextObject,

		/* ProjectileMovement you want to steer. Must be valid & Registered. */
		UProjectileMovementComponent*     ProjectileMov,

		/* Actor you want to hit.  Null = just fire forward. */
		const AActor*                     TargetActor,

		/* Speed to use when TargetActor is null (or still). */
		float                             InitialSpeed,

		/* How slow (|V| ≤ Tolerance) is considered “standing still”. */
		float                             StationaryTolerance,

		/* OUT: point that was actually aimed at (useful for debugging) */
		FVector&                          OutCalculatedAimPoint );
};
