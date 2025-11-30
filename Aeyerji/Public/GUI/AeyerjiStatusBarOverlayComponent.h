// AeyerjiStatusBarOverlayComponent.h

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayEffectTypes.h"
#include "AeyerjiStatusBarOverlayComponent.generated.h"

class UAeyerjiFloatingStatusBarComponent;
class UW_AeyerjiStatusBar;
class UAbilitySystemComponent;

/**
 * Local client manager that renders enemy status bars as screen-space widgets.
 * Attach ONE of these to the local PlayerController (AeyerjiPlayerController).
 */
UCLASS(ClassGroup=(Aeyerji), meta=(BlueprintSpawnableComponent))
class AEYERJI_API UAeyerjiStatusBarOverlayComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAeyerjiStatusBarOverlayComponent();

	/** Default widget class if the source component doesn't provide one. */
	UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBars")
	TSubclassOf<UW_AeyerjiStatusBar> DefaultWidgetClass;

	/** Base ZOrder for spawned widgets (higher draws on top). */
	UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBars")
	int32 BaseZOrder = 20;

	/** Max distance to draw (cm). 0 = no cull. */
	UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBars")
	float MaxDrawDistance = 8000.f;

	/** Hide when occluded by world geometry (line trace). */
	UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBars")
	bool bOcclusionCheck = true;

	/** Pad from screen edges when clamping (px). */
	UPROPERTY(EditAnywhere, Category="Aeyerji|StatusBars")
	float EdgePadding = 8.f;

	/** Register a source component (enemy). Returns the widget created (local only). */
	UW_AeyerjiStatusBar* RegisterSource(UAeyerjiFloatingStatusBarComponent* Source);

	/** Unregister a previously registered source. */
	void UnregisterSource(UAeyerjiFloatingStatusBarComponent* Source);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTick) override;

private:
	struct FTracked
	{
		TWeakObjectPtr<UAeyerjiFloatingStatusBarComponent> Source;
		TWeakObjectPtr<AActor> Target;
		TWeakObjectPtr<UW_AeyerjiStatusBar> Widget;
		FVector WorldOffset = FVector::ZeroVector;
		FVector2D ScreenPixelOffset = FVector2D::ZeroVector;
		int32 ZOrder = 0;
	};
	TArray<FTracked> Tracked;

	bool ProjectToScreen(const FVector& WorldLoc, FVector2D& OutPos) const;
	bool IsOccluded(const FVector& WorldLoc, const AActor* Ignore) const;
	APlayerController* GetPC() const
	{
		if (Cast<APlayerController>(GetOwner())->IsLocalController())
		{
			return Cast<APlayerController>(GetOwner());
		}
		return nullptr;
	}
};
