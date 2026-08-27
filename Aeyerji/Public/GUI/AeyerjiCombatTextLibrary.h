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
	/** Returns the locally configured combat-text filtering mode. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Combat Text")
	static EAeyerjiCombatTextMode GetCombatTextMode();

	/** Extracts the project damage-result payload from a GameplayCue effect context. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Combat Text")
	static bool ExtractDamageResultFromCueParameters(
		const FGameplayCueParameters& Parameters,
		FAeyerjiDamageResult& OutResult);

	/** Tests whether a result category is enabled by the selected filtering mode. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Combat Text")
	static bool ShouldShowResultTypeForMode(
		EAeyerjiCombatTextResultType ResultType,
		EAeyerjiCombatTextMode Mode);

	/** Builds finite, localized presentation data for one combat-text event. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Combat Text")
	static bool BuildCombatTextPayload(
		EAeyerjiCombatTextResultType ResultType,
		const FAeyerjiDamageResult& DamageResult,
		EAeyerjiCombatTextMode Mode,
		FText& OutText,
		FLinearColor& OutColor,
		float& OutScale,
		float& OutMagnitude);

	/** Limits combat text to cues involving the supplied local player's ownership chain. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Combat Text")
	static bool ShouldDisplayForLocalPlayer(
		APlayerController* LocalPlayerController,
		AActor* TargetActor,
		const FGameplayCueParameters& Parameters);
};
