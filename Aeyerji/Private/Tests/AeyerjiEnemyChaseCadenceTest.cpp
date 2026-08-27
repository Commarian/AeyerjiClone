#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include "Enemy/EnemyAIController.h"
#include "Engine/World.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiEnemyChaseCadenceTest,
	"Aeyerji.Enemy.Movement.ChaseSprintCadence",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiEnemyChaseCadenceTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	const AEnemyAIController* NativeDefaults = GetDefault<AEnemyAIController>();
	TestNotNull(TEXT("Native enemy controller defaults are available."), NativeDefaults);
	if (!NativeDefaults)
	{
		return false;
	}

	TestTrue(TEXT("Shared chase sprint cadence is enabled by default."),
		NativeDefaults->bEnableChaseSprintCadence);
	TestTrue(TEXT("A far sprint lasts 1.5 seconds by default."),
		FMath::IsNearlyEqual(NativeDefaults->ChaseSprintDurationSeconds, 1.5f));
	TestTrue(TEXT("Sprint recovery lasts 5 seconds by default."),
		FMath::IsNearlyEqual(NativeDefaults->ChaseSprintRecoverySeconds, 5.f));
	TestTrue(TEXT("A new sprint requires 250 cm beyond attack range."),
		FMath::IsNearlyEqual(NativeDefaults->ChaseSprintReengageDistance, 250.f));
	TestTrue(TEXT("Speed changes blend at 900 MaxWalkSpeed units per second."),
		FMath::IsNearlyEqual(NativeDefaults->ChaseSpeedChangeRate, 900.f));

	static const FName TuningPropertyNames[] =
	{
		GET_MEMBER_NAME_CHECKED(AEnemyAIController, bEnableChaseSprintCadence),
		GET_MEMBER_NAME_CHECKED(AEnemyAIController, ChaseSprintDurationSeconds),
		GET_MEMBER_NAME_CHECKED(AEnemyAIController, ChaseSprintRecoverySeconds),
		GET_MEMBER_NAME_CHECKED(AEnemyAIController, ChaseSprintReengageDistance),
		GET_MEMBER_NAME_CHECKED(AEnemyAIController, ChaseSpeedChangeRate)
	};
	for (const FName PropertyName : TuningPropertyNames)
	{
		const FProperty* Property = FindFProperty<FProperty>(AEnemyAIController::StaticClass(), PropertyName);
		TestNotNull(*FString::Printf(TEXT("%s remains reflected."), *PropertyName.ToString()), Property);
		if (Property)
		{
			TestTrue(*FString::Printf(TEXT("%s cannot be overridden per controller instance."), *PropertyName.ToString()),
				Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance));
		}
	}

	UWorld* World = GWorld;
	TestNotNull(TEXT("Automation has a world for the controller instance."), World);
	AEnemyAIController* Controller = World
		? World->SpawnActor<AEnemyAIController>(AEnemyAIController::StaticClass(), FTransform::Identity)
		: nullptr;
	TestNotNull(TEXT("Enemy controller spawns for deterministic cadence coverage."), Controller);
	if (!Controller)
	{
		return false;
	}

	constexpr float AttackRange = 150.f;
	constexpr float WalkSpeed = 300.f;
	constexpr float RunSpeed = 500.f;
	constexpr float SprintThreshold = AttackRange + 250.f;

	Controller->ResetChaseSprintCadenceForAutomation();
	TestEqual(TEXT("The re-engagement boundary itself remains WalkSpeed."),
		Controller->ResolveChaseCadenceSpeedForAutomation(
			SprintThreshold, AttackRange, 0.0, WalkSpeed, RunSpeed),
		WalkSpeed);
	TestEqual(TEXT("Moving beyond the boundary starts a RunSpeed sprint."),
		Controller->ResolveChaseCadenceSpeedForAutomation(
			SprintThreshold + 1.f, AttackRange, 0.0, WalkSpeed, RunSpeed),
		RunSpeed);
	TestTrue(TEXT("The controller remembers an active sprint across StateTree state changes."),
		Controller->IsChaseSprintingForAutomation());
	TestEqual(TEXT("The sprint remains active immediately before its deadline."),
		Controller->ResolveChaseCadenceSpeedForAutomation(
			SprintThreshold + 1.f, AttackRange, 1.49, WalkSpeed, RunSpeed),
		RunSpeed);
	TestEqual(TEXT("The sprint deadline switches the enemy to WalkSpeed."),
		Controller->ResolveChaseCadenceSpeedForAutomation(
			SprintThreshold + 1.f, AttackRange, 1.5, WalkSpeed, RunSpeed),
		WalkSpeed);
	TestFalse(TEXT("The bounded sprint phase is no longer active during recovery."),
		Controller->IsChaseSprintingForAutomation());
	TestTrue(TEXT("The five-second recovery deadline is recorded in absolute server time."),
		FMath::IsNearlyEqual(Controller->GetChaseSprintRecoveryEndTimeForAutomation(), 6.5));
	TestEqual(TEXT("A far target cannot restart sprint before recovery expires."),
		Controller->ResolveChaseCadenceSpeedForAutomation(
			SprintThreshold + 1.f, AttackRange, 6.49, WalkSpeed, RunSpeed),
		WalkSpeed);
	TestEqual(TEXT("Recovery expiry does not sprint while the target remains inside the boundary."),
		Controller->ResolveChaseCadenceSpeedForAutomation(
			SprintThreshold, AttackRange, 6.5, WalkSpeed, RunSpeed),
		WalkSpeed);
	TestEqual(TEXT("Recovery expiry permits another far sprint."),
		Controller->ResolveChaseCadenceSpeedForAutomation(
			SprintThreshold + 1.f, AttackRange, 6.5, WalkSpeed, RunSpeed),
		RunSpeed);
	TestEqual(TEXT("Entering the close-engagement band immediately ends the sprint."),
		Controller->ResolveChaseCadenceSpeedForAutomation(
			AttackRange, AttackRange, 6.75, WalkSpeed, RunSpeed),
		WalkSpeed);
	TestTrue(TEXT("Close engagement starts a fresh five-second recovery."),
		FMath::IsNearlyEqual(Controller->GetChaseSprintRecoveryEndTimeForAutomation(), 11.75));

	Controller->ResetChaseSprintCadenceForAutomation();
	TestEqual(TEXT("A fresh far chase starts another sprint before testing the exit seam."),
		Controller->ResolveChaseCadenceSpeedForAutomation(
			SprintThreshold + 1.f, AttackRange, 20.0, WalkSpeed, RunSpeed),
		RunSpeed);
	Controller->EndChaseSprintCadenceForAutomation(20.25);
	TestFalse(TEXT("Leaving Move To Attack Range ends an active sprint."),
		Controller->IsChaseSprintingForAutomation());
	TestTrue(TEXT("Leaving chase starts the full recovery without resetting cadence state."),
		FMath::IsNearlyEqual(Controller->GetChaseSprintRecoveryEndTimeForAutomation(), 25.25));
	TestEqual(TEXT("The chase-exit recovery blocks an immediate far re-sprint."),
		Controller->ResolveChaseCadenceSpeedForAutomation(
			SprintThreshold + 1.f, AttackRange, 25.24, WalkSpeed, RunSpeed),
		WalkSpeed);

	Controller->ResetChaseSprintCadenceForAutomation();
	Controller->bEnableChaseSprintCadence = false;
	TestEqual(TEXT("Disabling cadence preserves the legacy far-chase RunSpeed behavior."),
		Controller->ResolveChaseCadenceSpeedForAutomation(
			SprintThreshold + 1.f, AttackRange, 0.0, WalkSpeed, RunSpeed),
		RunSpeed);
	TestFalse(TEXT("Disabled cadence does not retain a sprint phase."),
		Controller->IsChaseSprintingForAutomation());
	Controller->bEnableChaseSprintCadence = true;

	Controller->ResetChaseSprintCadenceForAutomation();
	TestFalse(TEXT("An explicit cadence reset clears the active sprint phase."),
		Controller->IsChaseSprintingForAutomation());
	TestTrue(TEXT("An explicit cadence reset clears the recovery deadline."),
		Controller->GetChaseSprintRecoveryEndTimeForAutomation() < 0.0);

	Controller->Destroy();
	return true;
}

#endif
