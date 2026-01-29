#pragma once
#include "Engine/GameInstance.h"
#include "AeyerjiGameInstance.generated.h"

UCLASS(BlueprintType, Blueprintable)
class AEYERJI_API UAeyerjiGameInstance : public UGameInstance
{
	GENERATED_BODY()

protected:
	virtual void Shutdown() override;   // called in PIE stop & real quit

public:
	/** Stores the player's chosen difficulty slider (0..1000) so UI can persist it across loads; also syncs WorldTier. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Difficulty")
	void SetDifficultySlider(float NewValue);

	/** Returns the raw difficulty slider value last set by the UI (0..1000). */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Difficulty")
	float GetDifficultySlider() const { return DifficultySlider; }

	/** Returns true when the UI has explicitly set a difficulty slider value. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Difficulty")
	bool HasDifficultySelection() const { return bHasDifficultySelection; }

	/** Normalized difficulty scale (0..1) derived from the stored slider value. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Difficulty")
	float GetDifficultyScale() const;

	/** Stores the current world tier used by loot pool matching (integer tier, typically 0..999); also syncs DifficultySlider. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Loot")
	void SetWorldTier(int32 NewWorldTier);

	/** Returns the current world tier (integer). */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Loot")
	int32 GetWorldTier() const { return WorldTier; }

	/** Returns true when world tier has explicitly been set at least once. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Loot")
	bool HasWorldTierSelection() const { return bHasWorldTierSelection; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Difficulty")
	float DifficultySlider = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Difficulty")
	bool bHasDifficultySelection = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Loot")
	int32 WorldTier = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Loot")
	bool bHasWorldTierSelection = false;
};
