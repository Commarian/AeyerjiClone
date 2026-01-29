#pragma once
#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "GameplayTagContainer.h"
#include "AeyerjiAbilityTypes.h" 
#include "AeyerjiAbilitySlot.generated.h"

/** Fixed-size ability-bar record (7 per player, including potion slot) */
USTRUCT(BlueprintType)
struct FAeyerjiAbilitySlot
{
	GENERATED_BODY()
	
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) FGameplayTagContainer Tag;
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) FName        Description;
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) TObjectPtr<UTexture2D> Icon = nullptr;
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) TSubclassOf<UGameplayAbility> Class;
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite) int32        Level = 1;
	UPROPERTY(SaveGame, EditAnywhere, BlueprintReadWrite)
	EAeyerjiTargetMode TargetMode = EAeyerjiTargetMode::Instant;
};
