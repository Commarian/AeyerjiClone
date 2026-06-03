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
class UGameplayAbility;


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

	UE_DEPRECATED(5.4, "Use ApplyDeathState with FAeyerjiDeathStateOptions")
	void ApplyDeathStateLegacy(bool bDetachAttachments = true, FVector Impulse = FVector::ZeroVector, FVector ImpulseWorldLocation = FVector::ZeroVector, FName ImpulseBoneName = NAME_None)
	{
		FAeyerjiDeathStateOptions LegacyOptions;
		LegacyOptions.bDetachAttachments = bDetachAttachments;
		LegacyOptions.Impulse = Impulse;
		LegacyOptions.ImpulseWorldLocation = ImpulseWorldLocation;
		LegacyOptions.ImpulseBoneName = ImpulseBoneName;
		ApplyDeathState(LegacyOptions);
	}

	UFUNCTION(BlueprintCallable)
	void DetachDestroyAttachedActors();

	UFUNCTION(BlueprintCallable, Category = "Aeyerji|Death")
	static void GetPendingCorpseCleanup(TArray<AAeyerjiCharacter*>& OutCorpses);

	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnAbilitySystemReady);

	/** Broadcast after InitialiseAbilitySystem() succeeds (server & client) */
	UPROPERTY(EditAnywhere, Category="Aeyerji|GAS")
	FOnAbilitySystemReady OnAbilitySystemReady;
	
	bool IsAbilitySystemReady() const { return bASCInitialised; }

	UFUNCTION(BlueprintPure, Category = "Aeyerji|FX")
	UAeyerjiPickupFXComponent* GetPickupFXComponent() const { return PickupFXComponent; }

	/** Returns true while this pawn has the replicated stunned gameplay tag. */
	UFUNCTION(BlueprintPure, Category = "Aeyerji|Crowd Control")
	bool IsStunned() const;

	/** Cosmetic/UI hook fired when the stun tag count moves between active and inactive. */
	UFUNCTION(BlueprintImplementableEvent, Category = "Aeyerji|Crowd Control")
	void BP_OnStunStateChanged(bool bIsStunned);
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

	/** Created as a sub-object so the ASC owns & replicates it cleanly */

	/* ------------------ Gameplay setup ------------------ */

	/** GE that initialises HP / Mana / etc. (set in child BPs or defaults) */
	//UPROPERTY(EditDefaultsOnly, Category = "Aeyerji|GAS")
	//TSubclassOf<UGameplayEffect> DefaultAttributesGE;

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

	//virtual void PossessedBy(AController* NewController) override; // part of children as it should be
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY() bool bASCInitialised = false;

	void AddStartupAbilities();
	void BindDeathEvent();
	void BindCrowdControlEvents();
	void EnsurePrimaryAttributeSetRegistered();
	void RefreshCollisionCapsuleSize();
	void WarnOnScaledRootCapsule() const;

	/** Native hook fired immediately on the server when HP reaches zero. */
	virtual void OnDeath_Implementation();

	UFUNCTION(NetMulticast, Reliable)
	void MulticastApplyDeathState(FAeyerjiDeathStateOptions Options);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastOnDeath(AActor* Killer, float DamageTaken);

private:
	/* ----- One-time initialisation entry point (server & owning client) ----- */
	//void InitialiseAbilitySystem();

	/* ----- Helpers ----- */
	//void InitAttributes() const; - example is left in code comment but not needed since GAS has built-in functionality
	/* ----- Delegate fired from AttributeSet when HP hits 0 ----- */
	UFUNCTION()
	void HandleOutOfHealth(AActor* Victim, AActor* Killer, float DamageTaken);

	void ApplyDeathStateInternal(const FAeyerjiDeathStateOptions& Options);
	void HandleStunTagChanged(const FGameplayTag CallbackTag, int32 NewCount);
	void ApplyStunState();
	void ClearStunState();
	void RemoveFloatingWidgets();
	void StopRegeneration();
	void StopMovementAndInput();
	void ShutdownControllerLogic();
	void DisableDeathCollision();
	void RegisterCorpseForCleanup();
	void UnregisterCorpseFromCleanup();

	// Prevent repeated death-shutdown logic when multiple systems notify death.
	bool bHasAppliedDeathState = false;
	bool bCorpseRegisteredForCleanup = false;
	bool bStunStateApplied = false;
	bool bStunShouldRestoreMovement = false;
	bool bStunIgnoredControllerInput = false;
	TEnumAsByte<EMovementMode> PreStunMovementMode = MOVE_Walking;
	uint8 PreStunCustomMovementMode = 0;
	FDelegateHandle StunTagChangedHandle;

	static TArray<TWeakObjectPtr<AAeyerjiCharacter>> CorpsesPendingCleanup;

	void OnRep_Controller() override;
	
public:
	// --- Corpse management -------------------------------------------------
	static void RemoveInvalidCorpses();

};
//GOOD refactoring from player to aeyerjicharacter removing the double asc was nice.
// now do the same for enemyparentnative and we are 90% there to fixing ai glob slob bs kak
//

