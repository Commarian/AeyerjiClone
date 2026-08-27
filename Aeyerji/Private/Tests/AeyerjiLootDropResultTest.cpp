#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Systems/LootService.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiLootDropResultUsabilityTest,
	"Aeyerji.Loot.DropResultUsability",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiLootDropResultUsabilityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FLootDropResult EmptyResult;
	TestFalse(TEXT("A result without an item definition or definition key is rejected."),
		IsUsableLootDropResult(EmptyResult));

	FLootDropResult KeyedResult;
	KeyedResult.ItemDefinitionKey = TEXT("TestItemDefinition");
	TestTrue(TEXT("A result with valid rarities and a definition key is usable."),
		IsUsableLootDropResult(KeyedResult));

	FLootDropResult InvalidRarityResult = KeyedResult;
	InvalidRarityResult.Rarity = static_cast<EItemRarity>(255);
	TestFalse(TEXT("A result with an invalid rolled rarity is rejected."),
		IsUsableLootDropResult(InvalidRarityResult));

	FLootDropResult InvalidPityRarityResult = KeyedResult;
	InvalidPityRarityResult.PitySuccessRarity = static_cast<EItemRarity>(255);
	TestFalse(TEXT("A result with an invalid pity-success rarity is rejected."),
		IsUsableLootDropResult(InvalidPityRarityResult));

	return true;
}

#endif
