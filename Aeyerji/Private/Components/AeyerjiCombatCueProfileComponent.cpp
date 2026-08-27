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

namespace
{
	constexpr float MaxCueScalar = 100.f;
	constexpr float MaxCueDistance = 10000000.f;

	float FiniteCueScalar(const float Value, const float DefaultValue, const float MinValue, const float MaxValue)
	{
		return FMath::Clamp(FMath::IsFinite(Value) ? Value : DefaultValue, MinValue, MaxValue);
	}

	bool IsFiniteCueVector(const FVector& Value)
	{
		return FMath::IsFinite(Value.X) && FMath::IsFinite(Value.Y) && FMath::IsFinite(Value.Z);
	}

	FVector FiniteCueVector(const FVector& Value, const FVector& DefaultValue = FVector::ZeroVector)
	{
		return IsFiniteCueVector(Value) ? Value : DefaultValue;
	}

	FRotator FiniteCueRotator(const FRotator& Value)
	{
		return FMath::IsFinite(Value.Pitch) && FMath::IsFinite(Value.Yaw) && FMath::IsFinite(Value.Roll)
			? Value.GetNormalized()
			: FRotator::ZeroRotator;
	}

	FVector FiniteCueScale(const FVector& Value)
	{
		const FVector SafeValue = FiniteCueVector(Value, FVector::OneVector);
		return FVector(
			FMath::Clamp(SafeValue.X, -MaxCueScalar, MaxCueScalar),
			FMath::Clamp(SafeValue.Y, -MaxCueScalar, MaxCueScalar),
			FMath::Clamp(SafeValue.Z, -MaxCueScalar, MaxCueScalar));
	}
}

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
		const float PitchX = FiniteCueScalar(Presentation->PitchRange.X, 1.f, 0.01f, 4.f);
		const float PitchY = FiniteCueScalar(Presentation->PitchRange.Y, 1.f, 0.01f, 4.f);
		const float MinPitch = FMath::Min(PitchX, PitchY);
		const float MaxPitch = FMath::Max(PitchX, PitchY);
		UGameplayStatics::PlaySoundAtLocation(
			this,
			Presentation->Sound,
			Location,
			FiniteCueScalar(Presentation->VolumeMultiplier, 1.f, 0.f, MaxCueScalar),
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
				FiniteCueVector(Presentation->LocationOffset),
				FiniteCueRotator(Presentation->RotationOffset),
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
				FiniteCueScale(Presentation->EffectScale),
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
	if (Presentation.bUseCueLocation && IsFiniteCueVector(Parameters.Location) && !Parameters.Location.IsNearlyZero())
	{
		Location = Parameters.Location;
	}
	else if (GetOwner())
	{
		Location = FiniteCueVector(GetOwner()->GetActorLocation());
		if (ACharacter* CharacterOwner = Cast<ACharacter>(GetOwner()))
		{
			if (USkeletalMeshComponent* Mesh = CharacterOwner->GetMesh())
			{
				if (!Presentation.AttachSocket.IsNone() && Mesh->DoesSocketExist(Presentation.AttachSocket))
				{
					Location = FiniteCueVector(Mesh->GetSocketLocation(Presentation.AttachSocket), Location);
				}
			}
		}
	}

	const FVector Result = Location + FiniteCueVector(Presentation.LocationOffset);
	return FiniteCueVector(Result, Location);
}

FRotator UAeyerjiCombatCueProfileComponent::ResolvePresentationRotation(
	const FAeyerjiCombatCuePresentation& Presentation,
	const FGameplayCueParameters& Parameters) const
{
	const FRotator RotationOffset = FiniteCueRotator(Presentation.RotationOffset);
	FRotator Rotation = RotationOffset;
	if (IsFiniteCueVector(Parameters.Normal) && !Parameters.Normal.IsNearlyZero())
	{
		Rotation = Parameters.Normal.GetSafeNormal().Rotation() + RotationOffset;
	}
	return FiniteCueRotator(Rotation);
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
			const float InnerRadius = FiniteCueScalar(
				Presentation.WorldShakeInnerRadius, 0.f, 0.f, MaxCueDistance);
			const float OuterRadius = FMath::Max(InnerRadius, FiniteCueScalar(
				Presentation.WorldShakeOuterRadius, InnerRadius, 0.f, MaxCueDistance));
			UGameplayStatics::PlayWorldCameraShake(
				this,
				Presentation.CameraShake,
				Location,
				InnerRadius,
				OuterRadius,
				FiniteCueScalar(Presentation.WorldShakeFalloff, 1.f, 0.f, MaxCueScalar));
		}
		else if (PlayerController->PlayerCameraManager)
		{
			PlayerController->PlayerCameraManager->StartCameraShake(
				Presentation.CameraShake,
				FiniteCueScalar(Presentation.CameraShakeScale, 1.f, 0.f, MaxCueScalar),
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
			FiniteCueScalar(Presentation.ForceFeedbackIntensity, 1.f, 0.f, MaxCueScalar),
			0.f,
			nullptr,
			true);
		bHandled = true;
	}

	return bHandled;
}
