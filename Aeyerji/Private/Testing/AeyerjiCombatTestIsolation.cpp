#include "Testing/AeyerjiCombatTestIsolation.h"

#include "HAL/IConsoleManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

namespace
{
#if !UE_BUILD_SHIPPING
	TAutoConsoleVariable<int32> CVarCombatTestIsolated(
		TEXT("aeyerji.CombatTest.Isolated"),
		0,
		TEXT("Suppresses production world/encounter spawning for PIE combat-balance tests.\n")
		TEXT("0: Production spawning enabled (default)\n")
		TEXT("1: Production spawning suppressed; explicit AJ_CombatTest rosters remain enabled"),
		ECVF_Cheat);
#endif

	bool ParseWorldSpawningSuppression(const TCHAR* CommandLine)
	{
		return CommandLine
			&& FParse::Param(CommandLine, TEXT("AeyerjiCombatTestIsolated"));
	}
}

bool AeyerjiCombatTestIsolation::IsWorldSpawningSuppressed()
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return ParseWorldSpawningSuppression(FCommandLine::Get())
		|| CVarCombatTestIsolated.GetValueOnAnyThread() != 0;
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
bool AeyerjiCombatTestIsolation::ShouldSuppressWorldSpawning(const TCHAR* CommandLine)
{
	return ParseWorldSpawningSuppression(CommandLine);
}
#endif
