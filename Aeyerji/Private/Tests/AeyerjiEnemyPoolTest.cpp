#include "Director/AeyerjiSpawnerGroup.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"

#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiEnemyPoolKeyAutomationTest,
	"Aeyerji.Spawning.EnemyPool.KeyIdentity",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiEnemyPoolKeyAutomationTest::RunTest(const FString& Parameters)
{
	const FGameplayTag ArchetypeTag = FGameplayTag::RequestGameplayTag(TEXT("State.Dead"), /*ErrorIfNotFound=*/false);

	FAeyerjiEnemyPoolKey BaseKey;
	BaseKey.EnemyClass = APawn::StaticClass();
	BaseKey.EnemyArchetypeTag = ArchetypeTag;
	BaseKey.bIsElite = true;

	FAeyerjiEnemyPoolKey MatchingKey = BaseKey;
	TestTrue(TEXT("Matching pool keys compare equal."), BaseKey == MatchingKey);
	TestEqual(TEXT("Matching pool keys hash equally."), GetTypeHash(BaseKey), GetTypeHash(MatchingKey));

	FAeyerjiEnemyPoolKey BossKey = BaseKey;
	BossKey.bIsBoss = true;
	TestFalse(TEXT("Boss role changes pool identity."), BaseKey == BossKey);

	FAeyerjiEnemyPoolSettings Settings;
	TestFalse(TEXT("Pooling is opt-in for placed spawners."), Settings.bEnablePooling);
	TestTrue(TEXT("Pool keeps inactive actors by default once enabled."), Settings.MaxInactivePerPoolKey > 0);
	TestTrue(TEXT("Prewarm budget is positive."), Settings.PrewarmPerTick > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiEnemyExactPoolPrewarmAutomationTest,
	"Aeyerji.Spawning.EnemyPool.ExactPrewarmCounts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiEnemyExactPoolPrewarmAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UWorld* World = GWorld;
	TestNotNull(TEXT("Automation has a world."), World);
	if (!World)
	{
		return false;
	}

	AAeyerjiSpawnerGroup* Spawner = World->SpawnActor<AAeyerjiSpawnerGroup>();
	TestNotNull(TEXT("Exact-prewarm spawner is available."), Spawner);
	if (!Spawner)
	{
		return false;
	}

	FEnemySet Ordinary;
	Ordinary.EnemyClass = APawn::StaticClass();
	Ordinary.Count = 1;
	FEnemySet Elite = Ordinary;
	Elite.bIsElite = true;

	Spawner->BeginExactPoolPrewarm(3);
	TestTrue(TEXT("First ordinary planned actor prewarms."), Spawner->PrewarmExactEnemy(Ordinary));
	TestTrue(TEXT("Second ordinary planned actor prewarms."), Spawner->PrewarmExactEnemy(Ordinary));
	TestTrue(TEXT("Elite identity prewarms separately."), Spawner->PrewarmExactEnemy(Elite));

	FAeyerjiEnemyPoolKey OrdinaryKey;
	OrdinaryKey.EnemyClass = APawn::StaticClass();
	FAeyerjiEnemyPoolKey EliteKey = OrdinaryKey;
	EliteKey.bIsElite = true;
	TestEqual(TEXT("Ordinary exact key retains its planned count."),
		Spawner->GetExactPoolKeyCapacity(OrdinaryKey), 2);
	TestEqual(TEXT("Elite exact key retains its separately planned count."),
		Spawner->GetExactPoolKeyCapacity(EliteKey), 1);
	TestEqual(TEXT("The exact inactive pool equals the frozen population."),
		Spawner->GetInactivePooledEnemyCount(), 3);
	TestEqual(TEXT("Every planned actor was constructed during prewarm."),
		Spawner->GetPrewarmConstructionCount(), 3);

	Spawner->FinalizeExactPoolPrewarm();
	Spawner->ReleaseEnemyPool(true);
	Spawner->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiSpawnerRuntimeWaveSanitizationAutomationTest,
	"Aeyerji.Spawning.RuntimeWaves.Sanitization",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiSpawnerRuntimeWaveSanitizationAutomationTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UWorld* World = GWorld;
	TestNotNull(TEXT("Automation has a world."), World);
	if (!World)
	{
		return false;
	}

	AAeyerjiSpawnerGroup* Spawner = World->SpawnActor<AAeyerjiSpawnerGroup>();
	TestNotNull(TEXT("Runtime-wave spawner is available."), Spawner);
	if (!Spawner)
	{
		return false;
	}

	TArray<FWaveDefinition> RuntimeWaves;
	RuntimeWaves.SetNum(257);
	RuntimeWaves[0].PostSpawnDelay = std::numeric_limits<float>::quiet_NaN();
	RuntimeWaves[0].EnemySets.SetNum(12);
	RuntimeWaves[0].EnemySets[0].Count = -10;
	for (int32 Index = 1; Index < RuntimeWaves[0].EnemySets.Num(); ++Index)
	{
		RuntimeWaves[0].EnemySets[Index].Count = MAX_int32;
		RuntimeWaves[0].EnemySets[Index].SpawnInterval = std::numeric_limits<float>::infinity();
	}

	Spawner->ActivateEncounterWithRuntimeWaves(RuntimeWaves);
	TestEqual(TEXT("Runtime wave count is bounded."), Spawner->GetWaveCount(), 256);
	TestEqual(TEXT("Negative and oversized set counts respect the total population budget."),
		Spawner->GetWaveEnemyTotal(0), 100000);

	Spawner->ResetEncounter();
	Spawner->Destroy();
	return true;
}
