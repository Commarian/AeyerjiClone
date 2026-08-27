#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Director/AeyerjiEncounterDirector.h"
#include "Director/AeyerjiLevelDirector.h"
#include "Director/AeyerjiSpawnerGroup.h"
#include "Director/AeyerjiSpawnRegion.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Components/CapsuleComponent.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyParentNative.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerState.h"
#include "Inventory/AeyerjiLootPickup.h"
#include "Inventory/AeyerjiRewardPresentationActor.h"
#include "Items/ItemDefinition.h"
#include "Navigation/CrowdFollowingComponent.h"
#include "Navigation/CrowdManager.h"
#include "StateTree.h"
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
	TestEqual(TEXT("A weighted enemy is registered once for pressure and LOD tracking."),
		Director->GetTrackedEnemyCountForAutomation(), 1);
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
	TestEqual(TEXT("A newly placed region must be assigned an authored progression index."),
		Region->RiftProgressionIndex, INDEX_NONE);
	Region->RiftProgressionIndex = 0;
	TestEqual(TEXT("Progression index accepts the first authored route position."),
		Region->RiftProgressionIndex, 0);
	Region->Tags.Add(AAeyerjiSpawnRegion::RiftExcludedActorTag);
	TestFalse(TEXT("Rift.Excluded removes a boss or unsafe region from discovery."), Region->IsRiftEncounterEligible());
	Region->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiRiftEnemyRevealLockAuthorityTest,
	"Aeyerji.Rift.Authority.EnemyRevealLock",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiRiftEnemyRevealLockAuthorityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	AEnemyParentNative* Enemy = SpawnAutomationActor<AEnemyParentNative>(GWorld);
	TestNotNull(TEXT("Reveal-test enemy spawns."), Enemy);
	if (!Enemy)
	{
		return false;
	}

	Enemy->SetPooledEncounterInactive();
	TestEqual(TEXT("Pooled enemy publishes the inactive encounter phase."),
		Enemy->GetEncounterPhase(), EAeyerjiEnemyEncounterPhase::PooledInactive);
	TestFalse(TEXT("Pooled enemy is excluded from combat pressure."), Enemy->IsEncounterCombatActive());
	TestFalse(TEXT("Pooled enemy cannot take normal actor damage."), Enemy->CanBeDamaged());
	TestFalse(TEXT("Pooled enemy collision is disabled."), Enemy->GetActorEnableCollision());

	Enemy->PrepareForPooledActivation();
	TestTrue(TEXT("A pooled checkout restores damage participation after an inactive lock."), Enemy->CanBeDamaged());

	Enemy->CompleteEncounterReveal();
	Enemy->BeginEncounterReveal(EAeyerjiEnemyRevealStyle::GroundEmergence, 5.f);
	TestEqual(TEXT("Entrance publishes the revealing phase."),
		Enemy->GetEncounterPhase(), EAeyerjiEnemyEncounterPhase::Revealing);
	TestFalse(TEXT("Revealing enemy is excluded from combat pressure."), Enemy->IsEncounterCombatActive());
	TestFalse(TEXT("Revealing enemy cannot take normal actor damage."), Enemy->CanBeDamaged());
	TestFalse(TEXT("Revealing enemy collision remains disabled."), Enemy->GetActorEnableCollision());
	TestFalse(TEXT("Revealing enemy movement remains disabled."),
		Enemy->GetCharacterMovement()->IsComponentTickEnabled());

	// Simulate an authored pool/reveal hook leaving the movement component in a deeper
	// inactive state than DisableMovement. Native activation must repair this contract.
	Enemy->GetCharacterMovement()->Deactivate();
	Enemy->GetCharacterMovement()->SetUpdatedComponent(nullptr);
	Enemy->CompleteEncounterReveal();
	TestEqual(TEXT("Native timeout completion publishes the active phase."),
		Enemy->GetEncounterPhase(), EAeyerjiEnemyEncounterPhase::Active);
	TestTrue(TEXT("Completed reveal restores combat participation."), Enemy->IsEncounterCombatActive());
	TestTrue(TEXT("Completed reveal restores normal damage participation."), Enemy->CanBeDamaged());
	TestTrue(TEXT("Completed reveal restores actor collision."), Enemy->GetActorEnableCollision());
	TestTrue(TEXT("Completed reveal restores character movement ticking."),
		Enemy->GetCharacterMovement()->IsComponentTickEnabled());
	TestTrue(TEXT("Completed reveal restores an active character movement component."),
		Enemy->GetCharacterMovement()->IsActive());
	TestTrue(TEXT("Completed reveal keeps character movement registered."),
		Enemy->GetCharacterMovement()->IsRegistered());
	TestTrue(TEXT("Completed reveal restores the capsule as CharacterMovement's updated component."),
		Enemy->GetCharacterMovement()->UpdatedComponent.Get()
			== static_cast<USceneComponent*>(Enemy->GetCapsuleComponent()));
	TestEqual(TEXT("Completed reveal restores walking movement mode."),
		Enemy->GetCharacterMovement()->MovementMode, MOVE_Walking);

	Enemy->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiRiftEnemyStateTreePoolWakeAuthorityTest,
	"Aeyerji.Rift.Authority.EnemyStateTreePoolWake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiRiftEnemyStateTreePoolWakeAuthorityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	UStateTree* EnemyStateTree = LoadObject<UStateTree>(
		nullptr,
		TEXT("/Game/AI/StateTree/STEnemyGeneral.STEnemyGeneral"));
	TestNotNull(TEXT("The general enemy StateTree asset loads."), EnemyStateTree);

	UClass* EnemyClass = LoadClass<AEnemyParentNative>(
		nullptr,
		TEXT("/Game/Enemy/EnemyParent.EnemyParent_C"));
	TestNotNull(TEXT("The EnemyParent Blueprint class required by the StateTree schema loads."), EnemyClass);
	AEnemyParentNative* Enemy = GWorld && EnemyClass
		? GWorld->SpawnActor<AEnemyParentNative>(EnemyClass, FTransform::Identity)
		: nullptr;
	TestNotNull(TEXT("Pool-wake EnemyParent Blueprint spawns."), Enemy);
	if (Enemy && !Enemy->GetController())
	{
		Enemy->SpawnDefaultController();
	}
	AEnemyAIController* Controller = Enemy
		? Cast<AEnemyAIController>(Enemy->GetController())
		: nullptr;
	TestNotNull(TEXT("EnemyParent receives its configured Aeyerji AI controller."), Controller);
	if (!EnemyStateTree || !EnemyClass || !Controller || !Enemy)
	{
		if (Enemy)
		{
			Enemy->Destroy();
		}
		return false;
	}

	TestTrue(TEXT("EnemyParent controller uses the general enemy StateTree."),
		Controller->DefaultStateTree == EnemyStateTree);
	TestTrue(TEXT("Possession starts the configured enemy StateTree."),
		Controller->StateTreeComponent->IsRunning());

	UCrowdFollowingComponent* CrowdFollowing =
		Cast<UCrowdFollowingComponent>(Controller->GetPathFollowingComponent());
	TestNotNull(TEXT("Enemy controller uses its authored Crowd path follower."), CrowdFollowing);
	Controller->SetPathFollowingGameplayEnabled(false, TEXT("AutomationPooledInactive"));
	if (CrowdFollowing)
	{
		TestEqual(TEXT("An inactive pooled enemy releases its Detour Crowd slot."),
			CrowdFollowing->GetCrowdSimulationState(),
			ECrowdSimulationState::Disabled);
	}
	TestTrue(TEXT("A pooled wake establishes Crowd or a standard path-following fallback."),
		Controller->SetPathFollowingGameplayEnabled(true, TEXT("AutomationPooledReuse")));
	if (CrowdFollowing && CrowdFollowing->IsCrowdSimulationEnabled())
	{
		const UCrowdManager* CrowdManager = UCrowdManager::GetCurrent(GWorld);
		TestTrue(TEXT("Enabled Crowd simulation always owns a valid Detour agent slot."),
			CrowdManager && CrowdManager->IsAgentValid(CrowdFollowing));
	}

	Controller->StateTreeComponent->PauseLogic(TEXT("AutomationEncounterReveal"));
	TestTrue(TEXT("Encounter reveal pauses the running StateTree."),
		Controller->StateTreeComponent->IsPaused());
	Controller->StateTreeComponent->StopLogic(TEXT("AutomationPooledInactive"));
	TestFalse(TEXT("Pooled deactivation stops the StateTree."),
		Controller->StateTreeComponent->IsRunning());
	TestTrue(TEXT("UE 5.8 preserves the pause flag after StopLogic."),
		Controller->StateTreeComponent->IsPaused());

	TestTrue(TEXT("Pooled checkout restarts and resumes the configured StateTree."),
		Controller->EnsureConfiguredStateTreeRunning(TEXT("AutomationPooledReuse")));
	TestTrue(TEXT("Pooled checkout leaves the StateTree running."),
		Controller->StateTreeComponent->IsRunning());
	TestFalse(TEXT("Pooled checkout clears the stale reveal pause."),
		Controller->StateTreeComponent->IsPaused());

	Controller->UnPossess();
	Controller->Destroy();
	Enemy->Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiRiftEnemySleepWakeAuthorityTest,
	"Aeyerji.Rift.Authority.EnemySleepWake",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiRiftEnemySleepWakeAuthorityTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	AAeyerjiEncounterDirector* Director = SpawnAutomationActor<AAeyerjiEncounterDirector>(GWorld);
	AEnemyParentNative* Enemy = SpawnAutomationActor<AEnemyParentNative>(GWorld);
	TestNotNull(TEXT("Sleep-test director spawns."), Director);
	TestNotNull(TEXT("Sleep-test enemy spawns."), Enemy);
	if (!Director || !Enemy)
	{
		return false;
	}

	Director->BeginWeightedProgressRun(77, 20);
	Director->RegisterProgressEnemy(Enemy, 4, 77);
	Director->ConfigureRiftEnemyHomeForAutomation(Enemy, 2, FVector(100.f, 200.f, 0.f));
	Director->SetEnemySleepingForAutomation(Enemy, true);
	TestTrue(TEXT("Left-behind enemy enters the sleeping LOD state."),
		Director->IsEnemySleepingForAutomation(Enemy));
	TestEqual(TEXT("Sleeping preserves immutable objective registration."),
		Director->GetRegisteredProgressPointsForAutomation(Enemy), 4);
	TestEqual(TEXT("Sleeping grants no objective progress."), Director->GetWeightedProgressPoints(), 0);

	Director->SetEnemySleepingForAutomation(Enemy, false);
	TestFalse(TEXT("Returning to the region wakes the same enemy."),
		Director->IsEnemySleepingForAutomation(Enemy));
	TestEqual(TEXT("Waking preserves immutable objective registration."),
		Director->GetRegisteredProgressPointsForAutomation(Enemy), 4);

	Enemy->Destroy();
	Director->Destroy();
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
