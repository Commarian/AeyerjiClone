#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Combat/AeyerjiMeleeDeterministicStrike.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiPrimaryMeleeDeterministicStrikeTimingTest,
	"Aeyerji.Abilities.PrimaryMelee.DeterministicStrikeTiming",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiPrimaryMeleeDeterministicStrikeTimingTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const float BaselineDelay = FAeyerjiMeleeDeterministicStrikePolicy::CalculateImpactDelay(0.12f, 0.05f, 1.f);
	TestTrue(TEXT("Baseline impact delay combines windup and strike delay."),
		FMath::IsNearlyEqual(BaselineDelay, 0.17f, 0.0001f));

	const float HighSpeedDelay = FAeyerjiMeleeDeterministicStrikePolicy::CalculateImpactDelay(0.12f, 0.05f, 8.f);
	TestTrue(TEXT("High attack speed keeps a positive deterministic delay."),
		HighSpeedDelay > 0.f && FMath::IsNearlyEqual(HighSpeedDelay, 0.02125f, 0.0001f));

	const float ClampedDelay = FAeyerjiMeleeDeterministicStrikePolicy::CalculateImpactDelay(-1.f, -2.f, 0.f);
	TestEqual(TEXT("Negative timing inputs clamp to zero delay."), ClampedDelay, 0.f);

	TestTrue(TEXT("Normal montage finish flushes a pending unresolved strike."),
		FAeyerjiMeleeDeterministicStrikePolicy::ShouldResolveOnMontageFinish(false, true, false));
	TestFalse(TEXT("Cancelled montage does not resolve a pending strike."),
		FAeyerjiMeleeDeterministicStrikePolicy::ShouldResolveOnMontageFinish(true, true, false));
	TestFalse(TEXT("Already resolved strike is not resolved again."),
		FAeyerjiMeleeDeterministicStrikePolicy::ShouldResolveOnMontageFinish(false, true, true));

	TestTrue(TEXT("Hard cancel discards a pending unresolved strike."),
		FAeyerjiMeleeDeterministicStrikePolicy::ShouldCancelOnHardCancel(true, false));
	TestFalse(TEXT("Hard cancel does not discard an already resolved strike."),
		FAeyerjiMeleeDeterministicStrikePolicy::ShouldCancelOnHardCancel(true, true));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiPrimaryMeleeDeterministicStrikeTargetingPolicyTest,
	"Aeyerji.Abilities.PrimaryMelee.DeterministicStrikeTargetingPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiPrimaryMeleeDeterministicStrikeTargetingPolicyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestEqual(TEXT("Authored zero angle remains zero for single-target melee."),
		FAeyerjiMeleeDeterministicStrikePolicy::ResolveAttackAngle(0.f, true, 75.f),
		0.f);
	TestEqual(TEXT("Missing attack angle falls back to ability default."),
		FAeyerjiMeleeDeterministicStrikePolicy::ResolveAttackAngle(0.f, false, 75.f),
		75.f);
	TestTrue(TEXT("Positive attack angle enables cleave."),
		FAeyerjiMeleeDeterministicStrikePolicy::IsCleaveAngle(35.f));
	TestFalse(TEXT("Zero attack angle disables cleave."),
		FAeyerjiMeleeDeterministicStrikePolicy::IsCleaveAngle(0.f));

	TestTrue(TEXT("Locked target grace range applies the configured multiplier."),
		FMath::IsNearlyEqual(
			FAeyerjiMeleeDeterministicStrikePolicy::CalculateLockedTargetGraceRange(130.f, 2.5f),
			325.f,
			0.001f));
	TestEqual(TEXT("Negative attack range cannot create grace reach."),
		FAeyerjiMeleeDeterministicStrikePolicy::CalculateLockedTargetGraceRange(-130.f, 2.5f),
		0.f);

	TestTrue(TEXT("Target behind a 90-degree threshold requests refacing."),
		FAeyerjiMeleeDeterministicStrikePolicy::ShouldRefaceLockedTarget(-0.01f, 90.f));
	TestFalse(TEXT("Target inside a 90-degree forward hemisphere does not request refacing."),
		FAeyerjiMeleeDeterministicStrikePolicy::ShouldRefaceLockedTarget(0.01f, 90.f));

	return true;
}

#endif
