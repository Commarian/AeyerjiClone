// GA_AeyerjiBase.h

#pragma once

#include "CoreMinimal.h"

#include "Abilities/GameplayAbility.h"
#include "Abilities/AeyerjiAbilityTuning.h"
#include "GAS/AeyerjiDamageRules.h"
#include "GameplayTagContainer.h"
#include "TimerManager.h"

#include "GA_AeyerjiBase.generated.h"

class AAeyerjiCharacter;

class UAbilitySystemComponent;

#if WITH_DEV_AUTOMATION_TESTS
class FAeyerjiAbilityCooldownReductionTest;
#endif

/* Aeyerji's top level ability class for all

 * gameplay ability systems. Always inherit from this for common

 * functionality to work across the board

 */
UCLASS(Abstract)

class AEYERJI_API UGA_AeyerjiBase : public UGameplayAbility

{

  GENERATED_BODY()

public:
  UGA_AeyerjiBase();

  /** Explicit row key in the global ability tuning table. Falls back to the most specific Ability.* asset tag. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Aeyerji|Ability")
  FGameplayTag AbilityTag;

  UFUNCTION(BlueprintPure, Category = "Aeyerji|Ability")

  AAeyerjiCharacter *BP_GetAeyerjiCharacter() const;

  UFUNCTION(BlueprintPure, Category = "Aeyerji|Ability")

  UAbilitySystemComponent *BP_GetAeyerjiAbilitySystem() const;

  UFUNCTION(BlueprintCallable, Category = "Aeyerji|Ability",
            meta = (AdvancedDisplay = "bEndAbilityOnFailure"))

  bool BP_TryCommitAbility(bool bEndAbilityOnFailure = true);

  /** Returns this ability class's default outgoing physical damage rules. */
  UFUNCTION(BlueprintPure, Category="Aeyerji|Ability|Damage")
  const FAeyerjiDamageRuleConfig& GetDefaultDamageRules() const { return DefaultDamageRules; }

  UFUNCTION(BlueprintCallable, Category = "Aeyerji|Ability")

  bool BP_TeleportOwnerSafely(

      FVector DesiredLocation,

      FRotator DesiredRotation,

      FVector &OutFinalLocation,

      float GroundTraceDistance = 0.f,

      float CapsuleInflation = 0.f) const;

protected:
  /**
   * Delays legacy Blueprint-only activation until the configured cast impact time.
   * Native subclasses that override ActivateAbility keep ownership of their own impact flow.
   */
  virtual void ActivateAbility(const FGameplayAbilitySpecHandle Handle,
                               const FGameplayAbilityActorInfo* ActorInfo,
                               const FGameplayAbilityActivationInfo ActivationInfo,
                               const FGameplayEventData* TriggerEventData) override;

