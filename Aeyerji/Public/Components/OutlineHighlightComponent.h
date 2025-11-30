// OutlineHighlightComponent.h
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "Templates/Function.h"
#include "TimerManager.h"

#include "OutlineHighlightComponent.generated.h"

class UPrimitiveComponent;

/**
 * Minimal outline helper:
 * - Maps gameplay rarity indices to stencil slots
 * - Applies CustomDepth/CustomStencil to chosen primitives
 * - Toggles highlight visibility
 *
 * Post-process material is expected to read colors from a shared palette texture using the stencil value.
 */
UCLASS(ClassGroup = (Aeyerji), meta = (BlueprintSpawnableComponent))
class AEYERJI_API UOutlineHighlightComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOutlineHighlightComponent();

	/** If empty and bAffectAllPrimitivesIfNoExplicitTargets is true, all primitives on the owner will be affected. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline")
	TArray<TObjectPtr<UPrimitiveComponent>> ExplicitTargets;

	/** When true and ExplicitTargets is empty, every primitive component on the owner is controlled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline")
	bool bAffectAllPrimitivesIfNoExplicitTargets = true;

	/** Map from gameplay rarity/index value (0..255) to the stencil slot (0..255) used by the outline post-process. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline")
	TMap<int32, int32> RarityIndexToStencil;

	/** Custom depth write mask applied whenever the highlight is active. Usually ERSM_Default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Outline")
	ERendererStencilMask WriteMask = ERendererStencilMask::ERSM_Default;

	/** Apply the configured mapping and set the stencil, then reapply highlight state if already active. */
	UFUNCTION(BlueprintCallable, Category = "Outline")
	void InitializeFromRarityIndex(int32 RarityIndex);

	/** Directly set a stencil value on the current targets (0..255). */
	UFUNCTION(BlueprintCallable, Category = "Outline")
	void ApplyStencilValue(int32 StencilValue);

	/** Enable or disable custom depth rendering on the controlled primitives. */
	UFUNCTION(BlueprintCallable, Category = "Outline")
	void SetHighlighted(bool bEnable);

	/** Resolve the stencil slot for a given rarity/index value. */
	UFUNCTION(BlueprintPure, Category = "Outline")
	int32 ResolveStencilForRarity(int32 RarityIndex) const;

	/** Returns the last stencil value written to the primitives. */
	UFUNCTION(BlueprintPure, Category = "Outline")
	int32 GetCurrentStencilValue() const { return CachedStencil; }

	/** Temporarily forces the outline on, restoring the previous highlight state when finished. */
	UFUNCTION(BlueprintCallable, Category = "Outline")
	void PulseHighlight(float Duration, float FadeTime, int32 OverrideStencil = -1);

protected:
	virtual void BeginPlay() override;

	void GatherTargets(TArray<UPrimitiveComponent*>& OutTargets) const;
	void ApplyToTargets(TFunctionRef<void(UPrimitiveComponent*)> Fn) const;
	void HandleHighlightPulseFinished();

	UPROPERTY(VisibleInstanceOnly, Category = "Outline")
	int32 CachedStencil = 0;

	UPROPERTY(VisibleInstanceOnly, Category = "Outline")
	bool bCurrentlyHighlighted = false;

	UPROPERTY(VisibleInstanceOnly, Category = "Outline")
	bool bPulseActive = false;

	UPROPERTY(VisibleInstanceOnly, Category = "Outline")
	bool bHighlightedBeforePulse = false;

	FTimerHandle HighlightPulseHandle;
};
