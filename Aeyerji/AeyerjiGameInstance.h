#pragma once
#include "Engine/GameInstance.h"
#include "AeyerjiGameInstance.generated.h"

UCLASS(BlueprintType, Blueprintable)
class AEYERJI_API UAeyerjiGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	/** Compatibility wrapper that converts the legacy UI slider (0..1000) into the authoritative WorldTier. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Difficulty")
	void SetDifficultySlider(float NewValue);

	/** Returns the legacy slider value derived from the authoritative WorldTier. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Difficulty")
	float GetDifficultySlider() const { return DifficultySlider; }

	/** Returns true when the UI has explicitly set a difficulty slider value. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Difficulty")
	bool HasDifficultySelection() const { return bHasDifficultySelection; }

	/** Normalized difficulty scale (0..1) derived from the stored slider value. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Difficulty")
	float GetDifficultyScale() const;

	/** Stores the authoritative world tier used by balance and loot systems; also rewrites DifficultySlider for compatibility. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Loot")
	void SetWorldTier(int32 NewWorldTier);

	/** Applies a loaded world tier without rewriting the save while profile hydration is still in progress. */
	void ApplySavedWorldTier(int32 NewWorldTier);

	/** Returns the current world tier (integer). */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Loot")
	int32 GetWorldTier() const { return WorldTier; }

	/** Returns true when world tier has explicitly been set at least once. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Loot")
	bool HasWorldTierSelection() const { return bHasWorldTierSelection; }

	/** Starts gameplay flow from menu by selecting random/campaign map and traveling to it via the streaming subsystem. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Flow")
	bool StartGameplaySession(bool bCampaignMode);

	/** Requests a server-authoritative world-flow transition into the provided streaming zone id. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Flow")
	bool RequestZoneTransition(FName ZoneId);

	/** Travels to the configured main menu map using the streaming subsystem. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Flow")
	bool ReturnToMainMenuMap();

	/** Requests runtime sublevel streaming to switch into the target zone id. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Streaming")
	bool EnterStreamingZone(FName ZoneId);

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Difficulty")
	float DifficultySlider = 0.f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Difficulty")
	bool bHasDifficultySelection = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Loot")
	int32 WorldTier = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Aeyerji|Loot")
	bool bHasWorldTierSelection = false;

private:
	/** Immediately persists the current difficulty/world-tier selection to the active character save slot when possible. */
	void PersistDifficultySelectionToSave();

	/** Resolves the streaming subsystem from the current game instance world context. */
	class UAeyerjiStreamingSubsystem* GetStreamingSubsystem() const;
};
