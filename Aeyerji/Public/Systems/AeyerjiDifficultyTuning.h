#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Engine/DataAsset.h"
#include "Engine/DeveloperSettings.h"
#include "AeyerjiDifficultyTuning.generated.h"

class UDataTable;

/**
 * Shared difficulty tuning asset that defines the global level and world-tier curves.
 */
UCLASS(BlueprintType)
class AEYERJI_API UAeyerjiDifficultyTuning : public UDataAsset
{
	GENERATED_BODY()

public:
	UAeyerjiDifficultyTuning();

	/** Hard gameplay cap applied to player and enemy level calculations. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Difficulty", meta=(ClampMin="1"))
	int32 MaxGameplayLevel = 50;

	/** World tier treated as the Normal baseline for tuning and legacy alpha calculations. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Difficulty", meta=(ClampMin="0", ClampMax="999"))
	int32 NormalWorldTier = 167;

	/** Global baseline enemy level curve evaluated from the player's current level. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Difficulty")
	FRuntimeFloatCurve EnemyLevelByPlayerLevel;

	/** Global stat-budget curve evaluated from the selected world tier. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Difficulty")
	FRuntimeFloatCurve StatBudgetMultiplierByWorldTier;

	/** Clamps a gameplay level into the valid authored range for this tuning asset. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Difficulty")
	int32 ClampGameplayLevel(int32 InLevel) const;

	/** Evaluates the baseline enemy level for the provided player level. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Difficulty")
	int32 EvaluateEnemyLevel(int32 PlayerLevel) const;

	/** Evaluates the global stat-budget multiplier for the provided world tier. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Difficulty")
	float EvaluateStatBudget(int32 WorldTier) const;

	/** Derives a legacy 0..1 difficulty alpha from the world-tier stat-budget curve. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Difficulty")
	float EvaluateDifficultyAlpha(int32 WorldTier) const;
};

/**
 * Project settings entry point for the shared difficulty tuning asset.
 */
UCLASS(Config=Game, defaultconfig)
class AEYERJI_API UAeyerjiDifficultySettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	static constexpr int32 WorldTierMax = 999;
	static constexpr float DifficultySliderMax = 1000.f;

	UAeyerjiDifficultySettings();

	/** Places the settings under Project Settings -> Aeyerji -> Difficulty. */
	virtual FName GetCategoryName() const override { return TEXT("Aeyerji"); }

	/** Places the settings under Project Settings -> Aeyerji -> Difficulty. */
	virtual FName GetSectionName() const override { return TEXT("Difficulty"); }

	/** Soft reference to the shared tuning asset used for global difficulty evaluation. */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Difficulty")
	TSoftObjectPtr<UAeyerjiDifficultyTuning> DefaultTuning;

	/** Merge-friendly table containing Tier_1, Tier_2, and later Greater Rift rows. */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Difficulty")
	TSoftObjectPtr<UDataTable> DefaultRiftTierTable;

	/** Legacy-only fallback used when a Rift activity snapshot is unavailable or invalid. Normal Rift launches never read this value. */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Difficulty", meta=(ClampMin="1"))
	int32 RiftEnemyReferenceLevel = 1;

	/** Resolves the authored tuning asset or a seeded fallback CDO when no asset is assigned. */
	static const UAeyerjiDifficultyTuning* Get();

	/** Resolves the project-wide Greater Rift tier table. */
	static const UDataTable* GetRiftTierTable();

	/** Returns the legacy Rift fallback level clamped to the gameplay cap. */
	static int32 GetRiftEnemyReferenceLevel();

	/** Returns the globally authored gameplay level cap. */
	static int32 GetMaxGameplayLevel();

	/** Returns the globally authored Normal world tier. */
	static int32 GetNormalWorldTier();

	/** Clamps a gameplay level against the shared tuning cap. */
	static int32 ClampGameplayLevel(int32 InLevel);

	/** Converts an arbitrary floating-point attribute value into a finite, globally clamped gameplay level. */
	static int32 FloatToGameplayLevel(float InLevel);

	/** Converts the authoritative world tier into the legacy slider representation. */
	static float WorldTierToDifficultySlider(int32 WorldTier);

	/** Converts the legacy slider representation into the authoritative world tier. */
	static int32 DifficultySliderToWorldTier(float Slider);
};
