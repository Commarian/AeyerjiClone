// Fill out your copyright notice in the Description page of Project Settings.


#include "AeyerjiGameInstance.h"

#include "AbilitySystemInterface.h"
#include "AeyerjiGameMode.h"
#include "AeyerjiPlayerState.h"
#include "AeyerjiCharacter.h"
#include "CharacterStatsLibrary.h"
#include "AeyerjiSaveGame.h"
#include "Attributes/AeyerjiAttributeSet.h"
#include "Kismet/GameplayStatics.h"
#include "Math/UnrealMathUtility.h"

void UAeyerjiGameInstance::Shutdown()
{

	Super::Shutdown();        // always last
}

namespace
{
	constexpr int32 WorldTierMax = 999;
	constexpr float DifficultySliderMax = 1000.f;

	// Map the 0..999 world tier slider onto the 0..1000 difficulty slider range.
	float WorldTierToDifficultySlider(int32 WorldTier)
	{
		const float ClampedTier = FMath::Clamp(static_cast<float>(WorldTier), 0.f, static_cast<float>(WorldTierMax));
		return ClampedTier * (DifficultySliderMax / static_cast<float>(WorldTierMax));
	}

	// Map the 0..1000 difficulty slider back into the 0..999 world tier range.
	int32 DifficultySliderToWorldTier(float Slider)
	{
		const float Normalized = FMath::Clamp(Slider, 0.f, DifficultySliderMax) / DifficultySliderMax;
		return FMath::Clamp(FMath::RoundToInt(Normalized * static_cast<float>(WorldTierMax)), 0, WorldTierMax);
	}
}

void UAeyerjiGameInstance::SetDifficultySlider(float NewValue)
{
	DifficultySlider = FMath::Clamp(NewValue, 0.f, DifficultySliderMax);
	bHasDifficultySelection = true;

	const int32 NewWorldTier = DifficultySliderToWorldTier(DifficultySlider);
	if (WorldTier != NewWorldTier)
	{
		WorldTier = NewWorldTier;
	}
	bHasWorldTierSelection = true;
}

void UAeyerjiGameInstance::SetWorldTier(int32 NewWorldTier)
{
	WorldTier = FMath::Clamp(NewWorldTier, 0, WorldTierMax);
	bHasWorldTierSelection = true;

	const float NewDifficultySlider = WorldTierToDifficultySlider(WorldTier);
	if (!FMath::IsNearlyEqual(DifficultySlider, NewDifficultySlider))
	{
		DifficultySlider = NewDifficultySlider;
	}
	bHasDifficultySelection = true;
}

float UAeyerjiGameInstance::GetDifficultyScale() const
{
	const float Normalized = DifficultySlider / DifficultySliderMax;
	return FMath::Clamp(Normalized, 0.f, 1.f);
}
