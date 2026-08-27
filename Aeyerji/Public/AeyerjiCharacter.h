// Copyright ...

#pragma once

#include "CoreMinimal.h"
PRAGMA_DISABLE_DEPRECATION_WARNINGS
#include "GameFramework/Character.h"
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"
#include "Attributes/AeyerjiAttributeSet.h"

#include "AeyerjiCharacterMovementComponent.h"
#include "AeyerjiCharacter.generated.h"

class UAeyerjiPickupFXComponent;
class UAeyerjiNavSafetyComponent;
class UAeyerjiCombatCueProfileComponent;
class UGameplayAbility;
class UAnimMontage;
class UNiagaraComponent;
class UNiagaraSystem;


USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiDeathStateOptions
{
	GENERATED_BODY();

	/** Detach/destroy attached gameplay actors so the dead pawn stops driving carried props. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death")
	bool bDetachAttachments = true;

	/** Remove floating status bars / widgets attached to the pawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death")
	bool bRemoveFloatingWidgets = true;

	/** Stop any ongoing regeneration gameplay effects (HP/Mana regen). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death")
	bool bStopRegeneration = true;

	/** Register the dead pawn in the shared cleanup list on the authority. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death")
	bool bRegisterCorpseForCleanup = true;

	/** Stop character movement so the dead pawn can no longer navigate under gameplay control. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death")
	bool bDisableMovement = true;

	/** Stop controller-side AI logic such as StateTree, focus, and perception ticking. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death")
	bool bDisableControllerLogic = true;

	/** Disable pawn collision so the dead actor no longer blocks movement or targeting. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death")
	bool bDisableCollision = true;

	/** Deprecated: native death state no longer drives ragdoll collision. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death", meta=(DeprecatedProperty, DeprecationMessage="Native death state no longer drives ragdoll collision."))
	bool bDisableRagdollCollision = false;

	/** Deprecated: native death state no longer drives ragdoll collision. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death", meta=(DeprecatedProperty, DeprecationMessage="Native death state no longer drives ragdoll collision.", EditCondition="bDisableRagdollCollision", ClampMin="0.0"))
	float RagdollCollisionDisableDelay = 0.35f;

	/** Deprecated: native death state no longer applies ragdoll impulses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death", meta=(DeprecatedProperty, DeprecationMessage="Native death state no longer applies ragdoll impulses."))
	FVector Impulse = FVector::ZeroVector;

	/** Deprecated: native death state no longer applies ragdoll impulses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death", meta=(DeprecatedProperty, DeprecationMessage="Native death state no longer applies ragdoll impulses."))
	FVector ImpulseWorldLocation = FVector::ZeroVector;

	/** Deprecated: native death state no longer applies ragdoll impulses. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death", meta=(DeprecatedProperty, DeprecationMessage="Native death state no longer applies ragdoll impulses."))
	FName ImpulseBoneName = NAME_None;
};


/**
 *  Native GAS-ready character every pawn in Aeyerji should derive from.
 *  * Implements IAbilitySystemInterface.
 *  * Owns an ASC (Actor-side flavour).
 *  * Creates + caches the main AttributeSet.
 *  * Applies default attributes & abilities at spawn.
 *  * Hooks death delegate so GA_Death can fire.
 */
UCLASS()
class AEYERJI_API AAeyerjiCharacter
	: public ACharacter
	, public IAbilitySystemInterface
{
	GENERATED_BODY()

public:
	AAeyerjiCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

	/** IAbilitySystemInterface */
	virtual UAbilitySystemComponent* GetAbilitySystemComponent() const override
	{
		return AbilitySystemAeyerji;
	}

	/** Public accessor to the base attribute set (HP / Mana / ?) */
	UFUNCTION(BlueprintPure)
	const UAeyerjiAttributeSet* GetAttrSet() const
	{
		return AbilitySystemAeyerji ? AbilitySystemAeyerji->GetSet<UAeyerjiAttributeSet>() : nullptr;
	}

	/** Blueprint-implementable event for death (handy for VFX, UI, etc.) */
	UFUNCTION(BlueprintImplementableEvent, Category = "Aeyerji|Events")
	void BP_OnDeath(AActor* Killer, float DamageTaken);

	/** Applies the non-visual death shutdown for this pawn. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Death", meta = (AutoCreateRefTerm = "Options"))
	void ApplyDeathState(FAeyerjiDeathStateOptions Options = FAeyerjiDeathStateOptions());

	/** Restores native death state, collision, movement, and death guards before a pooled pawn is reused. */
	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Death")
	void ResetDeathStateForReuse();

	UFUNCTION(BlueprintCallable)
	void DetachDestroyAttachedActors();

	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Death")
	static void GetPendingCorpseCleanup(TArray<AAeyerjiCharacter*>& OutCorpses);

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAbilitySystemReady);

	/** Broadcast after InitialiseAbilitySystem() succeeds (server & client) */
	UPROPERTY(BlueprintAssignable, Category="Aeyerji|GAS")
	FOnAbilitySystemReady OnAbilitySystemReady;
	
	bool IsAbilitySystemReady() const { return bASCInitialised; }

	UFUNCTION(BlueprintPure, Category = "Aeyerji|FX")
	UAeyerjiPickupFXComponent* GetPickupFXComponent() const { return PickupFXComponent; }

	/** Returns the shared nav safety component that tracks and recovers live pawns from off-nav locations. */
	UFUNCTION(BlueprintPure, Category = "Aeyerji|Navigation")
	UAeyerjiNavSafetyComponent* GetNavSafetyComponent() const { return NavSafetyComponent; }

	UFUNCTION(BlueprintPure, Category = "Aeyerji|Combat Cue")
	UAeyerjiCombatCueProfileComponent* GetCombatCueProfileComponent() const { return CombatCueProfileComponent; }

	/** Plays one-shot ability cosmetics on every client; authority should call this after validation/commit. */
	UFUNCTION(NetMulticast, Unreliable)
	void Multicast_PlayAbilityCosmetics(
		UAnimMontage* Montage,
		float MontagePlayRate,
		UNiagaraSystem* NiagaraSystem,
		FVector NiagaraWorldLocation,
		FRotator NiagaraWorldRotation,
		FVector NiagaraScale,
		bool bAttachNiagaraToMesh,
		FName NiagaraAttachSocket,
		FVector NiagaraLocalOffset);

	/** Loads and plays a cast montage locally on each client from a soft path supplied by the authoritative ability. */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayAbilityMontageByPath(FSoftObjectPath MontagePath, float MontagePlayRate);

	/** Returns true while this pawn has the replicated stunned gameplay tag. */
	UFUNCTION(BlueprintPure, Category = "Aeyerji|Crowd Control")
	bool IsStunned() const;

	/** Returns true while this pawn has the replicated staggered gameplay tag. */
	UFUNCTION(BlueprintPure, Category = "Aeyerji|Crowd Control")
	bool IsStaggered() const;

	/** Returns true while any hard or soft crowd-control gameplay tag is active. */
	UFUNCTION(BlueprintPure, Category = "Aeyerji|Crowd Control")
	bool IsCrowdControlled() const;

	/** Cosmetic/UI hook fired when the stun tag count moves between active and inactive. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Aeyerji|Crowd Control")
	void BP_OnStunStateChanged(bool bIsStunned);

	/** Applies a transient archetype-owned root-capsule size without overwriting legacy Blueprint defaults. */
	void SetArchetypeCollisionCapsuleSize(float CapsuleRadius, float CapsuleHalfHeight);

	/** Removes the transient archetype capsule override and restores the authored Blueprint/component capsule size. */
	void ClearArchetypeCollisionCapsuleSize();
protected:
    /* ------------------ Components ------------------ */

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aeyerji|GAS")
    TObjectPtr<UAbilitySystemComponent> AbilitySystemAeyerji;

    /** Default attribute set subobject used by GAS replication and baseline stats. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aeyerji|GAS")
    TObjectPtr<UAeyerjiAttributeSet> AttributeSetAeyerji;

    /** Derives secondary stats from primaries and applies passive GE */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aeyerji|GAS")
	class UAeyerjiStatEngineComponent* StatEngine;

	/** Plays loot pickup FX directly on the character. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aeyerji|FX")
	TObjectPtr<UAeyerjiPickupFXComponent> PickupFXComponent;

	/** Keeps players and enemies recoverable if gameplay moves them off the nav mesh. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aeyerji|Navigation")
	TObjectPtr<UAeyerjiNavSafetyComponent> NavSafetyComponent;

	/** Designer-configurable per-target presentation profile for generic combat GameplayCues. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aeyerji|Combat Cue")
	TObjectPtr<UAeyerjiCombatCueProfileComponent> CombatCueProfileComponent;

	/** Looping overhead Niagara used while the character is stunned. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Crowd Control|Stun")
	TSoftObjectPtr<UNiagaraSystem> StunOverheadEffect;

	/** Preferred mesh socket for the stun overhead effect; falls back to the actor root if missing. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Crowd Control|Stun")
	FName StunOverheadSocketName = TEXT("head");

	/** Local offset applied after attaching the stun overhead effect. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|Crowd Control|Stun")
	FVector StunOverheadOffset = FVector(0.f, 0.f, 100.f);

	/* ------------------ Gameplay setup ------------------ */

	/** List of abilities every instance of this class should start with like DEATH*/
	UPROPERTY(EditDefaultsOnly, Category = "Aeyerji|GAS")
	TArray<TSubclassOf<UGameplayAbility>> DefaultAbilities;

	/** Passive death ability (can be overridden per-BP, e.g. to use BP_GA_Death). */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Aeyerji|GAS")
	TSubclassOf<UGameplayAbility> DeathAbilityClass;

	/** Level used by attribute curves (override per-pawn if you want) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Aeyerji|GAS")
	int32 CharacterLevel = 1;

	/** Optional authored capsule radius in cm; 0 preserves the Blueprint/component default size. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Collision", meta = (ClampMin = "0.0", Units = "cm"))
	float CollisionCapsuleRadius = 0.f;

	/** Optional authored capsule half-height in cm; 0 preserves the Blueprint/component default size. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aeyerji|Collision", meta = (ClampMin = "0.0", Units = "cm"))
	float CollisionCapsuleHalfHeight = 0.f;

	/* ------------------ AActor / ACharacter ------------------ */

	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY() bool bASCInitialised = false;

	void AddStartupAbilities();
	void BindDeathEvent();
	void BindCrowdControlEvents();
	void BindRuntimeAttributeHooks();
	void UnbindRuntimeAttributeHooks();
	void EnsurePrimaryAttributeSetRegistered();
	void CaptureBaseCollisionCapsuleSize();
	void RefreshCollisionCapsuleSize();
	void WarnOnScaledRootCapsule() const;

	/** Original Blueprint/component capsule size captured before any construction-time archetype override. */
	float BaseCollisionCapsuleRadius = 0.f;
	float BaseCollisionCapsuleHalfHeight = 0.f;
	bool bHasCapturedBaseCollisionCapsuleSize = false;

	/** Transient archetype values take priority over legacy per-Blueprint collision fields when positive. */
	float ArchetypeCollisionCapsuleRadius = 0.f;
	float ArchetypeCollisionCapsuleHalfHeight = 0.f;

	/** Native hook fired immediately when HP reaches zero, with the authoritative damage attribution. */
	virtual void OnDeath_Implementation(AActor* Killer, float DamageTaken);

	/**
	 * Gives derived characters one authoritative pre-presentation step before BP_OnDeath runs.
	 * Return true and fill OutFacingRotation when clients must apply an exact facing before
	 * spawning death presentation from the pawn transform.
	 */
	virtual bool PrepareDeathPresentation(AActor* Killer, FRotator& OutFacingRotation);

	/** Lets derived classes adjust native death shutdown, e.g. pooled enemies avoiding corpse cleanup. */
	virtual FAeyerjiDeathStateOptions BuildDeathStateOptionsForOutOfHealth() const;

	UFUNCTION(NetMulticast, Reliable)
	void MulticastApplyDeathState(FAeyerjiDeathStateOptions Options);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastResetDeathStateForReuse();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastOnDeath(
		AActor* Killer,
		float DamageTaken,
		bool bApplyFacingRotation,
		FRotator FacingRotation);

private:
	/* ----- Delegate fired from AttributeSet when HP hits 0 ----- */
	UFUNCTION()
	void HandleOutOfHealth(AActor* Victim, AActor* Killer, float DamageTaken);

	void ApplyDeathStateInternal(const FAeyerjiDeathStateOptions& Options);
	void ResetDeathStateForReuseInternal();
	void CancelActiveAbilitiesForDeath();
	void HandleStunTagChanged(const FGameplayTag CallbackTag, int32 NewCount);
	void HandleStaggerTagChanged(const FGameplayTag CallbackTag, int32 NewCount);
	void HandleRunSpeedChanged(const FOnAttributeChangeData& Data);
	void ApplyRunSpeedFromAttribute();
	void ApplyStunState();
	void ClearStunState();
	void ApplyStaggerState();
	void ClearStaggerState();
	void ClearCrowdControlStateForDeath();
	void CancelActiveAbilitiesForCrowdControl();
	void StopAIForCrowdControl();
	void SendCurrentCrowdControlStateTreeEvent();
	void SendAICrowdControlStateTreeEvent(const FGameplayTag& EventTag);
	void SpawnStunOverheadEffect();
	void DestroyStunOverheadEffect();
	/** Hides retained floating widgets on death and restores them after pooled reuse. */
	void SetFloatingWidgetsPresentationVisible(bool bVisible);
	void RemoveFloatingWidgets();
	void StopRegeneration();
	void StopMovementAndInput();
	void ShutdownControllerLogic();
	void DisableDeathCollision();
	void EnableLivingCollision();
	void RestartControllerLogicForReuse();
	void RegisterCorpseForCleanup();
	void UnregisterCorpseFromCleanup();

	// Prevent repeated death-shutdown logic when multiple systems notify death.
	bool bHasAppliedDeathState = false;
	bool bCorpseRegisteredForCleanup = false;
	bool bStunStateApplied = false;
	bool bStaggerStateApplied = false;
	bool bStunShouldRestoreMovement = false;
	bool bStunIgnoredControllerInput = false;
	bool bPreStunUseControllerRotationYaw = false;
	bool bPreStunOrientRotationToMovement = false;
	bool bPreStunUseControllerDesiredRotation = false;
	bool bWarnedStunOverheadEffectLoadFailure = false;
	TEnumAsByte<EMovementMode> PreStunMovementMode = MOVE_Walking;
	uint8 PreStunCustomMovementMode = 0;
	FDelegateHandle StunTagChangedHandle;
	FDelegateHandle StaggerTagChangedHandle;
	FDelegateHandle RunSpeedChangedHandle;
	TWeakObjectPtr<UAbilitySystemComponent> RuntimeAttributeHookASC;

	UPROPERTY(Transient)
	TObjectPtr<UNiagaraComponent> ActiveStunOverheadEffect = nullptr;

	static TArray<TWeakObjectPtr<AAeyerjiCharacter>> CorpsesPendingCleanup;

	
public:
	// --- Corpse management -------------------------------------------------
	static void RemoveInvalidCorpses();

};
