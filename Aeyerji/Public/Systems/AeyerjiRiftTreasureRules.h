#pragma once

#include "CoreMinimal.h"
#include "Director/AeyerjiTreasureTypes.h"

/** Pure deterministic candidate-count and spatial-selection rules shared by Rift authority and automation. */
namespace AeyerjiRiftTreasureRules
{
	/** Rolls the configured inclusive chest count from the supplied seeded stream. */
	AEYERJI_API int32 RollRequestedChestCount(
		FRandomStream& RandomStream,
		const FAeyerjiRiftTreasureSpawnConfig& Config);

	/**
	 * Selects candidate indices using authored weight, soft separation, and optional zone representation.
	 * Input order is the stable tie-breaker and must already be deterministic.
	 */
	AEYERJI_API TArray<int32> SelectCandidateIndices(
		const TArray<FAeyerjiRiftTreasureSelectionCandidate>& Candidates,
		int32 RequestedCount,
		FRandomStream& RandomStream,
		const FAeyerjiRiftTreasureSpawnConfig& Config);
}
