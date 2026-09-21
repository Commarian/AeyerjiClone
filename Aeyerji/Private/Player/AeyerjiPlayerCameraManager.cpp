#include "Player/AeyerjiPlayerCameraManager.h"

#include "GameFramework/PlayerController.h"
#include "Logging/AeyerjiLog.h"
#include "Player/PlayerParentNative.h"

APlayerParentNative* AAeyerjiPlayerCameraManager::GetProtectedGameplayPawn() const
{
	APlayerController* OwningController = GetOwningPlayerController();
	if (!IsValid(OwningController)
		|| !OwningController->IsLocalController()
		|| OwningController->GetStateName() != NAME_Playing)
	{
		return nullptr;
	}

	APlayerParentNative* PlayerPawn = Cast<APlayerParentNative>(OwningController->GetPawn());
	if (!IsValid(PlayerPawn)
		|| PlayerPawn->IsPendingKillPending()
		|| PlayerPawn->IsActorBeingDestroyed()
		|| PlayerPawn->GetController() != OwningController)
	{
		return nullptr;
	}

	return PlayerPawn;
}

bool AAeyerjiPlayerCameraManager::SanitizeControllerFallback(APlayerParentNative* PlayerPawn)
{
	APlayerController* OwningController = GetOwningPlayerController();
	if (!IsValid(OwningController) || !IsValid(PlayerPawn))
	{
		return false;
	}

	const bool bCurrentIsControllerFallback = ViewTarget.Target == OwningController || !IsValid(ViewTarget.Target);
	const bool bPendingIsControllerFallback = PendingViewTarget.Target == OwningController;
	if (!bCurrentIsControllerFallback && !bPendingIsControllerFallback)
	{
		return false;
	}

	// Setting the current valid target again is UE's supported way to abort a pending blend. If the
	// current target has already fallen back, the possessed pawn becomes the replacement instead.
	AActor* TargetToKeep = bCurrentIsControllerFallback ? static_cast<AActor*>(PlayerPawn) : ViewTarget.Target.Get();
	Super::SetViewTarget(TargetToKeep);
	return true;
}

void AAeyerjiPlayerCameraManager::SetViewTarget(AActor* NewViewTarget, FViewTargetTransitionParams TransitionParams)
{
	APlayerController* OwningController = GetOwningPlayerController();
	if (NewViewTarget == OwningController)
	{
		if (APlayerParentNative* PlayerPawn = GetProtectedGameplayPawn())
		{
			NewViewTarget = PlayerPawn;
		}
	}

	Super::SetViewTarget(NewViewTarget, TransitionParams);
}

void AAeyerjiPlayerCameraManager::UpdateCamera(float DeltaTime)
{
	APlayerParentNative* PlayerPawn = GetProtectedGameplayPawn();
	const FString CurrentBefore = GetNameSafe(ViewTarget.Target.Get());
	const FString PendingBefore = GetNameSafe(PendingViewTarget.Target.Get());
	const float BlendTimeBefore = BlendTimeToGo;
	bool bRepairedFallback = PlayerPawn && SanitizeControllerFallback(PlayerPawn);

	Super::UpdateCamera(DeltaTime);

	PlayerPawn = GetProtectedGameplayPawn();
	if (PlayerPawn && SanitizeControllerFallback(PlayerPawn))
	{
		bRepairedFallback = true;

		// Recalculate once with the corrected target so the current frame receives the pawn camera POV.
		// The zero delta avoids advancing camera animations or blends twice.
		Super::UpdateCamera(0.0f);
	}

	if (bRepairedFallback)
	{
		const UWorld* World = GetWorld();
		const double Now = World ? World->GetTimeSeconds() : 0.0;
		if (LastControllerFallbackDiagnosticTime < 0.0 || (Now - LastControllerFallbackDiagnosticTime) >= 1.0)
		{
			UE_LOG(LogAeyerji, Warning,
				TEXT("[CameraTrace] Camera manager blocked controller fallback. Pawn=%s CurrentBefore=%s PendingBefore=%s BlendTimeBefore=%.3f CurrentAfter=%s PendingAfter=%s ManagerClass=%s"),
				*GetNameSafe(PlayerPawn),
				*CurrentBefore,
				*PendingBefore,
				BlendTimeBefore,
				*GetNameSafe(ViewTarget.Target.Get()),
				*GetNameSafe(PendingViewTarget.Target.Get()),
				*GetClass()->GetName());
			LastControllerFallbackDiagnosticTime = Now;
		}
	}
}
