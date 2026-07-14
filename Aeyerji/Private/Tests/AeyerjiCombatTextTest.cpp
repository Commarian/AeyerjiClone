#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "GAS/AeyerjiGameplayEffectContext.h"
#include "GUI/AeyerjiCombatTextLibrary.h"
#include "Settings/AeyerjiGameUserSettings.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiCombatTextPayloadTest,
	"Aeyerji.Combat.CombatText.PayloadAndSettings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiCombatTextPayloadTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UAeyerjiGameUserSettings* Settings = NewObject<UAeyerjiGameUserSettings>();
	TestNotNull(TEXT("Aeyerji game-user settings can be created."), Settings);
	if (!Settings)
	{
		return false;
	}

	TestEqual(
		TEXT("Combat text defaults to ImportantOnly."),
		Settings->GetCombatTextMode(),
		EAeyerjiCombatTextMode::ImportantOnly);

	Settings->SetCombatTextMode(EAeyerjiCombatTextMode::All, false);
	TestEqual(TEXT("Combat text setter updates without saving."), Settings->GetCombatTextMode(), EAeyerjiCombatTextMode::All);

	FAeyerjiDamageResult DamageResult;
	DamageResult.FinalDamage = 123.f;
	DamageResult.bWasCritical = true;

	FText Text;
	FLinearColor Color;
	float Scale = 1.f;
	float Magnitude = 0.f;

	TestFalse(
		TEXT("ImportantOnly suppresses normal damage numbers."),
		UAeyerjiCombatTextLibrary::BuildCombatTextPayload(
			EAeyerjiCombatTextResultType::Damage,
			DamageResult,
			EAeyerjiCombatTextMode::ImportantOnly,
			Text,
			Color,
			Scale,
			Magnitude));

	TestTrue(
		TEXT("ImportantOnly shows critical damage."),
		UAeyerjiCombatTextLibrary::BuildCombatTextPayload(
			EAeyerjiCombatTextResultType::Critical,
			DamageResult,
			EAeyerjiCombatTextMode::ImportantOnly,
			Text,
			Color,
			Scale,
			Magnitude));
	TestEqual(TEXT("Critical text includes final damage."), Text.ToString(), FString(TEXT("CRIT 123")));
	TestTrue(TEXT("Critical text carries the final damage magnitude."), FMath::IsNearlyEqual(Magnitude, 123.f));

	TestTrue(
		TEXT("All mode shows normal damage numbers."),
		UAeyerjiCombatTextLibrary::BuildCombatTextPayload(
			EAeyerjiCombatTextResultType::Damage,
			DamageResult,
			EAeyerjiCombatTextMode::All,
			Text,
			Color,
			Scale,
			Magnitude));
	TestEqual(TEXT("Normal damage text is final damage."), Text.ToString(), FString(TEXT("123")));

	TestFalse(
		TEXT("Off mode suppresses dodged text."),
		UAeyerjiCombatTextLibrary::BuildCombatTextPayload(
			EAeyerjiCombatTextResultType::Dodged,
			DamageResult,
			EAeyerjiCombatTextMode::Off,
			Text,
			Color,
			Scale,
			Magnitude));

	FGameplayEffectContextHandle ContextHandle(new FAeyerjiGameplayEffectContext());
	FAeyerjiGameplayEffectContext* MutableContext = FAeyerjiGameplayEffectContext::ExtractMutable(ContextHandle);
	TestNotNull(TEXT("Aeyerji damage context can be extracted from handle."), MutableContext);
	if (!MutableContext)
	{
		return false;
	}

	MutableContext->GetMutableDamageResult().FinalDamage = 42.f;
	MutableContext->GetMutableDamageResult().bWasDodged = true;

	FGameplayCueParameters CueParameters;
	CueParameters.EffectContext = ContextHandle;

	FAeyerjiDamageResult ExtractedResult;
	TestTrue(
		TEXT("Damage result extracts from cue parameters."),
		UAeyerjiCombatTextLibrary::ExtractDamageResultFromCueParameters(CueParameters, ExtractedResult));
	TestTrue(TEXT("Extracted final damage roundtrips."), FMath::IsNearlyEqual(ExtractedResult.FinalDamage, 42.f));
	TestTrue(TEXT("Extracted dodge flag roundtrips."), ExtractedResult.bWasDodged);

	return true;
}

#endif
