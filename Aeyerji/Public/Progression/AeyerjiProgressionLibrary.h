#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "AeyerjiProgressionLibrary.generated.h"

/** Shared profile-safe progression rules used by gameplay and frontend presentation. */
UCLASS()
class AEYERJI_API UAeyerjiProgressionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Evaluates the project-wide XP_Needed curve for the supplied character level. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Progression")
	static float GetXPRequiredForLevel(int32 CharacterLevel);
};

