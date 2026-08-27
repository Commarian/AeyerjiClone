// Copyright (c) 2025 Aeyerji.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "AeyerjiMinimapMarkerComponent.generated.h"

class UTexture2D;

/** Visual category used by the native minimap placeholder renderer. */
UENUM(BlueprintType)
enum class EAeyerjiMinimapMarkerType : uint8
{
	Friendly UMETA(DisplayName="Friendly"),
	Enemy UMETA(DisplayName="Enemy"),
	Objective UMETA(DisplayName="Objective"),
	PointOfInterest UMETA(DisplayName="Point Of Interest")
};

/**
 * Optional, presentation-only metadata that overrides automatic markers or adds otherwise unknown actors.
 * Add it to a replicated actor Blueprint so each client can map the actor state it already receives.
 */
UCLASS(ClassGroup=(Aeyerji), meta=(BlueprintSpawnableComponent))
class AEYERJI_API UAeyerjiMinimapMarkerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAeyerjiMinimapMarkerComponent();

	/** Controls eligibility and suppresses automatic discovery for the owning actor when disabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Minimap")
	bool bVisibleOnMinimap = true;

	/** Selects the placeholder shape and default color used for this marker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Minimap")
	EAeyerjiMinimapMarkerType MarkerType = EAeyerjiMinimapMarkerType::PointOfInterest;

	/** Keeps this marker at the map rim when its owner is outside the displayed world radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Minimap")
	bool bClampToEdge = false;

	/** Keeps the marker visible when its owner has Aeyerji's dead gameplay state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Minimap")
	bool bShowWhenOwnerDead = false;

	/** Rotates the marker's heading indicator with the owning actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Minimap")
	bool bRotateWithOwner = false;

	/** Uses Custom Color instead of the minimap palette for this marker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Minimap|Style")
	bool bUseCustomColor = false;

	/** Designer-selected tint used when Use Custom Color is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Minimap|Style", meta=(EditCondition="bUseCustomColor", EditConditionHides))
	FLinearColor CustomColor = FLinearColor::White;

	/** Marker diameter in Slate units; zero uses the minimap widget's type-specific default. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Minimap|Style", meta=(ClampMin="0.0", UIMin="0.0", UIMax="32.0"))
	float MarkerSize = 0.f;

	/** Optional texture that replaces the native placeholder shape while preserving positioning and tint. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Aeyerji|Minimap|Style")
	TObjectPtr<UTexture2D> IconTexture = nullptr;
};
