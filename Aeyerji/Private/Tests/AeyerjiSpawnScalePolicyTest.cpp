#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include <limits>

#include "Director/AeyerjiSpawnScalePolicy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiSpawnScalePolicyTest,
	"Aeyerji.Spawning.SpawnScalePolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiSpawnScalePolicyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	FTransform UnitTransform(FRotator(10.f, 20.f, 30.f), FVector(100.f, 200.f, 300.f), FVector::OneVector);
	TestFalse(TEXT("Unit-scale spawn transform is left untouched."),
		FAeyerjiSpawnScalePolicy::StripNonUnitScale(UnitTransform));
	TestTrue(TEXT("Unit-scale spawn transform keeps its scale."),
		UnitTransform.GetScale3D().Equals(FVector::OneVector));

	FTransform GiantTransform(FRotator::ZeroRotator, FVector(1.f, 2.f, 3.f), FVector(100.f, 100.f, 100.f));
	TestTrue(TEXT("Hundredfold marker scale is stripped."),
		FAeyerjiSpawnScalePolicy::StripNonUnitScale(GiantTransform));
	TestTrue(TEXT("Stripped transform returns to unit scale."),
		GiantTransform.GetScale3D().Equals(FVector::OneVector));
	TestTrue(TEXT("Stripping scale preserves the spawn location."),
		GiantTransform.GetLocation().Equals(FVector(1.f, 2.f, 3.f)));
	TestTrue(TEXT("Stripping scale preserves the spawn rotation."),
		GiantTransform.GetRotation().Equals(FQuat::Identity));

	FTransform ShrunkTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector(0.5f, 0.75f, 1.f));
	TestTrue(TEXT("Non-uniform marker scale is stripped."),
		FAeyerjiSpawnScalePolicy::StripNonUnitScale(ShrunkTransform));
	TestTrue(TEXT("Non-uniform strip returns to unit scale."),
		ShrunkTransform.GetScale3D().Equals(FVector::OneVector));

	const float NaN = std::numeric_limits<float>::quiet_NaN();
	FTransform CorruptTransform(FRotator::ZeroRotator, FVector::ZeroVector, FVector(NaN, 1.f, 1.f));
	TestTrue(TEXT("Non-finite marker scale is stripped."),
		FAeyerjiSpawnScalePolicy::StripNonUnitScale(CorruptTransform));
	TestTrue(TEXT("Non-finite strip returns to unit scale."),
		CorruptTransform.GetScale3D().Equals(FVector::OneVector));

	TestFalse(TEXT("Unit-scale non-elite pawn is not anomalous."),
		FAeyerjiSpawnScalePolicy::IsAnomalousSpawnScale(FVector::OneVector, /*bHasIntentionalScale=*/false));
	TestTrue(TEXT("Hundredfold non-elite pawn is anomalous."),
		FAeyerjiSpawnScalePolicy::IsAnomalousSpawnScale(FVector(100.f, 100.f, 100.f), /*bHasIntentionalScale=*/false));
	TestFalse(TEXT("Hundredfold elite pawn is intentional, not anomalous."),
		FAeyerjiSpawnScalePolicy::IsAnomalousSpawnScale(FVector(100.f, 100.f, 100.f), /*bHasIntentionalScale=*/true));
	TestTrue(TEXT("Non-finite scale is anomalous even for elites."),
		FAeyerjiSpawnScalePolicy::IsAnomalousSpawnScale(FVector(NaN, 1.f, 1.f), /*bHasIntentionalScale=*/true));
	TestFalse(TEXT("Scale exactly at the threshold is not anomalous."),
		FAeyerjiSpawnScalePolicy::IsAnomalousSpawnScale(
			FVector(FAeyerjiSpawnScalePolicy::OverscaleWarningThreshold), /*bHasIntentionalScale=*/false));
	TestTrue(TEXT("Scale just above the threshold is anomalous."),
		FAeyerjiSpawnScalePolicy::IsAnomalousSpawnScale(
			FVector(FAeyerjiSpawnScalePolicy::OverscaleWarningThreshold + 0.01f), /*bHasIntentionalScale=*/false));

	return true;
}

#endif
