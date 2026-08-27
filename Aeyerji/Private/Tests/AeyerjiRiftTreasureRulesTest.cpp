#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Systems/AeyerjiRiftTreasureRules.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiRiftTreasureSeededSelectionRulesTest,
	"Aeyerji.Rift.Treasure.SeededSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiRiftTreasureSeededSelectionRulesTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FAeyerjiRiftTreasureSpawnConfig Config;
	Config.MinimumChests = 3;
	Config.MaximumChests = 3;
	Config.HardMinimumChestSeparation = 350.f;
	Config.PreferredChestSeparation = 1000.f;
	Config.SpreadStrength = 0.8f;
	Config.ZoneRepeatPenalty = 1.f;
	Config.UnusedZoneWeightMultiplier = 2.f;

	const TArray<FAeyerjiRiftTreasureSelectionCandidate> Candidates = {
		{TEXT("A"), FVector(0.f, 0.f, 0.f), 1.f, TEXT("North")},
		{TEXT("B"), FVector(100.f, 0.f, 0.f), 100.f, TEXT("North")},
		{TEXT("C"), FVector(1000.f, 0.f, 0.f), 1.f, TEXT("South")},
		{TEXT("D"), FVector(2000.f, 0.f, 0.f), 1.f, TEXT("East")},
		{TEXT("E"), FVector(3000.f, 0.f, 0.f), 1.f, TEXT("West")}
	};

	FRandomStream FirstStream(74819537);
	const int32 RequestedCount = AeyerjiRiftTreasureRules::RollRequestedChestCount(FirstStream, Config);
	const TArray<int32> FirstSelection = AeyerjiRiftTreasureRules::SelectCandidateIndices(
		Candidates, RequestedCount, FirstStream, Config);

	FRandomStream SecondStream(74819537);
	const int32 RepeatedRequestedCount = AeyerjiRiftTreasureRules::RollRequestedChestCount(SecondStream, Config);
	const TArray<int32> RepeatedSelection = AeyerjiRiftTreasureRules::SelectCandidateIndices(
		Candidates, RepeatedRequestedCount, SecondStream, Config);

	TestEqual(TEXT("The configured fixed count is requested."), RequestedCount, 3);
	TestEqual(TEXT("The same seed requests the same count."), RepeatedRequestedCount, RequestedCount);
	TestEqual(TEXT("The same seed produces the same selected count."), RepeatedSelection.Num(), FirstSelection.Num());
	for (int32 SelectionIndex = 0; SelectionIndex < FirstSelection.Num(); ++SelectionIndex)
	{
		TestEqual(
			FString::Printf(TEXT("The same seed reproduces selected index %d."), SelectionIndex),
			RepeatedSelection[SelectionIndex],
			FirstSelection[SelectionIndex]);
	}

	for (int32 FirstIndex = 0; FirstIndex < FirstSelection.Num(); ++FirstIndex)
	{
		for (int32 SecondIndex = FirstIndex + 1; SecondIndex < FirstSelection.Num(); ++SecondIndex)
		{
			const float Distance = FVector::Dist2D(
				Candidates[FirstSelection[FirstIndex]].NavigationAnchor,
				Candidates[FirstSelection[SecondIndex]].NavigationAnchor);
			TestTrue(
				FString::Printf(TEXT("Selected pair %d/%d obeys the hard separation."), FirstIndex, SecondIndex),
				Distance >= Config.HardMinimumChestSeparation);
		}
	}

	TestFalse(TEXT("The high-weight point inside the hard separation cannot coexist with A."),
		FirstSelection.Contains(0) && FirstSelection.Contains(1));
	return true;
}

#endif
