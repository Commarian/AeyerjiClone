#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "AeyerjiMinimapSettings.generated.h"

/** Project-wide tuning for the one-time runtime minimap raster. */
UCLASS(Config=Game, DefaultConfig, meta=(DisplayName="Minimap"))
class AEYERJI_API UAeyerjiMinimapSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Aeyerji"); }
	virtual FName GetSectionName() const override { return TEXT("Minimap"); }

	/** Square pixel resolution generated once for every configured floor in the active zone. */
	UPROPERTY(EditAnywhere, Config, Category="Generation", meta=(ClampMin="256", ClampMax="4096", UIMin="512", UIMax="4096"))
	int32 RasterResolution = 2048;

	/** Empty world-space margin around the generated navigation bounds, in centimeters. */
	UPROPERTY(EditAnywhere, Config, Category="Generation", meta=(ClampMin="0.0", UIMin="0.0", UIMax="5000.0"))
	float WorldPadding = 500.f;

	/** Maximum wait for client navigation generation before retaining the procedural fallback. */
	UPROPERTY(EditAnywhere, Config, Category="Generation", meta=(ClampMin="0.1", UIMin="1.0", UIMax="30.0"))
	float NavigationWaitTimeout = 10.f;

	/** Extra Z range retained around the active floor to avoid rapid switching on stairs. */
	UPROPERTY(EditAnywhere, Config, Category="Floors", meta=(ClampMin="0.0", UIMin="0.0", UIMax="500.0"))
	float FloorSwitchHysteresis = 100.f;

	/** Stable color used outside walkable navigation. */
	UPROPERTY(EditAnywhere, Config, Category="Style")
	FLinearColor EmptyColor = FLinearColor(0.008f, 0.018f, 0.032f, 1.f);

	/** Stable unlit fill used for walkable navigation polygons. */
	UPROPERTY(EditAnywhere, Config, Category="Style")
	FLinearColor WalkableFillColor = FLinearColor(0.055f, 0.22f, 0.27f, 1.f);

	/** Outline color used for navigation boundaries and room silhouettes. */
	UPROPERTY(EditAnywhere, Config, Category="Style")
	FLinearColor WalkableBoundaryColor = FLinearColor(0.12f, 0.70f, 0.78f, 0.92f);

	/** Boundary width in generated texture pixels. */
	UPROPERTY(EditAnywhere, Config, Category="Style", meta=(ClampMin="0.5", ClampMax="8.0"))
	float BoundaryThickness = 1.5f;
};
