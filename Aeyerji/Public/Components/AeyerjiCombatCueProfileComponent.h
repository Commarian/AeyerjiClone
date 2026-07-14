#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraTypes.h"
#include "Components/ActorComponent.h"
#include "Engine/DataAsset.h"
#include "Engine/DataTable.h"
#include "GUI/AeyerjiCombatTextTypes.h"

#include "AeyerjiCombatCueProfileComponent.generated.h"

class UNiagaraSystem;
class USoundBase;
class UCameraShakeBase;
class UForceFeedbackEffect;
struct FGameplayCueParameters;

/** Presentation assets to play for one combat cue result on a target. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiCombatCuePresentation
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue")
	TObjectPtr<USoundBase> Sound = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue")
	TObjectPtr<UNiagaraSystem> Effect = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue")
	bool bUseCueLocation = true;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue")
	bool bAttachEffectToTarget = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue")
	FName AttachSocket = NAME_None;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue")
	FVector LocationOffset = FVector::ZeroVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue")
	FRotator RotationOffset = FRotator::ZeroRotator;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue")
	FVector EffectScale = FVector(1.f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue", meta = (ClampMin = "0.0"))
	float VolumeMultiplier = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue", meta = (ClampMin = "0.01"))
	FVector2D PitchRange = FVector2D(0.95f, 1.05f);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue|Camera")
	TSubclassOf<UCameraShakeBase> CameraShake;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue|Camera", meta = (ClampMin = "0.0"))
	float CameraShakeScale = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue|Camera")
	ECameraShakePlaySpace CameraShakePlaySpace = ECameraShakePlaySpace::CameraLocal;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue|Camera")
	bool bPlayCameraShakeInWorld = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue|Camera", meta = (EditCondition = "bPlayCameraShakeInWorld", ClampMin = "0.0"))
	float WorldShakeInnerRadius = 0.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue|Camera", meta = (EditCondition = "bPlayCameraShakeInWorld", ClampMin = "0.0"))
	float WorldShakeOuterRadius = 1500.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue|Camera", meta = (EditCondition = "bPlayCameraShakeInWorld", ClampMin = "0.0"))
	float WorldShakeFalloff = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue|Camera", meta = (MustImplement = "/Script/Engine.CameraLensEffectInterface"))
	TSubclassOf<AActor> CameraLensEffect;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue|Feedback")
	TObjectPtr<UForceFeedbackEffect> ForceFeedback = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue|Feedback", meta = (ClampMin = "0.0"))
	float ForceFeedbackIntensity = 1.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue|Feedback")
	bool bLoopForceFeedback = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue|Feedback")
	bool bIgnoreForceFeedbackTimeDilation = false;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue|Feedback")
	bool bPlayForceFeedbackWhilePaused = false;

	bool HasPresentation() const { return Sound || Effect || CameraShake || CameraLensEffect || ForceFeedback; }
};

/** DataTable row assigned to a target to customize how generic combat GameplayCues present on that target. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiCombatCueProfileRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue")
	FAeyerjiCombatCuePresentation PhysicalHit;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue")
	FAeyerjiCombatCuePresentation CriticalHit;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue")
	FAeyerjiCombatCuePresentation Dodged;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue")
	FAeyerjiCombatCuePresentation Staggered;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue")
	FAeyerjiCombatCuePresentation KillingHit;

	const FAeyerjiCombatCuePresentation* FindPresentation(EAeyerjiCombatTextResultType CueType) const;
};

/** Deprecated fallback retained so early profile assets do not break while designers move to DataTables. */
UCLASS(BlueprintType, Const, meta = (DeprecatedNode, DeprecationMessage = "Use a DataTable with FAeyerjiCombatCueProfileRow rows."))
class AEYERJI_API UAeyerjiCombatCueProfile : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue")
	FAeyerjiCombatCueProfileRow Profile;
};

/** Target-side component that plays per-character presentation for generic combat GameplayCues. */
UCLASS(ClassGroup = (Aeyerji), meta = (BlueprintSpawnableComponent))
class AEYERJI_API UAeyerjiCombatCueProfileComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UAeyerjiCombatCueProfileComponent();

	UFUNCTION(BlueprintPure, Category = "Aeyerji|Combat Cue")
	FDataTableRowHandle GetCombatCueProfileRow() const { return CombatCueProfileRow; }

	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Combat Cue")
	void SetCombatCueProfileRow(FDataTableRowHandle NewProfileRow) { CombatCueProfileRow = NewProfileRow; }

	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Combat Cue")
	bool PlayCombatCuePresentation(EAeyerjiCombatTextResultType CueType, const FGameplayCueParameters& Parameters);

private:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Combat Cue", meta = (AllowPrivateAccess = "true", RowType = "/Script/Aeyerji.AeyerjiCombatCueProfileRow"))
	FDataTableRowHandle CombatCueProfileRow;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Combat Cue|Deprecated", meta = (AllowPrivateAccess = "true", DeprecatedProperty, DeprecationMessage = "Use CombatCueProfileRow instead."))
	TObjectPtr<UAeyerjiCombatCueProfile> LegacyCombatCueProfile = nullptr;

	const FAeyerjiCombatCueProfileRow* ResolveCombatCueProfile() const;
	FVector ResolvePresentationLocation(const FAeyerjiCombatCuePresentation& Presentation, const FGameplayCueParameters& Parameters) const;
	FRotator ResolvePresentationRotation(const FAeyerjiCombatCuePresentation& Presentation, const FGameplayCueParameters& Parameters) const;
	bool PlayLocalPlayerFeedback(const FAeyerjiCombatCuePresentation& Presentation, const FVector& Location, const FRotator& Rotation);
};
