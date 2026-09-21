#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "AbilitySystemComponent.h"
#include "Aeyerji/AeyerjiPlayerController.h"
#include "AeyerjiGameplayTags.h"
#include "Enemy/EnemyAIController.h"
#include "Enemy/EnemyParentNative.h"
#include "Engine/World.h"
#include "Serialization/Csv/CsvParser.h"
#include "Testing/AeyerjiCombatBalanceTestHarness.h"

namespace
{
	struct FCombatResponsivenessTestWorld
	{
		UWorld* World = nullptr;

		FCombatResponsivenessTestWorld()
		{
			UWorld::InitializationValues Values;
			Values.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false)
				.CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
			World = UWorld::CreateWorld(
				EWorldType::Game,
				false,
				NAME_None,
				nullptr,
				true,
				ERHIFeatureLevel::Num,
				&Values);
		}

		~FCombatResponsivenessTestWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}
		}

		AEnemyParentNative* SpawnTarget(const FVector& Location) const
		{
			AEnemyParentNative* Target = World
				? World->SpawnActor<AEnemyParentNative>(Location, FRotator::ZeroRotator)
				: nullptr;
			if (Target)
			{
				Target->GetAbilitySystemComponent()->InitAbilityActorInfo(Target, Target);
			}
			return Target;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiCombatResponsivenessPolicyTest,
	"Aeyerji.CombatTest.Targeting.ControllerPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiCombatResponsivenessPolicyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCombatResponsivenessTestWorld Fixture;
	if (!Fixture.World)
	{
		return false;
	}

	AAeyerjiPlayerController* Controller = Fixture.World->SpawnActor<AAeyerjiPlayerController>();
	AEnemyParentNative* FirstTarget = Fixture.SpawnTarget(FVector(100.f, 0.f, 0.f));
	AEnemyParentNative* SecondTarget = Fixture.SpawnTarget(FVector(200.f, 0.f, 0.f));
	if (!Controller || !FirstTarget || !SecondTarget)
	{
		AddError(TEXT("Could not create controller-policy fixture."));
		return false;
	}

	Controller->AttackClickPhysicalKey = EKeys::LeftMouseButton;
	Controller->InteractClickPhysicalKey = EKeys::LeftMouseButton;
	TestTrue(
		TEXT("A shared physical key is detected so the dedicated interaction path is skipped."),
		Controller->IsInteractClickMappedToAttackClick());
	Controller->InteractClickPhysicalKey = EKeys::E;
	TestFalse(
		TEXT("A distinct interaction key keeps its dedicated action path."),
		Controller->IsInteractClickMappedToAttackClick());
	TestEqual(TEXT("Held death handoff defaults to 120 screen pixels."), Controller->HeldAttackRetargetScreenRadiusPx, 120.f);
	TestEqual(TEXT("Held death handoff defaults to 600 cm."), Controller->HeldAttackRetargetWorldRadiusCm, 600.f);
	TestTrue(
		TEXT("A candidate inside both death-handoff radii is accepted."),
		AAeyerjiPlayerController::IsHeldAttackReplacementWithinRadii(
			FMath::Square(119.f), FMath::Square(599.f), 120.f, 600.f));
	TestFalse(
		TEXT("Screen-radius rejection remains cursor-local."),
		AAeyerjiPlayerController::IsHeldAttackReplacementWithinRadii(
			FMath::Square(121.f), FMath::Square(100.f), 120.f, 600.f));
	TestFalse(
		TEXT("World-radius rejection prevents global auto-aim."),
		AAeyerjiPlayerController::IsHeldAttackReplacementWithinRadii(
			FMath::Square(10.f), FMath::Square(601.f), 120.f, 600.f));
	TestTrue(
		TEXT("Cursor distance is the first death-handoff ranking key."),
		AAeyerjiPlayerController::IsHeldAttackReplacementBetter(
			10.f, 500.f, TEXT("FarWorld"), 20.f, 10.f, TEXT("NearWorld"), true));
	TestTrue(
		TEXT("Former-target distance is the second death-handoff ranking key."),
		AAeyerjiPlayerController::IsHeldAttackReplacementBetter(
			10.f, 20.f, TEXT("FarName"), 10.f, 30.f, TEXT("NearName"), true));
	TestTrue(
		TEXT("Actor name supplies a stable final death-handoff tie-break."),
		AAeyerjiPlayerController::IsHeldAttackReplacementBetter(
			10.f, 20.f, TEXT("Alpha"), 10.f, 20.f, TEXT("Bravo"), true));
	TestTrue(
		TEXT("A matching held primary completion rearms regardless of cancellation reason."),
		AAeyerjiPlayerController::ShouldRearmHeldPrimaryCommand(
			true,
			EAeyerjiMouseButton::Left,
			EAeyerjiMousePhase::Held,
			EAeyerjiMouseIntent::BasicAttack,
			true));
	TestFalse(
		TEXT("A released physical button cannot rearm the command."),
		AAeyerjiPlayerController::ShouldRearmHeldPrimaryCommand(
			true,
			EAeyerjiMouseButton::Left,
			EAeyerjiMousePhase::Held,
			EAeyerjiMouseIntent::BasicAttack,
			false));
	TestTrue(TEXT("A newer server serial is accepted."), AAeyerjiPlayerController::IsNewerPrimaryCommandSerial(43, 42));
	TestFalse(TEXT("An older server serial is rejected."), AAeyerjiPlayerController::IsNewerPrimaryCommandSerial(41, 42));
	TestFalse(TEXT("Serial zero is never a contextual attack command."), AAeyerjiPlayerController::IsNewerPrimaryCommandSerial(0, 42));

	Controller->MouseCommand.Intent = EAeyerjiMouseIntent::BasicAttack;
	Controller->MouseCommand.TargetActor = FirstTarget;
	Controller->MouseCommand.CommandSerial = 42;
	Controller->MouseCommand.bAwaitingServerAttackResult = true;
	Controller->Client_PrimaryAttackActivationResult_Implementation(FirstTarget, 41, true);
	TestTrue(
		TEXT("A stale server confirmation cannot consume the current retarget command."),
		Controller->MouseCommand.bAwaitingServerAttackResult);
	Controller->Client_PrimaryAttackActivationResult_Implementation(SecondTarget, 42, true);
	TestTrue(
		TEXT("A confirmation for another target cannot consume the current command."),
		Controller->MouseCommand.bAwaitingServerAttackResult);
	Controller->Client_PrimaryAttackActivationResult_Implementation(FirstTarget, 42, false);
	TestFalse(
		TEXT("The matching server response releases the pending command for retry."),
		Controller->MouseCommand.bAwaitingServerAttackResult);

	Controller->LastServerPrimaryAttackCommandSerial = 50;
	Controller->LastServerPrimaryAttackCommandTarget = SecondTarget;
	Controller->LastPrimaryAttackTarget = SecondTarget;
	Controller->Server_PreparePrimaryAttackForRetarget_Implementation(FirstTarget, 49, true, false);
	TestEqual(
		TEXT("A delayed server retarget cannot replace the newest accepted target."),
		Controller->LastPrimaryAttackTarget.Get(),
		static_cast<AActor*>(SecondTarget));
	Controller->Server_PreparePrimaryAttackForRetarget_Implementation(FirstTarget, 51, true, false);
	TestEqual(
		TEXT("A newer server retarget becomes the accepted explicit target."),
		Controller->LastPrimaryAttackTarget.Get(),
		static_cast<AActor*>(FirstTarget));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiTargetingTelemetryTest,
	"Aeyerji.CombatTest.Targeting.Responsiveness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiTargetingTelemetryTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	FCombatResponsivenessTestWorld Fixture;
	if (!TestNotNull(TEXT("Isolated targeting world"), Fixture.World))
	{
		return false;
	}

	AAeyerjiCombatBalanceTestHarness* Harness =
		Fixture.World->SpawnActor<AAeyerjiCombatBalanceTestHarness>();
	AEnemyAIController* Controller = Fixture.World->SpawnActor<AEnemyAIController>();
	AEnemyParentNative* FirstTarget = Fixture.SpawnTarget(FVector(100.f, 0.f, 0.f));
	AEnemyParentNative* SecondTarget = Fixture.SpawnTarget(FVector(200.f, 0.f, 0.f));
	if (!Harness || !Controller || !FirstTarget || !SecondTarget)
	{
		AddError(TEXT("Could not create targeting telemetry fixture."));
		return false;
	}

	Harness->TestState = EAeyerjiCombatTestState::Running;
	Harness->CombatStartWorldTime = Fixture.World->GetTimeSeconds();
	AAeyerjiCombatBalanceTestHarness::RecordTargetingEvent(
		Controller,
		FirstTarget,
		SecondTarget,
		EAeyerjiCombatTargetingTelemetryEvent::PlayerTargetHandoff,
		TEXT("PreviousTargetInvalid"),
		17);
	AAeyerjiCombatBalanceTestHarness::RecordTargetingEvent(
		Controller,
		FirstTarget,
		SecondTarget,
		EAeyerjiCombatTargetingTelemetryEvent::PlayerPrimaryActivated,
		TEXT("ServerAccepted"),
		17);
	AAeyerjiCombatBalanceTestHarness::RecordTargetingEvent(
		Controller,
		SecondTarget,
		nullptr,
		EAeyerjiCombatTargetingTelemetryEvent::EnemyLeashTargetCleared,
		TEXT("LeashReturnHome"),
		0);

	Controller->SetCurrentTargetForAutomation(FirstTarget);
	TestTrue(
		TEXT("Assigning a target binds its death state."),
		Controller->IsCurrentTargetDeathStateBoundForAutomation(FirstTarget));
	Controller->SetCurrentTargetForAutomation(SecondTarget);
	TestTrue(
		TEXT("Retargeting moves the death-state binding."),
		Controller->IsCurrentTargetDeathStateBoundForAutomation(SecondTarget));
	TestTrue(TEXT("The shared StateTree guard accepts a live target."), Controller->EnsureCurrentTargetIsLive());

	FirstTarget->GetAbilitySystemComponent()->AddLooseGameplayTag(AeyerjiTags::State_Dead);
	TestEqual(
		TEXT("The old target cannot clear the replacement after unbinding."),
		Controller->GetTargetActor(),
		static_cast<AActor*>(SecondTarget));

	SecondTarget->GetAbilitySystemComponent()->AddLooseGameplayTag(AeyerjiTags::State_Dead);
	TestNull(TEXT("The current target clears synchronously when State.Dead is added."), Controller->GetTargetActor());
	TestFalse(TEXT("The shared StateTree guard rejects a missing target."), Controller->EnsureCurrentTargetIsLive());
	TestFalse(
		TEXT("Clearing the dead target removes its delegate binding."),
		Controller->IsCurrentTargetDeathStateBoundForAutomation(SecondTarget));
	TestEqual(TEXT("Exactly one current-target death was recorded."), Harness->EnemyDeadTargetClears, 1);
	TestEqual(TEXT("Exactly one leash return was recorded."), Harness->EnemyLeashTargetClears, 1);

	const FCsvParser Parser(Harness->BuildTargetingCSV());
	const FCsvParser::FRows& Rows = Parser.GetRows();
	TestEqual(TEXT("Header plus four authority targeting events."), Rows.Num(), 5);
	for (const auto& Row : Rows)
	{
		TestEqual(TEXT("Targeting CSV rows match the seven-column schema."), Row.Num(), 7);
	}
	TestTrue(
		TEXT("Targeting summary agrees with authoritative counters."),
		Harness->BuildTargetingSummary().Contains(TEXT("EnemyDeadTargetClears=1\n"))
			&& Harness->BuildTargetingSummary().Contains(TEXT("EnemyLeashTargetClears=1\n"))
			&& Harness->BuildTargetingSummary().Contains(TEXT("PlayerTargetHandoffs=1\n"))
			&& Harness->BuildTargetingSummary().Contains(TEXT("DeadTargetHandoffs=1\n")));

	Controller->SetCurrentTargetForAutomation(FirstTarget);
	Controller->RunUnPossessCleanupForAutomation();
	TestFalse(
		TEXT("Unpossession removes the target death binding."),
		Controller->IsCurrentTargetDeathStateBoundForAutomation(FirstTarget));

	Controller->SetCurrentTargetForAutomation(FirstTarget);
	AddExpectedError(TEXT("Crowd slot unavailable; using standard path following with RVO"), EAutomationExpectedErrorFlags::Contains, 1);
	AddExpectedError(TEXT("Cannot run StateTree Reason=PooledReuse"), EAutomationExpectedErrorFlags::Contains, 1);
	Controller->ResetForPooledReuse(FVector::ZeroVector);
	TestFalse(
		TEXT("Pooled reuse removes the target death binding."),
		Controller->IsCurrentTargetDeathStateBoundForAutomation(FirstTarget));

	Harness->ResetTargetingTelemetry();
	TestEqual(TEXT("Targeting reset clears events."), Harness->TargetingEvents.Num(), 0);
	TestEqual(TEXT("Targeting reset clears totals."), Harness->EnemyDeadTargetClears, 0);
	TestEqual(TEXT("Targeting reset clears leash totals."), Harness->EnemyLeashTargetClears, 0);
	TestEqual(TEXT("Targeting reset clears dead-target handoffs."), Harness->DeadTargetHandoffs, 0);
	return true;
}

#endif
