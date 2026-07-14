#include "Systems/AeyerjiRiftRules.h"

namespace AeyerjiRiftRules
{
	bool IsCompletedInTime(const float AcceptedElapsedSeconds, const float TimeLimitSeconds)
	{
		return TimeLimitSeconds > 0.f
			&& AcceptedElapsedSeconds >= 0.f
			&& AcceptedElapsedSeconds < TimeLimitSeconds;
	}

	int32 ResolveCommonTierCap(const TArray<int32>& HighestUnlockedTiers)
	{
		if (HighestUnlockedTiers.IsEmpty())
		{
			return 0;
		}

		int32 CommonCap = MAX_int32;
		for (const int32 Tier : HighestUnlockedTiers)
		{
			if (Tier < 1)
			{
				return 0;
			}
			CommonCap = FMath::Min(CommonCap, Tier);
		}
		return CommonCap == MAX_int32 ? 0 : CommonCap;
	}

	int32 ResolveHighestUnlockedTier(
		const int32 PreviousHighestTier,
		const int32 SelectedTier,
		const bool bVictory,
		const bool bCompletedInTime)
	{
		const int32 SafePreviousTier = FMath::Max(PreviousHighestTier, 1);
		if (!bVictory || !bCompletedInTime)
		{
			return SafePreviousTier;
		}
		return FMath::Max(SafePreviousTier, FMath::Max(SelectedTier, 1) + 1);
	}

	void NormalizeProfileTiers(int32& HighestUnlockedTier, int32& LastSelectedTier)
	{
		HighestUnlockedTier = FMath::Max(HighestUnlockedTier, 1);
		LastSelectedTier = FMath::Clamp(LastSelectedTier, 1, HighestUnlockedTier);
	}

	TArray<int32> AllocateLargestRemainder(const TArray<float>& Weights, const int32 TotalBudget)
	{
		TArray<int32> Allocations;
		Allocations.Init(0, Weights.Num());
		if (Weights.IsEmpty() || TotalBudget <= 0)
		{
			return Allocations;
		}

		float TotalWeight = 0.f;
		for (const float Weight : Weights)
		{
			TotalWeight += FMath::Max(Weight, 0.f);
		}
		if (TotalWeight <= UE_SMALL_NUMBER)
		{
			return Allocations;
		}

		struct FRemainder
		{
			int32 Index = INDEX_NONE;
			float Fraction = 0.f;
		};

		TArray<FRemainder> Remainders;
		Remainders.Reserve(Weights.Num());
		int32 Assigned = 0;
		for (int32 Index = 0; Index < Weights.Num(); ++Index)
		{
			const float Exact = static_cast<float>(TotalBudget) * FMath::Max(Weights[Index], 0.f) / TotalWeight;
			Allocations[Index] = FMath::FloorToInt(Exact);
			Assigned += Allocations[Index];
			Remainders.Add({Index, Exact - static_cast<float>(Allocations[Index])});
		}

		Remainders.Sort([](const FRemainder& Left, const FRemainder& Right)
		{
			if (!FMath::IsNearlyEqual(Left.Fraction, Right.Fraction))
			{
				return Left.Fraction > Right.Fraction;
			}
			return Left.Index < Right.Index;
		});

		for (int32 Offset = 0; Offset < TotalBudget - Assigned; ++Offset)
		{
			Allocations[Remainders[Offset % Remainders.Num()].Index]++;
		}
		return Allocations;
	}

	int32 SelectClosestUnusedRegion(
		const TArray<float>& DistanceSquaredByRegion,
		const TArray<bool>& ViableByRegion,
		const TArray<bool>& ConsumedByRegion,
		const float MaximumDistanceSquared)
	{
		const int32 CandidateCount = FMath::Min3(
			DistanceSquaredByRegion.Num(), ViableByRegion.Num(), ConsumedByRegion.Num());
		int32 BestIndex = INDEX_NONE;
		float BestDistanceSquared = FMath::Max(MaximumDistanceSquared, 0.f);
		for (int32 Index = 0; Index < CandidateCount; ++Index)
		{
			const float DistanceSquared = DistanceSquaredByRegion[Index];
			if (!ViableByRegion[Index] || ConsumedByRegion[Index]
				|| DistanceSquared < 0.f || DistanceSquared > BestDistanceSquared)
			{
				continue;
			}
			if (BestIndex == INDEX_NONE || DistanceSquared < BestDistanceSquared)
			{
				BestIndex = Index;
				BestDistanceSquared = DistanceSquared;
			}
		}
		return BestIndex;
	}

	int32 ApplyAcceptedProgressAward(const int32 CurrentPoints, const int32 TargetPoints, const int32 AwardPoints)
	{
		const int32 SafeTarget = FMath::Max(TargetPoints, 0);
		if (SafeTarget <= 0 || CurrentPoints >= SafeTarget)
		{
			return SafeTarget;
		}
		return FMath::Min(FMath::Max(CurrentPoints, 0) + FMath::Max(AwardPoints, 1), SafeTarget);
	}

	EAeyerjiRiftRewardEligibility ResolveRewardEligibility(
		const bool bBossDefeated,
		const bool bCompletedInTime,
		const bool bBossPhaseDeathOccurred)
	{
		if (!bBossDefeated)
		{
			return EAeyerjiRiftRewardEligibility::None;
		}

		EAeyerjiRiftRewardEligibility Eligibility = EAeyerjiRiftRewardEligibility::Base;
		if (bCompletedInTime)
		{
			Eligibility |= EAeyerjiRiftRewardEligibility::Timed;
			if (!bBossPhaseDeathOccurred)
			{
				Eligibility |= EAeyerjiRiftRewardEligibility::Flawless;
			}
		}
		return Eligibility;
	}

	bool ShouldRecoverHeldCommand(
		const bool bButtonStillHeld,
		const bool bAbilityCancelled,
		const bool bPawnDead,
		const bool bModalUI,
		const bool bInteractionIntent,
		const bool bTargetOrGroundValid)
	{
		return bButtonStillHeld
			&& !bAbilityCancelled
			&& !bPawnDead
			&& !bModalUI
			&& !bInteractionIntent
			&& bTargetOrGroundValid;
	}
}
