#include "Components/AeyerjiCombatCueProfileComponent.h"

#include "Camera/CameraShakeBase.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SceneComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/ForceFeedbackParameters.h"
#include "GameplayCue_Types.h"
#include "Kismet/GameplayStatics.h"
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "Sound/SoundBase.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS

const FAeyerjiCombatCuePresentation* FAeyerjiCombatCueProfileRow::FindPresentation(
	EAeyerjiCombatTextResultType CueType) const
{
	switch (CueType)
	{
	case EAeyerjiCombatTextResultType::Damage:
		return &PhysicalHit;
	case EAeyerjiCombatTextResultType::Critical:
		return &CriticalHit;
	case EAeyerjiCombatTextResultType::Dodged:
		return &Dodged;
	case EAeyerjiCombatTextResultType::Staggered:
		return &Staggered;
	case EAeyerjiCombatTextResultType::Killing:
		return &KillingHit;
	default:
		return nullptr;
	}
}

UAeyerjiCombatCueProfileComponent::UAeyerjiCombatCueProfileComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bAutoActivate = true;
}

bool UAeyerjiCombatCueProfileComponent::PlayCombatCuePresentation(
	EAeyerjiCombatTextResultType CueType,
	const FGameplayCueParameters& Parameters)
{
	UWorld* World = GetWorld();
	if (!World || World->IsNetMode(NM_DedicatedServer))
	{
		return false;
	}

	const FAeyerjiCombatCueProfileRow* Profile = ResolveCombatCueProfile();
	if (!Profile)
	{
		return false;
	}

	const FAeyerjiCombatCuePresentation* Presentation = Profile->FindPresentation(CueType);
	if (!Presentation || !Presentation->HasPresentation())
	{
		return false;
	}

	const FVector Location = ResolvePresentationLocation(*Presentation, Parameters);
	const FRotator Rotation = ResolvePresentationRotation(*Presentation, Parameters);

	bool bHandled = false;
	if (Presentation->Sound)
	{
		const float MinPitch = FMath::Max(0.01f, FMath::Min(Presentation->PitchRange.X, Presentation->PitchRange.Y));
		const float MaxPitch = FMath::Max(MinPitch, FMath::Max(Presentation->PitchRange.X, Presentation->PitchRange.Y));
		UGameplayStatics::PlaySoundAtLocation(
			this,
			Presentation->Sound,
			Location,
			FMath::Max(0.f, Presentation->VolumeMultiplier),
			FMath::RandRange(MinPitch, MaxPitch));
		bHandled = true;
	}

	if (Presentation->Effect)
	{
		USceneComponent* AttachComponent = nullptr;
		if (Presentation->bAttachEffectToTarget)
		{
			if (ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner()))
			{
				AttachComponent = CharacterOwner->GetMesh();
			}

			if (!AttachComponent && GetOwner())
			{
				AttachComponent = GetOwner()->FindComponentByClass<USceneComponent>();
			}
		}

		if (AttachComponent)
		{
			UNiagaraFunctionLibrary::SpawnSystemAttached(
				Presentation->Effect,
				AttachComponent,
				Presentation->AttachSocket,
				Presentation->LocationOffset,
				Presentation->RotationOffset,
				EAttachLocation::KeepRelativeOffset,
				true,
				true,
				ENCPoolMethod::AutoRelease,
				true);
		}
		else
		{
			UNiagaraFunctionLibrary::SpawnSystemAtLocation(
				World,
				Presentation->Effect,
				Location,
				Rotation,
				Presentation->EffectScale,
				true,
				true,
				ENCPoolMethod::AutoRelease,
				true);
		}
		bHandled = true;
	}

	bHandled |= PlayLocalPlayerFeedback(*Presentation, Location, Rotation);

	return bHandled;
}

const FAeyerjiCombatCueProfileRow* UAeyerjiCombatCueProfileComponent::ResolveCombatCueProfile() const
{
	if (CombatCueProfileRow.DataTable && !CombatCueProfileRow.RowName.IsNone())
	{
		static const FString ContextString(TEXT("AeyerjiCombatCueProfile"));
		if (const FAeyerjiCombatCueProfileRow* Row =
			CombatCueProfileRow.GetRow<FAeyerjiCombatCueProfileRow>(ContextString))
		{
			return Row;
		}
	}

PRAGMA_DISABLE_DEPRECATION_WARNINGS
	return LegacyCombatCueProfile ? &LegacyCombatCueProfile->Profile : nullptr;
PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

FVector UAeyerjiCombatCueProfileComponent::ResolvePresentationLocation(
	const FAeyerjiCombatCuePresentation& Presentation,
	const FGameplayCueParameters& Parameters) const
{
	FVector Location = FVector::ZeroVector;
	if (Presentation.bUseCueLocation && !Parameters.Location.IsNearlyZero())
	{
		Location = Parameters.Location;
	}
	else if (GetOwner())
	{
		Location = GetOwner()->GetActorLocation();
		if (ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner()))
		{
			if (USkeletalMeshComponent* Mesh = CharacterOwner->GetMesh())
			{
				if (!Presentation.AttachSocket.IsNone() && Mesh->DoesSocketExist(Presentation.AttachSocket))
				{
					Location = Mesh->GetSocketLocation(Presentation.AttachSocket);
				}
			}
		}
	}

	return Location + Presentation.LocationOffset;
}

FRotator UAeyerjiCombatCueProfileComponent::ResolvePresentationRotation(
	const FAeyerjiCombatCuePresentation& Presentation,
	const FGameplayCueParameters& Parameters) const
{
	FRotator Rotation = Presentation.RotationOffset;
	if (!Parameters.Normal.IsNearlyZero())
	{
		Rotation = Parameters.Normal.Rotation() + Presentation.RotationOffset;
	}
	return Rotation;
}

bool UAeyerjiCombatCueProfileComponent::PlayLocalPlayerFeedback(
	const FAeyerjiCombatCuePresentation& Presentation,
	const FVector& Location,
	const FRotator& Rotation)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(World, 0);
	if (!PlayerController || !PlayerController->IsLocalController())
	{
		return false;
	}

	bool bHandled = false;
	if (Presentation.CameraShake)
	{
		if (Presentation.bPlayCameraShakeInWorld)
		{
			const float InnerRadius = FMath::Max(0.f, Presentation.WorldShakeInnerRadius);
			const float OuterRadius = FMath::Max(InnerRadius, Presentation.WorldShakeOuterRadius);
			UGameplayStatics::PlayWorldCameraShake(
				this,
				Presentation.CameraShake,
				Location,
				InnerRadius,
				OuterRadius,
				FMath::Max(0.f, Presentation.WorldShakeFalloff));
		}
		else if (PlayerController->PlayerCameraManager)
		{
			PlayerController->PlayerCameraManager->StartCameraShake(
				Presentation.CameraShake,
				FMath::Max(0.f, Presentation.CameraShakeScale),
				Presentation.CameraShakePlaySpace,
				Rotation);
		}
		bHandled = true;
	}

	if (Presentation.CameraLensEffect && PlayerController->PlayerCameraManager)
	{
		PlayerController->PlayerCameraManager->AddGenericCameraLensEffect(Presentation.CameraLensEffect);
		bHandled = true;
	}

	if (Presentation.ForceFeedback)
	{
		UGameplayStatics::SpawnForceFeedbackAtLocation(
			this,
			Presentation.ForceFeedback,
			Location,
			Rotation,
			Presentation.bLoopForceFeedback,
			FMath::Max(0.f, Presentation.ForceFeedbackIntensity),
			0.f,
			nullptr,
			true);
		bHandled = true;
	}

	return bHandled;
}
