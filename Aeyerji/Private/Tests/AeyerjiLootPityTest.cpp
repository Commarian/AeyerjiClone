#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AeyerjiGameplayTags.h"
#include "Engine/GameInstance.h"
#include "Items/ItemDefinition.h"
#include "Player/PlayerLootStats.h"
#include "Systems/AeyerjiDifficultyTuning.h"
#include "Systems/LootService.h"

namespace
{
	UItemDefinition* MakeLootGateDefinition(UObject* Outer, EItemCategory Category)
	{
		UItemDefinition* Definition = NewObject<UItemDefinition>(Outer);
		if (Definition)
		{
			Definition->ItemCategory = Category;
			Definition->DefaultSlot = static_cast<EEquipmentSlot>(Category);
			Definition->RequiredLevel = Category == EItemCategory::Corruption ? 50 : 1;
		}
		return Definition;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiLootPityMemoryTest,
	"Aeyerji.Loot.PityMemory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiLootPityMemoryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FPlayerLootStats Stats;
	const FGameplayTag PityGroup = AeyerjiTags::Loot_Pity_BossUnique;
	TestTrue(TEXT("Automation pity group tag is registered."), PityGroup.IsValid());
	if (!PityGroup.IsValid())
	{
		return false;
	}

	Stats.RecordPityAttempt(PityGroup, false, NAME_None, AeyerjiTags::Loot_Source_Boss);
	const FAeyerjiLootPityMemory* FirstMemory = Stats.FindPityMemory(PityGroup);
	TestNotNull(TEXT("Failed pity attempt creates memory."), FirstMemory);
	if (!FirstMemory)
	{
		return false;
	}

	TestEqual(TEXT("Failed pity attempt increments attempts since success."), FirstMemory->AttemptsSinceLastSuccess, 1);
	TestEqual(TEXT("Failed pity attempt increments total attempts."), FirstMemory->TotalAttempts, 1);
	TestEqual(TEXT("Failed pity attempt does not increment successes."), FirstMemory->TotalSuccesses, 0);
	TestTrue(TEXT("Failed pity attempt records source tag."), FirstMemory->LastSourceTag == FGameplayTag(AeyerjiTags::Loot_Source_Boss));

	const FName RewardKey(TEXT("/Game/Items/DA_AutomationBossReward.DA_AutomationBossReward"));
	Stats.RecordPityAttempt(PityGroup, true, RewardKey, AeyerjiTags::Loot_Source_Boss);
	const FAeyerjiLootPityMemory* SuccessMemory = Stats.FindPityMemory(PityGroup);
	TestNotNull(TEXT("Successful pity attempt keeps memory."), SuccessMemory);
	if (!SuccessMemory)
	{
		return false;
	}

	TestEqual(TEXT("Successful pity attempt resets attempts since success."), SuccessMemory->AttemptsSinceLastSuccess, 0);
	TestEqual(TEXT("Successful pity attempt increments total attempts."), SuccessMemory->TotalAttempts, 2);
	TestEqual(TEXT("Successful pity attempt increments successes."), SuccessMemory->TotalSuccesses, 1);
	TestEqual(TEXT("Successful pity attempt stores last definition key."), SuccessMemory->LastDroppedItemDefinitionKey, RewardKey);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiLootPityChanceTest,
	"Aeyerji.Loot.PityChance",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiLootPityChanceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FPlayerLootStats Stats;
	const FGameplayTag PityGroup = AeyerjiTags::Loot_Pity_BossUnique;
	TestTrue(TEXT("Automation pity group tag is registered."), PityGroup.IsValid());
	if (!PityGroup.IsValid())
	{
		return false;
	}

	Stats.RecordPityAttempt(PityGroup, false);
	Stats.RecordPityAttempt(PityGroup, false);

	FLootContext Context;
	Context.PityGroup = PityGroup;
	Context.BaseLegendaryChance = 0.05f;
	Context.PityHardAttemptsOverride = 2;

	UGameInstance* GameInstance = NewObject<UGameInstance>(GetTransientPackage());
	ULootService* LootService = NewObject<ULootService>(GameInstance);
	TestNotNull(TEXT("Loot service can be constructed for pure chance computation."), LootService);
	if (!LootService)
	{
		return false;
	}

	TestEqual(TEXT("Named pity hard threshold forces legendary chance."), LootService->ComputeLegendaryChance(Context, Stats), 1.0f);

	Context.PityHardAttemptsOverride = 10;
	Context.PitySoftStartOverride = 1;
	Context.PitySoftSlopeOverride = 0.1f;
	Context.PityMaxChanceOverride = 0.5f;
	TestTrue(
		TEXT("Named pity soft threshold adds chance from group misses."),
		FMath::IsNearlyEqual(LootService->ComputeLegendaryChance(Context, Stats), 0.15f));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiLootCorruptionLevelGateTest,
	"Aeyerji.Loot.CorruptionLevelGate",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiLootCorruptionLevelGateTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UGameInstance* GameInstance = NewObject<UGameInstance>(GetTransientPackage());
	ULootService* LootService = NewObject<ULootService>(GameInstance);
	UItemDefinition* NormalDefinition = MakeLootGateDefinition(LootService, EItemCategory::Assault);
	UItemDefinition* CorruptionDefinition = MakeLootGateDefinition(LootService, EItemCategory::Corruption);
	TestNotNull(TEXT("Loot service can be constructed."), LootService);
	TestNotNull(TEXT("Normal forced definition can be constructed."), NormalDefinition);
	TestNotNull(TEXT("Corruption forced definition can be constructed."), CorruptionDefinition);
	if (!LootService || !NormalDefinition || !CorruptionDefinition)
	{
		return false;
	}

	FLootContext Context;
	Context.EnemyLevel = 49;
	Context.PlayerLevel = 49;
	Context.ItemLevelJitterMin = 0;
	Context.ItemLevelJitterMax = 0;
	Context.ForcedItemDefinition = CorruptionDefinition;

	const FLootDropResult Level49Result = LootService->RollLoot(Context);
	TestNotEqual(TEXT("Forced Corruption definition is filtered below level 50."), Level49Result.ItemDefinition.Get(), CorruptionDefinition);

	Context.ForcedItemDefinition = NormalDefinition;
	const FLootDropResult NormalResult = LootService->RollLoot(Context);
	TestEqual(TEXT("Forced normal definition remains eligible below level 50."), NormalResult.ItemDefinition.Get(), NormalDefinition);

	Context.PlayerLevel = 50;
	Context.EnemyLevel = 50;
	Context.ForcedItemDefinition = CorruptionDefinition;
	const FLootDropResult Level50Result = LootService->RollLoot(Context);
	TestEqual(TEXT("Forced Corruption definition becomes eligible at level 50."), Level50Result.ItemDefinition.Get(), CorruptionDefinition);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiLootItemLevelJitterClampsAtMaxTest,
	"Aeyerji.Loot.ItemLevelJitterClampsAtMax",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiLootItemLevelJitterClampsAtMaxTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	UGameInstance* GameInstance = NewObject<UGameInstance>(GetTransientPackage());
	ULootService* LootService = NewObject<ULootService>(GameInstance);
	UItemDefinition* NormalDefinition = MakeLootGateDefinition(LootService, EItemCategory::Assault);
	TestNotNull(TEXT("Loot service can be constructed."), LootService);
	TestNotNull(TEXT("Forced normal definition can be constructed."), NormalDefinition);
	if (!LootService || !NormalDefinition)
	{
		return false;
	}

	FLootContext Context;
	Context.EnemyLevel = UAeyerjiDifficultySettings::GetMaxGameplayLevel();
	Context.PlayerLevel = UAeyerjiDifficultySettings::GetMaxGameplayLevel();
	Context.ItemLevelJitterMin = 0;
	Context.ItemLevelJitterMax = 2;
	Context.ForcedItemDefinition = NormalDefinition;

	const FLootDropResult Result = LootService->RollLoot(Context);
	TestEqual(TEXT("Loot item level is clamped to max gameplay level after positive jitter."), Result.ItemLevel, UAeyerjiDifficultySettings::GetMaxGameplayLevel());
	TestEqual(TEXT("Forced normal definition remains selected."), Result.ItemDefinition.Get(), NormalDefinition);
	return true;
}

#endif
