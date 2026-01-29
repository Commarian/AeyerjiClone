#include "Abilities/GA_AeyerjiBase.h"

#include "AbilitySystemComponent.h"

#include "AeyerjiCharacter.h"

#include "Attributes/AeyerjiAttributeSet.h"
#include "AeyerjiGameplayTags.h"

#include "Components/CapsuleComponent.h"

#include "Engine/World.h"

#include "GameFramework/CharacterMovementComponent.h"

#include "GameplayTagContainer.h"
#include "Abilities/AeyerjiAbilityData.h"

UGA_AeyerjiBase::UGA_AeyerjiBase()

{

  // All abilities are blocked while Dead.

  ActivationBlockedTags.AddTag(AeyerjiTags::State_Dead);

  // Sensible defaults for ARPG skills; tweak as needed.

  InstancingPolicy = EGameplayAbilityInstancingPolicy::InstancedPerActor;
  ReplicationPolicy = EGameplayAbilityReplicationPolicy::ReplicateYes;
  NetExecutionPolicy = EGameplayAbilityNetExecutionPolicy::ServerInitiated;
  NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ServerOnlyExecution;
  bServerRespectsRemoteAbilityCancellation = true;
}

AAeyerjiCharacter *UGA_AeyerjiBase::GetAeyerjiCharacter(
    const FGameplayAbilityActorInfo *ActorInfo) const

{

  return ActorInfo ? Cast<AAeyerjiCharacter>(ActorInfo->AvatarActor.Get())
                   : nullptr;
}

UAbilitySystemComponent *UGA_AeyerjiBase::GetAeyerjiAbilitySystem(
    const FGameplayAbilityActorInfo *ActorInfo) const

{

  return ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
}

AAeyerjiCharacter *UGA_AeyerjiBase::BP_GetAeyerjiCharacter() const

{

  return GetAeyerjiCharacter(GetCurrentActorInfo());
}

UAbilitySystemComponent *UGA_AeyerjiBase::BP_GetAeyerjiAbilitySystem() const

{

  return GetAeyerjiAbilitySystem(GetCurrentActorInfo());
}

bool UGA_AeyerjiBase::TryCommitAbilityInternal(
    const FGameplayAbilitySpecHandle &Handle,

    const FGameplayAbilityActorInfo *ActorInfo,

    const FGameplayAbilityActivationInfo &ActivationInfo,

    bool bEndAbilityOnFailure)

{

  if (!Handle.IsValid() || !ActorInfo)

  {

    return false;
  }

  if (CommitAbility(Handle, ActorInfo, ActivationInfo))

  {

    return true;
  }

  if (bEndAbilityOnFailure)

  {

    EndAbility(Handle, ActorInfo, ActivationInfo, /*bReplicateEnd*/ true,
               /*bWasCancelled*/ true);
  }

  return false;
}

