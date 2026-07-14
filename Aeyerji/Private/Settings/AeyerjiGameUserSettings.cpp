#include "Settings/AeyerjiGameUserSettings.h"

#include "Engine/Engine.h"

UAeyerjiGameUserSettings::UAeyerjiGameUserSettings()
{
	CombatTextMode = EAeyerjiCombatTextMode::ImportantOnly;
}

UAeyerjiGameUserSettings* UAeyerjiGameUserSettings::GetAeyerjiGameUserSettings()
{
	return GEngine ? Cast<UAeyerjiGameUserSettings>(GEngine->GetGameUserSettings()) : nullptr;
}

void UAeyerjiGameUserSettings::SetCombatTextMode(const EAeyerjiCombatTextMode NewMode, const bool bSaveImmediately)
{
	CombatTextMode = NewMode;
	ValidateSettings();

	if (bSaveImmediately)
	{
		SaveSettings();
	}
}

void UAeyerjiGameUserSettings::ValidateSettings()
{
	Super::ValidateSettings();

	const UEnum* CombatTextModeEnum = StaticEnum<EAeyerjiCombatTextMode>();
	if (!CombatTextModeEnum || !CombatTextModeEnum->IsValidEnumValue(static_cast<int64>(CombatTextMode)))
	{
		CombatTextMode = EAeyerjiCombatTextMode::ImportantOnly;
	}
}
