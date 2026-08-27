#include "Systems/AeyerjiRiftTreasureRules.h"

namespace
{
	constexpr float MinimumSoftSpreadFactor = 0.05f;
	constexpr float MaximumZoneRepeatPenalty = 100000.f;

	bool IsFiniteTreasureVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	float SanitizeNonNegative(const float Value, const float Fallback = 0.f)
	{
		return FMath::Max(FMath::IsFinite(Value) ? Value : Fallback, 0.f);
	}

	float ResolveDistanceWeight(
		const FVector& CandidateLocation,
		const TArray<int32>& SelectedIndices,
		const TArray<FAeyerjiRiftTreasureSelectionCandidate>& Candidates,
		const FAeyerjiRiftTreasureSpawnConfig& Config)
	{
		if (SelectedIndices.IsEmpty())
		{
			return 1.f;
		}

		const float HardMinimum = SanitizeNonNegative(Config.HardMinimumChestSeparation);
		const float Preferred = FMath::Max(HardMinimum, SanitizeNonNegative(Config.PreferredChestSeparation));
		float NearestDistanceSquared = TNumericLimits<float>::Max();
		for (const int32 SelectedIndex : SelectedIndices)
		{
			if (!Candidates.IsValidIndex(SelectedIndex))
			{
				continue;
			}

			NearestDistanceSquared = FMath::Min(
				NearestDistanceSquared,
				FVector::DistSquared2D(CandidateLocation, Candidates[SelectedIndex].NavigationAnchor));
		}

		if (!FMath::IsFinite(NearestDistanceSquared))
		{
			return 0.f;
		}

		const float HardMinimumSquared = FMath::Square(HardMinimum);
		if (NearestDistanceSquared < HardMinimumSquared)
		{
			return 0.f;
		}

		if (Preferred <= HardMinimum + KINDA_SMALL_NUMBER)
		{
			return 1.f;
		}

		const float NearestDistance = FMath::Sqrt(NearestDistanceSquared);
		const float NormalizedDistance = FMath::Clamp(
			(NearestDistance - HardMinimum) / (Preferred - HardMinimum),
			0.f,
			1.f);
		const float SpreadStrength = FMath::Clamp(
			FMath::IsFinite(Config.SpreadStrength) ? Config.SpreadStrength : 0.f,
			0.f,
			1.f);
		const float CloseDistanceFactor = FMath::Lerp(1.f, MinimumSoftSpreadFactor, SpreadStrength);
		return FMath::Lerp(CloseDistanceFactor, 1.f, NormalizedDistance);
	}

	float ResolveZoneWeight(
		const FName ZoneId,
		const TMap<FName, int32>& SelectedZones,
		const FAeyerjiRiftTreasureSpawnConfig& Config)
	{
		if (ZoneId.IsNone())
		{
			return 1.f;
		}

		const int32 SelectedInZone = SelectedZones.FindRef(ZoneId);
		if (SelectedInZone <= 0)
		{
			return SanitizeNonNegative(Config.UnusedZoneWeightMultiplier, 1.f);
		}

		const float RepeatPenalty = FMath::Clamp(
			SanitizeNonNegative(Config.ZoneRepeatPenalty),
			0.f,
			MaximumZoneRepeatPenalty);
		return 1.f / (1.f + (RepeatPenalty * static_cast<float>(SelectedInZone)));
	}
}

int32 AeyerjiRiftTreasureRules::RollRequestedChestCount(
	FRandomStream& RandomStream,
	const FAeyerjiRiftTreasureSpawnConfig& Config)
{
	const int32 Minimum = FMath::Max(0, Config.MinimumChests);
	const int32 Maximum = FMath::Max(Minimum, Config.MaximumChests);
	return Minimum == Maximum ? Minimum : RandomStream.RandRange(Minimum, Maximum);
}

TArray<int32> AeyerjiRiftTreasureRules::SelectCandidateIndices(
	const TArray<FAeyerjiRiftTreasureSelectionCandidate>& Candidates,
	const int32 RequestedCount,
	FRandomStream& RandomStream,
	const FAeyerjiRiftTreasureSpawnConfig& Config)
{
	TArray<int32> SelectedIndices;
	if (RequestedCount <= 0 || Candidates.IsEmpty())
	{
		return SelectedIndices;
	}

	TArray<bool> Consumed;
	Consumed.Init(false, Candidates.Num());
	TMap<FName, int32> SelectedZones;
	const int32 SelectionLimit = FMath::Min(RequestedCount, Candidates.Num());
	for (int32 SelectionNumber = 0; SelectionNumber < SelectionLimit; ++SelectionNumber)
	{
		TArray<float> EffectiveWeights;
		EffectiveWeights.Init(0.f, Candidates.Num());
		float TotalWeight = 0.f;

		for (int32 CandidateIndex = 0; CandidateIndex < Candidates.Num(); ++CandidateIndex)
		{
			if (Consumed[CandidateIndex])
			{
				continue;
			}

			const FAeyerjiRiftTreasureSelectionCandidate& Candidate = Candidates[CandidateIndex];
			if (!IsFiniteTreasureVector(Candidate.NavigationAnchor))
			{
				continue;
			}

			const float AuthoredWeight = SanitizeNonNegative(Candidate.SpawnWeight);
			const float DistanceWeight = ResolveDistanceWeight(
				Candidate.NavigationAnchor, SelectedIndices, Candidates, Config);
			const float ZoneWeight = ResolveZoneWeight(Candidate.ZoneId, SelectedZones, Config);
			const float EffectiveWeight = AuthoredWeight * DistanceWeight * ZoneWeight;
			if (!FMath::IsFinite(EffectiveWeight) || EffectiveWeight <= KINDA_SMALL_NUMBER)
			{
				continue;
			}

			EffectiveWeights[CandidateIndex] = EffectiveWeight;
			TotalWeight += EffectiveWeight;
		}

		if (!FMath::IsFinite(TotalWeight) || TotalWeight <= KINDA_SMALL_NUMBER)
		{
			break;
		}

		const float Roll = RandomStream.GetFraction() * TotalWeight;
		float AccumulatedWeight = 0.f;
		int32 ChosenIndex = INDEX_NONE;
		for (int32 CandidateIndex = 0; CandidateIndex < EffectiveWeights.Num(); ++CandidateIndex)
		{
			const float EffectiveWeight = EffectiveWeights[CandidateIndex];
			if (EffectiveWeight <= KINDA_SMALL_NUMBER)
			{
				continue;
			}

			AccumulatedWeight += EffectiveWeight;
			if (Roll < AccumulatedWeight)
			{
				ChosenIndex = CandidateIndex;
				break;
			}
		}

		if (ChosenIndex == INDEX_NONE)
		{
			for (int32 CandidateIndex = EffectiveWeights.Num() - 1; CandidateIndex >= 0; --CandidateIndex)
			{
				if (EffectiveWeights[CandidateIndex] > KINDA_SMALL_NUMBER)
				{
					ChosenIndex = CandidateIndex;
					break;
				}
			}
		}

		if (ChosenIndex == INDEX_NONE)
		{
			break;
		}

		Consumed[ChosenIndex] = true;
		SelectedIndices.Add(ChosenIndex);
		const FName ChosenZone = Candidates[ChosenIndex].ZoneId;
		if (!ChosenZone.IsNone())
		{
			SelectedZones.FindOrAdd(ChosenZone)++;
		}
	}

	return SelectedIndices;
}
