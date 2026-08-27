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

	/** Optional spatial sound played for this result. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue")
	TObjectPtr<USoundBase> Sound = nullptr;

	/** Optional Niagara effect played for this result. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue")
	TObjectPtr<UNiagaraSystem> Effect = nullptr;

	/** Uses the GameplayCue location when it is finite and non-zero; otherwise uses the owning actor. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue")
	bool bUseCueLocation = true;

	/** Attaches the Niagara effect to the target mesh or scene root instead of spawning it in world space. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue")
	bool bAttachEffectToTarget = false;

	/** Target mesh socket used for attached effects and as the fallback world location. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue")
	FName AttachSocket = NAME_None;

	/** Relative offset for attached effects, or world-space offset for detached presentation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue")
	FVector LocationOffset = FVector::ZeroVector;

	/** Rotation offset added to the cue impact normal when one is supplied. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue")
	FRotator RotationOffset = FRotator::ZeroRotator;

	/** World-space Niagara scale for effects that are not attached. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue")
	FVector EffectScale = FVector(1.f);

	/** Sound volume multiplier; runtime presentation bounds invalid or excessive values. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue", meta = (ClampMin = "0.0"))
	float VolumeMultiplier = 1.f;

	/** Inclusive random pitch range applied independently to each sound play. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue", meta = (ClampMin = "0.01"))
	FVector2D PitchRange = FVector2D(0.95f, 1.05f);

	/** Optional local camera shake class. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue|Camera")
	TSubclassOf<UCameraShakeBase> CameraShake;

	/** Scale used when starting a camera-local shake. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue|Camera", meta = (ClampMin = "0.0"))
	float CameraShakeScale = 1.f;

	/** Coordinate space used for non-world camera shakes. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue|Camera")
	ECameraShakePlaySpace CameraShakePlaySpace = ECameraShakePlaySpace::CameraLocal;

	/** Plays an attenuated world shake rather than starting it directly on the local camera manager. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue|Camera")
	bool bPlayCameraShakeInWorld = false;

	/** Radius receiving the full world-shake intensity. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue|Camera", meta = (EditCondition = "bPlayCameraShakeInWorld", ClampMin = "0.0"))
	float WorldShakeInnerRadius = 0.f;

	/** Maximum radius at which the world shake is audible to a local camera. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue|Camera", meta = (EditCondition = "bPlayCameraShakeInWorld", ClampMin = "0.0"))
	float WorldShakeOuterRadius = 1500.f;

	/** Falloff exponent between the inner and outer world-shake radii. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue|Camera", meta = (EditCondition = "bPlayCameraShakeInWorld", ClampMin = "0.0"))
	float WorldShakeFalloff = 1.f;

	/** Optional camera-lens effect added to the local player camera manager. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue|Camera", meta = (MustImplement = "/Script/Engine.CameraLensEffectInterface"))
	TSubclassOf<AActor> CameraLensEffect;

	/** Optional spatial force-feedback asset for the local player. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue|Feedback")
	TObjectPtr<UForceFeedbackEffect> ForceFeedback = nullptr;

	/** Force-feedback intensity multiplier. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue|Feedback", meta = (ClampMin = "0.0"))
	float ForceFeedbackIntensity = 1.f;

	/** Loops the spatial force-feedback effect until its spawned component stops it. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue|Feedback")
	bool bLoopForceFeedback = false;

	/** Reserved authoring intent for controller-driven feedback paths that support time dilation. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue|Feedback")
	bool bIgnoreForceFeedbackTimeDilation = false;

	/** Reserved authoring intent for controller-driven feedback paths that support pause playback. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue|Feedback")
	bool bPlayForceFeedbackWhilePaused = false;

	bool HasPresentation() const { return Sound || Effect || CameraShake || CameraLensEffect || ForceFeedback; }
};

/** DataTable row assigned to a target to customize how generic combat GameplayCues present on that target. */
USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiCombatCueProfileRow : public FTableRowBase
{
	GENERATED_BODY()

	/** Presentation used for ordinary physical damage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue")
	FAeyerjiCombatCuePresentation PhysicalHit;

	/** Presentation used for critical damage. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue")
	FAeyerjiCombatCuePresentation CriticalHit;

	/** Presentation used when an attack is dodged. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue")
	FAeyerjiCombatCuePresentation Dodged;

	/** Presentation used when the target becomes staggered. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Combat Cue")
	FAeyerjiCombatCuePresentation Staggered;

	/** Presentation used when the hit kills its target. */
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
	/** Legacy inline profile retained only while old assets migrate to DataTable rows. */
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
	/** DataTable row containing this target's five combat-result presentations. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Combat Cue", meta = (AllowPrivateAccess = "true", RowType = "/Script/Aeyerji.AeyerjiCombatCueProfileRow"))
	FDataTableRowHandle CombatCueProfileRow;

	/** Deprecated asset fallback; assign CombatCueProfileRow for all new targets. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Combat Cue|Deprecated", meta = (AllowPrivateAccess = "true", DeprecatedProperty, DeprecationMessage = "Use CombatCueProfileRow instead."))
	TObjectPtr<UAeyerjiCombatCueProfile> LegacyCombatCueProfile = nullptr;

	const FAeyerjiCombatCueProfileRow* ResolveCombatCueProfile() const;
	FVector ResolvePresentationLocation(const FAeyerjiCombatCuePresentation& Presentation, const FGameplayCueParameters& Parameters) const;
	FRotator ResolvePresentationRotation(const FAeyerjiCombatCuePresentation& Presentation, const FGameplayCueParameters& Parameters) const;
	bool PlayLocalPlayerFeedback(const FAeyerjiCombatCuePresentation& Presentation, const FVector& Location, const FRotator& Rotation);
};
