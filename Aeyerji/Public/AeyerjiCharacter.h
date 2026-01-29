// Copyright ...

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "AbilitySystemInterface.h"
#include "AbilitySystemComponent.h"
#include "GameplayTagContainer.h"
#include "Attributes/AeyerjiAttributeSet.h"

#include "AeyerjiCharacterMovementComponent.h"
#include "AeyerjiCharacter.generated.h"

struct FTimerHandle;
class UAeyerjiPickupFXComponent;
class UGameplayAbility;


USTRUCT(BlueprintType)
struct AEYERJI_API FAeyerjiDeathStateOptions
{
	GENERATED_BODY();

	/** Detach/destroy attachments so ragdolls are not constrained by gameplay props. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death")
	bool bDetachAttachments = true;

	/** Remove floating status bars / widgets attached to the pawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death")
	bool bRemoveFloatingWidgets = true;

	/** Stop any ongoing regeneration gameplay effects (HP/Mana regen). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death")
	bool bStopRegeneration = true;

	/** Register the corpse in the shared cleanup list on the authority. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death")
	bool bRegisterCorpseForCleanup = true;

	/** After ragdoll settles, disable collision so the body no longer blocks movement. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death")
	bool bDisableRagdollCollision = false;

	/** Delay before disabling ragdoll collision (allows the body to settle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death", meta=(EditCondition="bDisableRagdollCollision", ClampMin="0.0"))
	float RagdollCollisionDisableDelay = 0.35f;

	/** Optional ragdoll impulse. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death")
	FVector Impulse = FVector::ZeroVector;

	/** Optional impulse application world location. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death")
	FVector ImpulseWorldLocation = FVector::ZeroVector;

	/** Optional bone to apply the impulse to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Death")
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
protected:
    /* ------------------ Components ------------------ */

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Aeyerji|GAS")
    TObjectPtr<UAbilitySystemComponent> AbilitySystemAeyerji;

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

	/* ------------------ AActor / ACharacter ------------------ */

	//virtual void PossessedBy(AController* NewController) override; // part of children as it should be
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	UPROPERTY() bool bASCInitialised = false;

	void AddStartupAbilities();
	void BindDeathEvent();

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
	void RemoveFloatingWidgets();
	void StopRegeneration();
	void ScheduleRagdollCollisionDisable(float DelaySeconds);
	void DisableRagdollCollisionNow();
	void RegisterCorpseForCleanup();
	void UnregisterCorpseFromCleanup();

	// Prevent repeated ragdoll/apply logic when multiple systems notify death
	bool bHasAppliedDeathState = false;
	bool bCorpseRegisteredForCleanup = false;

	FTimerHandle RagdollCollisionDisableHandle;

	static TArray<TWeakObjectPtr<AAeyerjiCharacter>> CorpsesPendingCleanup;

	void OnRep_Controller() override;
	
public:
	// --- Corpse management -------------------------------------------------
	static void RemoveInvalidCorpses();

};
//GOOD refactoring from player to aeyerjicharacter removing the double asc was nice.
// now do the same for enemyparentnative and we are 90% there to fixing ai glob slob bs kak
//

