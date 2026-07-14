#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "AeyerjiInteractable.generated.h"

class AAeyerjiPlayerController;

/**
 * Generic interaction contract for world actors that can be used by a player.
 * The player controller owns client input and the server revalidates before execution.
 */
UINTERFACE(BlueprintType)
class AEYERJI_API UAeyerjiInteractable : public UInterface
{
	GENERATED_BODY()
};

class AEYERJI_API IAeyerjiInteractable
{
	GENERATED_BODY()

public:
	/** Returns true while this actor is available for the supplied player to use. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Aeyerji|Interaction")
	bool CanInteract(AAeyerjiPlayerController* Controller);

	/** Returns the world-space point the player should move toward before interacting. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Aeyerji|Interaction")
	FVector GetInteractionLocation();

	/** Returns the server-side maximum interaction distance in centimeters. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Aeyerji|Interaction")
	float GetInteractionRadius();

	/** Executes the authoritative interaction after the player controller has validated range. */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Aeyerji|Interaction")
	void Interact(AAeyerjiPlayerController* Controller);
};
