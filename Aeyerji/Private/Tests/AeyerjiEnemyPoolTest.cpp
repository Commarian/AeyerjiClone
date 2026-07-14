#include "Director/AeyerjiSpawnerGroup.h"

#include "GameFramework/Pawn.h"
#include "Misc/AutomationTest.h"

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
