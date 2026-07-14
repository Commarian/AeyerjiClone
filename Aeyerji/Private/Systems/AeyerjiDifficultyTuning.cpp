#include "Systems/AeyerjiDifficultyTuning.h"

#include "Curves/RichCurve.h"
#include "Engine/DataTable.h"

namespace
{
	/** Seeds a runtime curve when the asset instance has not authored any keys yet. */
	void SeedCurveIfEmpty(FRuntimeFloatCurve& Curve, const TArray<TPair<float, float>>& Keys)
	{
		FRichCurve* RichCurve = Curve.GetRichCurve();
		if (!RichCurve || RichCurve->GetNumKeys() > 0)
		{
			return;
		}

		for (const TPair<float, float>& Key : Keys)
		{
			const FKeyHandle Handle = RichCurve->AddKey(Key.Key, Key.Value);
			RichCurve->SetKeyInterpMode(Handle, ERichCurveInterpMode::RCIM_Linear);
		}
	}
}

UAeyerjiDifficultyTuning::UAeyerjiDifficultyTuning()
{
	SeedCurveIfEmpty(EnemyLevelByPlayerLevel, {
		TPair<float, float>(1.f, 1.f),
		TPair<float, float>(50.f, 50.f)
	});

	SeedCurveIfEmpty(StatBudgetMultiplierByWorldTier, {
		TPair<float, float>(0.f, 0.5f),
		TPair<float, float>(100.f, 1.f),
		TPair<float, float>(999.f, 2.f)
	});
}

int32 UAeyerjiDifficultyTuning::ClampGameplayLevel(const int32 InLevel) const
{
	return FMath::Clamp(InLevel, 1, FMath::Max(1, MaxGameplayLevel));
}

int32 UAeyerjiDifficultyTuning::EvaluateEnemyLevel(const int32 PlayerLevel) const
{
	const int32 ClampedPlayerLevel = ClampGameplayLevel(PlayerLevel);
	const FRichCurve* Curve = EnemyLevelByPlayerLevel.GetRichCurveConst();
	const float EvaluatedLevel = Curve && Curve->GetNumKeys() > 0
		? Curve->Eval(static_cast<float>(ClampedPlayerLevel), static_cast<float>(ClampedPlayerLevel))
		: static_cast<float>(ClampedPlayerLevel);
	return ClampGameplayLevel(FMath::RoundToInt(EvaluatedLevel));
}

float UAeyerjiDifficultyTuning::EvaluateStatBudget(const int32 WorldTier) const
{
	const int32 ClampedWorldTier = FMath::Clamp(WorldTier, 0, UAeyerjiDifficultySettings::WorldTierMax);
	const FRichCurve* Curve = StatBudgetMultiplierByWorldTier.GetRichCurveConst();
	const float EvaluatedBudget = Curve && Curve->GetNumKeys() > 0
		? Curve->Eval(static_cast<float>(ClampedWorldTier), 1.f)
		: 1.f;
	return FMath::Max(EvaluatedBudget, 0.f);
}

float UAeyerjiDifficultyTuning::EvaluateDifficultyAlpha(const int32 WorldTier) const
{
	const float NormalBudget = EvaluateStatBudget(NormalWorldTier);
	const float MaxBudget = EvaluateStatBudget(UAeyerjiDifficultySettings::WorldTierMax);
	if (MaxBudget <= NormalBudget)
	{
		return WorldTier >= NormalWorldTier ? 1.f : 0.f;
	}

	const float CurrentBudget = EvaluateStatBudget(WorldTier);
	return FMath::Clamp((CurrentBudget - NormalBudget) / (MaxBudget - NormalBudget), 0.f, 1.f);
}

UAeyerjiDifficultySettings::UAeyerjiDifficultySettings()
{
}

const UAeyerjiDifficultyTuning* UAeyerjiDifficultySettings::Get()
{
	const UAeyerjiDifficultySettings* Settings = GetDefault<UAeyerjiDifficultySettings>();
	if (Settings && !Settings->DefaultTuning.IsNull())
	{
		if (const UAeyerjiDifficultyTuning* LoadedTuning = Settings->DefaultTuning.LoadSynchronous())
		{
			return LoadedTuning;
		}
	}

	return GetDefault<UAeyerjiDifficultyTuning>();
}

const UDataTable* UAeyerjiDifficultySettings::GetRiftTierTable()
{
	const UAeyerjiDifficultySettings* Settings = GetDefault<UAeyerjiDifficultySettings>();
	if (Settings && !Settings->DefaultRiftTierTable.IsNull())
	{
		return Settings->DefaultRiftTierTable.LoadSynchronous();
	}
	return nullptr;
}

int32 UAeyerjiDifficultySettings::GetRiftEnemyReferenceLevel()
{
	const UAeyerjiDifficultySettings* Settings = GetDefault<UAeyerjiDifficultySettings>();
	return ClampGameplayLevel(Settings ? Settings->RiftEnemyReferenceLevel : GetMaxGameplayLevel());
}

int32 UAeyerjiDifficultySettings::GetMaxGameplayLevel()
{
	return Get()->ClampGameplayLevel(Get()->MaxGameplayLevel);
}

int32 UAeyerjiDifficultySettings::GetNormalWorldTier()
{
	return FMath::Clamp(Get()->NormalWorldTier, 0, WorldTierMax);
}

int32 UAeyerjiDifficultySettings::ClampGameplayLevel(const int32 InLevel)
{
	return Get()->ClampGameplayLevel(InLevel);
}

float UAeyerjiDifficultySettings::WorldTierToDifficultySlider(const int32 WorldTier)
{
	const float ClampedTier = FMath::Clamp(static_cast<float>(WorldTier), 0.f, static_cast<float>(WorldTierMax));
	return ClampedTier * (DifficultySliderMax / static_cast<float>(WorldTierMax));
}

int32 UAeyerjiDifficultySettings::DifficultySliderToWorldTier(const float Slider)
{
	const float Normalized = FMath::Clamp(Slider, 0.f, DifficultySliderMax) / DifficultySliderMax;
	return FMath::Clamp(FMath::RoundToInt(Normalized * static_cast<float>(WorldTierMax)), 0, WorldTierMax);
}
