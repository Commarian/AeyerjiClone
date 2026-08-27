#if WITH_DEV_AUTOMATION_TESTS

#include <limits>

#include "Misc/AutomationTest.h"

#include "Enemy/EnemyParentNative.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiEnemyDeathFacingTest,
	"Aeyerji.EnemyAI.DeathFacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiEnemyDeathFacingTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);

	FRotator FacingRotation = FRotator::ZeroRotator;
	TestTrue(
		TEXT("A horizontal killer direction resolves."),
		AEnemyParentNative::ResolveDeathFacingRotation(
			FVector::ZeroVector,
			FVector(0.f, 100.f, 400.f),
			FacingRotation));
	TestTrue(
		TEXT("Death facing ignores height and turns toward positive Y."),
		FMath::IsNearlyEqual(
			FMath::FindDeltaAngleDegrees(FacingRotation.Yaw, 90.f),
			0.f,
			KINDA_SMALL_NUMBER));
	TestTrue(TEXT("Death facing remains horizontal."), FacingRotation.Pitch == 0.f && FacingRotation.Roll == 0.f);

	TestFalse(
		TEXT("A purely vertical killer direction preserves the current facing."),
		AEnemyParentNative::ResolveDeathFacingRotation(
			FVector::ZeroVector,
			FVector(0.f, 0.f, 400.f),
			FacingRotation));
	TestFalse(
		TEXT("A non-finite killer position is rejected."),
		AEnemyParentNative::ResolveDeathFacingRotation(
			FVector::ZeroVector,
			FVector(std::numeric_limits<float>::infinity(), 0.f, 0.f),
			FacingRotation));

	TestTrue(
		TEXT("The authored model-forward offset is applied after look-at."),
		AEnemyParentNative::ResolveDeathFacingRotation(
			FVector::ZeroVector,
			FVector(100.f, 0.f, 0.f),
			FacingRotation,
			90.f));
	TestTrue(
		TEXT("The shared enemy correction rotates positive X facing to positive Y."),
		FMath::IsNearlyEqual(
			FMath::FindDeltaAngleDegrees(FacingRotation.Yaw, 90.f),
			0.f,
			KINDA_SMALL_NUMBER));

	return true;
}

#endif
