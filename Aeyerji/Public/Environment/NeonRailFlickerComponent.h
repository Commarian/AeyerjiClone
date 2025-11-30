// SPDX-License-Identifier: MIT
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "NeonRailFlickerComponent.generated.h"

class UMaterialInstanceDynamic;
class UNeonRailBuilderComponent;
class USplineMeshComponent;

USTRUCT()
struct FFlickerSegment
{
	GENERATED_BODY()

	FFlickerSegment() = default;

	explicit FFlickerSegment(USplineMeshComponent* InSegment)
		: Segment(InSegment)
	{
	}

	UPROPERTY()
	TWeakObjectPtr<USplineMeshComponent> Segment;

	UPROPERTY()
	bool bIsLit = true;

	FTimerHandle TimerHandle;
};

/** Drives random per-segment flicker to mimic dying neon tubes. */
UCLASS(ClassGroup = (Aeyerji), meta = (BlueprintSpawnableComponent))
class AEYERJI_API UNeonRailFlickerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UNeonRailFlickerComponent();

	/** Builder to pull segments from. Auto-resolves if not set. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flicker")
	TObjectPtr<UNeonRailBuilderComponent> Builder = nullptr;

	/** Minimum time that a segment stays lit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flicker", meta = (ClampMin = "0.0"))
	float MinOnTime = 0.35f;

	/** Maximum time that a segment stays lit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flicker", meta = (ClampMin = "0.0"))
	float MaxOnTime = 1.25f;

	/** Minimum time that a segment stays dark. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flicker", meta = (ClampMin = "0.0"))
	float MinOffTime = 0.05f;

	/** Maximum time that a segment stays dark. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flicker", meta = (ClampMin = "0.0"))
	float MaxOffTime = 0.3f;

	/** Adds a random offset to the initial toggle so everything does not blink in unison. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flicker")
	bool bRandomiseInitialDelay = true;

	/** Whether to toggle component visibility as part of the flicker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flicker")
	bool bToggleVisibility = true;

	/** Apply an emissive/intensity parameter on material slot 0 when flickering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flicker")
	bool bAffectMaterial = true;

	/** Parameter name to push intensity into. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flicker", meta = (EditCondition = "bAffectMaterial"))
	FName EmissiveParameterName = TEXT("GlowIntensity");

	/** Value applied while lit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flicker", meta = (EditCondition = "bAffectMaterial"))
	float EmissiveOnValue = 5.f;

	/** Value applied while dark. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flicker", meta = (EditCondition = "bAffectMaterial"))
	float EmissiveOffValue = 0.f;

	/** Requery segments and restart flicker. */
	UFUNCTION(BlueprintCallable, Category = "Flicker")
	void RefreshSegments();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleRailRebuilt(UNeonRailBuilderComponent* InBuilder);

	void ResolveBuilder();
	void SetupForSegments(const TArray<USplineMeshComponent*>& Segments);
	void ClearFlickerState();
	void ApplySegmentState(int32 Index, bool bIsLit);
	void ScheduleNextToggle(int32 Index, float OverrideDelay = -1.f);
	void ToggleSegment(int32 Index);

	float GetRandomOnTime() const;
	float GetRandomOffTime() const;

	UPROPERTY(Transient)
	TArray<FFlickerSegment> TrackedSegments;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> SegmentMaterials;

	bool bHasBoundDelegate = false;
};