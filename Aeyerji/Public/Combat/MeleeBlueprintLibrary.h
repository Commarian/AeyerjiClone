//  MeleeBlueprintLibrary.h
//  Utility Blueprint library for cone-based melee queries in UE-5.6
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "Engine/EngineTypes.h"

#include "MeleeBlueprintLibrary.generated.h"

/**
 * Static helpers for melee attacks (cone-shaped hit tests, filtering, etc.).
 * Use these nodes in your Gameplay Ability’s ActivateAbility graph instead
 * of re-building traces every time.
 */
UCLASS()
class AEYERJI_API UMeleeBlueprintLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:

	/**
	 * Perform a sphere trace forward from `Origin` and keep only the actors that
	 * fall inside a half-angle cone (dot-product test).
	 *
	 * @param WorldContextObject	Usually *Self* in Blueprint. Needed for world-access.
	 * @param Origin				Start point of the attack (typically Mesh socket or Actor location).
	 * @param ForwardVector			Direction the cone faces (use Character→GetActorForwardVector()).
	 * @param Range					How far the attack can reach in world units (centimetres).
	 * @param HalfAngleDeg			Half of the cone angle, in degrees (e.g. 45 gives a 90° cone).
	 * @param ObjectTypes			Objects that can be hit (Pawn, PhysicsBody, etc.).
	 * @param ActorsToIgnore		Usually “Self”; any actors you want ignored during the trace.
	 * @param OutHitActors			Filled with the unique actors that passed both the trace and cone check.
	 * @param bDrawDebug			Optional one-frame debug lines / spheres.
	 *
	 * @return **true** if at least one actor was added to OutHitActors.
	 */
	UFUNCTION(BlueprintCallable, Category="Combat|Melee",
			  meta=(WorldContext="WorldContextObject",
					DisplayName="Get Actors In Melee Cone",
					ExpandBoolAsExecs="ReturnValue"))
	static bool GetActorsInMeleeCone(const UObject*					 WorldContextObject,
									 const FVector&					 Origin,
									 const FVector&					 ForwardVector,
									 float							 Range,
									 float							 HalfAngleDeg,
									 const TArray<TEnumAsByte<EObjectTypeQuery>>& ObjectTypes,
									 const TArray<AActor*>&			 ActorsToIgnore,
									 TArray<AActor*>&				 OutHitActors,
									 bool							 bDrawDebug = false);
};
