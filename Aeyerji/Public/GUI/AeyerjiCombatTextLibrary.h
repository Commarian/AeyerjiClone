#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "GameplayEffectTypes.h"
#include "GAS/AeyerjiGameplayEffectContext.h"
#include "GUI/AeyerjiCombatTextTypes.h"

#include "AeyerjiCombatTextLibrary.generated.h"

class APlayerController;

/** Shared helpers for combat text GameplayCues and UI settings. */
UCLASS()
class AEYERJI_API UAeyerjiCombatTextLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category="Aeyerji|Combat Text")
	static EAeyerjiCombatTextMode GetCombatTextMode();

	UFUNCTION(BlueprintPure, Category="Aeyerji|Combat Text")
	static bool ExtractDamageResultFromCueParameters(
		const FGameplayCueParameters& Parameters,
		FAeyerjiDamageResult& OutResult);

	UFUNCTION(BlueprintPure, Category="Aeyerji|Combat Text")
	static bool ShouldShowResultTypeForMode(
		EAeyerjiCombatTextResultType ResultType,
		EAeyerjiCombatTextMode Mode);

	UFUNCTION(BlueprintPure, Category="Aeyerji|Combat Text")
	static bool BuildCombatTextPayload(
		EAeyerjiCombatTextResultType ResultType,
		const FAeyerjiDamageResult& DamageResult,
		EAeyerjiCombatTextMode Mode,
		FText& OutText,
		FLinearColor& OutColor,
		float& OutScale,
		float& OutMagnitude);

	UFUNCTION(BlueprintPure, Category="Aeyerji|Combat Text")
	static bool ShouldDisplayForLocalPlayer(
		APlayerController* LocalPlayerController,
		AActor* TargetActor,
		const FGameplayCueParameters& Parameters);
};
