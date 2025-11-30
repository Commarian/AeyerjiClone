#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SplineMeshComponent.h" // for ESplineMeshAxis
#include "NeonStripSplineActor.generated.h"

class USplineComponent;
class UStaticMesh;
class UMaterialInterface;
class URectLightComponent;
class UPointLightComponent;

UENUM(BlueprintType)
enum class ENeonLightMode : uint8
{
    None    UMETA(DisplayName="None"),
    Rect    UMETA(DisplayName="Rect Light"),
    Point   UMETA(DisplayName="Point Light")
};

UCLASS(BlueprintType)
class AEYERJI_API ANeonStripSplineActor : public AActor
{
    GENERATED_BODY()

public:
    ANeonStripSplineActor();

    virtual void OnConstruction(const FTransform& Transform) override;

    UFUNCTION(CallInEditor, Category="NeonStrip")
    void Rebuild();

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="NeonStrip")
    USceneComponent* Root;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="NeonStrip")
    USplineComponent* Spline;

public:
    // Geometry
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonStrip|Geometry")
    UStaticMesh* StripMesh;

    // Material override for the strip
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonStrip|Material")
    UMaterialInterface* StripMaterial;

    // Generate the visible strip mesh along the spline
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonStrip|Geometry")
    bool bGenerateMesh;

    // Width (Y) and Thickness (Z) scales for spline mesh (relative to 100uu mesh size)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonStrip|Geometry", meta=(ClampMin="0.01"))
    float StripWidth;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonStrip|Geometry", meta=(ClampMin="0.0"))
    float StripThickness;

    // Roll in degrees applied along the strip (0 = flat on XY plane)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonStrip|Geometry")
    float RollDegrees;

    // Global Z offset applied to each generated mesh segment to avoid z-fighting
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonStrip|Geometry")
    float MeshZOffset;

    // Spline mesh forward axis
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonStrip|Geometry")
    TEnumAsByte<ESplineMeshAxis::Type> ForwardAxis;

    // Lights
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonStrip|Lights")
    bool bGenerateLights;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonStrip|Lights", meta=(EditCondition="bGenerateLights"))
    ENeonLightMode LightMode;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonStrip|Lights", meta=(EditCondition="bGenerateLights && LightMode == ENeonLightMode::Rect", EditConditionHides))
    bool bLightPerSegment;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonStrip|Lights", meta=(EditCondition="bGenerateLights && LightMode == ENeonLightMode::Rect && !bLightPerSegment", EditConditionHides, ClampMin="10"))
    float LightSpacing;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonStrip|Lights", meta=(EditCondition="bGenerateLights"))
    FLinearColor LightColor;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonStrip|Lights", meta=(EditCondition="bGenerateLights && LightMode == ENeonLightMode::Rect", EditConditionHides, ClampMin="0"))
    float LightIntensity;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonStrip|Lights", meta=(EditCondition="bGenerateLights && LightMode == ENeonLightMode::Rect", EditConditionHides, ClampMin="0"))
    float LightAttenuationRadius;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonStrip|Lights", meta=(EditCondition="bGenerateLights && LightMode == ENeonLightMode::Rect", EditConditionHides, ClampMin="0.1"))
    float LightSourceWidth;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonStrip|Lights", meta=(EditCondition="bGenerateLights && LightMode == ENeonLightMode::Rect", EditConditionHides, ClampMin="0.1"))
    float LightSourceHeight;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonStrip|Lights", meta=(EditCondition="bGenerateLights && LightMode == ENeonLightMode::Rect", EditConditionHides))
    bool bAlignLightsToTangent;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonStrip|Lights", meta=(EditCondition="bGenerateLights && LightMode == ENeonLightMode::Rect", EditConditionHides))
    bool bCastLightShadows;

    // Z offset applied to spawned lights
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonStrip|Lights", meta=(EditCondition="bGenerateLights"))
    float LightZOffset;

    // Point light settings (fallback/cheap mode)
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonStrip|Lights|Point", meta=(EditCondition="bGenerateLights && LightMode == ENeonLightMode::Point", EditConditionHides))
    bool bPointPerSegment;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonStrip|Lights|Point", meta=(EditCondition="bGenerateLights && LightMode == ENeonLightMode::Point && !bPointPerSegment", EditConditionHides, ClampMin="10"))
    float PointLightSpacing;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonStrip|Lights|Point", meta=(EditCondition="bGenerateLights && LightMode == ENeonLightMode::Point", EditConditionHides, ClampMin="0"))
    float PointLightIntensity;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonStrip|Lights|Point", meta=(EditCondition="bGenerateLights && LightMode == ENeonLightMode::Point", EditConditionHides, ClampMin="0"))
    float PointLightAttenuationRadius;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="NeonStrip|Lights|Point", meta=(EditCondition="bGenerateLights && LightMode == ENeonLightMode::Point", EditConditionHides))
    bool bPointUseInverseSquaredFalloff;

private:
    void ClearGeneratedComponents();
    void BuildMeshes();
    void BuildLights();
};
