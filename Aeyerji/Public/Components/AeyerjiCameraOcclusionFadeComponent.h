// AeyerjiCameraOcclusionFadeComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "TimerManager.h"

#include "AeyerjiCameraOcclusionFadeComponent.generated.h"

class APlayerController;
class APawn;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPrimitiveComponent;

UENUM(BlueprintType)
enum class EAeyerjiRoofOccluderFilter : uint8
{
	CollisionChannel UMETA(DisplayName="Collision Channel"),
	ComponentTag     UMETA(DisplayName="Component Tag"),
	ActorTag         UMETA(DisplayName="Actor Tag")
};

UENUM(BlueprintType)
enum class EAeyerjiFadeMissingParamPolicy : uint8
{
	FadeSupportedSlots  UMETA(DisplayName="Fade Supported Slots"),
	SkipComponent       UMETA(DisplayName="Skip Component"),
	HardHideComponent   UMETA(DisplayName="Hard Hide Component")
};

/**
 * Local-only camera occlusion fade:
 * - Traces from camera to pawn samples
 * - Fades explicit roof components with a scalar material parameter
 */
UCLASS(ClassGroup=(Aeyerji), meta=(BlueprintSpawnableComponent), Config=Game, DefaultConfig)
class AEYERJI_API UAeyerjiCameraOcclusionFadeComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAeyerjiCameraOcclusionFadeComponent();

	/** Identification strategy for roof occluders. */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Camera Occlusion")
	EAeyerjiRoofOccluderFilter OccluderFilter = EAeyerjiRoofOccluderFilter::CollisionChannel;

	/** Trace channel used when OccluderFilter is CollisionChannel. */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Camera Occlusion")
	TEnumAsByte<ECollisionChannel> RoofTraceChannel = ECC_GameTraceChannel1;

	/** Component tag used when OccluderFilter is ComponentTag. */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Camera Occlusion")
	FName RoofComponentTag = FName("Occluder.Roof");

	/** Actor tag used when OccluderFilter is ActorTag. */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Camera Occlusion")
	FName RoofActorTag = FName("Occluder.Roof");

	/** Components with this tag are ignored even if they hit. */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Camera Occlusion")
	FName ExcludedComponentTag = FName("Occluder.NoFade");

	/** Scalar parameter name to drive the roof fade (0..1). */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Camera Occlusion|Material")
	FName RoofFadeParameter = FName("RoofFade");

	/** Behavior when the material does not expose the fade parameter. */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Camera Occlusion|Material")
	EAeyerjiFadeMissingParamPolicy MissingParamPolicy = EAeyerjiFadeMissingParamPolicy::FadeSupportedSlots;

	/** Fade value below which hard-hide toggles visibility. */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Camera Occlusion|Material", meta=(ClampMin="0.0", ClampMax="1.0"))
	float HardHideThreshold = 0.02f;

	/** Delay before starting to fade out once seen (seconds). */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Camera Occlusion|Timing", meta=(ClampMin="0.0"))
	float FadeOutDelay = 0.08f;

	/** Delay before starting to fade in once no longer seen (seconds). */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Camera Occlusion|Timing", meta=(ClampMin="0.0"))
	float FadeInDelay = 0.25f;

	/** Speed for fading out (units per second). */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Camera Occlusion|Timing", meta=(ClampMin="0.0"))
	float FadeSpeedOut = 2.5f;

	/** Speed for fading in (units per second). */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Camera Occlusion|Timing", meta=(ClampMin="0.0"))
	float FadeSpeedIn = 2.0f;

	/** Interval between occlusion traces (seconds). */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Camera Occlusion|Tracing", meta=(ClampMin="0.0"))
	float TraceInterval = 0.08f;

	/** Radius used for sphere traces (cm). */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Camera Occlusion|Tracing", meta=(ClampMin="0.0"))
	float TraceRadius = 20.f;

	/** When > 0, ignore occluders farther than this from the player (cm). */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Camera Occlusion|Tracing", meta=(ClampMin="0.0"))
	float MaxOccluderDistance = 0.f;

	/** Skip occluders whose bounds extents are smaller than this (cm). */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Camera Occlusion|Tracing", meta=(ClampMin="0.0"))
	float MinOccluderBoundsExtent = 0.f;

	/** Optional override for the sample offset radius (cm); 0 uses the pawn capsule radius. */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Camera Occlusion|Tracing", meta=(ClampMin="0.0"))
	float SampleOffsetRadius = 0.f;

	/** Adds a camera-direction sample for extra coverage. */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Camera Occlusion|Tracing")
	bool bIncludeCameraForwardSample = false;

	/** Distance for the camera-direction sample (cm). */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Camera Occlusion|Tracing", meta=(EditCondition="bIncludeCameraForwardSample", ClampMin="0.0"))
	float CameraForwardSampleDistance = 120.f;

	/** Trace complex for more accurate roof detection. */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Camera Occlusion|Tracing")
	bool bTraceComplex = false;

	/** Max number of occluders to fade at once; 0 = unlimited. */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Camera Occlusion|Safety", meta=(ClampMin="0"))
	int32 MaxHiddenComponents = 0;

	/** Minimum change before pushing fade values to MIDs. */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Camera Occlusion|Material", meta=(ClampMin="0.0"))
	float FadeUpdateThreshold = 0.005f;

	/** Time after fully visible before removing from tracking (seconds). */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Camera Occlusion|Safety", meta=(ClampMin="0.0"))
	float CleanupDelay = 3.f;

	/** Debug: draw traces. */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Camera Occlusion|Debug")
	bool bDrawDebugTraces = false;

	/** Debug: draw bounds for faded components. */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Camera Occlusion|Debug")
	bool bDrawDebugBounds = false;

	/** Debug: log occluder count. */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Camera Occlusion|Debug")
	bool bPrintOccluderCount = false;

	/** Debug draw duration (seconds). */
	UPROPERTY(EditAnywhere, Config, Category="Aeyerji|Camera Occlusion|Debug", meta=(ClampMin="0.0"))
	float DebugDrawDuration = 0.1f;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTick) override;

