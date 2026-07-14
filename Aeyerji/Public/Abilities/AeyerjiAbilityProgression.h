#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "AeyerjiAbilityProgression.generated.h"

/** Saved and replicated progression state for a single player-owned ability. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiAbilityProgressEntry
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Abilities")
	FGameplayTag AbilityTag;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Abilities", meta=(ClampMin="1"))
	int32 CurrentRank = 1;

	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Abilities", meta=(ClampMin="0"))
	int32 LastUpgradePointSpendCount = 0;
};