  /** Clears any timer and replicated cast lock owned by this ability instance. */
  virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
                          const FGameplayAbilityActorInfo* ActorInfo,
                          const FGameplayAbilityActivationInfo ActivationInfo,
                          bool bReplicateEndAbility,
                          bool bWasCancelled) override;

  /** Rejects a second action-bar ability while another server-authoritative cast is winding up. */
  virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
                                  const FGameplayAbilityActorInfo* ActorInfo,
                                  const FGameplayTagContainer* SourceTags = nullptr,
                                  const FGameplayTagContainer* TargetTags = nullptr,
                                  FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

  // Common helper: true if owner has State.Dead

  bool IsOwnerDead(const FGameplayAbilityActorInfo *ActorInfo) const;

  AAeyerjiCharacter *
  GetAeyerjiCharacter(const FGameplayAbilityActorInfo *ActorInfo) const;

  UAbilitySystemComponent *
  GetAeyerjiAbilitySystem(const FGameplayAbilityActorInfo *ActorInfo) const;

  bool TryCommitAbilityInternal(

      const FGameplayAbilitySpecHandle &Handle,

      const FGameplayAbilityActorInfo *ActorInfo,

      const FGameplayAbilityActivationInfo &ActivationInfo,

      bool bEndAbilityOnFailure);

  /** Resolves an explicit impact delay or defaults to the midpoint of the configured montage. */
  float CalculateAbilityImpactDelay(const FAeyerjiAbilityResolvedConfig& Config) const;

  /** Plays the configured montage and owns the replicated movement/primary-attack cast lock until EndAbility. */
  void BeginAbilityCastPresentation(const FGameplayAbilityActorInfo& ActorInfo,
                                    const FAeyerjiAbilityResolvedConfig& Config,
                                    float ImpactDelaySeconds);

  bool TeleportCharacterSafely(

      AAeyerjiCharacter *Character,

      const FVector &DesiredLocation,

      const FRotator &DesiredRotation,

      float GroundTraceDistance,

      float CapsuleInflation,

      FVector &OutFinalLocation) const;

  /* ---------- Ability SetByCaller helpers (optional) ---------- */
  /** Returns the base mana and cooldown values configured on this ability. */
  void EvaluateAbilityCostAndCooldown(const UAbilitySystemComponent* ASC, float& OutManaCost, float& OutCooldown) const;
  /** Returns the rank-resolved mana and cooldown values configured on this ability. */
  void EvaluateAbilityCostAndCooldown(const UAbilitySystemComponent* ASC, int32 AbilityRank, float& OutManaCost, float& OutCooldown) const;
  /** Applies the game's cooldown-reduction cap to a base duration. */
  static float ResolveCooldownWithReduction(float BaseCooldown, float CooldownReduction);
  /** Finds the explicit or most-specific Ability.* asset tag used as the table row key. */
  FGameplayTag ResolveAbilityTag() const;
  /** Resolves the granted spec level used as the active ability rank. */
  int32 ResolveAbilityRank(const UAbilitySystemComponent* ASC) const;
  int32 ResolveAbilityRank(const FGameplayAbilitySpecHandle& Handle, const FGameplayAbilityActorInfo* ActorInfo) const;
  /** Resolves the merged ability config for the supplied rank. */
  bool GetAbilityResolvedConfig(const UAbilitySystemComponent* ASC, int32 AbilityRank, FAeyerjiAbilityResolvedConfig& OutConfig) const;
  /** Attempts to set generic cost/cooldown SetByCaller magnitudes on the outgoing effect spec. */
  void ApplyAbilitySetByCallerToSpec(FGameplayEffectSpecHandle& SpecHandle, float InManaCost, float InCooldown) const;
  /** Adds this ability's resolved cooldown tags to an outgoing cooldown effect spec. */
  void ApplyResolvedCooldownTagsToSpec(FGameplayEffectSpecHandle& SpecHandle) const;
  /** Adds a damage-type tag to the outgoing effect spec if valid. */
  void ApplyDamageTypeTagToSpec(FGameplayEffectSpecHandle& SpecHandle, const FGameplayTag& DamageTypeTag) const;

  /** Adds this ability's explicit damage rules and overrides to an outgoing spec. */
  void ApplyDefaultDamageRulesToSpec(FGameplayEffectSpecHandle& SpecHandle) const;

  /** Optional default damage-type tag to apply to outgoing damage specs. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Damage")
  FGameplayTag DefaultDamageTypeTag;

  /** Optional mechanics enabled for damage specs created by this ability. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Damage")
  FAeyerjiDamageRuleConfig DefaultDamageRules;

  virtual bool CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
  virtual bool CheckCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
  virtual void ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const override;
  virtual void ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const override;
  virtual const FGameplayTagContainer* GetCooldownTags() const override;

private:
  void ContinueDeferredBlueprintActivation(FGameplayAbilitySpecHandle Handle,
                                           const FGameplayAbilityActorInfo* ActorInfo,
                                           FGameplayAbilityActivationInfo ActivationInfo,
                                           FGameplayEventData TriggerEventData,
                                           bool bHasTriggerEventData);
  void RemoveOwnedAbilityCastLock(const FGameplayAbilityActorInfo* ActorInfo);

#if WITH_DEV_AUTOMATION_TESTS
  friend class FAeyerjiAbilityCooldownReductionTest;
#endif

  mutable FGameplayTagContainer RuntimeCooldownTags;
  FTimerHandle DeferredBlueprintActivationTimerHandle;
  bool bOwnsAbilityCastLock = false;
};
