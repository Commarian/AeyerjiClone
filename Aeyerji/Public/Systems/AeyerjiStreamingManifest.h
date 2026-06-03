#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AeyerjiStreamingManifest.generated.h"

class UWorld;

/**
 * Designer-authored description of a streaming zone and which sublevels should be active for it.
 */
USTRUCT(BlueprintType)
struct AEYERJI_API FZoneDef
{
	GENERATED_BODY()

	/** Stable zone identifier used by runtime calls such as EnterZone(). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Streaming")
	FName ZoneId = NAME_None;

	/** Sublevels to stream in when this zone becomes active. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Streaming")
	TArray<FName> LevelsToLoad;

	/** Optional shared sublevels to keep resident while in this zone. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Streaming")
	TArray<FName> LevelsToKeep;

	/** Optional PlayerStart tag used when spawning gameplay pawns after this zone is ready. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Streaming")
	FName EntryPlayerStartTag = NAME_None;

	/** If true, world flow should respawn gameplay pawns after this zone reaches ready state. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Streaming")
	bool bSpawnPlayerAfterReady = true;

	/** If true, gameplay zones automatically start a run once streaming + spawn activation completes. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Streaming")
	bool bAutoStartRun = true;

	/** If true, streamed levels are made visible immediately after they are loaded. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Streaming")
	bool bMakeVisibleAfterLoad = true;

	/** If true, loading/unloading can block the game thread until complete. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Streaming")
	bool bBlockOnLoad = false;
};

/**
 * One gameplay map option used by random/campaign map selection.
 */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiGameplayMapDef
{
	GENERATED_BODY()

	/** Stable map identifier used for save/load and debug UI. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Flow")
	FName MapId = NAME_None;

	/** Gameplay map asset to travel to. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Flow")
	TSoftObjectPtr<UWorld> MapAsset;

	/** Optional startup zone for this map (used by world director/subsystem). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Flow")
	FName EntryZoneId = NAME_None;
};

/**
 * Primary data asset that centralizes streaming zone definitions and gameplay map selection data.
 */
UCLASS(BlueprintType)
class AEYERJI_API UAeyerjiStreamingManifest : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	/** Finds a zone definition by ZoneId. Returns false when ZoneId does not exist. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Streaming")
	bool GetZoneDefinition(FName ZoneId, FZoneDef& OutZoneDefinition) const;

	/** Finds a gameplay map definition by MapId. Returns false when MapId does not exist. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Flow")
	bool GetGameplayMapDefinition(FName MapId, FAeyerjiGameplayMapDef& OutMapDefinition) const;

public:
	/** All streaming zones available to the runtime subsystem. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Streaming")
	TArray<FZoneDef> Zones;

	/** Map rotation candidates for random/campaign gameplay entry. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Flow")
	TArray<FAeyerjiGameplayMapDef> GameplayMaps;

	/** Main menu map used by TravelToMainMenu() when provided. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Flow")
	TSoftObjectPtr<UWorld> MainMenuMap;

	/** Fallback zone used when no explicit startup zone is provided. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Aeyerji|Streaming")
	FName DefaultZoneId = NAME_None;
};
