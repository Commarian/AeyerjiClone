#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Director/AeyerjiEncounterDirector.h"
#include "Director/AeyerjiLevelDirector.h"
#include "Director/AeyerjiSpawnerGroup.h"
#include "Director/AeyerjiSpawnRegion.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Enemy/EnemyParentNative.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "Inventory/AeyerjiLootPickup.h"
#include "Inventory/AeyerjiRewardPresentationActor.h"
#include "Items/ItemDefinition.h"
#include "Systems/AeyerjiRiftTypes.h"
#include "Systems/AeyerjiDifficultyTuning.h"

namespace
{
	template <typename TActor>
	TActor* SpawnAutomationActor(UWorld* World)
	{
		return World
			? World->SpawnActor<TActor>(TActor::StaticClass(), FTransform::Identity)
			: nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiRiftWeightedRegistrationAuthorityTest,
	"Aeyerji.Rift.Authority.WeightedRegistration",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiRiftWeightedRegistrationAuthorityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UWorld* World = GWorld;
	TestNotNull(TEXT("Automation has a world."), World);
	if (!World)
	{
		return false;
	}

	AAeyerjiEncounterDirector* Director = SpawnAutomationActor<AAeyerjiEncounterDirector>(World);
	TestNotNull(TEXT("Encounter director spawns."), Director);
	if (!Director)
	{
		return false;
	}
	Director->BeginWeightedProgressRun(42, 10);

	AEnemyParentNative* FirstEnemy = SpawnAutomationActor<AEnemyParentNative>(World);
	TestNotNull(TEXT("First progress enemy spawns."), FirstEnemy);
	if (!FirstEnemy)
	{
		Director->Destroy();
		return false;
	}
	Director->RegisterProgressEnemy(FirstEnemy, 5, 42);
	Director->RegisterProgressEnemy(FirstEnemy, 9, 42);
	TestEqual(TEXT("The first registration stores immutable authored points."),
		Director->GetRegisteredProgressPointsForAutomation(FirstEnemy), 5);
	Director->NotifyProgressEnemyDiedForAutomation(FirstEnemy);
	TestEqual(TEXT("Duplicate registration preserves the first immutable point value."), Director->GetWeightedProgressPoints(), 5);
	TestEqual(TEXT("One accepted death increments actual defeats once."), Director->GetEnemiesDefeated(), 1);

	AEnemyParentNative* DestroyedEnemy = SpawnAutomationActor<AEnemyParentNative>(World);
	Director->RegisterProgressEnemy(DestroyedEnemy, 5, 42);
	Director->NotifyProgressEnemyDestroyedForAutomation(DestroyedEnemy);
	DestroyedEnemy->Destroy();
	TestEqual(TEXT("Destruction without death grants no weighted progress."), Director->GetWeightedProgressPoints(), 5);
	TestEqual(TEXT("Destruction without death grants no defeat."), Director->GetEnemiesDefeated(), 1);

	AEnemyParentNative* CompletingEnemy = SpawnAutomationActor<AEnemyParentNative>(World);
	Director->RegisterProgressEnemy(CompletingEnemy, 8, 42);
	Director->NotifyProgressEnemyDiedForAutomation(CompletingEnemy);
	TestEqual(TEXT("Accepted death clamps progress at the target."), Director->GetWeightedProgressPoints(), 10);
	TestEqual(TEXT("Completing death increments actual defeats."), Director->GetEnemiesDefeated(), 2);

	AEnemyParentNative* LateEnemy = SpawnAutomationActor<AEnemyParentNative>(World);
	Director->RegisterProgressEnemy(LateEnemy, 5, 42);
	Director->NotifyProgressEnemyDiedForAutomation(LateEnemy);
	TestEqual(TEXT("Progress remains frozen after reaching the target."), Director->GetWeightedProgressPoints(), 10);
	TestEqual(TEXT("Late events do not increment defeats."), Director->GetEnemiesDefeated(), 2);

	FirstEnemy->Destroy();
	CompletingEnemy->Destroy();
	LateEnemy->Destroy();
	Director->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiRiftSimultaneousActivationAuthorityTest,
	"Aeyerji.Rift.Authority.SimultaneousActivation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiRiftSimultaneousActivationAuthorityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	AAeyerjiSpawnRegion* Region = SpawnAutomationActor<AAeyerjiSpawnRegion>(GWorld);
	TestNotNull(TEXT("Spawn region spawns."), Region);
	if (!Region)
	{
		return false;
	}

	TestTrue(TEXT("An untagged positive-weight region is eligible by default."), Region->IsRiftEncounterEligible());
	Region->Tags.Add(AAeyerjiSpawnRegion::RiftExcludedActorTag);
	TestFalse(TEXT("Rift.Excluded removes a boss or unsafe region from discovery."), Region->IsRiftEncounterEligible());
	Region->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiRiftFrozenEnemyScalingAuthorityTest,
	"Aeyerji.Rift.Authority.FrozenEnemyScaling",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiRiftFrozenEnemyScalingAuthorityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	AAeyerjiLevelDirector* Director = SpawnAutomationActor<AAeyerjiLevelDirector>(GWorld);
	TestNotNull(TEXT("Level director spawns."), Director);
	if (!Director)
	{
		return false;
	}

	FAeyerjiRiftTierRow TierRow;
	TierRow.HealthMultiplier = 1.35f;
	TierRow.DamageMultiplier = 1.12f;
	TierRow.DefenseMultiplier = 1.08f;
	TierRow.RewardQualityMultiplier = 1.05f;
	TierRow.EnemyBudget = 120;
	TierRow.MaxActivityLevel = 20;
	FAeyerjiRiftMonsterPowerSnapshot Expected;
	Expected.MonsterPowerIndex = 2;
	Expected.HealthMultiplier = TierRow.HealthMultiplier;
	Expected.DamageMultiplier = TierRow.DamageMultiplier;
	Expected.DefenseMultiplier = TierRow.DefenseMultiplier;
	Expected.RewardQualityMultiplier = TierRow.RewardQualityMultiplier;
	FAeyerjiRiftActivitySnapshot Activity;
	Activity.ActivityType = EAeyerjiRiftActivityType::StandardRift;
	Activity.ActivityLevel = 1;
	TestTrue(TEXT("Authority accepts a level-1 Standard Rift snapshot."),
		Director->ApplyRiftActivityForNextRun(Activity, nullptr));
	TestEqual(TEXT("A level-1 Standard Rift resolves level-1 enemies."),
		Director->GetActiveRiftActivity().ActivityLevel, 1);

	Activity.ActivityType = EAeyerjiRiftActivityType::Excursion;
	Activity.ActivityLevel = 10;
	Activity.ExcursionTier = 2;
	TestTrue(TEXT("Authority accepts the frozen Excursion activity snapshot."),
		Director->ApplyRiftActivityForNextRun(Activity, &TierRow));
	TestEqual(TEXT("Excursion freezes the resolved launch Activity Level."),
		Director->GetActiveRiftActivity().ActivityLevel, 10);
	TestEqual(TEXT("Excursion freezes its selected tier separately from enemy level."),
		Director->GetActiveRiftActivity().ExcursionTier, 2);
	Activity.ActivityLevel = 50;
	TestTrue(TEXT("Authority accepts a capped Excursion snapshot."),
		Director->ApplyRiftActivityForNextRun(Activity, &TierRow));
	TestEqual(TEXT("An early Excursion tier caps a level-50 launch party."),
		Director->GetActiveRiftActivity().ActivityLevel, TierRow.MaxActivityLevel);
	TestTrue(TEXT("Tier health power is frozen."), FMath::IsNearlyEqual(
		Director->GetActiveRiftMonsterPower().HealthMultiplier, Expected.HealthMultiplier));

	TierRow.HealthMultiplier = 99.f;
	TestTrue(TEXT("Live table-row edits cannot rescale the frozen run."), FMath::IsNearlyEqual(
		Director->GetActiveRiftMonsterPower().HealthMultiplier, Expected.HealthMultiplier));
	TestEqual(TEXT("Future groups retain the frozen activity snapshot despite a later player level."),
		Director->GetActiveRiftActivity().ActivityLevel, TierRow.MaxActivityLevel);
	TestTrue(TEXT("Health receives Rift monster power."), Director->GetRiftAttributeMultiplier(
		UAeyerjiAttributeSet::GetHPMaxAttribute()) > 1.f);
	TestTrue(TEXT("Damage receives Rift monster power."), Director->GetRiftAttributeMultiplier(
		UAeyerjiAttributeSet::GetAttackDamageAttribute()) > 1.f);
	TestTrue(TEXT("Defense receives Rift monster power."), Director->GetRiftAttributeMultiplier(
		UAeyerjiAttributeSet::GetArmorAttribute()) > 1.f);
	TestTrue(TEXT("Movement speed is not indiscriminately rescaled."), FMath::IsNearlyEqual(
		Director->GetRiftAttributeMultiplier(UAeyerjiAttributeSet::GetRunSpeedAttribute()), 1.f));
	Director->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiRiftPrivateRewardClaimAuthorityTest,
	"Aeyerji.Rift.Authority.PrivateRewardClaims",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiRiftPrivateRewardClaimAuthorityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UWorld* World = GWorld;
	AAeyerjiRewardPresentationActor* Cache = SpawnAutomationActor<AAeyerjiRewardPresentationActor>(World);
	APlayerState* FirstPlayer = SpawnAutomationActor<APlayerState>(World);
	APlayerState* SecondPlayer = SpawnAutomationActor<APlayerState>(World);
	APlayerState* Outsider = SpawnAutomationActor<APlayerState>(World);
	TestNotNull(TEXT("Reward cache spawns."), Cache);
	TestNotNull(TEXT("First player state spawns."), FirstPlayer);
	TestNotNull(TEXT("Second player state spawns."), SecondPlayer);
	if (!Cache || !FirstPlayer || !SecondPlayer || !Outsider)
	{
		return false;
	}

	UItemDefinition* Definition = NewObject<UItemDefinition>(Cache);
	Definition->RequiredLevel = 1;
	Definition->InventorySize = FIntPoint(1, 1);
	FLootDropResult FirstResult;
	FirstResult.ItemDefinition = Definition;
	FirstResult.ItemLevel = 1;
	FirstResult.Seed = 1001;
	FLootDropResult SecondResult = FirstResult;
	SecondResult.Seed = 2002;
	Cache->AddPrivateRewardBundle(FirstPlayer, {FirstResult}, FGameplayTag());
	Cache->AddPrivateRewardBundle(SecondPlayer, {SecondResult}, FGameplayTag());

	TestFalse(TEXT("A player without a bundle cannot consume another player's reward."),
		Cache->ReleaseStoredLootAtTransform(FTransform::Identity, Outsider));
	TestEqual(TEXT("Outsider attempt leaves first bundle pending."), Cache->GetPendingPrivateRewardCount(FirstPlayer), 1);
	TestEqual(TEXT("Outsider attempt leaves second bundle pending."), Cache->GetPendingPrivateRewardCount(SecondPlayer), 1);

	TestTrue(TEXT("First player consumes only the first private bundle."),
		Cache->ReleaseStoredLootAtTransform(FTransform::Identity, FirstPlayer));
	TestEqual(TEXT("First bundle is consumed once."), Cache->GetPendingPrivateRewardCount(FirstPlayer), 0);
	TestEqual(TEXT("Second bundle remains private and pending."), Cache->GetPendingPrivateRewardCount(SecondPlayer), 1);
	TestFalse(TEXT("Repeated first-player claim cannot duplicate pickups."),
		Cache->ReleaseStoredLootAtTransform(FTransform::Identity, FirstPlayer));

	TestTrue(TEXT("Second player can consume the remaining bundle."),
		Cache->ReleaseStoredLootAtTransform(FTransform::Identity, SecondPlayer));
	TestEqual(TEXT("Second bundle is consumed once."), Cache->GetPendingPrivateRewardCount(SecondPlayer), 0);
	TestTrue(TEXT("Shared cache completes only after every private bundle is released."), Cache->HasReleasedReward());

	int32 FirstOwnedPickups = 0;
	int32 SecondOwnedPickups = 0;
	for (TActorIterator<AAeyerjiLootPickup> It(World); It; ++It)
	{
		if (It->GetReservedPlayerState() == FirstPlayer)
		{
			++FirstOwnedPickups;
			It->Destroy();
		}
		else if (It->GetReservedPlayerState() == SecondPlayer)
		{
			++SecondOwnedPickups;
			It->Destroy();
		}
	}
	TestEqual(TEXT("First player receives exactly one owned pickup."), FirstOwnedPickups, 1);
	TestEqual(TEXT("Second player receives exactly one owned pickup."), SecondOwnedPickups, 1);

	Cache->Destroy();
	FirstPlayer->Destroy();
	SecondPlayer->Destroy();
	Outsider->Destroy();
	return true;
}

#endif
