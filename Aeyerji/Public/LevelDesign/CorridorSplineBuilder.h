// CorridorSplineBuilder.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/SplineComponent.h"              // USplineComponent
#include "Components/SplineMeshComponent.h"          // ESplineMeshAxis, USplineMeshComponent
#include "CorridorSplineBuilder.generated.h"

class UStaticMesh;

UCLASS(Blueprintable)
class AEYERJI_API ACorridorSplineBuilder : public AActor
{
	GENERATED_BODY()

public:
	ACorridorSplineBuilder();

	/** The path to follow */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Corridor")
	USplineComponent* Spline;

	/** Mesh used for each segment (a straight corridor piece with UVs along X) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Corridor")
	UStaticMesh* SegmentMesh = nullptr;

	/** Forward axis of the mesh (usually X) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Corridor")
	TEnumAsByte<ESplineMeshAxis::Type> ForwardAxis = ESplineMeshAxis::X;

	/** Step size along the spline in centimeters (shorter = smoother bends) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Corridor", meta=(ClampMin="50.0"))
	float SegmentLength = 300.0f;

	/** Uniform width scale for start/end */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Corridor", meta=(ClampMin="0.1"))
	float WidthScale = 1.0f;

	/** Generate collision on segments */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="Corridor")
	bool bCreateCollision = true;

	/** Clear old segments then rebuild */
	UFUNCTION(CallInEditor, Category="Corridor")
	void BuildCorridor();

	/** Remove generated segments */
	UFUNCTION(CallInEditor, Category="Corridor")
	void ClearCorridor();

protected:
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	UPROPERTY(Transient)
	TArray<USplineMeshComponent*> GeneratedSegments;

	void GatherOrCreateSegments(int32 NumNeeded);
	void HideExtraSegments(int32 StartIndex);
};
