#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Systems/AeyerjiStreamingManifest.h"
#include "AeyerjiMinimapMapSubsystem.generated.h"

class ANavigationData;
class UAeyerjiStreamingSubsystem;
class UNavigationSystemV1;
class UTextureRenderTarget2D;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FAeyerjiMinimapMapReadySignature, FName, ZoneId);

/** One generated, vertically filtered map texture for the active streamed zone. */
USTRUCT()
struct FAeyerjiMinimapFloorMap
{
	GENERATED_BODY()

	FName FloorId = NAME_None;
	float MinZ = 0.f;
	float MaxZ = 0.f;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> Texture = nullptr;
};

/**
 * Local presentation cache that converts the active Recast navmesh into stable minimap textures.
 * It never replicates state and performs no rendering work on dedicated servers.
 */
UCLASS()
class AEYERJI_API UAeyerjiMinimapMapSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Returns this game instance's shared minimap map cache. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Minimap", meta=(WorldContext="WorldContextObject"))
	static UAeyerjiMinimapMapSubsystem* GetMinimapMapSubsystem(const UObject* WorldContextObject);

	/** True only after every generated floor texture has been flushed for the active zone. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Minimap")
	bool IsMapReady() const { return bMapReady && FloorMaps.IsValidIndex(ActiveFloorIndex); }

	/** Stable identifier of the floor currently selected from the local player's height. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Minimap")
	FName GetActiveFloor() const;

	/** Generated texture for the currently selected floor, or null while the fallback should be shown. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Minimap")
	UTextureRenderTarget2D* GetActiveMapTexture() const;

	/** Retrieves the square world bounds shared by every generated floor texture. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Minimap")
	bool GetMapWorldBounds(FVector2D& OutMin, FVector2D& OutMax) const;

	/** Updates the current floor from world Z while applying configured switch hysteresis. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Minimap")
	void UpdateActiveFloor(float PlayerWorldZ);

	/** Explicitly discards and regenerates the current zone map when dynamic navigation must be reflected. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Minimap")
	void RequestRebuild();

	/** Discards the current textures without scheduling recurring or automatic capture work. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Minimap")
	void InvalidateCurrentZone();

	/** Number of successful one-time generations, exposed for diagnostics and performance verification. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Minimap")
	int32 GetGenerationCount() const { return GenerationCount; }

	/** Converts UE world XY into north-up map UV coordinates for the supplied square bounds. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Minimap")
	static FVector2D WorldToMapUV(const FVector2D& WorldPosition, const FVector2D& MapMin, float MapSide);

	/** Returns -1 below, zero on, and +1 above the active floor. */
	int32 GetFloorRelationForHeight(float WorldZ) const;

	/** Pure floor selection helper shared by runtime code and automation tests. */
	static int32 SelectFloorIndexForHeight(
		const TArray<FAeyerjiMinimapFloorDef>& Floors,
		int32 CurrentFloorIndex,
		float WorldZ,
		float Hysteresis);

	UPROPERTY(BlueprintAssignable, Category="Aeyerji|Minimap|Events")
	FAeyerjiMinimapMapReadySignature OnMinimapMapReady;

private:
	UFUNCTION()
	void HandleStreamingRequestStarted(FName ZoneId, const TArray<FName>& LevelsToLoad, const TArray<FName>& LevelsToUnload);

	UFUNCTION()
	void HandleZoneReady(FName ZoneId);

	UFUNCTION()
	void HandleNavigationGenerationFinished(ANavigationData* NavigationData);

	void BeginGenerationRequest(FName ZoneId);
	void TryGenerateRequestedMap();
	bool GenerateMapFromNavigation();
	void ScheduleGenerationRetry();
	void StopWaitingForNavigation();
	void ClearGeneratedMap();
	bool ShouldRenderLocally() const;
	UAeyerjiStreamingSubsystem* GetStreamingSubsystem() const;

private:
	UPROPERTY(Transient)
	TArray<FAeyerjiMinimapFloorMap> FloorMaps;

	UPROPERTY(Transient)
	TObjectPtr<UNavigationSystemV1> BoundNavigationSystem = nullptr;

	FBox2D MapWorldBounds = FBox2D(EForceInit::ForceInit);
	FName ActiveZoneId = NAME_None;
	FName RequestedZoneId = NAME_None;
	FName ReadyZoneId = NAME_None;
	int32 ActiveFloorIndex = INDEX_NONE;
	int32 GenerationCount = 0;
	bool bMapReady = false;
	bool bGenerationRequested = false;
	double GenerationRequestStartSeconds = 0.0;
	FTimerHandle GenerationRetryTimer;
};