void UGA_AeyerjiBase::EvaluateAbilityCostAndCooldown(const UAbilitySystemComponent* ASC, float& OutManaCost, float& OutCooldown) const
{
  OutManaCost = 0.f;
  OutCooldown = 0.f;

  const UClass* AbilityClass = GetClass();

  // Locate the first UAeyerjiAbilityData* property on this ability (e.g., BlinkConfig).
  const FObjectProperty* DataProp = nullptr;
  for (TFieldIterator<FObjectProperty> It(AbilityClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
  {
    if (It->PropertyClass && It->PropertyClass->IsChildOf(UAeyerjiAbilityData::StaticClass()))
    {
      DataProp = *It;
      break;
    }
  }

  if (!DataProp)
  {
    return;
  }

  const UAeyerjiAbilityData* Config = Cast<UAeyerjiAbilityData>(DataProp->GetObjectPropertyValue_InContainer(this));
  if (!Config)
  {
    return;
  }

  const FAeyerjiAbilityCost AbilityCost = Config->EvaluateCost(ASC);
  OutManaCost = AbilityCost.ManaCost;
  OutCooldown = AbilityCost.Cooldown;
}

void UGA_AeyerjiBase::ApplyAbilitySetByCallerToSpec(FGameplayEffectSpecHandle& SpecHandle, float ManaCost, float Cooldown) const
{
  if (!SpecHandle.IsValid() || !SpecHandle.Data.IsValid())
  {
    return;
  }

  FGameplayEffectSpec* Spec = SpecHandle.Data.Get();

  // Only set values if the GE expects them.
  const FGameplayTag ManaTag = FGameplayTag::RequestGameplayTag(TEXT("SetByCaller.Cost.Mana"));
  const FGameplayTag CooldownTag = FGameplayTag::RequestGameplayTag(TEXT("SetByCaller.Cooldown.Seconds"));

  if (ManaTag.IsValid())
  {
    Spec->SetSetByCallerMagnitude(ManaTag, ManaCost);
  }

  if (CooldownTag.IsValid())
  {
    Spec->SetSetByCallerMagnitude(CooldownTag, Cooldown);
  }
}

void UGA_AeyerjiBase::ApplyDamageTypeTagToSpec(FGameplayEffectSpecHandle& SpecHandle, const FGameplayTag& DamageTypeTag) const
{
  if (!DamageTypeTag.IsValid() || !SpecHandle.IsValid() || !SpecHandle.Data.IsValid())
  {
    return;
  }

  SpecHandle.Data->AddDynamicAssetTag(DamageTypeTag);
}

bool UGA_AeyerjiBase::CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, FGameplayTagContainer* OptionalRelevantTags) const
{
  if (!ActorInfo || !ActorInfo->AbilitySystemComponent.IsValid())
  {
    return false;
  }

  UAbilitySystemComponent* ASC = ActorInfo->AbilitySystemComponent.Get();

  float ManaCost = 0.f;
  float CooldownSeconds = 0.f;
  EvaluateAbilityCostAndCooldown(ASC, ManaCost, CooldownSeconds);

  // Simple mana gate to avoid SetByCaller warnings from GE-based checks.
  if (ManaCost > 0.f)
  {
    const FGameplayAttribute ManaAttr = UAeyerjiAttributeSet::GetManaAttribute();
    if (ASC->HasAttributeSetForAttribute(ManaAttr))
    {
      const float CurrentMana = ASC->GetNumericAttribute(ManaAttr);
      if (CurrentMana + KINDA_SMALL_NUMBER < ManaCost)
      {
        return false;
      }
    }
  }

  // If a cost GE exists, skip Super::CheckCost to avoid evaluating it without SetByCaller.
  if (GetCostGameplayEffect() != nullptr)
  {
    return true;
  }

  return Super::CheckCost(Handle, ActorInfo, OptionalRelevantTags);
}

void UGA_AeyerjiBase::ApplyCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo) const
{
  if (!ActorInfo || !ActorInfo->AbilitySystemComponent.IsValid())
  {
    return;
  }

  float ManaCost = 0.f;
  float CooldownSeconds = 0.f;
  EvaluateAbilityCostAndCooldown(ActorInfo->AbilitySystemComponent.Get(), ManaCost, CooldownSeconds);

  if (const UGameplayEffect* CostGE = GetCostGameplayEffect())
  {
    FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(CostGE->GetClass(), GetAbilityLevel(Handle, ActorInfo));
    ApplyAbilitySetByCallerToSpec(SpecHandle, ManaCost, CooldownSeconds);

    if (SpecHandle.IsValid())
    {
      ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, SpecHandle);
    }
  }
}

