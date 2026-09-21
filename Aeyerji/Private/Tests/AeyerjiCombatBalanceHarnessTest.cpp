#if WITH_DEV_AUTOMATION_TESTS

#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"
#include "Systems/AeyerjiDifficultyTuning.h"
#include "Progression/AeyerjiRewardTuning.h"
#include "Testing/AeyerjiCombatBalanceTestHarness.h"
#include "Testing/AeyerjiCombatTestIsolation.h"

#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiCombatTestIsolationLaunchOptionTest,
	"Aeyerji.CombatTest.IsolationLaunchOption",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiCombatTestIsolationLaunchOptionTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestTrue(
		TEXT("The documented launch option enables world-spawn isolation."),
		AeyerjiCombatTestIsolation::ShouldSuppressWorldSpawning(TEXT("-log -AeyerjiCombatTestIsolated")));
	TestTrue(
		TEXT("Launch-option parsing is case-insensitive."),
		AeyerjiCombatTestIsolation::ShouldSuppressWorldSpawning(TEXT("-AEYERJICOMBATTESTISOLATED")));
	TestFalse(
		TEXT("Normal launches keep production world spawning enabled."),
		AeyerjiCombatTestIsolation::ShouldSuppressWorldSpawning(TEXT("-log -game")));
	TestFalse(
		TEXT("A similarly prefixed option cannot enable isolation accidentally."),
		AeyerjiCombatTestIsolation::ShouldSuppressWorldSpawning(TEXT("-AeyerjiCombatTestIsolatedExtra")));

	IConsoleVariable* IsolationVariable = IConsoleManager::Get().FindConsoleVariable(TEXT("aeyerji.CombatTest.Isolated"));
	TestNotNull(
		TEXT("The PIE-safe combat-test isolation console variable is registered."),
		IsolationVariable);
	if (IsolationVariable)
	{
		const int32 OriginalValue = IsolationVariable->GetInt();
		IsolationVariable->Set(1, ECVF_SetByConsole);
		TestTrue(
			TEXT("The PIE-safe console variable enables world-spawn isolation."),
			AeyerjiCombatTestIsolation::IsWorldSpawningSuppressed());
		IsolationVariable->Set(OriginalValue, ECVF_SetByConsole);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiCombatTestPresetContractTest,
	"Aeyerji.CombatTest.PresetContract",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiCombatTestPresetContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	struct FExpectedPreset
	{
		const TCHAR* Name;
		int32 Count;
		const TCHAR* Composition;
	};

	const FExpectedPreset ExpectedPresets[] =
	{
		{ TEXT("Sanity1"), 1, TEXT("Grunt") },
		{ TEXT("Floor8"), 8, TEXT("Mixed") },
		{ TEXT("Region12"), 12, TEXT("Mixed") },
		{ TEXT("Ranged12"), 12, TEXT("Ranged") },
		{ TEXT("Grunt20"), 20, TEXT("Grunt") },
		{ TEXT("Dense24"), 24, TEXT("Mixed") },
		{ TEXT("Elite24"), 24, TEXT("MixedElite") },
		{ TEXT("Overwhelmed36"), 36, TEXT("Mixed") },
		{ TEXT("Cap48"), 48, TEXT("Mixed") }
	};

	for (const FExpectedPreset& Expected : ExpectedPresets)
	{
		FAeyerjiCombatTestRequest Request;
		FString Error;
		TestTrue(
			*FString::Printf(TEXT("Preset %s resolves."), Expected.Name),
			AAeyerjiCombatBalanceTestHarness::ResolvePreset(Expected.Name, Request, Error));
		TestEqual(
			*FString::Printf(TEXT("Preset %s population matches the density ladder."), Expected.Name),
			Request.EnemyCount,
			Expected.Count);
		TestEqual(
			*FString::Printf(TEXT("Preset %s composition matches its contract."), Expected.Name),
			Request.CompositionName,
			FName(Expected.Composition));
		TestTrue(
			*FString::Printf(TEXT("Preset %s uses an ordered spawn annulus."), Expected.Name),
			Request.MinimumSpawnRadius >= 300.f
				&& Request.MaximumSpawnRadius >= Request.MinimumSpawnRadius);
		TestTrue(
			*FString::Printf(TEXT("Preset %s survives request sanitization."), Expected.Name),
			AAeyerjiCombatBalanceTestHarness::SanitizeRequest(Request, Error));
	}

	FAeyerjiCombatTestRequest InvalidCount;
	InvalidCount.EnemyCount = AAeyerjiCombatBalanceTestHarness::MaximumTestEnemyCount + 1;
	FString Error;
	TestFalse(
		TEXT("A custom request cannot exceed the production 48-awake test ceiling."),
		AAeyerjiCombatBalanceTestHarness::SanitizeRequest(InvalidCount, Error));

	FAeyerjiCombatTestRequest InvalidComposition;
	InvalidComposition.CompositionName = TEXT("NotARealComposition");
	TestFalse(
		TEXT("An unknown composition is rejected rather than silently replaced."),
		AAeyerjiCombatBalanceTestHarness::SanitizeRequest(InvalidComposition, Error));

	FAeyerjiCombatTestRequest ClampedScaling;
	ClampedScaling.EnemyLevel = MAX_int32;
	ClampedScaling.WorldTier = MAX_int32;
	TestTrue(
		TEXT("An otherwise valid request with out-of-range scaling inputs is accepted."),
		AAeyerjiCombatBalanceTestHarness::SanitizeRequest(ClampedScaling, Error));
	TestEqual(
		TEXT("Enemy level is clamped through the shared gameplay-level contract."),
		ClampedScaling.EnemyLevel,
		UAeyerjiDifficultySettings::GetMaxGameplayLevel());
	TestEqual(
		TEXT("World tier is clamped through the shared world-tier contract."),
		ClampedScaling.WorldTier,
		UAeyerjiDifficultySettings::WorldTierMax);

	FAeyerjiCombatTestRequest ManualEngage;
	ManualEngage.AutoEngageDelay = -1.f;
	TestTrue(
		TEXT("A -1 auto-engage delay remains valid for a Rewind-controlled start."),
		AAeyerjiCombatBalanceTestHarness::SanitizeRequest(ManualEngage, Error));

	FAeyerjiCombatTestRequest InvalidDelay;
	InvalidDelay.AutoEngageDelay = -2.f;
	TestFalse(
		TEXT("Unsupported negative auto-engage delays are rejected."),
		AAeyerjiCombatBalanceTestHarness::SanitizeRequest(InvalidDelay, Error));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiCombatBalanceMetricMathTest,
	"Aeyerji.CombatTest.BalanceMetricMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiCombatBalanceMetricMathTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const float KillRatePerSecond = AAeyerjiCombatBalanceTestHarness::CalculateCountRate(19, 10.f);
	const float KillsPerTenSeconds = KillRatePerSecond * 10.f;
	TestTrue(TEXT("Nineteen kills over ten seconds reports 19 kills per ten seconds."),
		FMath::IsNearlyEqual(KillsPerTenSeconds, 19.f));

	const float LosingPressure = AAeyerjiCombatBalanceTestHarness::CalculateNetPressure(2.1f, 14.f);
	TestTrue(TEXT("Arrival 2.1/s versus kill 1.4/s produces positive 0.7/s accumulating pressure."),
		FMath::IsNearlyEqual(LosingPressure, 0.7f, 0.001f));

	const float WinningPressure = AAeyerjiCombatBalanceTestHarness::CalculateNetPressure(1.7f, 19.f);
	TestTrue(TEXT("Arrival 1.7/s versus kill 1.9/s produces negative 0.2/s relieving pressure."),
		FMath::IsNearlyEqual(WinningPressure, -0.2f, 0.001f));

	TestEqual(TEXT("Zero duration never divides by zero."),
		AAeyerjiCombatBalanceTestHarness::CalculateCountRate(10, 0.f), 0.f);
	TestEqual(TEXT("Non-finite inputs are sanitized rather than contaminating a report."),
		AAeyerjiCombatBalanceTestHarness::CalculateNetPressure(std::numeric_limits<float>::quiet_NaN(), 10.f), -1.f);

	TestTrue(TEXT("A 125 ms authority frame records 75 ms beyond the 50 ms hitch threshold."),
		FMath::IsNearlyEqual(
			AAeyerjiCombatBalanceTestHarness::CalculateHitchOverageMilliseconds(0.125f, 0.05f),
			75.f,
			0.001f));
	TestEqual(TEXT("A frame below the hitch threshold records no overage."),
		AAeyerjiCombatBalanceTestHarness::CalculateHitchOverageMilliseconds(0.016f, 0.05f), 0.f);
	TestEqual(TEXT("Non-finite frame times cannot contaminate hitch reports."),
		AAeyerjiCombatBalanceTestHarness::CalculateHitchOverageMilliseconds(
			std::numeric_limits<float>::infinity(), 0.05f), 0.f);

	const UAeyerjiRewardTuning* DefaultRewardTuning = GetDefault<UAeyerjiRewardTuning>();
	TestNotNull(TEXT("Reward tuning has a native class default."), DefaultRewardTuning);
	if (DefaultRewardTuning)
	{
		TestTrue(TEXT("The native trash XP fallback is 0.2."),
			FMath::IsNearlyEqual(
				DefaultRewardTuning->TrashXPRewardMultiplier,
				UAeyerjiRewardTuning::DefaultTrashXPRewardMultiplier));
		TestFalse(TEXT("Existing RewardTuning assets inherit the native trash XP fallback until explicitly overridden."),
			DefaultRewardTuning->bOverride_TrashXPRewardMultiplier);
	}

	return true;
}

#endif
