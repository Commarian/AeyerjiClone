#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "SweepingLightBarrier.generated.h"

class USplineComponent;
class UStaticMeshComponent;
class URectLightComponent;
class UBoxComponent;
class UMaterialInterface;
class UDamageType;
class UGameplayEffect;

UCLASS(BlueprintType)
class AEYERJI_API ASweepingLightBarrier : public AActor
{
    GENERATED_BODY()

public:
    ASweepingLightBarrier();

    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void Tick(float DeltaSeconds) override;

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Barrier")
    USplineComponent* Path;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Barrier")
    UStaticMeshComponent* BarrierMesh;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Barrier")
    URectLightComponent* RectLight;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Barrier")
    UBoxComponent* DamageBox;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrier|Visual")
    UMaterialInterface* BarrierMaterial;

public:
    // Geometry
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrier|Geometry", meta=(ClampMin="10"))
    float BarrierWidth; // across the path (cm)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrier|Geometry", meta=(ClampMin="10"))
    float BarrierHeight; // visual height (cm)

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrier|Geometry")
    float YawOffsetDegrees; // rotates mesh around tangent

    // Sweep motion
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrier|Motion", meta=(ClampMin="10"))
    float SweepSpeed; // uu/sec along the spline

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrier|Motion", meta=(ClampMin="0"))
    float PauseAtEnds; // seconds to pause at 0/1

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrier|Motion")
    bool bPingPong;

    // Lighting
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrier|Light")
    bool bUseRectLight;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrier|Light")
    FLinearColor LightColor;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrier|Light", meta=(ClampMin="0"))
    float LightIntensity;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrier|Light", meta=(ClampMin="1"))
    float LightSourceWidth;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrier|Light", meta=(ClampMin="1"))
    float LightSourceHeight;

    // Damage
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrier|Damage")
    bool bDamageContinuous;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrier|Damage", meta=(ClampMin="0"))
    float DamagePerSecond;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrier|Damage")
    TSubclassOf<UDamageType> DamageTypeClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrier|Damage")
    TSubclassOf<UGameplayEffect> DamageEffectClass;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrier|Damage")
    FGameplayTag DamageSetByCallerTag;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Barrier|Damage")
    FGameplayTag DamageTypeTag;

private:
    float SplineLen;
    float Alpha; // 0..1 along the path
    int32 Direction; // +1 or -1
    float PauseTimer;

    TSet<TWeakObjectPtr<AActor>> Overlapping;

    void UpdateBarrierTransform();
    void ApplyBarrierDamage(AActor* Other, float Damage);

    UFUNCTION()
    void OnDamageOverlapBegin(UPrimitiveComponent* Overlapped, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep, const FHitResult& Hit);

    UFUNCTION()
    void OnDamageOverlapEnd(UPrimitiveComponent* Overlapped, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex);
};
