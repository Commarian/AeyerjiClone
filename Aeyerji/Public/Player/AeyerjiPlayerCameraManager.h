#pragma once

#include "Camera/PlayerCameraManager.h"
#include "AeyerjiPlayerCameraManager.generated.h"

class APlayerParentNative;

/**
 * Owns Aeyerji's local gameplay-camera invariant. While a healthy PlayerParent pawn is possessed,
 * the owning controller is never a valid fallback view target. Other actors remain valid so
 * cinematics and explicit camera rigs continue to work normally.
 */
UCLASS(NotPlaceable, Transient)
class AEYERJI_API AAeyerjiPlayerCameraManager : public APlayerCameraManager
{
	GENERATED_BODY()

public:
	virtual void SetViewTarget(AActor* NewViewTarget, FViewTargetTransitionParams TransitionParams = FViewTargetTransitionParams()) override;
	virtual void UpdateCamera(float DeltaTime) override;

private:
	/** Returns the locally controlled, live gameplay pawn for which controller fallback is invalid. */
	APlayerParentNative* GetProtectedGameplayPawn() const;

	/** Cancels a current or pending transition to the owning controller while preserving valid camera actors. */
	bool SanitizeControllerFallback(APlayerParentNative* PlayerPawn);

	/** Rate-limits diagnostics if an engine or Blueprint path repeatedly tries to restore the fallback. */
	double LastControllerFallbackDiagnosticTime = -1.0;
};