private:
	struct FMaterialSlot
	{
		int32 MaterialIndex = INDEX_NONE;
		TWeakObjectPtr<UMaterialInstanceDynamic> MID;
	};

	struct FOccluderFadeState
	{
		TArray<FMaterialSlot> Materials;
		float CurrentFade = 1.f;
		float TargetFade = 1.f;
		float LastAppliedFade = 1.f;
		double LastSeenTime = -1.0;
		double LastNotSeenTime = -1.0;
		bool bInitialized = false;
		bool bHardHide = false;
		bool bCachedHiddenState = false;
		bool bOriginalHiddenInGame = false;
		bool bHiddenBySystem = false;
	};

	void EvaluateOccluders();
	bool ResolveViewContext(FVector& OutCameraLoc, FVector& OutCameraDir, APawn*& OutPawn, APlayerController*& OutPC) const;
	void BuildSamplePoints(const APawn* Pawn, const FVector& CameraLoc, TArray<FVector>& OutSamples) const;
	bool IsValidOccluder(const UPrimitiveComponent* Comp) const;
	bool ShouldRunLocal() const;
	bool EnsureFadeState(UPrimitiveComponent* Comp, FOccluderFadeState& State);
	bool MaterialSupportsFadeParam(const UMaterialInterface* Material) const;
	void ApplyFade(UPrimitiveComponent* Comp, FOccluderFadeState& State, float FadeValue, bool bForce = false);
	void RestoreComponentVisibility(UPrimitiveComponent* Comp, FOccluderFadeState& State);
	void UpdateTargets(const TSet<TWeakObjectPtr<UPrimitiveComponent>>& NewOccluders, double Now);
	void EnforceOccluderCap(TSet<TWeakObjectPtr<UPrimitiveComponent>>& Occluders, const FVector& PlayerLoc) const;
	bool UpdateFadeValue(FOccluderFadeState& State, float DeltaSeconds);

	FTimerHandle TraceTimer;
	TMap<TWeakObjectPtr<UPrimitiveComponent>, FOccluderFadeState> OccluderStates;
};
