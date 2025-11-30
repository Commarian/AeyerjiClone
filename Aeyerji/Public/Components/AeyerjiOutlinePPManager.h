// AeyerjiOutlinePPManager.h
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "AeyerjiOutlinePPManager.generated.h"

class APostProcessVolume;
class UMaterialInstanceDynamic;
class UMaterialInterface;
/**
 * Simple actor that pushes an outline post-process MID into the target volume at startup.
 * Place this in a level (or spawn it) and optionally assign a PostProcessVolume and PP material.
 */
UCLASS()
class AEYERJI_API AAeyerjiOutlinePPManager : public AActor
{
	GENERATED_BODY()

public:
	AAeyerjiOutlinePPManager();

protected:
	virtual void BeginPlay() override;

	/** Post-process volume to modify. When unset, we try to find an unbound volume in the world at runtime. */
	UPROPERTY(EditInstanceOnly, Category = "Outline|PostProcess")
	TObjectPtr<APostProcessVolume> TargetPostProcessVolume = nullptr;

	/**
	 * Source outline post-process material. If specified we create a MID from it and push it into the
	 * target volume as a new blendable. Otherwise we attempt to wrap an existing material blendable.
	 */
	UPROPERTY(EditAnywhere, Category = "Outline|PostProcess")
	TObjectPtr<UMaterialInterface> OutlinePostProcessMaterial = nullptr;

	/** Additional outline materials that should be injected into the post-process volume (each gets its own MID). */
	UPROPERTY(EditAnywhere, Category = "Outline|PostProcess")
	TArray<TObjectPtr<UMaterialInterface>> AdditionalOutlineMaterials;

	/** Runtime MIDs we push to the post-process volume, keyed by their source material. */
	UPROPERTY(Transient)
	TMap<TObjectPtr<UMaterialInterface>, TObjectPtr<UMaterialInstanceDynamic>> OutlineMIDs;

public:
	/** Returns the runtime MID that was generated for the supplied material (nullptr if none). */
	UFUNCTION(BlueprintPure, Category = "Outline|PostProcess")
	UMaterialInstanceDynamic* GetMIDForMaterial(const UMaterialInterface* SourceMaterial) const;

private:
	APostProcessVolume* FindFallbackPostProcessVolume() const;
	UMaterialInstanceDynamic* AcquireOrWrapExistingMID(APostProcessVolume* PostProcessVolume);
	UMaterialInstanceDynamic* AddMaterialToVolume(APostProcessVolume* PostProcessVolume, UMaterialInterface* Material);
};
