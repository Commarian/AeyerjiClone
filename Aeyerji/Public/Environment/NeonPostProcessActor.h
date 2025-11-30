#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "NeonPostProcessActor.generated.h"

class UPostProcessComponent;

UCLASS(BlueprintType)
class AEYERJI_API ANeonPostProcessActor : public AActor
{
    GENERATED_BODY()

public:
    ANeonPostProcessActor();

    virtual void OnConstruction(const FTransform& Transform) override;

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="NeonPreset")
    UPostProcessComponent* PostProcess;

public:
    // Use manual exposure (recommended for neon setup)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonPreset|Exposure")
    bool bUseManualExposure;

    // Quick toggle to exaggerate values for visibility checks
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonPreset|Debug")
    bool bDebugVisualize;

    // Base tuning
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonPreset|Look", meta=(ClampMin="0.0"))
    float BloomIntensity;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonPreset|Look", meta=(ClampMin="0.0"))
    float BloomThreshold;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonPreset|Look", meta=(ClampMin="0.0"))
    float VignetteIntensity;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonPreset|Look", meta=(ClampMin="0.0"))
    float SceneFringeIntensity;

private:
    void ApplySettings();
};

