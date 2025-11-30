// SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SplineMeshComponent.h"
#include "NeonRailBuilderComponent.generated.h"

class USplineComponent;
class UStaticMesh;
class UMaterialInterface;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FNeonRailBuiltSignature, UNeonRailBuilderComponent*, Builder);

/** Component that mirrors the BP_NeonRail construction script by spawning spline meshes along a spline. */
UCLASS(ClassGroup = (Aeyerji), meta = (BlueprintSpawnableComponent))
class AEYERJI_API UNeonRailBuilderComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNeonRailBuilderComponent();

	/** The spline to sample. If not set, the component searches the owning actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NeonRail", meta = (UseComponentPicker = "true"))
	TObjectPtr<USplineComponent> Spline = nullptr;

	/** Mesh for each spline segment. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NeonRail")
	TObjectPtr<UStaticMesh> TubeMesh = nullptr;

	/** Optional material override for the spline meshes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NeonRail")
	TObjectPtr<UMaterialInterface> NeonMaterial = nullptr;

	/** Length of each piece along the spline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NeonRail", meta = (ClampMin = "1.0"))
	float SegmentLength = 200.f;

	/** Added to the Z component of each endpoint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NeonRail")
	float Height = 0.f;

	/** Forward axis passed to SetForwardAxis. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NeonRail")
	TEnumAsByte<ESplineMeshAxis::Type> ForwardAxis = ESplineMeshAxis::Type::X;

	/** Optional tangent scaling; 1.0 matches the original blueprint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NeonRail", meta = (ClampMin = "0.0"))
	float TangentScale = 1.f;

	/** Clear spline mesh components spawned by this builder before rebuilding. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "NeonRail")
	bool bClearPrevious = true;

	/** Broadcast after BuildRail finishes. */
	UPROPERTY(BlueprintAssignable, Category = "NeonRail")
	FNeonRailBuiltSignature OnRailRebuilt;

	/** Mirrors the blueprint construction script logic and can be called in editor. */
	UFUNCTION(CallInEditor, BlueprintCallable, Category = "NeonRail")
	void BuildRail();

	/** Returns the currently spawned spline mesh segments that belong to this builder. */
	UFUNCTION(BlueprintCallable, Category = "NeonRail")
	void GetSpawnedSegments(TArray<USplineMeshComponent*>& OutSegments) const;

protected:
	virtual void OnRegister() override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

#if WITH_EDITOR
	virtual void PostEditChangeProperty(struct FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

private:
	static FName RailTag();

	USplineComponent* ResolveSpline() const;
	void ClearPreviousMeshes();
	void SpawnOneSegment(float T0, float T1);

	void CacheTaggedSegmentsIfNeeded() const;
	void CacheSplineVersion();

	UPROPERTY(Transient)
	mutable TArray<TWeakObjectPtr<USplineMeshComponent>> SpawnedSegments;

	UPROPERTY(Transient)
	uint32 CachedSplineVersion = 0;

	bool bHasCachedVersion = false;
};