#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

#include <limits>

#include "Enemy/AeyerjiLeashPolicy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FAeyerjiLeashPolicyTest,
	"Aeyerji.AI.LeashPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FAeyerjiLeashPolicyTest::RunTest(const FString& Parameters)
{
	(void)Parameters;

	TestTrue(TEXT("Pawn past its leash range leashes."),
		FAeyerjiLeashPolicy::IsBeyondLeash(2001.f, 2000.f, /*bHasHome=*/true));
	TestFalse(TEXT("Pawn inside its leash range does not leash."),
		FAeyerjiLeashPolicy::IsBeyondLeash(1999.f, 2000.f, /*bHasHome=*/true));
	TestFalse(TEXT("Pawn exactly at its leash range does not leash."),
		FAeyerjiLeashPolicy::IsBeyondLeash(2000.f, 2000.f, /*bHasHome=*/true));
	TestFalse(TEXT("Pawn without a recorded home never leashes."),
		FAeyerjiLeashPolicy::IsBeyondLeash(99999.f, 2000.f, /*bHasHome=*/false));
	TestFalse(TEXT("Zero leash range opts out of leashing."),
		FAeyerjiLeashPolicy::IsBeyondLeash(99999.f, 0.f, /*bHasHome=*/true));
	TestFalse(TEXT("Negative leash range opts out of leashing."),
		FAeyerjiLeashPolicy::IsBeyondLeash(99999.f, -50.f, /*bHasHome=*/true));

	const float NaN = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("Non-finite distance fails closed."),
		FAeyerjiLeashPolicy::IsBeyondLeash(NaN, 2000.f, /*bHasHome=*/true));
	TestFalse(TEXT("Non-finite range fails closed."),
		FAeyerjiLeashPolicy::IsBeyondLeash(99999.f, NaN, /*bHasHome=*/true));

	return true;
}

#endif