void UGA_AeyerjiBase::ApplyCooldown(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo, const      FGameplayAbilityActivationInfo ActivationInfo) const
{
  if (!ActorInfo || !ActorInfo->AbilitySystemComponent.IsValid())
  {
    return;
  }

  float ManaCost = 0.f;
  float CooldownSeconds = 0.f;
  EvaluateAbilityCostAndCooldown(ActorInfo->AbilitySystemComponent.Get(), ManaCost, CooldownSeconds);

  // Default path mirrors UGameplayAbility::ApplyCooldown but adds SetByCaller.
  if (const UGameplayEffect* CooldownGE = GetCooldownGameplayEffect())
  {
    FGameplayEffectSpecHandle SpecHandle = MakeOutgoingGameplayEffectSpec(CooldownGE->GetClass(), GetAbilityLevel(Handle, ActorInfo));
    ApplyAbilitySetByCallerToSpec(SpecHandle, ManaCost, CooldownSeconds);

    if (SpecHandle.IsValid())
    {
      ApplyGameplayEffectSpecToOwner(Handle, ActorInfo, ActivationInfo, SpecHandle);
    }
  }
}

bool UGA_AeyerjiBase::BP_TryCommitAbility(bool bEndAbilityOnFailure)

{

  return TryCommitAbilityInternal(

      GetCurrentAbilitySpecHandle(),

      GetCurrentActorInfo(),

      GetCurrentActivationInfo(),

      bEndAbilityOnFailure);
}

bool UGA_AeyerjiBase::TeleportCharacterSafely(

    AAeyerjiCharacter *Character,

    const FVector &DesiredLocation,

    const FRotator &DesiredRotation,

    float GroundTraceDistance,

    float CapsuleInflation,

    FVector &OutFinalLocation) const

{

  if (!Character)

  {

    OutFinalLocation = DesiredLocation;

    return false;
  }

  UCapsuleComponent *Capsule = Character->GetCapsuleComponent();

  const float HalfHeight =
      Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 0.f;

  const float Radius = Capsule ? Capsule->GetScaledCapsuleRadius() : 0.f;

  FVector TargetLocation = DesiredLocation;

  if (GroundTraceDistance > 0.f)

  {

    if (UWorld *World = Character->GetWorld())

    {

      const FVector TraceStart =
          DesiredLocation + FVector::UpVector * HalfHeight;

      const FVector TraceEnd =
          TraceStart - FVector::UpVector * (GroundTraceDistance + HalfHeight);

      FHitResult Hit;

      FCollisionQueryParams Params(SCENE_QUERY_STAT(AeyerjiAbilityGroundTrace),
                                   false, Character);

      const float CollisionRadius = Radius + FMath::Max(0.f, CapsuleInflation);

      const bool bHit = World->SweepSingleByChannel(

          Hit,

          TraceStart,

          TraceEnd,

          FQuat::Identity,

          ECC_Visibility,

          FCollisionShape::MakeSphere(CollisionRadius),

          Params);

      if (bHit)

      {

        TargetLocation = Hit.ImpactPoint + FVector::UpVector * HalfHeight;

      }

      else

      {

        TargetLocation = TraceEnd + FVector::UpVector * HalfHeight;
      }
    }
  }

  OutFinalLocation = TargetLocation;

  const bool bTeleported =
      Character->TeleportTo(TargetLocation, DesiredRotation);

  if (bTeleported)

  {

    if (UCharacterMovementComponent *MoveComp =
            Character->GetCharacterMovement())

    {

      MoveComp->StopMovementImmediately();
    }
  }

  return bTeleported;
}

bool UGA_AeyerjiBase::BP_TeleportOwnerSafely(

    FVector DesiredLocation,

    FRotator DesiredRotation,

    FVector &OutFinalLocation,

    float GroundTraceDistance,

    float CapsuleInflation) const

{

  return TeleportCharacterSafely(

      GetAeyerjiCharacter(GetCurrentActorInfo()),

      DesiredLocation,

      DesiredRotation,

      GroundTraceDistance,

      CapsuleInflation,

      OutFinalLocation);
}

bool UGA_AeyerjiBase::IsOwnerDead(
    const FGameplayAbilityActorInfo *ActorInfo) const

{

  if (const UAbilitySystemComponent *ASC = GetAeyerjiAbilitySystem(ActorInfo))

  {

    return ASC->HasMatchingGameplayTag(AeyerjiTags::State_Dead);
  }

  return false;
}
