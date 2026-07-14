#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameUserSettings.h"
#include "GUI/AeyerjiCombatTextTypes.h"

#include "AeyerjiGameUserSettings.generated.h"

/** Local, per-user presentation settings for Aeyerji. */
UCLASS(Config=GameUserSettings, ConfigDoNotCheckDefaults)
class AEYERJI_API UAeyerjiGameUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	UAeyerjiGameUserSettings();

	/** Returns the active game-user settings object when the project settings class is configured. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Settings")
	static UAeyerjiGameUserSettings* GetAeyerjiGameUserSettings();

	UFUNCTION(BlueprintPure, Category="Aeyerji|Combat Text")
	EAeyerjiCombatTextMode GetCombatTextMode() const { return CombatTextMode; }

	/** Updates the local combat text mode and optionally persists it immediately. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Combat Text", meta=(AdvancedDisplay="bSaveImmediately"))
	void SetCombatTextMode(EAeyerjiCombatTextMode NewMode, bool bSaveImmediately = true);

	virtual void ValidateSettings() override;

private:
	UPROPERTY(Config, EditAnywhere, Category="Aeyerji|Combat Text")
	EAeyerjiCombatTextMode CombatTextMode = EAeyerjiCombatTextMode::ImportantOnly;
};
