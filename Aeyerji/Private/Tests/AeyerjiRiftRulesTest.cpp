#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include <limits>

#include "Systems/AeyerjiRiftRules.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiRiftTimingAndRewardMatrixTest,
	"Aeyerji.Rift.Rules.TimingAndRewardMatrix",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiRiftTimingAndRewardMatrixTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestTrue(TEXT("A boss death before 900 seconds is in time."),
		AeyerjiRiftRules::IsCompletedInTime(899.999f, 900.f));
	TestFalse(TEXT("A boss death exactly at 900 seconds is overtime."),
		AeyerjiRiftRules::IsCompletedInTime(900.f, 900.f));
	TestFalse(TEXT("A boss death after 900 seconds is overtime."),
		AeyerjiRiftRules::IsCompletedInTime(900.001f, 900.f));
	TestFalse(TEXT("A non-finite elapsed time is never accepted."),
		AeyerjiRiftRules::IsCompletedInTime(std::numeric_limits<float>::quiet_NaN(), 900.f));
	TestFalse(TEXT("A non-finite time limit is never accepted."),
		AeyerjiRiftRules::IsCompletedInTime(30.f, std::numeric_limits<float>::infinity()));

	const EAeyerjiRiftRewardEligibility NoBoss = AeyerjiRiftRules::ResolveRewardEligibility(false, false, false);
	const EAeyerjiRiftRewardEligibility Overtime = AeyerjiRiftRules::ResolveRewardEligibility(true, false, false);
	const EAeyerjiRiftRewardEligibility TimedWithDeath = AeyerjiRiftRules::ResolveRewardEligibility(true, true, true);
	const EAeyerjiRiftRewardEligibility TimedFlawless = AeyerjiRiftRules::ResolveRewardEligibility(true, true, false);
	TestEqual(TEXT("No accepted boss death earns no Rift layers."), static_cast<uint8>(NoBoss), static_cast<uint8>(EAeyerjiRiftRewardEligibility::None));
	TestEqual(TEXT("Overtime earns base only."), static_cast<uint8>(Overtime), static_cast<uint8>(EAeyerjiRiftRewardEligibility::Base));
	TestTrue(TEXT("Timed clear with a death earns base and timed."),
		EnumHasAllFlags(TimedWithDeath, EAeyerjiRiftRewardEligibility::Base | EAeyerjiRiftRewardEligibility::Timed));
	TestFalse(TEXT("Boss-phase death removes flawless."),
		EnumHasAnyFlags(TimedWithDeath, EAeyerjiRiftRewardEligibility::Flawless));
	TestTrue(TEXT("Timed clear without a boss-phase death earns every layer."),
		EnumHasAllFlags(TimedFlawless,
			EAeyerjiRiftRewardEligibility::Base
			| EAeyerjiRiftRewardEligibility::Timed
			| EAeyerjiRiftRewardEligibility::Flawless));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiRiftTierProgressionAndMigrationTest,
	"Aeyerji.Rift.Rules.TierProgressionAndMigration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiRiftTierProgressionAndMigrationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestEqual(TEXT("Party tier cap is the lowest loaded unlock."),
		AeyerjiRiftRules::ResolveCommonTierCap({5, 3, 7}), 3);
	TestEqual(TEXT("Missing party profiles have no valid cap."),
		AeyerjiRiftRules::ResolveCommonTierCap({}), 0);
	TestEqual(TEXT("Invalid profile tiers invalidate the cap."),
		AeyerjiRiftRules::ResolveCommonTierCap({4, 0}), 0);

	TestEqual(TEXT("On-time victory advances exactly one tier."),
		AeyerjiRiftRules::ResolveHighestUnlockedTier(3, 3, true, true), 4);
	TestEqual(TEXT("Overtime victory does not advance."),
		AeyerjiRiftRules::ResolveHighestUnlockedTier(3, 3, true, false), 3);
	TestEqual(TEXT("Failure does not advance."),
		AeyerjiRiftRules::ResolveHighestUnlockedTier(3, 3, false, true), 3);
	TestEqual(TEXT("An already-higher profile is never lowered."),
		AeyerjiRiftRules::ResolveHighestUnlockedTier(8, 3, true, true), 8);
	TestEqual(TEXT("Advancing the maximum tier saturates instead of overflowing."),
		AeyerjiRiftRules::ResolveHighestUnlockedTier(MAX_int32, MAX_int32, true, true), MAX_int32);

	int32 HighestTier = 0;
	int32 LastSelectedTier = 0;
	AeyerjiRiftRules::NormalizeProfileTiers(HighestTier, LastSelectedTier);
	TestEqual(TEXT("Legacy highest tier migrates to Tier 1."), HighestTier, 1);
	TestEqual(TEXT("Legacy last selection migrates to Tier 1."), LastSelectedTier, 1);
	HighestTier = 4;
	LastSelectedTier = 9;
	AeyerjiRiftRules::NormalizeProfileTiers(HighestTier, LastSelectedTier);
	TestEqual(TEXT("Last selection is clamped to the profile unlock."), LastSelectedTier, 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiRiftAllocationAndProgressTest,
	"Aeyerji.Rift.Rules.AllocationAndProgress",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiRiftAllocationAndProgressTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const TArray<int32> EqualAllocation = AeyerjiRiftRules::AllocateLargestRemainder({1.f, 1.f, 1.f}, 5);
	TestEqual(TEXT("Allocation preserves group count."), EqualAllocation.Num(), 3);
	TestEqual(TEXT("Stable order wins the first equal remainder."), EqualAllocation[0], 2);
	TestEqual(TEXT("Stable order wins the second equal remainder."), EqualAllocation[1], 2);
	TestEqual(TEXT("Final equal-weight group receives the remaining floor."), EqualAllocation[2], 1);
	TestEqual(TEXT("Allocation exactly preserves the budget."),
		EqualAllocation[0] + EqualAllocation[1] + EqualAllocation[2], 5);

	TArray<bool> Consumed = {false, false, false};
	const TArray<bool> Viable = {true, true, true};
	const TArray<float> Distances = {400.f, 100.f, 100.f};
	const int32 FirstRegion = AeyerjiRiftRules::SelectClosestUnusedRegion(Distances, Viable, Consumed, 900.f);
	TestEqual(TEXT("Closest region is selected and stable order breaks equal-distance ties."), FirstRegion, 1);
	Consumed[FirstRegion] = true;
	const int32 SimultaneousRetry = AeyerjiRiftRules::SelectClosestUnusedRegion(Distances, Viable, Consumed, 900.f);
	TestEqual(TEXT("An already consumed region cannot be selected by a simultaneous retry."), SimultaneousRetry, 2);

	const TArray<int32> WeightedAllocation = AeyerjiRiftRules::AllocateLargestRemainder({1.f, 2.f, 1.f}, 7);
	TestEqual(TEXT("Weighted first group allocation."), WeightedAllocation[0], 2);
	TestEqual(TEXT("Weighted middle group allocation."), WeightedAllocation[1], 3);
	TestEqual(TEXT("Weighted final group allocation."), WeightedAllocation[2], 2);

	const TArray<int32> InvalidWeightAllocation = AeyerjiRiftRules::AllocateLargestRemainder(
		{std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(), 1.f}, 4);
	TestEqual(TEXT("NaN weights receive no allocation."), InvalidWeightAllocation[0], 0);
	TestEqual(TEXT("Infinite weights receive no allocation."), InvalidWeightAllocation[1], 0);
	TestEqual(TEXT("Finite weights preserve the full budget."), InvalidWeightAllocation[2], 4);

	TestTrue(TEXT("An equal-index lateral region remains valid at the progression frontier."),
		AeyerjiRiftRules::CanStageProgressionIndex(4, 4));
	TestTrue(TEXT("A forward region remains valid at the progression frontier."),
		AeyerjiRiftRules::CanStageProgressionIndex(5, 4));
	TestFalse(TEXT("A region behind the progression frontier can never stage."),
		AeyerjiRiftRules::CanStageProgressionIndex(3, 4));
	const TArray<int32> TransferredPopulation =
		AeyerjiRiftRules::AllocateTransferredPopulation(8, 3);
	TestEqual(TEXT("Skipped population preserves forward destination count."), TransferredPopulation.Num(), 3);
	TestEqual(TEXT("Stable forward order receives the first transfer remainder."), TransferredPopulation[0], 3);
	TestEqual(TEXT("Stable forward order receives the second transfer remainder."), TransferredPopulation[1], 3);
	TestEqual(TEXT("The final destination receives the remaining transfer."), TransferredPopulation[2], 2);
	TestEqual(TEXT("Skipped transfer preserves the finite population."),
		TransferredPopulation[0] + TransferredPopulation[1] + TransferredPopulation[2], 8);

	TestEqual(TEXT("Progress clamps once at its target."),
		AeyerjiRiftRules::ApplyAcceptedProgressAward(97, 100, 5), 100);
	TestEqual(TEXT("Later events cannot add progress after completion."),
		AeyerjiRiftRules::ApplyAcceptedProgressAward(100, 100, 5), 100);
	TestEqual(TEXT("Progress addition saturates at the target without integer overflow."),
		AeyerjiRiftRules::ApplyAcceptedProgressAward(MAX_int32 - 2, MAX_int32, MAX_int32), MAX_int32);
	TestEqual(TEXT("Authored zero-point entries retain the one-point compatibility floor."),
		AeyerjiRiftRules::ApplyAcceptedProgressAward(10, 100, 0), 11);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiRiftHeldCommandRecoveryRulesTest,
	"Aeyerji.Rift.Rules.HeldCommandRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiRiftHeldCommandRecoveryRulesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestTrue(TEXT("A valid held attack or move is reconstructed."),
		AeyerjiRiftRules::ShouldRecoverHeldCommand(true, false, false, false, false, true));
	TestFalse(TEXT("Released input cannot create a ghost command."),
		AeyerjiRiftRules::ShouldRecoverHeldCommand(false, false, false, false, false, true));
	TestFalse(TEXT("Cancelled abilities suppress recovery until release."),
		AeyerjiRiftRules::ShouldRecoverHeldCommand(true, true, false, false, false, true));
	TestFalse(TEXT("Death blocks recovery."),
		AeyerjiRiftRules::ShouldRecoverHeldCommand(true, false, true, false, false, true));
	TestFalse(TEXT("Modal UI blocks recovery."),
		AeyerjiRiftRules::ShouldRecoverHeldCommand(true, false, false, true, false, true));
	TestFalse(TEXT("Interaction commands are never reconstructed."),
		AeyerjiRiftRules::ShouldRecoverHeldCommand(true, false, false, false, true, true));
	TestFalse(TEXT("An invalid target or ground hit blocks recovery."),
		AeyerjiRiftRules::ShouldRecoverHeldCommand(true, false, false, false, false, false));
	return true;
}

#endif
