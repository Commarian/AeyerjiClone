#pragma once

#include "CoreTypes.h"

namespace AeyerjiCombatTestIsolation
{
	/** True when the non-shipping launch option or PIE-safe console variable suppresses production world spawning. */
	bool IsWorldSpawningSuppressed();

#if WITH_DEV_AUTOMATION_TESTS
	/** Parses an explicit command line so the launch-option contract can be tested without mutating the process command line. */
	bool ShouldSuppressWorldSpawning(const TCHAR* CommandLine);
#endif
}
