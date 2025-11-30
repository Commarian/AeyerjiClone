// SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NeonRailActor.generated.h"

class USplineComponent;
class UNeonRailBuilderComponent;
class UNeonRailFlickerComponent;
struct FPropertyChangedEvent;

/** Ready-to-use actor that exposes a spline, builds mesh segments, and flickers them. */
UCLASS()
class AEYERJI_API ANeonRailActor : public AActor
{
	GENERATED_BODY()

public:
	ANeonRailActor();

protected:
	virtual void OnConstruction(const FTransform& Transform) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

public:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USplineComponent> Spline;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UNeonRailBuilderComponent> RailBuilder;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UNeonRailFlickerComponent> Flicker;

	/** Auto rebuild the rail during construction. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NeonRail")
	bool bAutoRebuildOnConstruction = true;
};