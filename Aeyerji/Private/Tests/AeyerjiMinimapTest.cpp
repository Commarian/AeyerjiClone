#include "GUI/AeyerjiMinimapMapSubsystem.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiMinimapCoordinateAutomationTest,
	"Aeyerji.Minimap.WorldToUV",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiMinimapCoordinateAutomationTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);

	const FVector2D MapMin(-100.f, -200.f);
	const float MapSide = 1000.f;
	const FVector2D MinimumUV = UAeyerjiMinimapMapSubsystem::WorldToMapUV(MapMin, MapMin, MapSide);
	TestTrue(TEXT("Minimum world Y maps to left"), FMath::IsNearlyEqual(MinimumUV.X, 0.0));
	TestTrue(TEXT("Minimum world X maps to bottom"), FMath::IsNearlyEqual(MinimumUV.Y, 1.0));

	const FVector2D MaximumUV = UAeyerjiMinimapMapSubsystem::WorldToMapUV(
		MapMin + FVector2D(MapSide, MapSide), MapMin, MapSide);
	TestTrue(TEXT("Maximum world Y maps to right"), FMath::IsNearlyEqual(MaximumUV.X, 1.0));
	TestTrue(TEXT("Maximum world X maps to top"), FMath::IsNearlyEqual(MaximumUV.Y, 0.0));

	const FVector2D CenterUV = UAeyerjiMinimapMapSubsystem::WorldToMapUV(
		MapMin + FVector2D(MapSide * 0.5f), MapMin, MapSide);
	TestTrue(TEXT("Map center remains centered"), CenterUV.Equals(FVector2D(0.5, 0.5), UE_KINDA_SMALL_NUMBER));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiMinimapFloorAutomationTest,
	"Aeyerji.Minimap.FloorSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiMinimapFloorAutomationTest::RunTest(const FString& Parameters)
{
	static_cast<void>(Parameters);

	TArray<FAeyerjiMinimapFloorDef> Floors;
	FAeyerjiMinimapFloorDef& Lower = Floors.AddDefaulted_GetRef();
	Lower.FloorId = FName(TEXT("Lower"));
	Lower.MinZ = 0.f;
	Lower.MaxZ = 300.f;
	FAeyerjiMinimapFloorDef& Upper = Floors.AddDefaulted_GetRef();
	Upper.FloorId = FName(TEXT("Upper"));
	Upper.MinZ = 300.f;
	Upper.MaxZ = 600.f;

	TestEqual(
		TEXT("Initial height selects containing floor"),
		UAeyerjiMinimapMapSubsystem::SelectFloorIndexForHeight(Floors, INDEX_NONE, 150.f, 100.f),
		0);
	TestEqual(
		TEXT("Hysteresis retains current floor near stairs"),
		UAeyerjiMinimapMapSubsystem::SelectFloorIndexForHeight(Floors, 0, 350.f, 100.f),
		0);
	TestEqual(
		TEXT("Height beyond hysteresis selects upper floor"),
		UAeyerjiMinimapMapSubsystem::SelectFloorIndexForHeight(Floors, 0, 450.f, 100.f),
		1);
	TestEqual(
		TEXT("Nearest floor is selected outside all ranges"),
		UAeyerjiMinimapMapSubsystem::SelectFloorIndexForHeight(Floors, INDEX_NONE, 800.f, 100.f),
		1);

	FAeyerjiMinimapFloorDef& Invalid = Floors.InsertDefaulted_GetRef(0);
	Invalid.FloorId = FName(TEXT("Invalid"));
	Invalid.MinZ = 200.f;
	Invalid.MaxZ = 100.f;
	TestEqual(
		TEXT("Invalid floor ranges are ignored"),
		UAeyerjiMinimapMapSubsystem::SelectFloorIndexForHeight(Floors, INDEX_NONE, 150.f, 0.f),
		1);
	TestEqual(
		TEXT("Empty floor definitions select no floor"),
		UAeyerjiMinimapMapSubsystem::SelectFloorIndexForHeight({}, INDEX_NONE, 0.f, 0.f),
		INDEX_NONE);
	return true;
}

#endif
