#include "Environment/NeonPostProcessActor.h"

#include "Components/PostProcessComponent.h"

ANeonPostProcessActor::ANeonPostProcessActor()
{
    PrimaryActorTick.bCanEverTick = false;

    PostProcess = CreateDefaultSubobject<UPostProcessComponent>(TEXT("PostProcess"));
    RootComponent = PostProcess;

    // Defaults
    bUseManualExposure = true;
    bDebugVisualize = false;
    BloomIntensity = 0.4f;
    BloomThreshold = 1.0f;
    VignetteIntensity = 0.2f;
    SceneFringeIntensity = 0.02f;

    // Global by default
    PostProcess->bUnbound = true;

    ApplySettings();
}

void ANeonPostProcessActor::OnConstruction(const FTransform& Transform)
{
    Super::OnConstruction(Transform);
    ApplySettings();
}

void ANeonPostProcessActor::ApplySettings()
{
    if (!PostProcess)
    {
        return;
    }

    FPostProcessSettings& S = PostProcess->Settings;

    // Exposure
    S.bOverride_AutoExposureMethod = true;
#if ENGINE_MAJOR_VERSION >= 5
    S.AutoExposureMethod = bUseManualExposure ? EAutoExposureMethod::AEM_Manual : EAutoExposureMethod::AEM_Histogram;
#else
    S.AutoExposureMethod = bUseManualExposure ? AEM_Manual : AEM_Histogram;
#endif

    // Bloom
    S.bOverride_BloomIntensity = true;
    S.BloomIntensity = BloomIntensity;
    S.bOverride_BloomThreshold = true;
    S.BloomThreshold = BloomThreshold;

    // Vignette
    S.bOverride_VignetteIntensity = true;
    S.VignetteIntensity = VignetteIntensity;

    // Chromatic aberration
    S.bOverride_SceneFringeIntensity = true;
    S.SceneFringeIntensity = SceneFringeIntensity;

    if (bDebugVisualize)
    {
        // Exaggerated values to prove the volume is active
        S.VignetteIntensity = 1.0f;
        S.bOverride_ColorSaturation = true;
        S.ColorSaturation = FLinearColor(0.f, 0.f, 0.f, 0.f); // grayscale
        S.bOverride_BloomIntensity = true;
        S.BloomIntensity = 2.0f;
    }

    // Ensure the component is enabled and unbound for global effect
    PostProcess->Settings = S;
    PostProcess->bUnbound = true;
    PostProcess->SetMobility(EComponentMobility::Movable);
}

