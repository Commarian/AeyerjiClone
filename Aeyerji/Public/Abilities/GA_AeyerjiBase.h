// GA_AeyerjiBase.h

#pragma once

#include "CoreMinimal.h"

#include "Abilities/GameplayAbility.h"
#include "GameplayTagContainer.h"

#include "GA_AeyerjiBase.generated.h"

class AAeyerjiCharacter;

class UAbilitySystemComponent;
class UAeyerjiAbilityData;

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

  UFUNCTION(BlueprintPure, Category = "Aeyerji|Ability")

  AAeyerjiCharacter *BP_GetAeyerjiCharacter() const;

  UFUNCTION(BlueprintPure, Category = "Aeyerji|Ability")

  UAbilitySystemComponent *BP_GetAeyerjiAbilitySystem() const;

  UFUNCTION(BlueprintCallable, Category = "Aeyerji|Ability",
            meta = (AdvancedDisplay = "bEndAbilityOnFailure"))

  bool BP_TryCommitAbility(bool bEndAbilityOnFailure = true);

  UFUNCTION(BlueprintCallable, Category = "Aeyerji|Ability")

  bool BP_TeleportOwnerSafely(

      FVector DesiredLocation,

      FRotator DesiredRotation,

      FVector &OutFinalLocation,

      float GroundTraceDistance = 0.f,

      float CapsuleInflation = 0.f) const;

protected:
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

  bool TeleportCharacterSafely(

      AAeyerjiCharacter *Character,

      const FVector &DesiredLocation,

      const FRotator &DesiredRotation,

      float GroundTraceDistance,

      float CapsuleInflation,

      FVector &OutFinalLocation) const;

  /* ---------- Ability SetByCaller helpers (optional) ---------- */
  /** Returns mana cost / cooldown seconds from the first UAeyerjiAbilityData* property found on the ability CDO. */
  void EvaluateAbilityCostAndCooldown(const UAbilitySystemComponent* ASC, float& OutManaCost, float& OutCooldown) const;
  /** Attempts to set generic cost/cooldown SetByCaller magnitudes on the outgoing effect spec. */
  void ApplyAbilitySetByCallerToSpec(FGameplayEffectSpecHandle& SpecHandle, float ManaCost, float Cooldown) const;
  /** Adds a damage-type tag to the outgoing effect spec if valid. */
  void ApplyDamageTypeTagToSpec(FGameplayEffectSpecHandle& SpecHandle, const FGameplayTag& DamageTypeTag) const;

  /** Optional default damage-type tag to apply to outgoing damage specs. */
  UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Damage")
  FGameplayTag DefaultDamageTypeTag;

  virtual bool CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;
  virtual void ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const override;
  virtual void ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const override;
};
