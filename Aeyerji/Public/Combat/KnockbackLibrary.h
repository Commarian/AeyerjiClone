// KnockbackLibrary.h
#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameFramework/Character.h"
#include "KnockbackLibrary.generated.h"

UCLASS()
class AEYERJI_API UKnockbackLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Returns a unit vector randomly jittered around Forward by the given yaw/pitch (degrees).
	 *  If bFlattenToGround, the result is projected onto the XY plane and normalized.
	 */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Knockback")
	static FVector RandomizedKnockbackDirection(const FVector& Forward,
		float YawJitterDeg = 20.f,
		float PitchJitterDeg = 5.f,
		bool bFlattenToGround = true);

	/** Applies randomized knockback to Target, using Source's forward as the base direction.
	 *  Force = random in [MinForce, MaxForce]. Adds UpBoost to Z. Uses LaunchCharacter by default.
	 *  Run this on the server for authoritative physics/networking.
	 */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Knockback", meta=(WorldContext="WorldContextObject"))
	static void ApplyKnockback(UObject* WorldContextObject,
		AActor* Source,
		ACharacter* Target,
		float MinForce = 150.f,
		float MaxForce = 1500.f,
		float YawJitterDeg = 20.f,
		float PitchJitterDeg = 5.f,
		bool bFlattenToGround = true,
		float UpBoost = 120.f,
		bool bUseLaunchCharacter = true);
};
