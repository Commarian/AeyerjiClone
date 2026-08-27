#include "Systems/AeyerjiRiftRules.h"

namespace AeyerjiRiftRules
{
	bool IsCompletedInTime(const float AcceptedElapsedSeconds, const float TimeLimitSeconds)
	{
		return FMath::IsFinite(AcceptedElapsedSeconds)
			&& FMath::IsFinite(TimeLimitSeconds)
			&& TimeLimitSeconds > 0.f
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
		const int32 SafeSelectedTier = FMath::Max(SelectedTier, 1);
		const int32 AdvancedTier = SafeSelectedTier < MAX_int32 ? SafeSelectedTier + 1 : MAX_int32;
		return FMath::Max(SafePreviousTier, AdvancedTier);
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

		double TotalWeight = 0.0;
		for (const float Weight : Weights)
		{
			if (FMath::IsFinite(Weight) && Weight > 0.f)
			{
				TotalWeight += static_cast<double>(Weight);
			}
		}
		if (!FMath::IsFinite(TotalWeight) || TotalWeight <= UE_SMALL_NUMBER)
		{
			return Allocations;
		}

		struct FRemainder
		{
			int32 Index = INDEX_NONE;
			double Fraction = 0.0;
		};

		TArray<FRemainder> Remainders;
		Remainders.Reserve(Weights.Num());
		int64 Assigned = 0;
		for (int32 Index = 0; Index < Weights.Num(); ++Index)
		{
			const double SafeWeight = FMath::IsFinite(Weights[Index]) && Weights[Index] > 0.f
				? static_cast<double>(Weights[Index])
				: 0.0;
			const double Exact = static_cast<double>(TotalBudget) * SafeWeight / TotalWeight;
			Allocations[Index] = static_cast<int32>(FMath::FloorToInt64(FMath::Clamp(
				Exact, 0.0, static_cast<double>(TotalBudget))));
			Assigned += Allocations[Index];
			Remainders.Add({Index, Exact - static_cast<double>(Allocations[Index])});
		}

		Remainders.Sort([](const FRemainder& Left, const FRemainder& Right)
		{
			if (!FMath::IsNearlyEqual(Left.Fraction, Right.Fraction))
			{
				return Left.Fraction > Right.Fraction;
			}
			return Left.Index < Right.Index;
		});

		const int64 Remaining = FMath::Clamp<int64>(static_cast<int64>(TotalBudget) - Assigned, 0, Remainders.Num());
		for (int64 Offset = 0; Offset < Remaining; ++Offset)
		{
			Allocations[Remainders[static_cast<int32>(Offset)].Index]++;
		}
		return Allocations;
	}

	bool CanStageProgressionIndex(const int32 CandidateIndex, const int32 HighestOpenedIndex)
	{
		return CandidateIndex >= 0
			&& (HighestOpenedIndex < 0 || CandidateIndex >= HighestOpenedIndex);
	}

	TArray<int32> AllocateTransferredPopulation(const int32 Population, const int32 ForwardRegionCount)
	{
		if (ForwardRegionCount <= 0)
		{
			return {};
		}

		TArray<float> EqualWeights;
		EqualWeights.Init(1.f, ForwardRegionCount);
		return AllocateLargestRemainder(EqualWeights, FMath::Max(Population, 0));
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
		if (!FMath::IsFinite(MaximumDistanceSquared) || MaximumDistanceSquared < 0.f)
		{
			return INDEX_NONE;
		}

		float BestDistanceSquared = MaximumDistanceSquared;
		for (int32 Index = 0; Index < CandidateCount; ++Index)
		{
			const float DistanceSquared = DistanceSquaredByRegion[Index];
			if (!ViableByRegion[Index] || ConsumedByRegion[Index]
				|| !FMath::IsFinite(DistanceSquared)
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
		const int64 AwardedTotal = static_cast<int64>(FMath::Max(CurrentPoints, 0))
			+ static_cast<int64>(FMath::Max(AwardPoints, 1));
		return static_cast<int32>(FMath::Min<int64>(AwardedTotal, SafeTarget));
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
