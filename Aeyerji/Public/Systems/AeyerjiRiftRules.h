#pragma once

#include "CoreMinimal.h"

/** Bit mask for the immutable reward layers earned by a boss outcome. */
enum class EAeyerjiRiftRewardEligibility : uint8
{
	None = 0,
	Base = 1 << 0,
	Timed = 1 << 1,
	Flawless = 1 << 2
};
ENUM_CLASS_FLAGS(EAeyerjiRiftRewardEligibility);

/** Pure deterministic Greater Rift rules shared by authority code and automation. */
namespace AeyerjiRiftRules
{
	/** Timed success uses a strict boundary: a death accepted at the limit is overtime. */
	AEYERJI_API bool IsCompletedInTime(float AcceptedElapsedSeconds, float TimeLimitSeconds);

	/** Resolves the party cap, returning zero when no valid loaded profile values were supplied. */
	AEYERJI_API int32 ResolveCommonTierCap(const TArray<int32>& HighestUnlockedTiers);

	/** Advances at most to SelectedTier + 1 and never lowers an already-higher profile. */
	AEYERJI_API int32 ResolveHighestUnlockedTier(
		int32 PreviousHighestTier,
		int32 SelectedTier,
		bool bVictory,
		bool bCompletedInTime);

	/** Migrates missing/invalid tier values and clamps the last selection to the unlock. */
	AEYERJI_API void NormalizeProfileTiers(int32& HighestUnlockedTier, int32& LastSelectedTier);

	/** Stable largest-remainder allocation; input order is the final tie-breaker. */
	AEYERJI_API TArray<int32> AllocateLargestRemainder(const TArray<float>& Weights, int32 TotalBudget);

	/** A monotonic Rift frontier accepts equal-index lateral regions but rejects anything behind it. */
	AEYERJI_API bool CanStageProgressionIndex(int32 CandidateIndex, int32 HighestOpenedIndex);

	/** Evenly transfers skipped finite population to stable forward-region order. */
	AEYERJI_API TArray<int32> AllocateTransferredPopulation(int32 Population, int32 ForwardRegionCount);

	/** Selects the closest viable, unconsumed region; stable input order resolves equal distances. */
	AEYERJI_API int32 SelectClosestUnusedRegion(
		const TArray<float>& DistanceSquaredByRegion,
		const TArray<bool>& ViableByRegion,
		const TArray<bool>& ConsumedByRegion,
		float MaximumDistanceSquared);

	/** Applies one accepted death award and clamps permanently at the target. */
	AEYERJI_API int32 ApplyAcceptedProgressAward(int32 CurrentPoints, int32 TargetPoints, int32 AwardPoints);

	/** Resolves the four base/timed/flawless boss reward outcomes. */
	AEYERJI_API EAeyerjiRiftRewardEligibility ResolveRewardEligibility(
		bool bBossDefeated,
		bool bCompletedInTime,
		bool bBossPhaseDeathOccurred);

	/** Pure held-command gate used by the local controller recovery state machine. */
	AEYERJI_API bool ShouldRecoverHeldCommand(
		bool bButtonStillHeld,
		bool bAbilityCancelled,
		bool bPawnDead,
		bool bModalUI,
		bool bInteractionIntent,
		bool bTargetOrGroundValid);
}
