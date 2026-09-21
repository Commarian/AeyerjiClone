#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Abilities/MeteorStrike/AeyerjiMeteorStrike.h"
#include "GameplayTagContainer.h"

#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiMeteorStrikeFallMathTest,
	"Aeyerji.Combat.MeteorStrike.FallMath",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiMeteorStrikeFallMathTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestTrue(TEXT("Valid fall duration is unchanged."),
		FMath::IsNearlyEqual(AAeyerjiMeteorStrike::ClampFallDuration(1.f), 1.f));
	TestTrue(TEXT("Short fall clamps to minimum."),
		FMath::IsNearlyEqual(AAeyerjiMeteorStrike::ClampFallDuration(0.01f), AAeyerjiMeteorStrike::MinFallDuration));
	TestTrue(TEXT("Long fall clamps to maximum."),
		FMath::IsNearlyEqual(AAeyerjiMeteorStrike::ClampFallDuration(30.f), AAeyerjiMeteorStrike::MaxFallDuration));

	TestTrue(TEXT("Valid spawn height is unchanged."),
		FMath::IsNearlyEqual(AAeyerjiMeteorStrike::ClampSpawnHeight(1500.f), 1500.f));
	TestTrue(TEXT("Invalid spawn height falls back to default."),
		FMath::IsNearlyEqual(AAeyerjiMeteorStrike::ClampSpawnHeight(std::numeric_limits<float>::quiet_NaN()),
			AAeyerjiMeteorStrike::DefaultSpawnHeight));

	const FVector Impact(100.f, -200.f, 50.f);
	TestTrue(TEXT("Spawn sits directly above impact."),
		AAeyerjiMeteorStrike::ComputeSpawnLocation(Impact, 1200.f).Equals(Impact + FVector(0.f, 0.f, 1200.f)));

	TestTrue(TEXT("Fall starts at zero progress."),
		FMath::IsNearlyEqual(AAeyerjiMeteorStrike::ComputeFallProgress(10.0, 10.0, 1.f), 0.f));
	TestTrue(TEXT("Fall midpoint is half progress."),
		FMath::IsNearlyEqual(AAeyerjiMeteorStrike::ComputeFallProgress(10.0, 10.5, 1.f), 0.5f));
	TestTrue(TEXT("Fall progress clamps at landing."),
		FMath::IsNearlyEqual(AAeyerjiMeteorStrike::ComputeFallProgress(10.0, 12.0, 1.f), 1.f));

	TestTrue(TEXT("Landed meteor rests exactly on impact."),
		AAeyerjiMeteorStrike::ComputeFallLocation(Impact, 1200.f, 1.f).Equals(Impact));

	TestTrue(TEXT("Ability tag Ability.AG.MeteorStrike is registered."),
		FGameplayTag::RequestGameplayTag(TEXT("Ability.AG.MeteorStrike"), false).IsValid());
	TestTrue(TEXT("Tunable tag Ability.Param.Meteor.SpawnHeight is registered."),
		FGameplayTag::RequestGameplayTag(TEXT("Ability.Param.Meteor.SpawnHeight"), false).IsValid());
	TestTrue(TEXT("Tunable tag Ability.Param.Meteor.ActorClass is registered."),
		FGameplayTag::RequestGameplayTag(TEXT("Ability.Param.Meteor.ActorClass"), false).IsValid());
	TestTrue(TEXT("Cooldown tag Cooldown.Meteor is registered."),
		FGameplayTag::RequestGameplayTag(TEXT("Cooldown.Meteor"), false).IsValid());

	return true;
}

#endif
