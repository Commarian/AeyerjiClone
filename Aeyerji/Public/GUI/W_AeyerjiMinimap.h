// Copyright (c) 2025 Aeyerji.
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GUI/AeyerjiMinimapMarkerComponent.h"
#include "W_AeyerjiMinimap.generated.h"

class AActor;
class UTexture2D;

/** Cached local presentation data for one client-visible minimap actor. */
struct FAeyerjiTrackedMinimapMarker
{
	TWeakObjectPtr<AActor> Actor;
	TWeakObjectPtr<UTexture2D> IconTexture;
	EAeyerjiMinimapMarkerType MarkerType = EAeyerjiMinimapMarkerType::PointOfInterest;
	FLinearColor CustomColor = FLinearColor::White;
	float MarkerSize = 0.f;
	bool bClampToEdge = false;
	bool bShowWhenOwnerDead = false;
	bool bRotateWithOwner = false;
	bool bUseCustomColor = false;
};

/**
 * Local-only radar minimap with procedural placeholder art.
 * It reads actors already replicated to the owning client and never sends gameplay or networking requests.
 */
UCLASS(Blueprintable)
class AEYERJI_API UW_AeyerjiMinimap : public UUserWidget
{
	GENERATED_BODY()

public:
	/** Immediately rebuilds the cached actor list instead of waiting for the next refresh interval. */
	UFUNCTION(BlueprintCallable, Category="Aeyerji|Minimap")
	void RefreshMarkers();

	/** Returns the number of valid marker candidates currently cached for local rendering. */
	UFUNCTION(BlueprintPure, Category="Aeyerji|Minimap")
	int32 GetTrackedMarkerCount() const { return TrackedMarkers.Num(); }

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	virtual int32 NativePaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

	/** World-space radius in centimeters represented from the owning pawn to the map rim. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Minimap|Range", meta=(ClampMin="100.0", UIMin="1000.0", UIMax="20000.0"))
	float MapWorldRadius = 6000.f;

	/** Delay between actor discovery passes; marker positions still animate every rendered frame. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Minimap|Range", meta=(ClampMin="0.05", UIMin="0.1", UIMax="2.0"))
	float MarkerRefreshInterval = 0.25f;

	/** Rotates world offsets so the owning pawn always points toward the top of the map. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Minimap|Range")
	bool bRotateWithPlayer = true;

	/** Automatically presents other Aeyerji player pawns as friendly markers. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Minimap|Discovery")
	bool bAutoDiscoverPlayers = true;

	/** Automatically presents living Aeyerji enemy pawns inside the displayed radius. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Minimap|Discovery")
	bool bAutoDiscoverEnemies = true;

	/** Automatically presents active survival defense objective actors. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Minimap|Discovery")
	bool bAutoDiscoverObjectives = true;

	/** Allows friendly markers, including automatic remote-player markers, to render. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Minimap|Discovery")
	bool bShowFriendlies = true;

	/** Allows enemy markers, including automatically discovered enemy pawns, to render. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Minimap|Discovery")
	bool bShowEnemies = true;

	/** Allows objective markers to render and remain clamped to the map edge. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Minimap|Discovery")
	bool bShowObjectives = true;

	/** Allows designer-authored point-of-interest marker components to render. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Minimap|Discovery")
	bool bShowPointsOfInterest = true;

	/** Viewport size of the native minimap placeholder. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Minimap|Layout", meta=(ClampMin="64.0", UIMin="128.0", UIMax="512.0"))
	FVector2D DisplaySize = FVector2D(220.f, 220.f);

	/** Distance from the top-right viewport corner; the default clears WBP_MissionHUD's gold/menu strip. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Minimap|Layout", meta=(ClampMin="0.0", UIMin="0.0", UIMax="128.0"))
	FVector2D ViewportEdgePadding = FVector2D(24.f, 64.f);

	/** Empty space between the circular frame and clamped marker positions. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Minimap|Layout", meta=(ClampMin="0.0", UIMin="0.0", UIMax="48.0"))
	float MarkerEdgePadding = 12.f;

	/** Diameter used by automatic ally, enemy, and point-of-interest placeholder markers. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Minimap|Style", meta=(ClampMin="2.0", UIMin="4.0", UIMax="24.0"))
	float DefaultMarkerSize = 8.f;

	/** Diameter used by the centered local-player direction marker. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Minimap|Style", meta=(ClampMin="4.0", UIMin="6.0", UIMax="32.0"))
	float PlayerMarkerSize = 14.f;

	/** Diameter used by automatically discovered objective markers. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Minimap|Style", meta=(ClampMin="4.0", UIMin="6.0", UIMax="32.0"))
	float ObjectiveMarkerSize = 12.f;

	/** Thickness used by the map frame, rings, and placeholder icon outlines. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Minimap|Style", meta=(ClampMin="0.5", UIMin="1.0", UIMax="6.0"))
	float LineThickness = 1.5f;

	/** Fill tint used by the circular placeholder map surface. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Minimap|Style")
	FLinearColor BackgroundColor = FLinearColor(0.012f, 0.025f, 0.045f, 0.86f);

	/** Tint used by the outer map frame. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Minimap|Style")
	FLinearColor FrameColor = FLinearColor(0.10f, 0.82f, 0.92f, 0.92f);

	/** Tint used by the procedural grid and range rings. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Minimap|Style")
	FLinearColor GridColor = FLinearColor(0.10f, 0.48f, 0.58f, 0.28f);

	/** Tint used by the owning player's centered direction marker. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Minimap|Style")
	FLinearColor PlayerColor = FLinearColor(0.25f, 0.95f, 1.f, 1.f);

	/** Default tint used by remote friendly-player markers. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Minimap|Style")
	FLinearColor FriendlyColor = FLinearColor(0.20f, 0.90f, 0.42f, 1.f);

	/** Default tint used by living enemy markers. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Minimap|Style")
	FLinearColor EnemyColor = FLinearColor(1.f, 0.18f, 0.14f, 1.f);

	/** Default tint used by active objective markers. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Minimap|Style")
	FLinearColor ObjectiveColor = FLinearColor(1.f, 0.72f, 0.12f, 1.f);

	/** Default tint used by designer-authored point-of-interest markers. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Minimap|Style")
	FLinearColor PointOfInterestColor = FLinearColor(0.72f, 0.38f, 1.f, 1.f);

private:
	bool IsMarkerTypeEnabled(EAeyerjiMinimapMarkerType MarkerType) const;
	bool IsMarkerActorVisible(const FAeyerjiTrackedMinimapMarker& Marker) const;
	FLinearColor ResolveMarkerColor(const FAeyerjiTrackedMinimapMarker& Marker) const;
	float ResolveMarkerSize(const FAeyerjiTrackedMinimapMarker& Marker) const;
	bool ProjectMarkerToMap(
		const AActor& MarkerActor,
		const AActor& PlayerActor,
		const FVector2f& Center,
		float DrawRadius,
		bool bClampToEdge,
		FVector2f& OutPosition,
		bool& bOutAtEdge) const;

	TArray<FAeyerjiTrackedMinimapMarker> TrackedMarkers;
	float SecondsUntilMarkerRefresh = 0.f;
};
